#include "KeireHubRuntime/ContentCatalog.h"

#include "LocalCatalogSupport.h"
#include "Persistence.h"

#include <set>
#include <utility>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumContentCatalogBytes = 4 * 1024 * 1024;
        constexpr std::size_t MaximumContentItems = 1024;

        [[nodiscard]] HubError ContentError(const HubErrorCode code, std::string message,
                                            const std::filesystem::path& catalog, std::string details = {})
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .AffectedItem = Detail::PathToUtf8(catalog.filename()),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] HubStatus ParseCollection(const Detail::Json& values, const bool learn, const std::string& locale,
                                                const std::filesystem::path& root,
                                                std::set<std::string, std::less<>>& identities,
                                                std::vector<ResolvedContentItem>& destination)
        {
            if (!values.is_array() || values.size() > MaximumContentItems)
                return HubStatus::Failure(
                    {.Code = HubErrorCode::InvalidData, .Message = "A local content collection is invalid."});

            destination.reserve(values.size());
            for (const auto& value : values)
            {
                Detail::Json document{{"schemaVersion", HubContentCatalog::CurrentSchemaVersion},
                                      {"locale", locale},
                                      {"learn", Detail::Json::array()},
                                      {"resources", Detail::Json::array()}};
                document[learn ? "learn" : "resources"].push_back(value);
                auto parsed = ParseContentCatalog(document.dump());
                if (!parsed)
                    continue;

                auto& collection = learn ? parsed.Value().Learn : parsed.Value().Resources;
                auto metadata = std::move(collection.front());
                if (identities.contains(metadata.Id))
                    continue;

                ResolvedContentItem item{.Metadata = std::move(metadata)};
                if (item.Metadata.LocalPath)
                {
                    item.LocalFile = Detail::ResolveConfinedRegularFile(root, *item.Metadata.LocalPath);
                    if (!item.LocalFile)
                        continue;
                }
                if (item.Metadata.Thumbnail)
                {
                    item.ThumbnailFile = Detail::ResolveConfinedRegularFile(root, *item.Metadata.Thumbnail);
                    if (!item.ThumbnailFile)
                        continue;
                }
                identities.insert(item.Metadata.Id);
                destination.push_back(std::move(item));
            }
            return HubStatus::Success();
        }
    } // namespace

    ContentCatalog::ContentCatalog(std::filesystem::path catalogPath, std::filesystem::path contentRoot)
        : m_CatalogPath(std::move(catalogPath)), m_ContentRoot(std::move(contentRoot)),
          m_Snapshot(std::make_shared<const ContentCatalogSnapshot>())
    {
    }

    HubStatus ContentCatalog::Load()
    {
        auto document = Detail::ReadJsonFile(m_CatalogPath, MaximumContentCatalogBytes);
        if (!document)
            return HubStatus::Failure(document.Error());

        try
        {
            const auto schema = document.Value().at("schemaVersion").get<std::uint32_t>();
            if (schema != HubContentCatalog::CurrentSchemaVersion)
            {
                return HubStatus::Failure(ContentError(HubErrorCode::UnsupportedSchema,
                                                       "The local content catalog schema is unsupported.",
                                                       m_CatalogPath));
            }
            const auto locale = document.Value().at("locale").get<std::string>();
            Detail::Json empty{{"schemaVersion", schema},
                               {"locale", locale},
                               {"learn", Detail::Json::array()},
                               {"resources", Detail::Json::array()}};
            auto validatedHeader = ParseContentCatalog(empty.dump());
            if (!validatedHeader)
                return HubStatus::Failure(validatedHeader.Error());

            const auto& learn = document.Value().value("learn", Detail::Json::array());
            const auto& resources = document.Value().value("resources", Detail::Json::array());
            if (!learn.is_array() || !resources.is_array() || learn.size() > MaximumContentItems ||
                resources.size() > MaximumContentItems || learn.size() + resources.size() > MaximumContentItems)
            {
                return HubStatus::Failure(ContentError(
                    HubErrorCode::InvalidData, "The local content catalog contains too many entries.", m_CatalogPath));
            }

            ContentCatalogSnapshot next;
            next.Locale = locale;
            std::set<std::string, std::less<>> identities;
            if (auto status = ParseCollection(learn, true, locale, m_ContentRoot, identities, next.Learn); !status)
                return status;
            if (auto status = ParseCollection(resources, false, locale, m_ContentRoot, identities, next.Resources);
                !status)
            {
                return status;
            }
            m_Snapshot = std::make_shared<const ContentCatalogSnapshot>(std::move(next));
            return HubStatus::Success();
        }
        catch (const std::exception& error)
        {
            return HubStatus::Failure(ContentError(HubErrorCode::InvalidData, "The local content catalog is malformed.",
                                                   m_CatalogPath, error.what()));
        }
    }

    std::shared_ptr<const ContentCatalogSnapshot> ContentCatalog::Snapshot() const noexcept { return m_Snapshot; }

    const std::filesystem::path& ContentCatalog::CatalogPath() const noexcept { return m_CatalogPath; }

    const std::filesystem::path& ContentCatalog::ContentRoot() const noexcept { return m_ContentRoot; }
} // namespace KeireHub

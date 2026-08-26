#include <KeireHubRuntimeInternal/InstallTransactionInternal.h>

#include <KeireHubRuntimeInternal/DistributionEncoding.h>
#include <KeireHubRuntimeInternal/Persistence.h>
#include <KeireHubRuntimeInternal/Sha256.h>

#include "KeireHubRuntime/PackageResolver.h"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumReceiptBytes = std::size_t{16U} * 1024U * 1024U;
        constexpr std::size_t MaximumReceiptFiles = 100000;

        [[nodiscard]] HubError ReceiptError(std::string message, std::string affectedItem = {},
                                            std::string details = {})
        {
            return {.Code = HubErrorCode::InvalidData,
                    .Message = std::move(message),
                    .AffectedItem = std::move(affectedItem),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] bool IsVersion(std::string_view value)
        {
            return value.size() <= 128 && SemanticVersion::Parse(value).HasValue();
        }
    } // namespace

    std::string_view ToString(const InstallProduct product) noexcept
    {
        switch (product)
        {
        case InstallProduct::Editor:
            return "editor";
        case InstallProduct::Hub:
            return "hub";
        }
        return "editor";
    }

    std::optional<InstallProduct> ParseInstallProduct(const std::string_view value) noexcept
    {
        if (value == "editor")
            return InstallProduct::Editor;
        if (value == "hub")
            return InstallProduct::Hub;
        return std::nullopt;
    }

    HubStatus ValidateInstallReceipt(const InstallReceipt& receipt)
    {
        if (receipt.SchemaVersion != InstallReceipt::CurrentSchemaVersion ||
            !Detail::IsBoundedIdentifier(receipt.ProductId) || !Detail::IsBoundedIdentifier(receipt.InstallationId) ||
            !IsVersion(receipt.Version) || !Detail::IsBoundedIdentifier(receipt.BuildIdentity, 256) ||
            !Detail::IsSha256(receipt.ManifestFingerprint) || receipt.Files.empty() ||
            receipt.Files.size() > MaximumReceiptFiles)
        {
            return HubStatus::Failure(
                ReceiptError("The installation receipt identity is invalid.", InstallReceiptFileName));
        }

        std::set<std::string, std::less<>> paths;
        for (const auto& file : receipt.Files)
        {
            const auto key = Detail::NormalizedInstallPathKey(file.Path);
            if (!Detail::IsSafeRelativePath(file.Path) || file.Path == InstallReceiptFileName ||
                file.SizeBytes > Detail::MaximumInstallFileBytes || !Detail::IsSha256(file.Sha256) ||
                !paths.insert(key).second)
            {
                return HubStatus::Failure(
                    ReceiptError("The installation receipt inventory is unsafe.", Detail::PathToUtf8(file.Path)));
            }
        }

        const auto marker =
            std::ranges::find(receipt.Files, std::filesystem::path(InstallMarkerFileName), &InstallOwnedFile::Path);
        if (marker == receipt.Files.end())
        {
            return HubStatus::Failure(
                ReceiptError("The installation receipt does not own its product marker.", InstallMarkerFileName));
        }
        return HubStatus::Success();
    }

    HubResult<std::string> EncodeInstallReceipt(const InstallReceipt& receipt)
    {
        if (const auto status = ValidateInstallReceipt(receipt); !status)
            return HubResult<std::string>::Failure(status.Error());
        try
        {
            auto files = receipt.Files;
            std::ranges::sort(files, {},
                              [](const InstallOwnedFile& file) { return Detail::NormalizedInstallPathKey(file.Path); });
            Detail::Json document{{"schemaVersion", receipt.SchemaVersion},
                                  {"productId", receipt.ProductId},
                                  {"installationId", receipt.InstallationId},
                                  {"product", ToString(receipt.Product)},
                                  {"version", receipt.Version},
                                  {"buildIdentity", receipt.BuildIdentity},
                                  {"manifestFingerprint", receipt.ManifestFingerprint},
                                  {"files", Detail::Json::array()}};
            for (const auto& file : files)
            {
                document["files"].push_back({{"path", Detail::PathToUtf8(file.Path.generic_string())},
                                             {"sizeBytes", file.SizeBytes},
                                             {"sha256", file.Sha256}});
            }
            return HubResult<std::string>::Success(document.dump(2) + '\n');
        }
        catch (const std::exception& error)
        {
            return HubResult<std::string>::Failure(
                ReceiptError("The installation receipt could not be encoded.", InstallReceiptFileName, error.what()));
        }
    }

    HubResult<InstallReceipt> ReadInstallReceipt(const std::filesystem::path& root)
    {
        const auto path = root / InstallReceiptFileName;
        auto bytes = Detail::ReadTextFile(path, MaximumReceiptBytes);
        if (!bytes)
            return HubResult<InstallReceipt>::Failure(bytes.Error());
        try
        {
            const auto document = Detail::Json::parse(bytes.Value());
            if (!document.is_object() || document.size() != 8 || !document.at("files").is_array())
            {
                return HubResult<InstallReceipt>::Failure(
                    ReceiptError("The installation receipt shape is invalid.", InstallReceiptFileName));
            }
            const auto product = ParseInstallProduct(document.at("product").get<std::string>());
            if (!product)
            {
                return HubResult<InstallReceipt>::Failure(
                    ReceiptError("The installation receipt product is invalid.", "product"));
            }
            InstallReceipt receipt{.SchemaVersion = document.at("schemaVersion").get<std::uint32_t>(),
                                   .ProductId = document.at("productId").get<std::string>(),
                                   .InstallationId = document.at("installationId").get<std::string>(),
                                   .Product = *product,
                                   .Version = document.at("version").get<std::string>(),
                                   .BuildIdentity = document.at("buildIdentity").get<std::string>(),
                                   .ManifestFingerprint = document.at("manifestFingerprint").get<std::string>(),
                                   .DocumentSha256 = Detail::HashInstallDocument(bytes.Value())};
            if (document.at("files").size() > MaximumReceiptFiles)
                throw std::invalid_argument("The receipt inventory is too large.");
            receipt.Files.reserve(document.at("files").size());
            for (const auto& file : document.at("files"))
            {
                if (!file.is_object() || file.size() != 3)
                    throw std::invalid_argument("A receipt inventory entry has an invalid shape.");
                receipt.Files.push_back({.Path = Detail::PathFromUtf8(file.at("path").get<std::string>()),
                                         .SizeBytes = file.at("sizeBytes").get<std::uint64_t>(),
                                         .Sha256 = file.at("sha256").get<std::string>()});
            }
            if (const auto status = ValidateInstallReceipt(receipt); !status)
                return HubResult<InstallReceipt>::Failure(status.Error());
            return HubResult<InstallReceipt>::Success(std::move(receipt));
        }
        catch (const std::exception& error)
        {
            return HubResult<InstallReceipt>::Failure(
                ReceiptError("The installation receipt is malformed.", InstallReceiptFileName, error.what()));
        }
    }
} // namespace KeireHub

namespace KeireHub::Detail
{
    std::string NormalizedInstallPathKey(const std::filesystem::path& path)
    {
        auto result = PathToUtf8(path.lexically_normal().generic_string());
#if defined(_WIN32)
        std::ranges::transform(result, result.begin(), [](const unsigned char character)
                               { return static_cast<char>(std::tolower(character)); });
#endif
        return result;
    }

    HubResult<std::string> EncodeInstallMarker(const InstallMarker& marker)
    {
        if (!IsBoundedIdentifier(marker.ProductId) || !IsBoundedIdentifier(marker.InstallationId) ||
            !IsSha256(marker.ManifestFingerprint))
        {
            return HubResult<std::string>::Failure(
                ReceiptError("The installation marker identity is invalid.", InstallMarkerFileName));
        }
        const Json document{{"schemaVersion", 1},
                            {"productId", marker.ProductId},
                            {"installationId", marker.InstallationId},
                            {"product", ToString(marker.Product)},
                            {"manifestFingerprint", marker.ManifestFingerprint}};
        return HubResult<std::string>::Success(document.dump(2) + '\n');
    }

    HubResult<InstallMarker> ReadInstallMarker(const std::filesystem::path& root)
    {
        auto bytes = ReadTextFile(root / InstallMarkerFileName, std::size_t{16U} * 1024U);
        if (!bytes)
            return HubResult<InstallMarker>::Failure(bytes.Error());
        try
        {
            const auto document = Json::parse(bytes.Value());
            if (!document.is_object() || document.size() != 5 || document.at("schemaVersion").get<int>() != 1)
                throw std::invalid_argument("The marker shape is invalid.");
            const auto product = ParseInstallProduct(document.at("product").get<std::string>());
            if (!product)
                throw std::invalid_argument("The marker product is invalid.");
            InstallMarker marker{.ProductId = document.at("productId").get<std::string>(),
                                 .InstallationId = document.at("installationId").get<std::string>(),
                                 .Product = *product,
                                 .ManifestFingerprint = document.at("manifestFingerprint").get<std::string>()};
            if (!IsBoundedIdentifier(marker.ProductId) || !IsBoundedIdentifier(marker.InstallationId) ||
                !IsSha256(marker.ManifestFingerprint))
            {
                throw std::invalid_argument("The marker identity is invalid.");
            }
            return HubResult<InstallMarker>::Success(std::move(marker));
        }
        catch (const std::exception& error)
        {
            return HubResult<InstallMarker>::Failure(
                ReceiptError("The installation marker is malformed.", InstallMarkerFileName, error.what()));
        }
    }

    HubResult<std::string> HashInstallFile(const std::filesystem::path& path)
    {
        return Sha256File(path, MaximumInstallFileBytes);
    }

    std::string HashInstallDocument(const std::string_view document)
    {
        Sha256Builder builder;
        builder.Update(std::as_bytes(std::span(document.data(), document.size())));
        return DigestToString(builder.Finish());
    }

    HubStatus ValidateInstallTree(const std::filesystem::path& root, const bool allowMissing)
    {
        std::error_code error;
        const auto rootStatus = std::filesystem::symlink_status(root, error);
        if (error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(rootStatus)))
            return allowMissing
                       ? HubStatus::Success()
                       : HubStatus::Failure(ReceiptError("The installation path is missing.", PathToUtf8(root)));
        if (error || !std::filesystem::is_directory(rootStatus) || std::filesystem::is_symlink(rootStatus))
        {
            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                       .Message = "The installation path is not an ordinary directory.",
                                       .AffectedItem = PathToUtf8(root),
                                       .TechnicalDetails = error.message()});
        }

        const auto unsafe = [&](const std::filesystem::path& path, const std::filesystem::file_status& status)
        {
            if (std::filesystem::is_symlink(status) ||
                (!std::filesystem::is_directory(status) && !std::filesystem::is_regular_file(status)))
                return true;
#if defined(_WIN32)
            const auto attributes = GetFileAttributesW(path.c_str());
            return attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
            return false;
#endif
        };
        if (unsafe(root, rootStatus))
        {
            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                       .Message = "The installation root is a reparse point or unsafe object.",
                                       .AffectedItem = PathToUtf8(root)});
        }
        for (std::filesystem::recursive_directory_iterator iterator(root, error), end; iterator != end && !error;
             iterator.increment(error))
        {
            const auto status = iterator->symlink_status(error);
            if (error || unsafe(iterator->path(), status))
            {
                return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                           .Message = "The installation tree contains a link or unsafe object.",
                                           .AffectedItem = PathToUtf8(iterator->path()),
                                           .TechnicalDetails = error.message()});
            }
        }
        if (error)
        {
            return HubStatus::Failure({.Code = HubErrorCode::IoRead,
                                       .Message = "The installation tree could not be inspected completely.",
                                       .AffectedItem = PathToUtf8(root),
                                       .TechnicalDetails = error.message()});
        }
        return HubStatus::Success();
    }

    HubStatus ValidateInstallMutationPath(const std::filesystem::path& root, const std::filesystem::path& relative,
                                          const bool allowMissingLeaf)
    {
        if (!root.is_absolute() || !IsSafeRelativePath(relative))
        {
            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                       .Message = "A mutation path is not absolute and safely relative.",
                                       .AffectedItem = PathToUtf8(root / relative)});
        }

        const auto target = (root / relative).lexically_normal();
        const auto rootKey = NormalizedInstallPathKey(root.lexically_normal());
        const auto targetKey = NormalizedInstallPathKey(target);
        if (targetKey.size() <= rootKey.size() || !targetKey.starts_with(rootKey))
        {
            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                       .Message = "A mutation path escapes its installation root.",
                                       .AffectedItem = PathToUtf8(target)});
        }

        auto current = target.root_path();
        const auto relativeTarget = target.relative_path();
        std::size_t index = 0;
        const auto componentCount =
            static_cast<std::size_t>(std::distance(relativeTarget.begin(), relativeTarget.end()));
        for (const auto& component : relativeTarget)
        {
            current /= component;
            ++index;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(current, error);
            const bool missing =
                error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status));
            if (missing)
            {
                if (index == componentCount && !allowMissingLeaf)
                {
                    return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                               .Message = "A mutation source is missing.",
                                               .AffectedItem = PathToUtf8(current)});
                }
                continue;
            }
            if (error || std::filesystem::is_symlink(status))
            {
                return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                           .Message = "A mutation path contains a link or unreadable component.",
                                           .AffectedItem = PathToUtf8(current),
                                           .TechnicalDetails = error.message()});
            }
#if defined(_WIN32)
            const auto attributes = GetFileAttributesW(current.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                           .Message = "A mutation path contains a reparse point.",
                                           .AffectedItem = PathToUtf8(current)});
            }
#endif
            if (index != componentCount && !std::filesystem::is_directory(status))
            {
                return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                           .Message = "A mutation parent is not an ordinary directory.",
                                           .AffectedItem = PathToUtf8(current)});
            }
            if (index == componentCount && !std::filesystem::is_directory(status) &&
                !std::filesystem::is_regular_file(status))
            {
                return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                           .Message = "A mutation leaf is not an ordinary filesystem object.",
                                           .AffectedItem = PathToUtf8(current)});
            }
        }
        return HubStatus::Success();
    }
} // namespace KeireHub::Detail

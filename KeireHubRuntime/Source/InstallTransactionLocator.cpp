#include <KeireHubRuntimeInternal/InstallTransactionLocatorInternal.h>

#include <KeireHubRuntimeInternal/InstallMutationFileSystem.h>
#include <KeireHubRuntimeInternal/InstallTransactionInternal.h>
#include <KeireHubRuntimeInternal/Persistence.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <random>
#include <ranges>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace KeireHub::Detail
{
    namespace
    {
        constexpr std::size_t MaximumLocatorBytes = std::size_t{16U} * 1024U;
        constexpr const char* PendingLocatorName = ".locator.pending.json";

        [[nodiscard]] HubError LocatorError(const HubErrorCode code, std::string message,
                                            const std::filesystem::path& path = {}, std::string details = {})
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .AffectedItem = PathToUtf8(path),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right)
        {
            auto leftKey = PathToUtf8(std::filesystem::absolute(left).lexically_normal());
            auto rightKey = PathToUtf8(std::filesystem::absolute(right).lexically_normal());
#if defined(_WIN32)
            std::ranges::transform(leftKey, leftKey.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
            std::ranges::transform(rightKey, rightKey.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
#endif
            return leftKey == rightKey;
        }

        [[nodiscard]] bool IsRandomId(const std::string_view value)
        {
            return value.size() == 32U && std::ranges::all_of(value, [](const unsigned char character)
                                                              { return std::isxdigit(character) != 0; });
        }

        [[nodiscard]] std::string EncodeLocator(const std::string_view transactionId, const InstallProduct product,
                                                const std::filesystem::path& destination,
                                                const std::filesystem::path& transactionRoot)
        {
            return Json{{"schemaVersion", 1},
                        {"transactionId", transactionId},
                        {"product", ToString(product)},
                        {"destinationRoot", PathToUtf8(destination)},
                        {"transactionRootName", PathToUtf8(transactionRoot.filename())}}
                       .dump(2) +
                   '\n';
        }
    } // namespace

    std::string SecureInstallRandomId()
    {
        std::array<unsigned char, 16> bytes{};
#if defined(_WIN32)
        using GenerateRandom = LONG(WINAPI*)(void*, unsigned char*, unsigned long, unsigned long);
        const auto module = LoadLibraryExW(L"bcrypt.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module)
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                    "Windows cryptography could not be loaded");
        const auto generate = reinterpret_cast<GenerateRandom>(GetProcAddress(module, "BCryptGenRandom"));
        const auto status = generate ? generate(nullptr, bytes.data(), static_cast<unsigned long>(bytes.size()), 2U)
                                     : static_cast<LONG>(-1);
        FreeLibrary(module);
        if (status < 0)
            throw std::system_error(static_cast<int>(status), std::system_category(),
                                    "Windows could not generate a secure installation transaction ID");
#else
        std::random_device random;
        for (auto& byte : bytes)
            byte = static_cast<unsigned char>(random());
#endif
        constexpr char Hex[] = "0123456789abcdef";
        std::string result(bytes.size() * 2U, '0');
        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
            result[index * 2U] = Hex[(bytes[index] >> 4U) & 0x0fU];
            result[index * 2U + 1U] = Hex[bytes[index] & 0x0fU];
        }
        return result;
    }

    std::filesystem::path InstallTransactionLocatorPath(const std::filesystem::path& destination)
    {
        auto path = destination;
        path += InstallTransactionLocatorSuffix;
        return path;
    }

    HubResult<InstallTransactionLocator>
    CreateInstallTransactionLocator(InstallMutationAuthority& mutation, const std::filesystem::path& destination,
                                    const InstallProduct product)
    {
        const auto absoluteDestination = std::filesystem::absolute(destination).lexically_normal();
        const auto parent = absoluteDestination.parent_path();
        const auto locatorPath = InstallTransactionLocatorPath(absoluteDestination);
        try
        {
            auto parentFiles = mutation.Pin(parent);
            if (!parentFiles)
                return HubResult<InstallTransactionLocator>::Failure(parentFiles.Error());
            auto existing = parentFiles.Value()->Describe(locatorPath.filename(), true);
            if (!existing)
                return HubResult<InstallTransactionLocator>::Failure(existing.Error());
            if (!existing.Value().Sha256.empty())
            {
                return HubResult<InstallTransactionLocator>::Failure(LocatorError(
                    HubErrorCode::DestinationConflict, "An installation transaction locator already exists.",
                    locatorPath));
            }

            const auto transactionId = SecureInstallRandomId();
            auto rootName = absoluteDestination.filename();
            rootName += InstallTransactionRootPrefix;
            rootName += transactionId;
            const auto transactionRoot = parent / rootName;
            auto transactionFiles = mutation.Pin(transactionRoot, true, true);
            if (!transactionFiles)
                return HubResult<InstallTransactionLocator>::Failure(transactionFiles.Error());

            const auto document = EncodeLocator(transactionId, product, absoluteDestination, transactionRoot);
            if (const auto written =
                    transactionFiles.Value()->WriteTextAtomically(PendingLocatorName, document, false);
                !written)
                return HubResult<InstallTransactionLocator>::Failure(written.Error());
            auto pendingFile = transactionFiles.Value()->Describe(PendingLocatorName);
            if (!pendingFile)
                return HubResult<InstallTransactionLocator>::Failure(pendingFile.Error());
            auto locatorFile = pendingFile.Value();
            locatorFile.Path = locatorPath.filename();
            if (const auto moved = transactionFiles.Value()->RenameVerifiedTo(
                    pendingFile.Value(), *parentFiles.Value(), false, locatorPath.filename());
                !moved)
            {
                return HubResult<InstallTransactionLocator>::Failure(moved.Error());
            }
            return HubResult<InstallTransactionLocator>::Success(
                {.TransactionId = transactionId,
                 .Product = product,
                 .DestinationRoot = absoluteDestination,
                 .LocatorPath = locatorPath,
                 .TransactionRoot = transactionRoot,
                 .LocatorFile = std::move(locatorFile),
                 .DocumentSha256 = HashInstallDocument(document)});
        }
        catch (const std::exception& error)
        {
            return HubResult<InstallTransactionLocator>::Failure(
                LocatorError(HubErrorCode::IoWrite, "The installation transaction locator could not be created.",
                             locatorPath, error.what()));
        }
    }

    HubResult<std::optional<InstallTransactionLocator>>
    ReadInstallTransactionLocator(InstallMutationAuthority& mutation, const std::filesystem::path& destination,
                                  const InstallProduct product)
    {
        const auto absoluteDestination = std::filesystem::absolute(destination).lexically_normal();
        const auto parent = absoluteDestination.parent_path();
        const auto locatorPath = InstallTransactionLocatorPath(absoluteDestination);
        auto parentFiles = mutation.Pin(parent);
        if (!parentFiles)
            return HubResult<std::optional<InstallTransactionLocator>>::Failure(parentFiles.Error());
        auto locatorFile = parentFiles.Value()->Describe(locatorPath.filename(), true);
        if (!locatorFile)
            return HubResult<std::optional<InstallTransactionLocator>>::Failure(locatorFile.Error());
        if (locatorFile.Value().Sha256.empty())
            return HubResult<std::optional<InstallTransactionLocator>>::Success(std::nullopt);
        auto text = parentFiles.Value()->ReadText(locatorPath.filename(), MaximumLocatorBytes);
        if (!text)
            return HubResult<std::optional<InstallTransactionLocator>>::Failure(text.Error());
        try
        {
            const auto document = Json::parse(text.Value());
            if (!document.is_object() || document.size() != 5U || document.at("schemaVersion").get<int>() != 1)
                throw std::invalid_argument("Unsupported transaction locator schema.");
            const auto transactionId = document.at("transactionId").get<std::string>();
            const auto encodedProduct = ParseInstallProduct(document.at("product").get<std::string>());
            const auto encodedDestination = PathFromUtf8(document.at("destinationRoot").get<std::string>());
            const auto rootName = PathFromUtf8(document.at("transactionRootName").get<std::string>());
            auto expectedRootName = absoluteDestination.filename();
            expectedRootName += InstallTransactionRootPrefix;
            expectedRootName += transactionId;
            if (!IsRandomId(transactionId) || !encodedProduct || *encodedProduct != product ||
                !SamePath(encodedDestination, absoluteDestination) || rootName != expectedRootName ||
                rootName.has_parent_path() || rootName.is_absolute())
            {
                throw std::invalid_argument("Transaction locator ownership does not match this installation.");
            }
            const auto transactionRoot = parent / rootName;
            auto transactionFiles = mutation.Pin(transactionRoot);
            if (!transactionFiles)
                return HubResult<std::optional<InstallTransactionLocator>>::Failure(transactionFiles.Error());
            return HubResult<std::optional<InstallTransactionLocator>>::Success(
                InstallTransactionLocator{.TransactionId = transactionId,
                                          .Product = product,
                                          .DestinationRoot = absoluteDestination,
                                          .LocatorPath = locatorPath,
                                          .TransactionRoot = transactionRoot,
                                          .LocatorFile = std::move(locatorFile).Value(),
                                          .DocumentSha256 = HashInstallDocument(text.Value())});
        }
        catch (const std::exception& error)
        {
            return HubResult<std::optional<InstallTransactionLocator>>::Failure(LocatorError(
                HubErrorCode::UnsafeInstallRoot, "The installation transaction locator is invalid.", locatorPath,
                error.what()));
        }
    }

    HubStatus RemoveInstallTransactionLocator(InstallMutationAuthority& mutation,
                                              const InstallTransactionLocator& locator)
    {
        auto parentFiles = mutation.Pin(locator.LocatorPath.parent_path());
        if (!parentFiles)
            return HubStatus::Failure(parentFiles.Error());
        return parentFiles.Value()->RemoveVerified(locator.LocatorFile);
    }
} // namespace KeireHub::Detail

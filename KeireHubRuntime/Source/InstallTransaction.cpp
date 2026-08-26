#include "KeireHubRuntime/InstallTransaction.h"

#include <KeireHubRuntimeInternal/InstallMutationFileSystem.h>
#include <KeireHubRuntimeInternal/InstallLegacyMigrationInternal.h>
#include <KeireHubRuntimeInternal/InstallTransactionInternal.h>
#include <KeireHubRuntimeInternal/InstallTransactionLocatorInternal.h>
#include <KeireHubRuntimeInternal/Persistence.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <ranges>
#include <set>
#include <system_error>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumJournalBytes = std::size_t{32U} * 1024U * 1024U;
        constexpr const char* JournalFileName = "journal.json";
        constexpr const char* TransactionOwnerFileName = ".keire-install-transaction.json";

        enum class TransactionOperation
        {
            Install,
            Uninstall
        };

        struct TransactionJournal final
        {
            TransactionOperation Operation = TransactionOperation::Install;
            InstallProduct Product = InstallProduct::Editor;
            InstallTransactionPhase Phase = InstallTransactionPhase::Staged;
            std::string TransactionId;
            std::string LocatorSha256;
            std::filesystem::path DestinationRoot;
            std::optional<InstallRegistration> PreviousRegistration;
            std::optional<InstallRegistration> NewRegistration;
        };

        [[nodiscard]] HubError TransactionError(const HubErrorCode code, std::string message,
                                                const std::filesystem::path& item = {}, std::string details = {})
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .AffectedItem = Detail::PathToUtf8(item),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] std::string_view PhaseName(const InstallTransactionPhase phase) noexcept
        {
            switch (phase)
            {
            case InstallTransactionPhase::Staged:
                return "staged";
            case InstallTransactionPhase::BackupMoved:
                return "backupMoved";
            case InstallTransactionPhase::PayloadActivated:
                return "payloadActivated";
            case InstallTransactionPhase::RegistrationWritten:
                return "registrationWritten";
            case InstallTransactionPhase::Verified:
                return "verified";
            case InstallTransactionPhase::Committed:
                return "committed";
            }
            return "staged";
        }

        [[nodiscard]] std::optional<InstallTransactionPhase> ParsePhase(const std::string_view value) noexcept
        {
            if (value == "staged")
                return InstallTransactionPhase::Staged;
            if (value == "backupMoved")
                return InstallTransactionPhase::BackupMoved;
            if (value == "payloadActivated")
                return InstallTransactionPhase::PayloadActivated;
            if (value == "registrationWritten")
                return InstallTransactionPhase::RegistrationWritten;
            if (value == "verified")
                return InstallTransactionPhase::Verified;
            if (value == "committed")
                return InstallTransactionPhase::Committed;
            return std::nullopt;
        }

        [[nodiscard]] std::filesystem::path StageRoot(const Detail::InstallTransactionLocator& locator)
        {
            return locator.TransactionRoot / "stage";
        }

        [[nodiscard]] std::filesystem::path BackupRoot(const Detail::InstallTransactionLocator& locator)
        {
            return locator.TransactionRoot / "backup";
        }

        [[nodiscard]] bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right)
        {
            auto leftKey = Detail::PathToUtf8(std::filesystem::absolute(left).lexically_normal());
            auto rightKey = Detail::PathToUtf8(std::filesystem::absolute(right).lexically_normal());
#if defined(_WIN32)
            std::ranges::transform(leftKey, leftKey.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
            std::ranges::transform(rightKey, rightKey.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
#endif
            return leftKey == rightKey;
        }

        [[nodiscard]] Detail::Json EncodeRegistration(const InstallRegistration& registration)
        {
            return {{"productId", registration.ProductId},
                    {"installationId", registration.InstallationId},
                    {"root", Detail::PathToUtf8(registration.Root)},
                    {"version", registration.Version},
                    {"manifestFingerprint", registration.ManifestFingerprint},
                    {"receiptSha256", registration.ReceiptSha256}};
        }

        [[nodiscard]] InstallRegistration DecodeRegistration(const Detail::Json& document)
        {
            auto root = Detail::PathFromUtf8(document.at("root").get<std::string>());
            root.make_preferred();
            return {.ProductId = document.at("productId").get<std::string>(),
                    .InstallationId = document.at("installationId").get<std::string>(),
                    .Root = std::move(root),
                    .Version = document.at("version").get<std::string>(),
                    .ManifestFingerprint = document.at("manifestFingerprint").get<std::string>(),
                    .ReceiptSha256 = document.at("receiptSha256").get<std::string>()};
        }

        [[nodiscard]] bool RegistrationMatches(const InstallRegistration& registration, const InstallReceipt& receipt,
                                               const std::filesystem::path& root)
        {
            return registration.ProductId == receipt.ProductId &&
                   registration.InstallationId == receipt.InstallationId && SamePath(registration.Root, root) &&
                   registration.Version == receipt.Version &&
                   registration.ManifestFingerprint == receipt.ManifestFingerprint &&
                   registration.ReceiptSha256 == receipt.DocumentSha256;
        }

        [[nodiscard]] HubStatus ValidateRegistrationStore(const InstallRegistrationStore& store)
        {
            if (!store.Read || !store.Write || !store.Remove)
            {
                return HubStatus::Failure(TransactionError(HubErrorCode::InvalidArgument,
                                                           "The installation registration store is incomplete."));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus WriteJournal(const Detail::InstallTransactionLocator& locator,
                                             const TransactionJournal& journal)
        {
            Detail::Json document{
                {"schemaVersion", 2},
                {"operation", journal.Operation == TransactionOperation::Install ? "install" : "uninstall"},
                {"product", ToString(journal.Product)},
                {"phase", PhaseName(journal.Phase)},
                {"transactionId", journal.TransactionId},
                {"locatorSha256", journal.LocatorSha256},
                {"destinationRoot", Detail::PathToUtf8(journal.DestinationRoot)},
                {"previousRegistration", nullptr},
                {"newRegistration", nullptr}};
            if (journal.PreviousRegistration)
                document["previousRegistration"] = EncodeRegistration(*journal.PreviousRegistration);
            if (journal.NewRegistration)
                document["newRegistration"] = EncodeRegistration(*journal.NewRegistration);
            Detail::InstallMutationFileSystem transactionFiles(locator.TransactionRoot);
            return transactionFiles.WriteTextAtomically(JournalFileName, document.dump(2) + '\n', true);
        }

        [[nodiscard]] HubResult<TransactionJournal> ReadJournal(const Detail::InstallTransactionLocator& locator)
        {
            auto document = Detail::ReadJsonFile(locator.TransactionRoot / JournalFileName, MaximumJournalBytes);
            if (!document)
                return HubResult<TransactionJournal>::Failure(document.Error());
            try
            {
                if (!document.Value().is_object() || document.Value().at("schemaVersion").get<int>() != 2)
                    throw std::invalid_argument("Unsupported journal schema.");
                const auto product = ParseInstallProduct(document.Value().at("product").get<std::string>());
                const auto phase = ParsePhase(document.Value().at("phase").get<std::string>());
                const auto operation = document.Value().at("operation").get<std::string>();
                TransactionJournal journal{
                    .Operation = operation == "install"     ? TransactionOperation::Install
                                 : operation == "uninstall" ? TransactionOperation::Uninstall
                                                            : throw std::invalid_argument("Unknown operation."),
                    .Product = product.value_or(locator.Product),
                    .Phase = phase.value_or(InstallTransactionPhase::Staged),
                    .TransactionId = document.Value().at("transactionId").get<std::string>(),
                    .LocatorSha256 = document.Value().at("locatorSha256").get<std::string>(),
                    .DestinationRoot = Detail::PathFromUtf8(document.Value().at("destinationRoot").get<std::string>())};
                if (!product || !phase || *product != locator.Product ||
                    journal.TransactionId != locator.TransactionId || journal.LocatorSha256 != locator.DocumentSha256 ||
                    !SamePath(journal.DestinationRoot, locator.DestinationRoot))
                {
                    throw std::invalid_argument("Journal ownership does not match the requested installation.");
                }
                if (!document.Value().at("previousRegistration").is_null())
                    journal.PreviousRegistration = DecodeRegistration(document.Value().at("previousRegistration"));
                if (!document.Value().at("newRegistration").is_null())
                    journal.NewRegistration = DecodeRegistration(document.Value().at("newRegistration"));
                return HubResult<TransactionJournal>::Success(std::move(journal));
            }
            catch (const std::exception& error)
            {
                return HubResult<TransactionJournal>::Failure(
                    TransactionError(HubErrorCode::InvalidData, "The installation transaction journal is invalid.",
                                     locator.TransactionRoot / JournalFileName, error.what()));
            }
        }

        [[nodiscard]] HubStatus WriteTransactionOwner(const Detail::InstallTransactionLocator& locator)
        {
            const auto document = Detail::Json{{"schemaVersion", 2},
                                               {"transactionId", locator.TransactionId},
                                               {"locatorSha256", locator.DocumentSha256},
                                               {"product", ToString(locator.Product)},
                                               {"destinationRoot", Detail::PathToUtf8(locator.DestinationRoot)}}
                                      .dump(2) +
                                  '\n';
            Detail::InstallMutationFileSystem transactionFiles(locator.TransactionRoot);
            return transactionFiles.WriteTextAtomically(TransactionOwnerFileName, document, false);
        }

        [[nodiscard]] HubStatus ValidateTransactionOwner(const Detail::InstallTransactionLocator& locator)
        {
            if (const auto treeStatus = Detail::ValidateInstallTree(locator.TransactionRoot, false); !treeStatus)
                return treeStatus;
            auto document = Detail::ReadJsonFile(locator.TransactionRoot / TransactionOwnerFileName, 16U * 1024U);
            if (!document)
                return HubStatus::Failure(document.Error());
            try
            {
                if (document.Value().at("schemaVersion").get<int>() != 2 ||
                    document.Value().at("transactionId").get<std::string>() != locator.TransactionId ||
                    document.Value().at("locatorSha256").get<std::string>() != locator.DocumentSha256 ||
                    document.Value().at("product").get<std::string>() != ToString(locator.Product) ||
                    !SamePath(Detail::PathFromUtf8(document.Value().at("destinationRoot").get<std::string>()),
                              locator.DestinationRoot))
                {
                    throw std::invalid_argument("Transaction owner mismatch.");
                }
                return HubStatus::Success();
            }
            catch (const std::exception& error)
            {
                return HubStatus::Failure(TransactionError(HubErrorCode::UnsafeInstallRoot,
                                                           "The installation transaction is not owned by this product.",
                                                           locator.TransactionRoot, error.what()));
            }
        }

        [[nodiscard]] HubStatus CleanupTransaction(Detail::InstallMutationAuthority& mutation,
                                                   const Detail::InstallTransactionLocator& locator);

        [[nodiscard]] HubStatus UpdatePhase(const Detail::InstallTransactionLocator& locator,
                                            TransactionJournal& journal,
                                            const InstallTransactionPhase phase)
        {
            journal.Phase = phase;
            return WriteJournal(locator, journal);
        }

        [[nodiscard]] bool ContinueAfter(const InstallTransactionRequest& request, const InstallTransactionPhase phase)
        {
            return !request.ContinueAfterPhase || request.ContinueAfterPhase(phase);
        }

        [[nodiscard]] HubStatus Interrupted(const std::filesystem::path& root, const InstallTransactionPhase phase)
        {
            return HubStatus::Failure(TransactionError(
                HubErrorCode::WorkerInterrupted, "The installation transaction was interrupted after a durable phase.",
                root, std::string(PhaseName(phase))));
        }

        [[nodiscard]] HubResult<std::optional<InstallRegistration>>
        ReadRegistration(const InstallTransactionRequest& request)
        {
            if (const auto status = ValidateRegistrationStore(request.Registration); !status)
                return HubResult<std::optional<InstallRegistration>>::Failure(status.Error());
            return request.Registration.Read(request.Product);
        }

        [[nodiscard]] HubStatus VerifyFile(const std::filesystem::path& root, const InstallOwnedFile& file,
                                           const bool allowMissing = false)
        {
            const auto path = root / file.Path;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            if ((!error && !std::filesystem::exists(status)) || error == std::errc::no_such_file_or_directory)
            {
                return allowMissing ? HubStatus::Success()
                                    : HubStatus::Failure(TransactionError(
                                          HubErrorCode::NotFound, "An owned installation file is missing.", file.Path));
            }
            if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status) ||
                std::filesystem::file_size(path, error) != file.SizeBytes || error)
            {
                return HubStatus::Failure(TransactionError(HubErrorCode::InvalidData,
                                                           "An owned installation file has drifted or is unsafe.",
                                                           file.Path, error.message()));
            }
            auto digest = Detail::HashInstallFile(path);
            if (!digest || digest.Value() != file.Sha256)
            {
                return digest ? HubStatus::Failure(TransactionError(
                                    HubErrorCode::InvalidData, "An owned installation file has drifted.", file.Path))
                              : HubStatus::Failure(digest.Error());
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus VerifyReceiptFiles(const std::filesystem::path& root, const InstallReceipt& receipt)
        {
            if (const auto status = Detail::ValidateInstallTree(root, false); !status)
                return status;
            for (const auto& file : receipt.Files)
            {
                if (const auto status = VerifyFile(root, file); !status)
                    return status;
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus VerifyIdentity(const InstallTransactionRequest& request, const InstallReceipt& receipt,
                                               const bool requireFiles)
        {
            if (receipt.Product != request.Product)
            {
                return HubStatus::Failure(TransactionError(HubErrorCode::UnsafeInstallRoot,
                                                           "The installation receipt belongs to another product.",
                                                           request.DestinationRoot));
            }
            auto marker = Detail::ReadInstallMarker(request.DestinationRoot);
            if (!marker || marker.Value().Product != receipt.Product || marker.Value().ProductId != receipt.ProductId ||
                marker.Value().InstallationId != receipt.InstallationId ||
                marker.Value().ManifestFingerprint != receipt.ManifestFingerprint)
            {
                return HubStatus::Failure(marker
                                              ? TransactionError(HubErrorCode::UnsafeInstallRoot,
                                                                 "The installation marker does not match its receipt.",
                                                                 request.DestinationRoot / InstallMarkerFileName)
                                              : marker.Error());
            }
            auto registration = ReadRegistration(request);
            if (!registration)
                return HubStatus::Failure(registration.Error());
            if (!registration.Value() || !RegistrationMatches(*registration.Value(), receipt, request.DestinationRoot))
            {
                return HubStatus::Failure(TransactionError(HubErrorCode::UnsafeInstallRoot,
                                                           "The installation registration does not match its receipt.",
                                                           request.DestinationRoot));
            }
            return requireFiles ? VerifyReceiptFiles(request.DestinationRoot, receipt) : HubStatus::Success();
        }

        [[nodiscard]] HubResult<std::optional<InstallReceipt>>
        ClassifyDestination(Detail::InstallMutationAuthority& mutation, const InstallTransactionRequest& request)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(request.DestinationRoot, error);
            if (error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status)))
            {
                auto registration = ReadRegistration(request);
                if (!registration)
                    return HubResult<std::optional<InstallReceipt>>::Failure(registration.Error());
                if (registration.Value())
                {
                    return HubResult<std::optional<InstallReceipt>>::Failure(TransactionError(
                        HubErrorCode::UnsafeInstallRoot, "Another registered installation must be removed first.",
                        registration.Value()->Root));
                }
                return HubResult<std::optional<InstallReceipt>>::Success(std::nullopt);
            }
            if (const auto tree = Detail::ValidateInstallTree(request.DestinationRoot, false); !tree)
                return HubResult<std::optional<InstallReceipt>>::Failure(tree.Error());
            if (std::filesystem::is_empty(request.DestinationRoot, error) && !error)
            {
                auto registration = ReadRegistration(request);
                if (!registration)
                    return HubResult<std::optional<InstallReceipt>>::Failure(registration.Error());
                if (registration.Value())
                {
                    return HubResult<std::optional<InstallReceipt>>::Failure(TransactionError(
                        HubErrorCode::UnsafeInstallRoot, "Another registered installation must be removed first.",
                        registration.Value()->Root));
                }
                return HubResult<std::optional<InstallReceipt>>::Success(std::nullopt);
            }
            if (error)
            {
                return HubResult<std::optional<InstallReceipt>>::Failure(
                    TransactionError(HubErrorCode::IoRead, "The installation destination could not be classified.",
                                     request.DestinationRoot, error.message()));
            }
            auto receipt = ReadInstallReceipt(request.DestinationRoot);
            if (!receipt)
            {
                const auto receiptPath = request.DestinationRoot / InstallReceiptFileName;
                if (std::filesystem::exists(receiptPath, error) || error)
                {
                    return HubResult<std::optional<InstallReceipt>>::Failure(TransactionError(
                        HubErrorCode::UnsafeInstallRoot,
                        "A non-empty destination contains an invalid product receipt.", request.DestinationRoot,
                        error ? error.message() : receipt.Error().TechnicalDetails));
                }
                auto migrated = Detail::MigrateLegacyInstallation(mutation, request);
                if (!migrated)
                {
                    return HubResult<std::optional<InstallReceipt>>::Failure(TransactionError(
                        HubErrorCode::UnsafeInstallRoot,
                        "A non-empty destination is not a verified product installation.", request.DestinationRoot,
                        migrated.Error().Message + " " + migrated.Error().TechnicalDetails));
                }
                receipt = std::move(migrated);
            }
            if (const auto verified = VerifyIdentity(request, receipt.Value(), true); !verified)
                return HubResult<std::optional<InstallReceipt>>::Failure(verified.Error());
            return HubResult<std::optional<InstallReceipt>>::Success(std::move(receipt).Value());
        }

        [[nodiscard]] std::string NewInstallationId()
        {
            return Detail::SecureInstallRandomId();
        }

        [[nodiscard]] HubResult<InstallOwnedFile> DescribeFile(const std::filesystem::path& root,
                                                               const std::filesystem::path& relative)
        {
            const auto path = root / relative;
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error || size > Detail::MaximumInstallFileBytes)
            {
                return HubResult<InstallOwnedFile>::Failure(
                    TransactionError(HubErrorCode::IoRead, "A staged installation file has an invalid size.", relative,
                                     error.message()));
            }
            auto digest = Detail::HashInstallFile(path);
            if (!digest)
                return HubResult<InstallOwnedFile>::Failure(digest.Error());
            return HubResult<InstallOwnedFile>::Success(
                {.Path = relative, .SizeBytes = size, .Sha256 = std::move(digest).Value()});
        }

        [[nodiscard]] HubStatus CopyOwnedFile(Detail::InstallMutationAuthority& mutation,
                                              const std::filesystem::path& sourceRoot,
                                              const std::filesystem::path& destinationRoot,
                                              const InstallOwnedFile& file)
        {
            auto source = mutation.Pin(sourceRoot);
            if (!source)
                return HubStatus::Failure(source.Error());
            auto destination = mutation.Pin(destinationRoot, true);
            if (!destination)
                return HubStatus::Failure(destination.Error());
            return source.Value()->CopyVerifiedTo(file, *destination.Value());
        }

        [[nodiscard]] HubStatus MoveVerifiedOwnedFile(Detail::InstallMutationAuthority& mutation,
                                                      const std::filesystem::path& sourceRoot,
                                                      const std::filesystem::path& destinationRoot,
                                                      const InstallOwnedFile& file,
                                                      const bool allowMissingSource = false)
        {
            auto source = mutation.Pin(sourceRoot);
            if (!source)
                return HubStatus::Failure(source.Error());
            auto destination = mutation.Pin(destinationRoot, true);
            if (!destination)
                return HubStatus::Failure(destination.Error());
            return source.Value()->RenameVerifiedTo(file, *destination.Value(), allowMissingSource);
        }

        [[nodiscard]] HubStatus RemoveVerifiedOwnedFile(Detail::InstallMutationAuthority& mutation,
                                                        const std::filesystem::path& root, const InstallOwnedFile& file,
                                                        const std::string_view failureMessage)
        {
            auto fileSystem = mutation.Pin(root);
            if (!fileSystem)
                return HubStatus::Failure(fileSystem.Error());
            auto removed = fileSystem.Value()->RemoveVerified(file);
            if (!removed)
            {
                auto error = removed.Error();
                error.Message = std::string(failureMessage);
                return HubStatus::Failure(std::move(error));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubResult<InstallOwnedFile> DescribeReceiptFile(const std::filesystem::path& root,
                                                                      const InstallReceipt& receipt)
        {
            std::error_code error;
            const auto size = std::filesystem::file_size(root / InstallReceiptFileName, error);
            if (error)
            {
                return HubResult<InstallOwnedFile>::Failure(
                    TransactionError(HubErrorCode::IoRead, "The installation receipt could not be inspected.",
                                     InstallReceiptFileName, error.message()));
            }
            InstallOwnedFile file{.Path = InstallReceiptFileName, .SizeBytes = size, .Sha256 = receipt.DocumentSha256};
            if (const auto verified = VerifyFile(root, file); !verified)
                return HubResult<InstallOwnedFile>::Failure(verified.Error());
            return HubResult<InstallOwnedFile>::Success(std::move(file));
        }

        void PruneOwnedParents(Detail::InstallMutationAuthority& mutation, const std::filesystem::path& root,
                               const std::filesystem::path& relative) noexcept
        {
            auto fileSystem = mutation.Pin(root);
            if (!fileSystem)
                return;
            for (auto parent = (root / relative).parent_path(); parent != root && !parent.empty();
                 parent = parent.parent_path())
            {
                const auto parentRelative = parent.lexically_relative(root);
                if (const auto removed = fileSystem.Value()->RemoveEmptyDirectory(parentRelative); !removed)
                    break;
            }
        }

        [[nodiscard]] HubStatus RestoreRegistration(const InstallTransactionRequest& request,
                                                    const TransactionJournal& journal)
        {
            if (journal.PreviousRegistration)
                return request.Registration.Write(*journal.PreviousRegistration);
            if (journal.NewRegistration)
                return request.Registration.Remove(*journal.NewRegistration);
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus MoveReceiptInventory(Detail::InstallMutationAuthority& mutation,
                                                     const std::filesystem::path& source,
                                                     const std::filesystem::path& destination,
                                                     const InstallReceipt& receipt, bool includeReceipt,
                                                     bool allowMissingFiles = false);

        [[nodiscard]] HubStatus RestoreBackup(Detail::InstallMutationAuthority& mutation,
                                              const InstallTransactionRequest& request,
                                              const TransactionJournal& journal,
                                              const Detail::InstallTransactionLocator& locator)
        {
            const auto backup = BackupRoot(locator);
            std::error_code error;
            if (!std::filesystem::exists(backup, error))
                return error ? HubStatus::Failure(TransactionError(HubErrorCode::IoRead,
                                                                   "The installation backup could not be inspected.",
                                                                   backup, error.message()))
                             : HubStatus::Success();
            if (const auto safe = Detail::ValidateInstallTree(backup, false); !safe)
                return safe;
            if (std::filesystem::exists(backup / InstallReceiptFileName, error))
            {
                auto receipt = ReadInstallReceipt(backup);
                if (!receipt)
                    return HubStatus::Failure(receipt.Error());
                const bool allowMissingFiles = journal.Operation == TransactionOperation::Uninstall;
                if (const auto moved = MoveReceiptInventory(mutation, backup, request.DestinationRoot, receipt.Value(),
                                                            false, allowMissingFiles);
                    !moved)
                    return moved;
                auto receiptFile = DescribeReceiptFile(backup, receipt.Value());
                if (!receiptFile)
                    return HubStatus::Failure(receiptFile.Error());
                if (const auto copied = CopyOwnedFile(mutation, backup, request.DestinationRoot, receiptFile.Value());
                    !copied)
                    return copied;
            }
            else if (error)
            {
                return HubStatus::Failure(TransactionError(HubErrorCode::IoRead,
                                                           "The installation backup receipt could not be inspected.",
                                                           backup / InstallReceiptFileName, error.message()));
            }
            else if (journal.PreviousRegistration &&
                     std::filesystem::exists(request.DestinationRoot / InstallReceiptFileName, error))
            {
                // A mutation can fail after moving only part of an inventory and before moving its receipt.
                // The still-active receipt remains registration-bound and is authoritative for restoring only the
                // verified files that reached the transaction backup.
                auto receipt = ReadInstallReceipt(request.DestinationRoot);
                if (!receipt)
                    return HubStatus::Failure(receipt.Error());
                if (!RegistrationMatches(*journal.PreviousRegistration, receipt.Value(), request.DestinationRoot))
                {
                    return HubStatus::Failure(TransactionError(HubErrorCode::UnsafeInstallRoot,
                                                               "The partial backup receipt is not registration-bound.",
                                                               request.DestinationRoot / InstallReceiptFileName));
                }
                if (const auto moved =
                        MoveReceiptInventory(mutation, backup, request.DestinationRoot, receipt.Value(), false, true);
                    !moved)
                    return moved;
            }
            return RestoreRegistration(request, journal);
        }

        [[nodiscard]] HubStatus RemoveActivatedFiles(Detail::InstallMutationAuthority& mutation,
                                                     const InstallTransactionRequest& request,
                                                     const Detail::InstallTransactionLocator& locator)
        {
            const auto stageReceiptPath = StageRoot(locator) / InstallReceiptFileName;
            const auto activeReceiptPath = request.DestinationRoot / InstallReceiptFileName;
            const auto receiptRoot = std::filesystem::exists(stageReceiptPath) ? StageRoot(locator)
                                                                               : request.DestinationRoot;
            auto receipt = ReadInstallReceipt(receiptRoot);
            if (!receipt)
                return HubStatus::Failure(receipt.Error());
            for (auto iterator = receipt.Value().Files.rbegin(); iterator != receipt.Value().Files.rend(); ++iterator)
            {
                const auto path = request.DestinationRoot / iterator->Path;
                std::error_code error;
                if (!std::filesystem::exists(path, error))
                    continue;
                if (const auto removed = RemoveVerifiedOwnedFile(mutation, request.DestinationRoot, *iterator,
                                                                 "Rollback could not remove an activated file.");
                    !removed)
                    return removed;
                PruneOwnedParents(mutation, request.DestinationRoot, iterator->Path);
            }
            std::error_code error;
            if (std::filesystem::exists(activeReceiptPath, error))
            {
                auto active = ReadInstallReceipt(request.DestinationRoot);
                if (!active || active.Value().DocumentSha256 != receipt.Value().DocumentSha256)
                {
                    return HubStatus::Failure(TransactionError(
                        HubErrorCode::InvalidData, "Rollback found a modified active receipt.", activeReceiptPath));
                }
                auto receiptFile = DescribeReceiptFile(request.DestinationRoot, active.Value());
                if (!receiptFile)
                    return HubStatus::Failure(receiptFile.Error());
                if (const auto removed = RemoveVerifiedOwnedFile(mutation, request.DestinationRoot, receiptFile.Value(),
                                                                 "Rollback could not remove the active receipt.");
                    !removed)
                    return removed;
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus Rollback(Detail::InstallMutationAuthority& mutation,
                                         const InstallTransactionRequest& request, const TransactionJournal& journal,
                                         const Detail::InstallTransactionLocator& locator)
        {
            // Activation moves files one by one while the journal still says BackupMoved. Using the complete staged
            // receipt here makes rollback safe even when the process fails between two file renames.
            if (journal.Operation == TransactionOperation::Install &&
                journal.Phase >= InstallTransactionPhase::BackupMoved)
            {
                if (const auto removed = RemoveActivatedFiles(mutation, request, locator); !removed)
                    return removed;
            }
            if (const auto restored = RestoreBackup(mutation, request, journal, locator); !restored)
                return restored;
            return CleanupTransaction(mutation, locator);
        }

        [[nodiscard]] HubStatus RecoverExisting(Detail::InstallMutationAuthority& mutation,
                                                const InstallTransactionRequest& request)
        {
            auto locator = Detail::ReadInstallTransactionLocator(mutation, request.DestinationRoot, request.Product);
            if (!locator)
                return HubStatus::Failure(locator.Error());
            if (!locator.Value())
                return HubStatus::Success();
            if (const auto owner = ValidateTransactionOwner(*locator.Value()); !owner)
                return owner;
            auto journal = ReadJournal(*locator.Value());
            if (!journal)
                return HubStatus::Failure(journal.Error());
            if (journal.Value().Phase == InstallTransactionPhase::Committed)
                return CleanupTransaction(mutation, *locator.Value());
            return Rollback(mutation, request, journal.Value(), *locator.Value());
        }

        [[nodiscard]] HubStatus PreserveOriginalFailure(Detail::InstallMutationAuthority& mutation,
                                                        const InstallTransactionRequest& request,
                                                        const TransactionJournal& journal,
                                                        const Detail::InstallTransactionLocator& locator,
                                                        const HubError& error)
        {
            const auto rollback = Rollback(mutation, request, journal, locator);
            if (rollback)
                return HubStatus::Failure(error);
            auto combined = error;
            combined.TechnicalDetails += combined.TechnicalDetails.empty() ? "" : " | ";
            combined.TechnicalDetails +=
                "Rollback also failed: " + rollback.Error().Message + " " + rollback.Error().TechnicalDetails;
            return HubStatus::Failure(std::move(combined));
        }

        [[nodiscard]] HubResult<InstallReceipt> StagePackage(Detail::InstallMutationAuthority& mutation,
                                                             const InstallTransactionRequest& request,
                                                             const Detail::InstallerPackageManifest& manifest,
                                                             const std::optional<InstallReceipt>& previous,
                                                             const Detail::InstallTransactionLocator& locator)
        {
            const auto stage = StageRoot(locator);
            InstallReceipt receipt{.ProductId = manifest.ProductId,
                                   .InstallationId = previous ? previous->InstallationId : NewInstallationId(),
                                   .Product = request.Product,
                                   .Version = manifest.Version,
                                   .BuildIdentity = manifest.BuildIdentity,
                                   .ManifestFingerprint = manifest.Fingerprint,
                                   .Files = manifest.Files};
            std::vector<std::filesystem::path> extraFiles{manifest.ManifestPath};
            for (const auto& optional :
                 {std::filesystem::path("Uninstall.exe"),
                  std::filesystem::path(std::string(".keire-") + std::string(ToString(request.Product)) + "-install")})
            {
                std::error_code error;
                if (std::filesystem::is_regular_file(request.SourceRoot / optional, error) && !error)
                    extraFiles.push_back(optional);
            }
            std::vector<InstallOwnedFile> extraDescriptions;
            extraDescriptions.reserve(extraFiles.size());
            for (const auto& relative : extraFiles)
            {
                auto described = DescribeFile(request.SourceRoot, relative);
                if (!described)
                    return HubResult<InstallReceipt>::Failure(described.Error());
                receipt.Files.push_back(described.Value());
                extraDescriptions.push_back(std::move(described).Value());
            }
            auto marker = Detail::EncodeInstallMarker({.ProductId = receipt.ProductId,
                                                       .InstallationId = receipt.InstallationId,
                                                       .Product = receipt.Product,
                                                       .ManifestFingerprint = receipt.ManifestFingerprint});
            if (!marker)
                return HubResult<InstallReceipt>::Failure(marker.Error());
            receipt.Files.push_back({.Path = InstallMarkerFileName,
                                     .SizeBytes = marker.Value().size(),
                                     .Sha256 = Detail::HashInstallDocument(marker.Value())});
            auto encoded = EncodeInstallReceipt(receipt);
            if (!encoded)
                return HubResult<InstallReceipt>::Failure(encoded.Error());
            receipt.DocumentSha256 = Detail::HashInstallDocument(encoded.Value());
            Detail::InstallMutationFileSystem stageFiles(stage);
            if (const auto status = stageFiles.WriteTextAtomically(InstallReceiptFileName, encoded.Value(), false);
                !status)
                return HubResult<InstallReceipt>::Failure(status.Error());
            if (const auto status = stageFiles.WriteTextAtomically(InstallMarkerFileName, marker.Value(), false); !status)
                return HubResult<InstallReceipt>::Failure(status.Error());
            // The receipt is durable before package copying starts. If a copy is interrupted, cleanup can remove only
            // exact matching partial files and preserve any unknown transaction neighbor.
            for (const auto& file : manifest.Files)
            {
                if (const auto copied = CopyOwnedFile(mutation, request.SourceRoot, stage, file); !copied)
                    return HubResult<InstallReceipt>::Failure(copied.Error());
            }
            for (const auto& file : extraDescriptions)
            {
                if (const auto copied = CopyOwnedFile(mutation, request.SourceRoot, stage, file); !copied)
                    return HubResult<InstallReceipt>::Failure(copied.Error());
            }
            if (const auto safe = Detail::ValidateInstallTree(stage, false); !safe)
                return HubResult<InstallReceipt>::Failure(safe.Error());
            for (const auto& file : receipt.Files)
            {
                if (const auto verified = VerifyFile(stage, file); !verified)
                    return HubResult<InstallReceipt>::Failure(verified.Error());
            }
            return HubResult<InstallReceipt>::Success(std::move(receipt));
        }

        [[nodiscard]] HubStatus GuardNewFileCollisions(const std::filesystem::path& destination,
                                                       const InstallReceipt& receipt,
                                                       const std::optional<InstallReceipt>& previous)
        {
            std::set<std::string, std::less<>> previouslyOwned;
            if (previous)
            {
                for (const auto& file : previous->Files)
                    previouslyOwned.insert(Detail::NormalizedInstallPathKey(file.Path));
                previouslyOwned.insert(Detail::NormalizedInstallPathKey(InstallReceiptFileName));
            }
            std::error_code error;
            for (const auto& file : receipt.Files)
            {
                if (previouslyOwned.contains(Detail::NormalizedInstallPathKey(file.Path)))
                    continue;
                if (std::filesystem::exists(destination / file.Path, error) || error)
                {
                    return HubStatus::Failure(
                        TransactionError(HubErrorCode::DestinationConflict,
                                         "The new package would overwrite content not owned by the previous receipt.",
                                         file.Path, error.message()));
                }
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus MoveReceiptInventory(Detail::InstallMutationAuthority& mutation,
                                                     const std::filesystem::path& source,
                                                     const std::filesystem::path& destination,
                                                     const InstallReceipt& receipt, const bool includeReceipt,
                                                     const bool allowMissingFiles)
        {
            for (const auto& file : receipt.Files)
            {
                if (const auto moved = MoveVerifiedOwnedFile(mutation, source, destination, file, allowMissingFiles);
                    !moved)
                    return moved;
            }
            if (!includeReceipt)
                return HubStatus::Success();
            auto receiptFile = DescribeReceiptFile(source, receipt);
            if (!receiptFile)
                return HubStatus::Failure(receiptFile.Error());
            return MoveVerifiedOwnedFile(mutation, source, destination, receiptFile.Value());
        }

        struct CleanupTreeInventory final
        {
            std::filesystem::path Root;
            bool Exists = false;
            std::optional<InstallReceipt> Receipt;
            std::vector<std::filesystem::path> Directories;
        };

        [[nodiscard]] HubResult<CleanupTreeInventory> InspectCleanupTree(const std::filesystem::path& root)
        {
            CleanupTreeInventory result{.Root = root};
            std::error_code error;
            const auto rootStatus = std::filesystem::symlink_status(root, error);
            if (error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(rootStatus)))
                return HubResult<CleanupTreeInventory>::Success(std::move(result));
            if (error)
            {
                return HubResult<CleanupTreeInventory>::Failure(TransactionError(
                    HubErrorCode::IoRead, "A transaction cleanup tree could not be inspected.", root, error.message()));
            }
            result.Exists = true;
            if (const auto safe = Detail::ValidateInstallTree(root, false); !safe)
                return HubResult<CleanupTreeInventory>::Failure(safe.Error());
            if (std::filesystem::is_empty(root, error) && !error)
                return HubResult<CleanupTreeInventory>::Success(std::move(result));
            if (error)
            {
                return HubResult<CleanupTreeInventory>::Failure(
                    TransactionError(HubErrorCode::IoRead, "A transaction cleanup tree could not be classified.", root,
                                     error.message()));
            }

            const auto receiptPath = root / InstallReceiptFileName;
            const auto receiptStatus = std::filesystem::symlink_status(receiptPath, error);
            if (error || !std::filesystem::is_regular_file(receiptStatus))
            {
                return HubResult<CleanupTreeInventory>::Failure(TransactionError(
                    HubErrorCode::DestinationConflict,
                    "Transaction content without an exact receipt was preserved instead of recursively removed.", root,
                    error.message()));
            }
            auto receipt = ReadInstallReceipt(root);
            if (!receipt)
                return HubResult<CleanupTreeInventory>::Failure(receipt.Error());

            std::set<std::string, std::less<>> allowedFiles{Detail::NormalizedInstallPathKey(InstallReceiptFileName)};
            std::set<std::string, std::less<>> allowedDirectories;
            for (const auto& file : receipt.Value().Files)
            {
                allowedFiles.insert(Detail::NormalizedInstallPathKey(file.Path));
                for (auto parent = file.Path.parent_path(); !parent.empty() && parent != ".";
                     parent = parent.parent_path())
                {
                    allowedDirectories.insert(Detail::NormalizedInstallPathKey(parent));
                }
                if (const auto verified = VerifyFile(root, file, true); !verified)
                    return HubResult<CleanupTreeInventory>::Failure(verified.Error());
            }
            auto receiptFile = DescribeReceiptFile(root, receipt.Value());
            if (!receiptFile)
                return HubResult<CleanupTreeInventory>::Failure(receiptFile.Error());

            for (std::filesystem::recursive_directory_iterator iterator(root, error), end; iterator != end && !error;
                 iterator.increment(error))
            {
                const auto relative = std::filesystem::relative(iterator->path(), root, error);
                if (error)
                    break;
                const auto status = iterator->symlink_status(error);
                if (error)
                    break;
                const auto key = Detail::NormalizedInstallPathKey(relative);
                if (std::filesystem::is_regular_file(status))
                {
                    if (!allowedFiles.contains(key))
                    {
                        return HubResult<CleanupTreeInventory>::Failure(TransactionError(
                            HubErrorCode::DestinationConflict,
                            "Unknown transaction content was preserved instead of recursively removed.",
                            iterator->path()));
                    }
                }
                else if (std::filesystem::is_directory(status))
                {
                    if (!allowedDirectories.contains(key))
                    {
                        return HubResult<CleanupTreeInventory>::Failure(TransactionError(
                            HubErrorCode::DestinationConflict,
                            "An unknown transaction directory was preserved instead of recursively removed.",
                            iterator->path()));
                    }
                    result.Directories.push_back(relative);
                }
            }
            if (error)
            {
                return HubResult<CleanupTreeInventory>::Failure(
                    TransactionError(HubErrorCode::IoRead, "A transaction cleanup tree could not be enumerated.", root,
                                     error.message()));
            }
            std::ranges::sort(
                result.Directories, [](const auto& left, const auto& right)
                { return std::distance(left.begin(), left.end()) > std::distance(right.begin(), right.end()); });
            result.Receipt = std::move(receipt).Value();
            return HubResult<CleanupTreeInventory>::Success(std::move(result));
        }

        [[nodiscard]] HubStatus RemoveCleanupTree(Detail::InstallMutationAuthority& mutation,
                                                  const CleanupTreeInventory& inventory)
        {
            if (!inventory.Exists)
                return HubStatus::Success();
            auto pinned = mutation.Pin(inventory.Root);
            if (!pinned)
                return HubStatus::Failure(pinned.Error());
            auto fileSystem = std::move(pinned).Value();
            if (inventory.Receipt)
            {
                for (const auto& file : inventory.Receipt->Files)
                {
                    std::error_code error;
                    const auto status = std::filesystem::symlink_status(inventory.Root / file.Path, error);
                    if (error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status)))
                    {
                        continue;
                    }
                    if (error)
                    {
                        return HubStatus::Failure(TransactionError(
                            HubErrorCode::IoRead, "A receipt-owned cleanup file could not be inspected.", file.Path,
                            error.message()));
                    }
                    if (const auto removed = RemoveVerifiedOwnedFile(
                            mutation, inventory.Root, file, "A receipt-owned transaction file could not be removed.");
                        !removed)
                    {
                        return removed;
                    }
                }
                auto receiptFile = DescribeReceiptFile(inventory.Root, *inventory.Receipt);
                if (!receiptFile)
                    return HubStatus::Failure(receiptFile.Error());
                if (const auto removed = RemoveVerifiedOwnedFile(mutation, inventory.Root, receiptFile.Value(),
                                                                 "The transaction receipt could not be removed.");
                    !removed)
                {
                    return removed;
                }
            }
            for (const auto& directory : inventory.Directories)
            {
                std::error_code error;
                const auto status = std::filesystem::symlink_status(inventory.Root / directory, error);
                if (error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status)))
                {
                    continue;
                }
                if (!std::filesystem::is_directory(status))
                {
                    return HubStatus::Failure(TransactionError(
                        HubErrorCode::DestinationConflict,
                        "A non-empty or changed transaction directory was preserved instead of recursively removed.",
                        inventory.Root / directory, error.message()));
                }
                if (const auto removed = fileSystem->RemoveEmptyDirectory(directory); !removed)
                    return removed;
            }
            const auto removed = fileSystem->RemoveRootIfEmpty();
            fileSystem.reset();
            mutation.Unpin(inventory.Root);
            return removed;
        }

        [[nodiscard]] HubStatus CleanupTransaction(Detail::InstallMutationAuthority& mutation,
                                                   const Detail::InstallTransactionLocator& locator)
        {
            const auto& root = locator.TransactionRoot;
            std::error_code error;
            const auto rootStatus = std::filesystem::symlink_status(root, error);
            if (error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(rootStatus)))
                return HubStatus::Success();
            if (error)
            {
                return HubStatus::Failure(TransactionError(
                    HubErrorCode::IoRead, "The transaction path could not be inspected.", root, error.message()));
            }
            if (const auto owner = ValidateTransactionOwner(locator); !owner)
                return owner;

            auto stage = InspectCleanupTree(StageRoot(locator));
            if (!stage)
                return HubStatus::Failure(stage.Error());
            auto backup = InspectCleanupTree(BackupRoot(locator));
            if (!backup)
                return HubStatus::Failure(backup.Error());

            const std::set<std::string, std::less<>> allowedTopLevel{
                Detail::NormalizedInstallPathKey("stage"), Detail::NormalizedInstallPathKey("backup"),
                Detail::NormalizedInstallPathKey(TransactionOwnerFileName),
                Detail::NormalizedInstallPathKey(JournalFileName)};
            for (std::filesystem::directory_iterator iterator(root, error), end; iterator != end && !error;
                 iterator.increment(error))
            {
                if (!allowedTopLevel.contains(Detail::NormalizedInstallPathKey(iterator->path().filename())))
                {
                    return HubStatus::Failure(TransactionError(
                        HubErrorCode::DestinationConflict,
                        "Unknown top-level transaction content was preserved instead of recursively removed.",
                        iterator->path()));
                }
            }
            if (error)
            {
                return HubStatus::Failure(TransactionError(
                    HubErrorCode::IoRead, "The transaction root could not be enumerated.", root, error.message()));
            }

            std::optional<InstallOwnedFile> journalFile;
            const auto journalStatus = std::filesystem::symlink_status(root / JournalFileName, error);
            if (!error && std::filesystem::exists(journalStatus))
            {
                auto described = DescribeFile(root, JournalFileName);
                if (!described)
                    return HubStatus::Failure(described.Error());
                if (auto journal = ReadJournal(locator); !journal)
                    return HubStatus::Failure(journal.Error());
                journalFile = std::move(described).Value();
            }
            else if (error && error != std::errc::no_such_file_or_directory)
            {
                return HubStatus::Failure(TransactionError(HubErrorCode::IoRead,
                                                           "The transaction journal could not be inspected.",
                                                           root / JournalFileName, error.message()));
            }
            auto ownerFile = DescribeFile(root, TransactionOwnerFileName);
            if (!ownerFile)
                return HubStatus::Failure(ownerFile.Error());

            if (const auto removed = RemoveCleanupTree(mutation, stage.Value()); !removed)
                return removed;
            if (const auto removed = RemoveCleanupTree(mutation, backup.Value()); !removed)
                return removed;
            if (journalFile)
            {
                if (const auto removed = RemoveVerifiedOwnedFile(mutation, root, *journalFile,
                                                                 "The transaction journal could not be removed.");
                    !removed)
                    return removed;
            }
            if (const auto removed = RemoveVerifiedOwnedFile(mutation, root, ownerFile.Value(),
                                                             "The transaction owner marker could not be removed.");
                !removed)
                return removed;
            auto rootFilesResult = mutation.Pin(root);
            if (!rootFilesResult)
                return HubStatus::Failure(rootFilesResult.Error());
            auto rootFiles = std::move(rootFilesResult).Value();
            const auto removedRoot = rootFiles->RemoveRootIfEmpty();
            rootFiles.reset();
            mutation.Unpin(root);
            if (!removedRoot)
                return removedRoot;
            return Detail::RemoveInstallTransactionLocator(mutation, locator);
        }
    } // namespace

    HubResult<std::optional<InstallRegistration>>
    Detail::ReadPendingInstallPreviousRegistration(const std::filesystem::path& destination,
                                                   const InstallProduct product)
    {
        if (destination.empty() || !destination.is_absolute())
        {
            return HubResult<std::optional<InstallRegistration>>::Failure(TransactionError(
                HubErrorCode::InvalidArgument, "The installation destination must be absolute.", destination));
        }
        Detail::InstallMutationAuthority mutation;
        auto located = Detail::ReadInstallTransactionLocator(mutation, destination, product);
        if (!located)
            return HubResult<std::optional<InstallRegistration>>::Failure(located.Error());
        if (!located.Value())
            return HubResult<std::optional<InstallRegistration>>::Success(std::nullopt);
        if (const auto owner = ValidateTransactionOwner(*located.Value()); !owner)
            return HubResult<std::optional<InstallRegistration>>::Failure(owner.Error());
        auto journal = ReadJournal(*located.Value());
        if (!journal)
            return HubResult<std::optional<InstallRegistration>>::Failure(journal.Error());
        if (journal.Value().Operation != TransactionOperation::Install)
        {
            return HubResult<std::optional<InstallRegistration>>::Failure(TransactionError(
                HubErrorCode::InvalidTransition, "Shell integration requires a pending install transaction.",
                located.Value()->TransactionRoot));
        }
        return HubResult<std::optional<InstallRegistration>>::Success(journal.Value().PreviousRegistration);
    }

    HubStatus RecoverInstallTransaction(const InstallTransactionRequest& request)
    {
        if (request.DestinationRoot.empty() || !request.DestinationRoot.is_absolute())
        {
            return HubStatus::Failure(TransactionError(HubErrorCode::InvalidArgument,
                                                       "The installation destination must be absolute.",
                                                       request.DestinationRoot));
        }
        if (const auto store = ValidateRegistrationStore(request.Registration); !store)
            return store;
        Detail::InstallMutationAuthority mutation;
        return RecoverExisting(mutation, request);
    }

    HubStatus VerifyInstalledPackage(const InstallTransactionRequest& request)
    {
        if (const auto recovered = RecoverInstallTransaction(request); !recovered)
            return recovered;
        auto receipt = ReadInstallReceipt(request.DestinationRoot);
        if (!receipt)
            return HubStatus::Failure(receipt.Error());
        return VerifyIdentity(request, receipt.Value(), true);
    }

    HubStatus InstallPackageTransaction(const InstallTransactionRequest& request)
    {
        if (request.SourceRoot.empty() || !request.SourceRoot.is_absolute() || request.DestinationRoot.empty() ||
            !request.DestinationRoot.is_absolute() || SamePath(request.SourceRoot, request.DestinationRoot))
        {
            return HubStatus::Failure(
                TransactionError(HubErrorCode::InvalidArgument,
                                 "Installer source and destination paths must be distinct and absolute."));
        }
        if (const auto store = ValidateRegistrationStore(request.Registration); !store)
            return store;
        Detail::InstallMutationAuthority mutation;
        if (const auto recovered = RecoverExisting(mutation, request); !recovered)
            return recovered;
        auto manifest = Detail::ReadInstallerPackageManifest(request.SourceRoot, request.Product);
        if (!manifest)
            return HubStatus::Failure(manifest.Error());
        auto previous = ClassifyDestination(mutation, request);
        if (!previous)
            return HubStatus::Failure(previous.Error());

        auto createdLocator =
            Detail::CreateInstallTransactionLocator(mutation, request.DestinationRoot, request.Product);
        if (!createdLocator)
            return HubStatus::Failure(createdLocator.Error());
        const auto locator = std::move(createdLocator).Value();
        if (auto transactionFiles = mutation.Pin(locator.TransactionRoot); !transactionFiles)
            return HubStatus::Failure(transactionFiles.Error());
        if (auto stageFiles = mutation.Pin(StageRoot(locator), true); !stageFiles)
            return HubStatus::Failure(stageFiles.Error());
        if (auto backupFiles = mutation.Pin(BackupRoot(locator), true); !backupFiles)
            return HubStatus::Failure(backupFiles.Error());
        if (const auto owner = WriteTransactionOwner(locator); !owner)
            return owner;

        auto registration = ReadRegistration(request);
        if (!registration)
            return HubStatus::Failure(registration.Error());
        TransactionJournal journal{.Operation = TransactionOperation::Install,
                                   .Product = request.Product,
                                   .Phase = InstallTransactionPhase::Staged,
                                   .TransactionId = locator.TransactionId,
                                   .LocatorSha256 = locator.DocumentSha256,
                                   .DestinationRoot = request.DestinationRoot,
                                   .PreviousRegistration = registration.Value()};
        auto staged = StagePackage(mutation, request, manifest.Value(), previous.Value(), locator);
        if (!staged)
        {
            const auto cleanup = CleanupTransaction(mutation, locator);
            return cleanup ? HubStatus::Failure(staged.Error()) : HubStatus::Failure(cleanup.Error());
        }
        journal.NewRegistration = InstallRegistration{.ProductId = staged.Value().ProductId,
                                                      .InstallationId = staged.Value().InstallationId,
                                                      .Root = request.DestinationRoot,
                                                      .Version = staged.Value().Version,
                                                      .ManifestFingerprint = staged.Value().ManifestFingerprint,
                                                      .ReceiptSha256 = staged.Value().DocumentSha256};
        if (const auto collision = GuardNewFileCollisions(request.DestinationRoot, staged.Value(), previous.Value());
            !collision)
        {
            const auto cleanup = CleanupTransaction(mutation, locator);
            return cleanup ? collision : cleanup;
        }
        if (const auto status = WriteJournal(locator, journal); !status)
            return PreserveOriginalFailure(mutation, request, journal, locator, status.Error());
        if (!ContinueAfter(request, journal.Phase))
            return Interrupted(locator.TransactionRoot, journal.Phase);

        if (previous.Value())
        {
            if (const auto moved = MoveReceiptInventory(mutation, request.DestinationRoot,
                                                        BackupRoot(locator), *previous.Value(), true);
                !moved)
                return PreserveOriginalFailure(mutation, request, journal, locator, moved.Error());
        }
        if (const auto status = UpdatePhase(locator, journal, InstallTransactionPhase::BackupMoved); !status)
            return PreserveOriginalFailure(mutation, request, journal, locator, status.Error());
        if (!ContinueAfter(request, journal.Phase))
            return Interrupted(locator.TransactionRoot, journal.Phase);

        if (auto destinationFiles = mutation.Pin(request.DestinationRoot, true); !destinationFiles)
            return PreserveOriginalFailure(mutation, request, journal, locator, destinationFiles.Error());
        if (const auto moved =
                MoveReceiptInventory(mutation, StageRoot(locator), request.DestinationRoot, staged.Value(), false);
            !moved)
            return PreserveOriginalFailure(mutation, request, journal, locator, moved.Error());
        auto stagedReceiptFile = DescribeReceiptFile(StageRoot(locator), staged.Value());
        if (!stagedReceiptFile)
            return PreserveOriginalFailure(mutation, request, journal, locator, stagedReceiptFile.Error());
        if (const auto copied = CopyOwnedFile(mutation, StageRoot(locator), request.DestinationRoot,
                                              stagedReceiptFile.Value());
            !copied)
            return PreserveOriginalFailure(mutation, request, journal, locator, copied.Error());
        if (const auto status = UpdatePhase(locator, journal, InstallTransactionPhase::PayloadActivated); !status)
            return PreserveOriginalFailure(mutation, request, journal, locator, status.Error());
        if (!ContinueAfter(request, journal.Phase))
            return Interrupted(locator.TransactionRoot, journal.Phase);

        if (const auto status = request.Registration.Write(*journal.NewRegistration); !status)
            return PreserveOriginalFailure(mutation, request, journal, locator, status.Error());
        if (const auto status = UpdatePhase(locator, journal, InstallTransactionPhase::RegistrationWritten);
            !status)
            return PreserveOriginalFailure(mutation, request, journal, locator, status.Error());
        if (!ContinueAfter(request, journal.Phase))
            return Interrupted(locator.TransactionRoot, journal.Phase);

        if (const auto verified = VerifyIdentity(request, staged.Value(), true); !verified)
            return PreserveOriginalFailure(mutation, request, journal, locator, verified.Error());
        if (const auto status = UpdatePhase(locator, journal, InstallTransactionPhase::Verified); !status)
            return PreserveOriginalFailure(mutation, request, journal, locator, status.Error());
        if (!ContinueAfter(request, journal.Phase))
            return Interrupted(locator.TransactionRoot, journal.Phase);
        if (request.DeferCommit)
            return HubStatus::Success();
        if (const auto status = UpdatePhase(locator, journal, InstallTransactionPhase::Committed); !status)
            return PreserveOriginalFailure(mutation, request, journal, locator, status.Error());
        if (!ContinueAfter(request, journal.Phase))
            return Interrupted(locator.TransactionRoot, journal.Phase);
        return CleanupTransaction(mutation, locator);
    }

    HubStatus CommitInstallTransaction(const InstallTransactionRequest& request)
    {
        if (request.DestinationRoot.empty() || !request.DestinationRoot.is_absolute())
        {
            return HubStatus::Failure(TransactionError(HubErrorCode::InvalidArgument,
                                                       "The installation destination must be absolute.",
                                                       request.DestinationRoot));
        }
        if (const auto store = ValidateRegistrationStore(request.Registration); !store)
            return store;
        Detail::InstallMutationAuthority mutation;

        auto located = Detail::ReadInstallTransactionLocator(mutation, request.DestinationRoot, request.Product);
        if (!located)
            return HubStatus::Failure(located.Error());
        if (!located.Value())
        {
            // A repeated commit after successful cleanup is an idempotent verification of the active installation.
            return VerifyInstalledPackage(request);
        }
        const auto locator = std::move(*located.Value());
        if (const auto owner = ValidateTransactionOwner(locator); !owner)
            return owner;
        auto journal = ReadJournal(locator);
        if (!journal)
            return HubStatus::Failure(journal.Error());
        if (journal.Value().Operation != TransactionOperation::Install)
        {
            return HubStatus::Failure(TransactionError(HubErrorCode::InvalidTransition,
                                                       "Only an install transaction can be committed.",
                                                       locator.TransactionRoot));
        }
        if (journal.Value().Phase == InstallTransactionPhase::Committed)
            return CleanupTransaction(mutation, locator);
        if (journal.Value().Phase != InstallTransactionPhase::Verified || !journal.Value().NewRegistration)
        {
            return HubStatus::Failure(TransactionError(HubErrorCode::InvalidTransition,
                                                       "The installation transaction has not reached verification.",
                                                       locator.TransactionRoot));
        }
        auto receipt = ReadInstallReceipt(request.DestinationRoot);
        if (!receipt)
            return HubStatus::Failure(receipt.Error());
        if (!RegistrationMatches(*journal.Value().NewRegistration, receipt.Value(), request.DestinationRoot))
        {
            return HubStatus::Failure(TransactionError(HubErrorCode::InvalidTransition,
                                                       "The verified receipt no longer matches the pending commit.",
                                                       request.DestinationRoot / InstallReceiptFileName));
        }
        if (const auto verified = VerifyIdentity(request, receipt.Value(), true); !verified)
            return verified;
        auto mutableJournal = std::move(journal).Value();
        if (const auto status = UpdatePhase(locator, mutableJournal, InstallTransactionPhase::Committed); !status)
            return status;
        return CleanupTransaction(mutation, locator);
    }

    HubResult<InstallUninstallResult> UninstallPackageTransaction(const InstallTransactionRequest& request)
    {
        if (const auto store = ValidateRegistrationStore(request.Registration); !store)
            return HubResult<InstallUninstallResult>::Failure(store.Error());
        Detail::InstallMutationAuthority mutation;
        if (const auto recovered = RecoverExisting(mutation, request); !recovered)
            return HubResult<InstallUninstallResult>::Failure(recovered.Error());
        if (const auto safe = Detail::ValidateInstallTree(request.DestinationRoot, false); !safe)
            return HubResult<InstallUninstallResult>::Failure(safe.Error());
        auto destinationFilesResult = mutation.Pin(request.DestinationRoot);
        if (!destinationFilesResult)
            return HubResult<InstallUninstallResult>::Failure(destinationFilesResult.Error());
        auto destinationFiles = std::move(destinationFilesResult).Value();
        auto receipt = ReadInstallReceipt(request.DestinationRoot);
        if (!receipt)
            return HubResult<InstallUninstallResult>::Failure(receipt.Error());
        if (const auto identity = VerifyIdentity(request, receipt.Value(), false); !identity)
            return HubResult<InstallUninstallResult>::Failure(identity.Error());

        auto createdLocator =
            Detail::CreateInstallTransactionLocator(mutation, request.DestinationRoot, request.Product);
        if (!createdLocator)
            return HubResult<InstallUninstallResult>::Failure(createdLocator.Error());
        const auto locator = std::move(createdLocator).Value();
        if (auto transactionFiles = mutation.Pin(locator.TransactionRoot); !transactionFiles)
            return HubResult<InstallUninstallResult>::Failure(transactionFiles.Error());
        if (auto backupFiles = mutation.Pin(BackupRoot(locator), true); !backupFiles)
            return HubResult<InstallUninstallResult>::Failure(backupFiles.Error());
        if (const auto owner = WriteTransactionOwner(locator); !owner)
            return HubResult<InstallUninstallResult>::Failure(owner.Error());
        auto registration = ReadRegistration(request);
        if (!registration)
            return HubResult<InstallUninstallResult>::Failure(registration.Error());
        TransactionJournal journal{.Operation = TransactionOperation::Uninstall,
                                   .Product = request.Product,
                                   .Phase = InstallTransactionPhase::Staged,
                                   .TransactionId = locator.TransactionId,
                                   .LocatorSha256 = locator.DocumentSha256,
                                   .DestinationRoot = request.DestinationRoot,
                                   .PreviousRegistration = registration.Value()};
        if (const auto status = WriteJournal(locator, journal); !status)
            return HubResult<InstallUninstallResult>::Failure(status.Error());
        if (!ContinueAfter(request, journal.Phase))
            return HubResult<InstallUninstallResult>::Failure(
                Interrupted(locator.TransactionRoot, journal.Phase).Error());

        InstallUninstallResult result;
        std::error_code error;
        for (const auto& file : receipt.Value().Files)
        {
            const auto path = request.DestinationRoot / file.Path;
            if (const auto verified = VerifyFile(request.DestinationRoot, file, true); !verified)
            {
                ++result.PreservedModifiedFileCount;
                continue;
            }
            if (!std::filesystem::exists(path, error))
                continue;
            if (const auto moved =
                    MoveVerifiedOwnedFile(mutation, request.DestinationRoot, BackupRoot(locator), file);
                !moved)
                return HubResult<InstallUninstallResult>::Failure(
                    PreserveOriginalFailure(mutation, request, journal, locator, moved.Error()).Error());
            ++result.RemovedFileCount;
            PruneOwnedParents(mutation, request.DestinationRoot, file.Path);
        }
        auto receiptFile = DescribeReceiptFile(request.DestinationRoot, receipt.Value());
        if (!receiptFile)
            return HubResult<InstallUninstallResult>::Failure(receiptFile.Error());
        if (const auto moved = MoveVerifiedOwnedFile(mutation, request.DestinationRoot,
                                                     BackupRoot(locator), receiptFile.Value());
            !moved)
            return HubResult<InstallUninstallResult>::Failure(
                PreserveOriginalFailure(mutation, request, journal, locator, moved.Error()).Error());
        if (const auto status = UpdatePhase(locator, journal, InstallTransactionPhase::BackupMoved); !status)
            return HubResult<InstallUninstallResult>::Failure(
                PreserveOriginalFailure(mutation, request, journal, locator, status.Error()).Error());
        if (!ContinueAfter(request, journal.Phase))
            return HubResult<InstallUninstallResult>::Failure(
                Interrupted(locator.TransactionRoot, journal.Phase).Error());
        if (const auto status = request.Registration.Remove(*journal.PreviousRegistration); !status)
            return HubResult<InstallUninstallResult>::Failure(
                PreserveOriginalFailure(mutation, request, journal, locator, status.Error()).Error());
        if (const auto status = UpdatePhase(locator, journal, InstallTransactionPhase::RegistrationWritten);
            !status)
            return HubResult<InstallUninstallResult>::Failure(
                PreserveOriginalFailure(mutation, request, journal, locator, status.Error()).Error());
        if (!ContinueAfter(request, journal.Phase))
            return HubResult<InstallUninstallResult>::Failure(
                Interrupted(locator.TransactionRoot, journal.Phase).Error());
        if (const auto status = UpdatePhase(locator, journal, InstallTransactionPhase::Committed); !status)
            return HubResult<InstallUninstallResult>::Failure(
                PreserveOriginalFailure(mutation, request, journal, locator, status.Error()).Error());
        if (!ContinueAfter(request, journal.Phase))
            return HubResult<InstallUninstallResult>::Failure(
                Interrupted(locator.TransactionRoot, journal.Phase).Error());
        if (const auto cleaned = CleanupTransaction(mutation, locator); !cleaned)
            return HubResult<InstallUninstallResult>::Failure(cleaned.Error());
        // Root removal is optional: a modified or unknown neighbour deliberately keeps the selected directory
        // non-empty. The retained handle guarantees that a concurrent pathname replacement is never removed.
        (void)destinationFiles->RemoveRootIfEmpty();
        destinationFiles.reset();
        mutation.Unpin(request.DestinationRoot);
        return HubResult<InstallUninstallResult>::Success(result);
    }
} // namespace KeireHub

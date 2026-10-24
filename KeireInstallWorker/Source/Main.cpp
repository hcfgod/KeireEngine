#include "KeireInstallWorker/ShellIntegration.h"
#include "KeireInstallWorker/WorkerRegistration.h"

#include "KeireHubRuntime/InstallTransaction.h"
#include <KeireHubRuntimeInternal/InstallTransactionInternal.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace
{
    struct Arguments final
    {
        std::string Command;
        KeireHub::InstallProduct Product = KeireHub::InstallProduct::Editor;
        std::filesystem::path Source;
        std::filesystem::path Root;
        std::filesystem::path PreviousRoot;
        bool StartMenu = false;
        bool Desktop = false;
        bool RepairMissingFiles = false;
    };

    [[nodiscard]] std::optional<Arguments> ParseArguments(const int count, char** values)
    {
        if (count < 6)
            return std::nullopt;
        Arguments result{.Command = values[1]};
        for (int index = 2; index < count; index += 2)
        {
            if (index + 1 >= count)
                return std::nullopt;
            const std::string_view name(values[index]);
            if (name == "--product")
            {
                const auto product = KeireHub::ParseInstallProduct(values[index + 1]);
                if (!product)
                    return std::nullopt;
                result.Product = *product;
            }
            else if (name == "--source")
            {
                result.Source = std::filesystem::absolute(values[index + 1]).lexically_normal();
            }
            else if (name == "--root")
            {
                result.Root = std::filesystem::absolute(values[index + 1]).lexically_normal();
            }
            else if (name == "--previous-root")
            {
                result.PreviousRoot = std::filesystem::absolute(values[index + 1]).lexically_normal();
            }
            else if (name == "--start-menu" || name == "--desktop" || name == "--repair-missing")
            {
                const std::string_view enabled(values[index + 1]);
                if (enabled != "0" && enabled != "1")
                    return std::nullopt;
                if (name == "--repair-missing")
                {
                    result.RepairMissingFiles = enabled == "1";
                }
                else
                {
                    (name == "--start-menu" ? result.StartMenu : result.Desktop) = enabled == "1";
                }
            }
            else
            {
                return std::nullopt;
            }
        }
        if (result.Root.empty() ||
            ((result.Command == "install" || result.Command == "install-deferred" ||
              result.Command == "validate-relocation") &&
             result.Source.empty()) ||
            (result.Command == "validate-relocation" && result.PreviousRoot.empty()))
            return std::nullopt;
        return result;
    }

#if defined(KEIRE_INSTALL_WORKER_FAULT_INJECTION)
    [[nodiscard]] std::optional<KeireHub::InstallTransactionPhase> FaultPhase()
    {
        std::string configured;
#if defined(_WIN32)
        char* value = nullptr;
        std::size_t length = 0;
        if (::_dupenv_s(&value, &length, "KEIRE_INSTALL_WORKER_INTERRUPT_AFTER") == 0 && value)
            configured.assign(value);
        std::free(value);
#else
        if (const auto* value = std::getenv("KEIRE_INSTALL_WORKER_INTERRUPT_AFTER"))
            configured.assign(value);
#endif
        if (configured.empty())
            return std::nullopt;
        const std::string_view phase(configured);
        if (phase == "staged")
            return KeireHub::InstallTransactionPhase::Staged;
        if (phase == "backupMoved")
            return KeireHub::InstallTransactionPhase::BackupMoved;
        if (phase == "payloadActivated")
            return KeireHub::InstallTransactionPhase::PayloadActivated;
        if (phase == "registrationWritten")
            return KeireHub::InstallTransactionPhase::RegistrationWritten;
        if (phase == "verified")
            return KeireHub::InstallTransactionPhase::Verified;
        if (phase == "committed")
            return KeireHub::InstallTransactionPhase::Committed;
        return std::nullopt;
    }
#endif

    void PrintError(const KeireHub::HubError& error)
    {
        std::cerr << error.Message;
        if (!error.AffectedItem.empty())
            std::cerr << " [" << error.AffectedItem << ']';
        if (!error.TechnicalDetails.empty())
            std::cerr << ": " << error.TechnicalDetails;
        std::cerr << '\n';
    }

    [[nodiscard]] std::string RelocationPathKey(const std::filesystem::path& path)
    {
        auto key = KeireHub::Detail::NormalizedInstallPathKey(path);
        while (!key.empty() && key.back() == '/')
        {
            key.pop_back();
        }
        return key;
    }

    class CommandTiming final
    {
      public:
        explicit CommandTiming(const std::string_view command) : m_Command(command) {}

        ~CommandTiming() { std::cout << "installer command=" << m_Command << " elapsed_ms=" << Elapsed() << std::endl; }

        void Phase(const KeireHub::InstallTransactionPhase phase)
        {
            constexpr std::string_view names[]{"staged",   "backupMoved", "payloadActivated", "registrationWritten",
                                               "verified", "committed"};
            const auto elapsed = Elapsed();
            std::cout << "installer phase=" << names[static_cast<std::size_t>(phase)] << " elapsed_ms=" << elapsed
                      << " phase_ms=" << elapsed - m_Previous << std::endl;
            m_Previous = elapsed;
        }

      private:
        [[nodiscard]] std::int64_t Elapsed() const
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_Start)
                .count();
        }

        std::string_view m_Command;
        std::chrono::steady_clock::time_point m_Start = std::chrono::steady_clock::now();
        std::int64_t m_Previous = 0;
    };
} // namespace

int main(const int count, char** values)
{
    const auto arguments = ParseArguments(count, values);
    if (!arguments || (arguments->Command != "install" && arguments->Command != "install-deferred" &&
                       arguments->Command != "commit" && arguments->Command != "verify" &&
                       arguments->Command != "--verify-installation" && arguments->Command != "integrate" &&
                       arguments->Command != "recover" && arguments->Command != "uninstall" &&
                       arguments->Command != "prepare" && arguments->Command != "validate-relocation"))
    {
        std::cerr << "Usage: KeireInstallWorker "
                     "<prepare|install|install-deferred|integrate|commit|--verify-installation|recover|uninstall|"
                     "validate-relocation> "
                     "--product <editor|hub> "
                     "--root <absolute-path> [--source <absolute-path>] "
                     "[--previous-root <absolute-path>] [--start-menu <0|1>] [--desktop <0|1>] "
                     "[--repair-missing <0|1>]\n";
        return 64;
    }
    CommandTiming timing(arguments->Command);
    auto registrationStore = KeireInstallWorker::CreateProductRegistrationStore(arguments->Product);
    if (arguments->Command == "validate-relocation")
    {
        // This is deliberately read-only: removal and the new install remain separate, explicit transactions.
        const auto active = registrationStore.Read(arguments->Product);
        if (!active)
        {
            PrintError(active.Error());
            return 1;
        }
        const auto oldKey = RelocationPathKey(arguments->PreviousRoot);
        const auto newKey = RelocationPathKey(arguments->Root);
        if (arguments->Product != KeireHub::InstallProduct::Hub || !active.Value() ||
            RelocationPathKey(active.Value()->Root) != oldKey || oldKey == newKey || oldKey.starts_with(newKey + "/") ||
            newKey.starts_with(oldKey + "/"))
        {
            std::cerr << "Relocation requires the registered Hub and a distinct, non-nested destination.\n";
            return 1;
        }
        if (const auto tree = KeireHub::Detail::ValidateInstallTree(arguments->Root, true); !tree)
        {
            PrintError(tree.Error());
            return 1;
        }
        if (const auto path = KeireHub::Detail::ValidateInstallMutationPath(arguments->Root.root_path(),
                                                                            arguments->Root.relative_path(), true);
            !path)
        {
            PrintError(path.Error());
            return 1;
        }
        std::error_code error;
        const bool exists = std::filesystem::exists(arguments->Root, error);
        if (error || (exists && (!std::filesystem::is_directory(arguments->Root, error) || error ||
                                 !std::filesystem::is_empty(arguments->Root, error) || error)))
        {
            std::cerr << "The new Hub location must be an empty folder or an unused path.\n";
            return 1;
        }
        const auto manifest = KeireHub::Detail::ReadInstallerPackageManifest(arguments->Source, arguments->Product);
        if (!manifest)
        {
            PrintError(manifest.Error());
            return 1;
        }
        return 0;
    }
    KeireHub::InstallTransactionRequest request{.Product = arguments->Product,
                                                .SourceRoot = arguments->Source,
                                                .DestinationRoot = arguments->Root,
                                                .Registration = registrationStore,
                                                .DeferCommit = arguments->Command == "install-deferred",
                                                .RepairMissingFiles = arguments->RepairMissingFiles};
#if defined(KEIRE_INSTALL_WORKER_FAULT_INJECTION)
    const auto fault = FaultPhase();
#endif
    request.ContinueAfterPhase = [&](const KeireHub::InstallTransactionPhase phase)
    {
        timing.Phase(phase);
#if defined(KEIRE_INSTALL_WORKER_FAULT_INJECTION)
        return !fault || phase != *fault;
#else
        return true;
#endif
    };

    const auto readRegistration = [&]() { return registrationStore.Read(arguments->Product); };
    const auto recoverAndReconcile = [&]() -> KeireHub::HubStatus
    {
        if (const auto recovered = KeireHub::RecoverInstallTransaction(request); !recovered)
            return recovered;
        auto active = readRegistration();
        if (!active)
            return KeireHub::HubStatus::Failure(active.Error());
        return KeireInstallWorker::ReconcileShellIntegrations(arguments->Product, active.Value());
    };

    if (arguments->Command == "prepare")
    {
        if (const auto recovered = recoverAndReconcile(); !recovered)
        {
            PrintError(recovered.Error());
            return 1;
        }
        if (const auto prepared = KeireHub::PrepareInstallDestination(request); !prepared)
        {
            PrintError(prepared.Error());
            return 1;
        }
        return 0;
    }

    if (arguments->Command == "install" || arguments->Command == "install-deferred")
    {
        if (const auto recovered = recoverAndReconcile(); !recovered)
        {
            PrintError(recovered.Error());
            return 1;
        }
        const auto status = KeireHub::InstallPackageTransaction(request);
        if (!status)
        {
            PrintError(status.Error());
            return status.Error().Code == KeireHub::HubErrorCode::WorkerInterrupted ? 86 : 1;
        }
        return 0;
    }

    if (arguments->Command == "integrate")
    {
        auto active = readRegistration();
        if (!active || !active.Value() || active.Value()->Root.lexically_normal() != arguments->Root.lexically_normal())
        {
            if (!active)
                PrintError(active.Error());
            else
                std::cerr << "The active registration does not match the shell-integration root.\n";
            return 1;
        }
        auto previous = KeireHub::Detail::ReadPendingInstallPreviousRegistration(arguments->Root, arguments->Product);
        if (!previous)
        {
            PrintError(previous.Error());
            return 1;
        }
        const auto integrated = KeireInstallWorker::PrepareShellIntegrations(
            arguments->Product, *active.Value(), previous.Value(), arguments->StartMenu, arguments->Desktop);
        if (!integrated)
        {
            PrintError(integrated.Error());
            return 1;
        }
        return 0;
    }

    if (arguments->Command == "commit")
    {
        auto active = readRegistration();
        if (!active || !active.Value())
        {
            if (!active)
                PrintError(active.Error());
            else
                std::cerr << "The pending installation registration is missing.\n";
            return 1;
        }
        if (const auto integrated = KeireInstallWorker::CommitShellIntegrations(arguments->Product, *active.Value());
            !integrated)
        {
            PrintError(integrated.Error());
            return 1;
        }
        const auto committed = KeireHub::CommitInstallTransaction(request);
        if (!committed)
        {
            const auto original = committed.Error();
            (void)recoverAndReconcile();
            PrintError(original);
            return 1;
        }
        return 0;
    }

    if (arguments->Command == "recover")
    {
        const auto recovered = recoverAndReconcile();
        if (!recovered)
        {
            PrintError(recovered.Error());
            return 1;
        }
        return 0;
    }

    if (arguments->Command == "uninstall")
    {
        if (const auto recovered = recoverAndReconcile(); !recovered)
        {
            PrintError(recovered.Error());
            return 1;
        }
        auto active = readRegistration();
        if (!active)
        {
            PrintError(active.Error());
            return 1;
        }
        if (!active.Value())
            return 0;
        const auto result = KeireHub::UninstallPackageTransaction(request);
        if (!result)
        {
            PrintError(result.Error());
            return 1;
        }
        if (const auto removed = KeireInstallWorker::RemoveShellIntegrations(arguments->Product, *active.Value());
            !removed)
        {
            PrintError(removed.Error());
            return 1;
        }
        std::cout << "removedFiles=" << result.Value().RemovedFileCount
                  << " preservedModifiedFiles=" << result.Value().PreservedModifiedFileCount << '\n';
        return 0;
    }

    if (const auto recovered = recoverAndReconcile(); !recovered)
    {
        PrintError(recovered.Error());
        return 1;
    }
    const auto status = KeireHub::VerifyInstalledPackage(request);
    if (!status)
    {
        PrintError(status.Error());
        return status.Error().Code == KeireHub::HubErrorCode::WorkerInterrupted ? 86 : 1;
    }
    return 0;
}

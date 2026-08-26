#include "KeireInstallWorker/WorkerRegistration.h"
#include "KeireInstallWorker/ShellIntegration.h"

#include "KeireHubRuntime/InstallTransaction.h"
#include <KeireHubRuntimeInternal/InstallTransactionInternal.h>

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
        bool StartMenu = false;
        bool Desktop = false;
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
            else if (name == "--start-menu" || name == "--desktop")
            {
                const std::string_view enabled(values[index + 1]);
                if (enabled != "0" && enabled != "1")
                    return std::nullopt;
                (name == "--start-menu" ? result.StartMenu : result.Desktop) = enabled == "1";
            }
            else
            {
                return std::nullopt;
            }
        }
        if (result.Root.empty() ||
            ((result.Command == "install" || result.Command == "install-deferred") && result.Source.empty()))
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
} // namespace

int main(const int count, char** values)
{
    const auto arguments = ParseArguments(count, values);
    if (!arguments || (arguments->Command != "install" && arguments->Command != "install-deferred" &&
                       arguments->Command != "commit" && arguments->Command != "verify" &&
                       arguments->Command != "--verify-installation" && arguments->Command != "integrate" &&
                       arguments->Command != "recover" && arguments->Command != "uninstall"))
    {
        std::cerr
            << "Usage: KeireInstallWorker "
               "<install|install-deferred|integrate|commit|--verify-installation|recover|uninstall> "
               "--product <editor|hub> "
               "--root <absolute-path> [--source <absolute-path>] "
               "[--start-menu <0|1>] [--desktop <0|1>]\n";
        return 64;
    }
    auto registrationStore = KeireInstallWorker::CreateProductRegistrationStore(arguments->Product);
    KeireHub::InstallTransactionRequest request{
        .Product = arguments->Product,
        .SourceRoot = arguments->Source,
        .DestinationRoot = arguments->Root,
        .Registration = registrationStore,
        .DeferCommit = arguments->Command == "install-deferred"};
#if defined(KEIRE_INSTALL_WORKER_FAULT_INJECTION)
    const auto fault = FaultPhase();
    request.ContinueAfterPhase = [fault](const KeireHub::InstallTransactionPhase phase)
    { return !fault || phase != *fault; };
#endif

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
        if (!active || !active.Value() ||
            active.Value()->Root.lexically_normal() != arguments->Root.lexically_normal())
        {
            if (!active)
                PrintError(active.Error());
            else
                std::cerr << "The active registration does not match the shell-integration root.\n";
            return 1;
        }
        auto previous = KeireHub::Detail::ReadPendingInstallPreviousRegistration(arguments->Root,
                                                                                  arguments->Product);
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
        if (const auto integrated =
                KeireInstallWorker::CommitShellIntegrations(arguments->Product, *active.Value());
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
        if (const auto removed =
                KeireInstallWorker::RemoveShellIntegrations(arguments->Product, *active.Value());
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

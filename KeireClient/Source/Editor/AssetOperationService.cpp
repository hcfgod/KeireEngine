#include "KeireClient/Editor/AssetOperationService.h"

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace KeireEditor
{
    namespace
    {
        constexpr auto CooperativeCancellationTimeout = std::chrono::milliseconds(250);

        [[nodiscard]] std::filesystem::path AbsoluteNormalized(const std::filesystem::path& path)
        {
            return std::filesystem::absolute(path).lexically_normal();
        }
    } // namespace

    AssetOperationService::AssetOperationService(std::filesystem::path workerExecutable,
                                                 std::filesystem::path projectRoot)
        : m_WorkerExecutable(AbsoluteNormalized(std::move(workerExecutable))),
          m_ProjectRoot(AbsoluteNormalized(std::move(projectRoot)))
    {
        if (!std::filesystem::is_regular_file(m_WorkerExecutable))
            throw std::runtime_error("Kéire asset worker was not found: " +
                                     Keire::Detail::PathToUtf8(m_WorkerExecutable));
        if (!std::filesystem::is_directory(m_ProjectRoot / "Assets"))
            throw std::invalid_argument("Asset operation service requires a valid project root.");
    }

    AssetOperationService::~AssetOperationService() { Shutdown(); }

    void AssetOperationService::QueueImport(const AssetOperationPriority priority, AssetOperationContext context)
    {
        if (priority == AssetOperationPriority::AutomaticRefresh || priority == AssetOperationPriority::MaterialRefresh)
        {
            const auto queued =
                std::ranges::find_if(m_Queue,
                                     [priority](const PendingOperation& item)
                                     {
                                         return item.Priority == priority &&
                                                item.Request.Kind == Keire::Detail::AssetWorkerOperationKind::ImportAll;
                                     });
            if (queued != m_Queue.end())
            {
                queued->Context.Generation = std::max(queued->Context.Generation, context.Generation);
                if (queued->Context.ReloadAsset != context.ReloadAsset)
                    queued->Context.ReloadAsset = {};
                return;
            }
        }
        PendingOperation operation;
        operation.Request.Kind = Keire::Detail::AssetWorkerOperationKind::ImportAll;
        operation.Priority = priority;
        operation.Context = context;
        Queue(std::move(operation));
    }

    void AssetOperationService::QueueExternalImport(std::vector<Keire::ExternalAssetImportItem> items,
                                                    AssetOperationContext context)
    {
        if (items.empty())
            throw std::invalid_argument("External asset import requires at least one item.");
        PendingOperation operation;
        operation.Request.Kind = Keire::Detail::AssetWorkerOperationKind::ExternalImport;
        operation.Request.ExternalItems = std::move(items);
        operation.Priority = AssetOperationPriority::ExternalImport;
        operation.Context = context;
        Queue(std::move(operation));
    }

    void AssetOperationService::QueueCreateAsset(std::filesystem::path relativePath, std::vector<std::byte> source,
                                                 Keire::AssetImportSettings settings, AssetOperationContext context)
    {
        QueueCreateAssetWithAuxiliary(std::move(relativePath), std::move(source), std::move(settings),
                                      std::move(context), {});
    }

    void AssetOperationService::QueueCreateAssetWithAuxiliary(
        std::filesystem::path relativePath, std::vector<std::byte> source, Keire::AssetImportSettings settings,
        AssetOperationContext context, std::vector<AssetCreationAuxiliarySource> auxiliarySources)
    {
        if (relativePath.empty() || relativePath.is_absolute() || relativePath.filename().empty() ||
            relativePath.lexically_normal().generic_string().starts_with(".."))
            throw std::invalid_argument("Asset creation requires a confined project-relative path.");
        PendingOperation operation;
        operation.Request.Kind = Keire::Detail::AssetWorkerOperationKind::CreateAsset;
        operation.Request.CreateRelativePath = std::move(relativePath);
        operation.Request.CreateSettings = std::move(settings);
        operation.Payload = std::move(source);
        for (const auto& auxiliary : auxiliarySources)
        {
            if (auxiliary.RelativePath.empty() || auxiliary.RelativePath.is_absolute() ||
                auxiliary.RelativePath.filename().empty() ||
                auxiliary.RelativePath.lexically_normal().generic_string().starts_with(".."))
                throw std::invalid_argument("Asset creation auxiliary sources require confined relative paths.");
        }
        operation.AuxiliaryPayloads = std::move(auxiliarySources);
        operation.Priority = AssetOperationPriority::ExplicitAction;
        operation.Context = std::move(context);
        Queue(std::move(operation));
    }

    void AssetOperationService::QueueCook(Keire::AssetBuildProfile profile, std::filesystem::path output)
    {
        PendingOperation operation;
        operation.Request.Kind = Keire::Detail::AssetWorkerOperationKind::Cook;
        operation.Request.BuildProfile = std::move(profile);
        operation.Request.CookOutput = std::move(output);
        operation.Priority = AssetOperationPriority::Cook;
        Queue(std::move(operation));
    }

    void AssetOperationService::QueueExtractMaterials(const Keire::AssetId model,
                                                      std::filesystem::path relativeDirectory,
                                                      AssetOperationContext context)
    {
        if (!model || relativeDirectory.empty() || relativeDirectory.is_absolute() ||
            relativeDirectory.lexically_normal().generic_string().starts_with(".."))
            throw std::invalid_argument("Material extraction requires a model and confined relative directory.");
        PendingOperation operation;
        operation.Request.Kind = Keire::Detail::AssetWorkerOperationKind::ExtractMaterials;
        operation.Request.ExtractModel = model;
        operation.Request.ExtractDirectory = std::move(relativeDirectory);
        operation.Priority = AssetOperationPriority::ExplicitAction;
        operation.Context = std::move(context);
        Queue(std::move(operation));
    }

    void AssetOperationService::QueueMutation(Keire::Detail::AssetWorkerMutation mutation,
                                              AssetOperationContext context)
    {
        PendingOperation operation;
        operation.Request.Kind = Keire::Detail::AssetWorkerOperationKind::Mutate;
        operation.Request.Mutation = std::move(mutation);
        operation.Priority = AssetOperationPriority::ExplicitAction;
        operation.Context = std::move(context);
        Queue(std::move(operation));
    }

    void AssetOperationService::QueueReceipt(const Keire::ExternalAssetImportReceiptId receipt, const bool redo)
    {
        if (!receipt)
            throw std::invalid_argument("Asset receipt operation requires a receipt.");
        PendingOperation operation;
        operation.Request.Kind = redo ? Keire::Detail::AssetWorkerOperationKind::RedoExternalImport
                                      : Keire::Detail::AssetWorkerOperationKind::UndoExternalImport;
        operation.Request.Receipt = receipt;
        operation.Priority = AssetOperationPriority::UndoRedo;
        Queue(std::move(operation));
    }

    void AssetOperationService::Queue(PendingOperation operation)
    {
        if (m_ShuttingDown)
            throw std::logic_error("Asset operation service is shutting down.");
        operation.Request.ProjectRoot = m_ProjectRoot;
        operation.Request.OperationId = Keire::AssetId::Generate().ToString();
        operation.Sequence = m_NextSequence++;
        const auto position = std::ranges::find_if(
            m_Queue, [&operation](const PendingOperation& queued)
            { return static_cast<unsigned>(queued.Priority) > static_cast<unsigned>(operation.Priority); });
        m_Queue.insert(position, std::move(operation));
    }

    void AssetOperationService::Update()
    {
        if (m_ShuttingDown)
            return;
        if (!m_Running)
            StartNext();
        if (!m_Running)
            return;
        try
        {
            if (const auto progress = Keire::Detail::ReadAssetWorkerProgress(m_Running->ProgressPath))
                m_Progress = *progress;
        }
        catch (const std::exception&)
        {
            // Each document is atomically replaced. A malformed final document is handled when the worker exits.
        }
        if (m_Running->Process.Poll())
            FinishCurrent();
    }

    bool AssetOperationService::PreemptBackgroundImports()
    {
        const auto queuedBefore = m_Queue.size();
        std::erase_if(m_Queue,
                      [](const PendingOperation& operation)
                      {
                          return (operation.Priority == AssetOperationPriority::AutomaticRefresh ||
                                  operation.Priority == AssetOperationPriority::MaterialRefresh) &&
                                 operation.Request.Kind == Keire::Detail::AssetWorkerOperationKind::ImportAll;
                      });
        bool preempted = m_Queue.size() != queuedBefore;
        if (m_Running &&
            (m_Running->Pending.Priority == AssetOperationPriority::AutomaticRefresh ||
             m_Running->Pending.Priority == AssetOperationPriority::MaterialRefresh) &&
            m_Running->Pending.Request.Kind == Keire::Detail::AssetWorkerOperationKind::ImportAll)
        {
            CancelCurrent();
            constexpr auto interactivePreemptionTimeout = std::chrono::milliseconds(25);
            if (!m_Running->Process.WaitFor(interactivePreemptionTimeout))
                m_Running->Process.Terminate();
            FinishCurrent();
            auto& completion = m_Completions.back();
            completion.Result.Success = false;
            completion.Result.Cancelled = true;
            completion.Result.Diagnostic = "Background asset refresh yielded to an interactive editor action.";
            preempted = true;
        }
        return preempted;
    }

    void AssetOperationService::StartNext()
    {
        if (m_Queue.empty())
            return;
        auto pending = std::move(m_Queue.front());
        m_Queue.pop_front();
        const auto directory = m_ProjectRoot / "Library" / "AssetOperations" / pending.Request.OperationId;
        std::filesystem::create_directories(directory);
        const auto requestPath = directory / "request.json";
        const auto progressPath = directory / "progress.json";
        const auto resultPath = directory / "result.json";
        const auto cancelPath = directory / "cancel";
        if (pending.Request.Kind == Keire::Detail::AssetWorkerOperationKind::CreateAsset)
        {
            const auto extension = pending.Request.CreateRelativePath.extension();
            auto payloadPath = std::filesystem::path("source");
            payloadPath += extension.native();
            pending.Request.CreatePayloadPath = directory / payloadPath;
            std::string payload;
            if (!pending.Payload.empty())
                payload.assign(reinterpret_cast<const char*>(pending.Payload.data()), pending.Payload.size());
            Keire::Detail::WriteTextFileAtomically(pending.Request.CreatePayloadPath, payload);
            pending.Payload.clear();
            pending.Payload.shrink_to_fit();
            for (std::size_t index = 0; index < pending.AuxiliaryPayloads.size(); ++index)
            {
                auto auxiliaryPayloadPath = std::filesystem::path("auxiliary-" + std::to_string(index));
                auxiliaryPayloadPath += pending.AuxiliaryPayloads[index].RelativePath.extension().native();
                auxiliaryPayloadPath = directory / auxiliaryPayloadPath;
                std::string auxiliaryPayload;
                if (!pending.AuxiliaryPayloads[index].Source.empty())
                {
                    auxiliaryPayload.assign(
                        reinterpret_cast<const char*>(pending.AuxiliaryPayloads[index].Source.data()),
                        pending.AuxiliaryPayloads[index].Source.size());
                }
                Keire::Detail::WriteTextFileAtomically(auxiliaryPayloadPath, auxiliaryPayload);
                pending.Request.CreateAuxiliarySources.push_back(
                    {pending.AuxiliaryPayloads[index].RelativePath, std::move(auxiliaryPayloadPath)});
            }
            pending.AuxiliaryPayloads.clear();
            pending.AuxiliaryPayloads.shrink_to_fit();
        }
        pending.Request.SourceIndexPath = m_ProjectRoot / "Library/AssetCache/Runtime/source-index.json";
        Keire::Detail::WriteAssetWorkerRequest(requestPath, pending.Request);
        std::vector<std::string> arguments{
            "--request", Keire::Detail::PathToUtf8(requestPath), "--progress", Keire::Detail::PathToUtf8(progressPath),
            "--result",  Keire::Detail::PathToUtf8(resultPath),  "--cancel",   Keire::Detail::PathToUtf8(cancelPath)};
        auto process = Keire::Detail::ChildProcess::Start(m_WorkerExecutable, arguments, m_ProjectRoot);
        m_Running.emplace(
            RunningOperation{std::move(pending), directory, progressPath, resultPath, cancelPath, std::move(process)});
        m_Progress = Keire::AssetOperationProgress{.Phase = Keire::AssetOperationPhase::Scanning};
    }

    void AssetOperationService::FinishCurrent()
    {
        auto running = std::move(*m_Running);
        m_Running.reset();
        AssetOperationCompletion completion;
        completion.Kind = running.Pending.Request.Kind;
        completion.Context = running.Pending.Context;
        completion.SourceIndexPath = running.Pending.Request.SourceIndexPath;
        completion.WorkerOutput = running.Process.TakeOutput();
        const auto exitCode = running.Process.ExitCode().value_or(127);
        try
        {
            Keire::Detail::WriteTextFileAtomically(running.Directory / "worker.log", completion.WorkerOutput);
            if (!std::filesystem::is_regular_file(running.ResultPath))
            {
                completion.Result.Success = false;
                completion.Result.Diagnostic =
                    "Asset worker terminated unexpectedly with exit code " + std::to_string(exitCode) +
                    " before writing its result document. See worker.log in the operation directory.";
            }
            else
            {
                completion.Result = Keire::Detail::ReadAssetWorkerResult(running.ResultPath);
                if (exitCode != 0)
                    completion.Result.Success = false;
                if (!completion.Result.Success && completion.Result.Diagnostic.empty())
                    completion.Result.Diagnostic = "Asset worker exited with code " + std::to_string(exitCode) + ".";
            }
        }
        catch (const std::exception& error)
        {
            completion.Result.Success = false;
            completion.Result.Diagnostic = "Asset worker result could not be read after exit code " +
                                           std::to_string(exitCode) + ": " + error.what();
        }
        m_Completions.push_back(std::move(completion));
        m_Progress.reset();
    }

    void AssetOperationService::CancelCurrent()
    {
        if (!m_Running)
            return;
        Keire::Detail::WriteTextFileAtomically(m_Running->CancelPath, "cancel\n");
    }

    void AssetOperationService::Shutdown() noexcept
    {
        if (m_ShuttingDown)
            return;
        m_ShuttingDown = true;
        m_Queue.clear();
        if (!m_Running)
            return;
        try
        {
            CancelCurrent();
            if (!m_Running->Process.WaitFor(CooperativeCancellationTimeout))
            {
                m_Running->Process.Terminate();
                (void)m_Running->Process.WaitFor(CooperativeCancellationTimeout);
            }
            (void)m_Running->Process.TakeOutput();
        }
        catch (...)
        {
        }
        m_Running.reset();
        m_Progress.reset();
    }

    std::optional<AssetOperationCompletion> AssetOperationService::TakeCompletion()
    {
        if (m_Completions.empty())
            return std::nullopt;
        auto result = std::move(m_Completions.front());
        m_Completions.pop_front();
        return result;
    }

    std::optional<Keire::AssetOperationProgress> AssetOperationService::Progress() const noexcept { return m_Progress; }

    bool AssetOperationService::Busy() const noexcept { return m_Running.has_value() || !m_Queue.empty(); }

    bool AssetOperationService::Publishing() const noexcept
    {
        return m_Progress && (m_Progress->Phase == Keire::AssetOperationPhase::Publishing ||
                              m_Progress->Phase == Keire::AssetOperationPhase::RollingBack);
    }

    std::filesystem::path AssetOperationService::ResolveWorkerExecutable(const std::filesystem::path& editorExecutable)
    {
        const auto editor = AbsoluteNormalized(editorExecutable);
#if defined(_WIN32)
        constexpr std::string_view workerName = "KeireAssetWorker.exe";
#else
        constexpr std::string_view workerName = "KeireAssetWorker";
#endif
        const auto sibling = editor.parent_path() / workerName;
        if (std::filesystem::is_regular_file(sibling))
            return sibling;
        const auto configurationRoot = editor.parent_path().parent_path();
        const auto development = configurationRoot / "KeireAssetWorker" / workerName;
        return std::filesystem::is_regular_file(development) ? development : sibling;
    }
} // namespace KeireEditor

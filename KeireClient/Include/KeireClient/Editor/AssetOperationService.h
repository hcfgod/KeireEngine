#pragma once

#include "Keire/Core.h"

#include "KeireInternal/Assets/AssetWorkerProtocol.h"
#include "KeireInternal/Process.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace KeireEditor
{
    enum class AssetOperationPriority : std::uint8_t
    {
        ExternalImport,
        ExplicitAction,
        UndoRedo,
        Cook,
        AutomaticRefresh,
        MaterialRefresh
    };

    enum class AssetOperationFollowUp : std::uint8_t
    {
        None,
        Reveal,
        OpenScene,
        OpenInputActions,
        OpenExternal,
        OpenMaterialGraph,
        OpenShaderGraph,
        AdoptSceneCopy
    };

    enum class AssetMutationPhase : std::uint8_t
    {
        Initial,
        Undo,
        Redo
    };

    struct AssetMutationUndoState
    {
        Keire::Detail::AssetWorkerMutation Forward;
        Keire::Detail::AssetWorkerMutation Reverse;
        std::string Name;
        bool RecordCommand = true;
        bool RevealResult = false;
    };

    enum class GraphFunctionExtractionKind : std::uint8_t
    {
        Shader,
        Material
    };

    struct GraphFunctionExtractionState
    {
        GraphFunctionExtractionKind Kind = GraphFunctionExtractionKind::Shader;
        Keire::AssetId SourceAsset;
        Keire::AssetId PlaceholderAsset;
        std::optional<Keire::ShaderGraphDefinition> ShaderBefore;
        std::optional<Keire::ShaderGraphDefinition> ShaderAfter;
        std::optional<Keire::MaterialGraphDefinition> MaterialBefore;
        std::optional<Keire::MaterialGraphDefinition> MaterialAfter;
    };

    struct AssetOperationContext
    {
        Keire::AssetId ReloadAsset;
        std::uint64_t Generation = 0;
        bool Viewport = false;
        Keire::EntityId ViewportTarget;
        AssetOperationFollowUp FollowUp = AssetOperationFollowUp::None;
        std::string UndoName;
        std::optional<Keire::SceneDefinition> SceneSnapshot;
        Keire::AssetId SourceSceneAsset;
        std::filesystem::path SceneSource;
        std::shared_ptr<AssetMutationUndoState> MutationUndo;
        std::shared_ptr<GraphFunctionExtractionState> GraphFunctionExtraction;
        AssetMutationPhase MutationPhase = AssetMutationPhase::Initial;
        Keire::AssetId ManagedAssembly;
        std::filesystem::path ManagedSourceRoot;
        Keire::AssetId ParentSource;
        std::string Reason;
    };

    struct AssetOperationCompletion
    {
        Keire::Detail::AssetWorkerOperationKind Kind = Keire::Detail::AssetWorkerOperationKind::ImportAll;
        Keire::Detail::AssetWorkerResult Result;
        AssetOperationContext Context;
        std::filesystem::path SourceIndexPath;
        std::string WorkerOutput;
    };

    struct AssetCreationAuxiliarySource
    {
        std::filesystem::path RelativePath;
        std::vector<std::byte> Source;
    };

    class AssetOperationService final
    {
      public:
        AssetOperationService(const std::filesystem::path& workerExecutable, const std::filesystem::path& projectRoot);
        ~AssetOperationService();

        AssetOperationService(const AssetOperationService&) = delete;
        AssetOperationService& operator=(const AssetOperationService&) = delete;

        void QueueImport(AssetOperationPriority priority, const AssetOperationContext& context = {});
        void QueueAssetImport(Keire::AssetId asset, AssetOperationPriority priority,
                              AssetOperationContext context = {});
        void QueueAssetImport(std::vector<Keire::AssetId> assets, AssetOperationPriority priority,
                              AssetOperationContext context = {});
        void QueueExternalImport(std::vector<Keire::ExternalAssetImportItem> items, AssetOperationContext context);
        void QueueCreateAsset(std::filesystem::path relativePath, std::vector<std::byte> source,
                              Keire::AssetImportSettings settings, AssetOperationContext context);
        void QueueCreateAssetWithAuxiliary(std::filesystem::path relativePath, std::vector<std::byte> source,
                                           Keire::AssetImportSettings settings, AssetOperationContext context,
                                           std::vector<AssetCreationAuxiliarySource> auxiliarySources);
        void QueueExtractMaterials(Keire::AssetId model, std::filesystem::path relativeDirectory,
                                   AssetOperationContext context = {});
        void QueueMutation(Keire::Detail::AssetWorkerMutation mutation, AssetOperationContext context = {});
        void QueueCook(Keire::AssetBuildProfile profile, std::filesystem::path output);
        void QueueLightingBake(Keire::AssetId scene, bool force, AssetOperationContext context = {});
        void QueueReceipt(Keire::ExternalAssetImportReceiptId receipt, bool redo);
        void Update();
        [[nodiscard]] bool PreemptBackgroundImports();
        void CancelCurrent();
        void Shutdown() noexcept;

        [[nodiscard]] std::optional<AssetOperationCompletion> TakeCompletion();
        [[nodiscard]] std::optional<Keire::AssetOperationProgress> Progress() const noexcept;
        [[nodiscard]] bool Busy() const noexcept;
        [[nodiscard]] bool Publishing() const noexcept;
        [[nodiscard]] std::size_t QueuedCount() const noexcept { return m_Queue.size(); }

        [[nodiscard]] static std::filesystem::path
        ResolveWorkerExecutable(const std::filesystem::path& editorExecutable);

      private:
        struct PendingOperation
        {
            Keire::Detail::AssetWorkerRequest Request;
            AssetOperationPriority Priority = AssetOperationPriority::ExplicitAction;
            AssetOperationContext Context;
            std::vector<std::byte> Payload;
            std::vector<AssetCreationAuxiliarySource> AuxiliaryPayloads;
            std::uint64_t Sequence = 0;
        };

        struct RunningOperation
        {
            PendingOperation Pending;
            std::filesystem::path Directory;
            std::filesystem::path ProgressPath;
            std::filesystem::path ResultPath;
            std::filesystem::path CancelPath;
            Keire::Detail::ChildProcess Process;
        };

        void Queue(PendingOperation operation);
        void StartNext();
        void FinishCurrent();

        std::filesystem::path m_WorkerExecutable;
        std::filesystem::path m_ProjectRoot;
        std::deque<PendingOperation> m_Queue;
        std::optional<RunningOperation> m_Running;
        std::deque<AssetOperationCompletion> m_Completions;
        std::optional<Keire::AssetOperationProgress> m_Progress;
        std::uint64_t m_NextSequence = 1;
        bool m_ShuttingDown = false;
    };
} // namespace KeireEditor

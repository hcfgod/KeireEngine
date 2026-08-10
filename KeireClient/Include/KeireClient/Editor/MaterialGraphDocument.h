#pragma once

#include "Keire/Jobs/JobSystem.h"
#include "Keire/Rendering/MaterialGraph.h"
#include "KeireClient/Editor/AssetDocumentHost.h"
#include "KeireClient/Editor/AuthoringWidgets.h"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor
{
    struct MaterialGraphPreviewSettings
    {
        Keire::MaterialGraphPreviewMesh Mesh = Keire::MaterialGraphPreviewMesh::Sphere;
        Keire::AssetId CustomMesh;
        Keire::AssetId Environment;
        std::vector<std::string> Keywords;
        float Exposure = 1.0F;
        float EnvironmentIntensity = 1.0F;
        float RotationDegrees = 33.0F;

        bool operator==(const MaterialGraphPreviewSettings&) const = default;
    };

    struct MaterialGraphDocumentSpecification
    {
        Keire::MaterialGraphCompileOptions CompileOptions;
        std::function<void(Keire::AssetId, const Keire::MaterialGraphCompilation&, const MaterialGraphPreviewSettings&)>
            Preview;
        std::function<void(Keire::AssetId, const Keire::MaterialGraphDefinition&,
                           const Keire::MaterialGraphCompilation&,
                           std::span<const Keire::Ref<Keire::ShaderAsset>> developmentShaders)>
            LiveApply;
        std::function<void(Keire::AssetId)> StopPreview;
        std::function<void(Keire::AssetId, std::span<const std::byte>)> Persist;
    };

    struct MaterialGraphCanvasModel
    {
        std::vector<NodeGraphNode> Nodes;
        std::vector<NodeGraphConnection> Connections;
        std::vector<std::pair<StableNodeId, Keire::AssetId>> NodeIdentities;
        std::vector<std::pair<StableNodeId, Keire::AssetId>> PinIdentities;
        std::vector<std::pair<StableNodeId, Keire::AssetId>> ConnectionIdentities;

        [[nodiscard]] std::optional<Keire::AssetId> Node(StableNodeId id) const noexcept;
        [[nodiscard]] std::optional<Keire::AssetId> Pin(StableNodeId id) const noexcept;
        [[nodiscard]] std::optional<Keire::AssetId> Connection(StableNodeId id) const noexcept;
    };

    class MaterialGraphDocument final
    {
      public:
        using Host = AssetDocumentHost<Keire::MaterialGraphDefinition>;

        explicit MaterialGraphDocument(MaterialGraphDocumentSpecification specification);
        ~MaterialGraphDocument() noexcept;

        MaterialGraphDocument(const MaterialGraphDocument&) = delete;
        MaterialGraphDocument& operator=(const MaterialGraphDocument&) = delete;

        void Open(Keire::AssetId asset, std::span<const std::byte> bytes, std::uint64_t revision,
                  Keire::Ref<Keire::UndoContext> undo = {});
        void Open(Keire::AssetId asset, Keire::MaterialGraphDefinition definition, std::uint64_t revision,
                  Keire::Ref<Keire::UndoContext> undo = {});
        void Create(Keire::AssetId asset,
                    Keire::MaterialGraphDefinition definition = Keire::CreateDefaultMaterialGraph(),
                    Keire::Ref<Keire::UndoContext> undo = {});
        void Save();
        void Discard();
        void Close() noexcept;
        void SetJobSystem(Keire::Ref<Keire::JobSystem> jobs);
        [[nodiscard]] bool Undo();
        [[nodiscard]] bool Redo();

        [[nodiscard]] bool IsOpen() const noexcept { return m_Host.IsOpen(); }
        [[nodiscard]] bool Dirty() const noexcept { return m_Host.Dirty(); }
        [[nodiscard]] bool Publishable() const noexcept { return m_Compilation.Succeeded(); }
        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Host.Asset(); }
        [[nodiscard]] const Keire::MaterialGraphDefinition& Definition() const { return m_Host.Draft(); }
        [[nodiscard]] const Keire::MaterialGraphCompilation& Compilation() const noexcept { return m_Compilation; }
        [[nodiscard]] const std::optional<Keire::MaterialGraphCompilation>& LastGoodCompilation() const noexcept
        {
            return m_LastGoodCompilation;
        }
        [[nodiscard]] const std::optional<Keire::MaterialGraphDefinition>& LastGoodDefinition() const noexcept
        {
            return m_LastGoodDefinition;
        }
        [[nodiscard]] std::string_view Diagnostic() const noexcept;
        [[nodiscard]] Keire::Ref<Keire::UndoContext> UndoContext() const noexcept { return m_Host.UndoContext(); }

        [[nodiscard]] bool Edit(std::string_view name,
                                const std::function<void(Keire::MaterialGraphDefinition&)>& operation);
        [[nodiscard]] bool AddNode(Keire::MaterialGraphNode node);
        [[nodiscard]] bool EditNode(Keire::AssetId node,
                                    const std::function<void(Keire::MaterialGraphNode&)>& operation);
        [[nodiscard]] bool MoveNode(Keire::AssetId node, Keire::Vector2 position);
        [[nodiscard]] bool RemoveNode(Keire::AssetId node);
        [[nodiscard]] bool AddConnection(Keire::MaterialGraphConnection connection);
        [[nodiscard]] bool RemoveConnection(Keire::AssetId connection);
        [[nodiscard]] NodeGraphConnectionValidation CheckConnection(Keire::MaterialGraphEndpoint output,
                                                                    Keire::MaterialGraphEndpoint input) const;

        [[nodiscard]] MaterialGraphCanvasModel BuildCanvasModel() const;
        void SetCompileOptions(Keire::MaterialGraphCompileOptions options);
        void SetPreviewSettings(MaterialGraphPreviewSettings settings);
        [[nodiscard]] const MaterialGraphPreviewSettings& PreviewSettings() const noexcept { return m_PreviewSettings; }
        void AdvanceCompilation(double deltaSeconds);
        [[nodiscard]] bool CompilationPending() const noexcept;
        void ApplyLiveRevision() const;

      private:
        struct BackgroundCompilation
        {
            std::uint64_t Generation = 0;
            Keire::MaterialGraphDefinition Definition;
            Keire::MaterialGraphCompilation Compilation;
            std::vector<Keire::Ref<Keire::ShaderAsset>> DevelopmentShaders;
        };

        struct BackgroundCompilationState
        {
            std::mutex Mutex;
            std::optional<BackgroundCompilation> Result;
        };

        void QueueCompilation(const Keire::MaterialGraphDefinition& definition);
        void StartPendingCompilation();
        void ConsumeBackgroundCompilation(bool wait);
        void EnsureJobScope();
        void CancelBackgroundCompilation() noexcept;
        void ApplyCompilation(Keire::MaterialGraphDefinition definition, Keire::MaterialGraphCompilation compilation,
                              std::vector<Keire::Ref<Keire::ShaderAsset>> developmentShaders = {});
        void CompileAndPreview(const Keire::MaterialGraphDefinition& definition, bool compileDevelopmentShaders);
        void RecompileCurrent(bool compileDevelopmentShaders = false);

        MaterialGraphDocumentSpecification m_Specification;
        Host m_Host;
        Keire::MaterialGraphCompilation m_Compilation;
        std::optional<Keire::MaterialGraphCompilation> m_LastGoodCompilation;
        std::optional<Keire::MaterialGraphDefinition> m_LastGoodDefinition;
        std::vector<Keire::Ref<Keire::ShaderAsset>> m_LastGoodDevelopmentShaders;
        std::optional<Keire::MaterialGraphDefinition> m_PendingDefinition;
        Keire::Ref<Keire::JobSystem> m_JobSystem;
        Keire::Ref<Keire::JobScope> m_JobScope;
        Keire::JobHandle m_BackgroundCompilation;
        std::shared_ptr<BackgroundCompilationState> m_BackgroundCompilationState;
        MaterialGraphPreviewSettings m_PreviewSettings;
        std::string m_Diagnostic;
        double m_CompileDebounceSeconds = 0.0;
        std::uint64_t m_RequestedGeneration = 0;
        std::uint64_t m_InFlightGeneration = 0;
        bool m_OwnJobSystem = false;
    };
} // namespace KeireEditor

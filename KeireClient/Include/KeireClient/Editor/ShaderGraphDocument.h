#pragma once

#include "Keire/Jobs/JobSystem.h"
#include "Keire/Rendering/MaterialEcosystem.h"
#include "Keire/Rendering/ShaderGraph.h"
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
    struct ShaderGraphPreviewSettings
    {
        Keire::ShaderGraphPreviewMesh Mesh = Keire::ShaderGraphPreviewMesh::Sphere;
        Keire::AssetId CustomMesh;
        Keire::AssetId Environment;
        std::vector<std::string> Keywords;
        float Exposure = 1.0F;
        float EnvironmentIntensity = 1.0F;
        float RotationDegrees = 33.0F;

        bool operator==(const ShaderGraphPreviewSettings&) const = default;
    };

    struct ShaderGraphDocumentSpecification
    {
        Keire::ShaderGraphCompileOptions CompileOptions;
        std::function<void(Keire::AssetId, const Keire::ShaderGraphCompilation&, const ShaderGraphPreviewSettings&)>
            Preview;
        std::function<void(Keire::AssetId, const Keire::ShaderGraphDefinition&, const Keire::ShaderGraphCompilation&,
                           std::span<const Keire::Ref<Keire::ShaderAsset>> developmentShaders)>
            LiveApply;
        std::function<void(Keire::AssetId)> StopPreview;
        std::function<void(Keire::AssetId, std::span<const std::byte>)> Persist;
    };

    struct ShaderGraphCanvasModel
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

    class ShaderGraphDocument final
    {
      public:
        using Host = AssetDocumentHost<Keire::ShaderGraphDefinition>;

        explicit ShaderGraphDocument(ShaderGraphDocumentSpecification specification);
        ~ShaderGraphDocument() noexcept;

        ShaderGraphDocument(const ShaderGraphDocument&) = delete;
        ShaderGraphDocument& operator=(const ShaderGraphDocument&) = delete;

        void Open(Keire::AssetId asset, std::span<const std::byte> bytes, std::uint64_t revision,
                  Keire::Ref<Keire::UndoContext> undo = {});
        void Open(Keire::AssetId asset, Keire::ShaderGraphDefinition definition, std::uint64_t revision,
                  Keire::Ref<Keire::UndoContext> undo = {});
        void Open(Keire::AssetId asset, Keire::GraphFunctionDefinition definition, std::uint64_t revision,
                  Keire::Ref<Keire::UndoContext> undo = {});
        void Create(Keire::AssetId asset, Keire::ShaderGraphDefinition definition = Keire::CreateDefaultShaderGraph(),
                    Keire::Ref<Keire::UndoContext> undo = {});
        void Save();
        void Discard();
        void Close() noexcept;
        void SetJobSystem(Keire::Ref<Keire::JobSystem> jobs);
        [[nodiscard]] bool Undo();
        [[nodiscard]] bool Redo();

        [[nodiscard]] bool IsOpen() const noexcept { return m_Host.IsOpen(); }
        [[nodiscard]] bool Dirty() const noexcept { return m_Host.Dirty(); }
        [[nodiscard]] bool Publishable() const noexcept;
        [[nodiscard]] bool ReusableGraph() const noexcept;
        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Host.Asset(); }
        [[nodiscard]] const Keire::ShaderGraphDefinition& Definition() const { return m_Host.Draft(); }
        [[nodiscard]] const Keire::ShaderGraphCompilation& Compilation() const noexcept { return m_Compilation; }
        [[nodiscard]] const std::optional<Keire::ShaderGraphCompilation>& LastGoodCompilation() const noexcept
        {
            return m_LastGoodCompilation;
        }
        [[nodiscard]] const std::optional<Keire::ShaderGraphDefinition>& LastGoodDefinition() const noexcept
        {
            return m_LastGoodDefinition;
        }
        [[nodiscard]] std::string_view Diagnostic() const noexcept;
        [[nodiscard]] Keire::Ref<Keire::UndoContext> UndoContext() const noexcept { return m_Host.UndoContext(); }

        [[nodiscard]] bool Edit(std::string_view name,
                                const std::function<void(Keire::ShaderGraphDefinition&)>& operation);
        [[nodiscard]] bool AddNode(Keire::ShaderGraphNode node);
        [[nodiscard]] bool EditNode(Keire::AssetId node, const std::function<void(Keire::ShaderGraphNode&)>& operation);
        [[nodiscard]] bool MoveNode(Keire::AssetId node, Keire::Vector2 position);
        [[nodiscard]] bool MoveNodes(std::span<const std::pair<Keire::AssetId, Keire::Vector2>> nodes);
        [[nodiscard]] bool RemoveNode(Keire::AssetId node);
        [[nodiscard]] bool RemoveNodes(std::span<const Keire::AssetId> nodes);
        [[nodiscard]] bool AddConnection(Keire::ShaderGraphConnection connection);
        [[nodiscard]] bool SetConnectionRouting(Keire::AssetId connection, std::vector<Keire::Vector2> routingPoints);
        [[nodiscard]] bool RemoveConnection(Keire::AssetId connection);
        [[nodiscard]] NodeGraphConnectionValidation CheckConnection(Keire::ShaderGraphEndpoint output,
                                                                    Keire::ShaderGraphEndpoint input) const;

        [[nodiscard]] ShaderGraphCanvasModel BuildCanvasModel() const;
        void SetCompileOptions(Keire::ShaderGraphCompileOptions options);
        void SetPreviewSettings(ShaderGraphPreviewSettings settings);
        [[nodiscard]] const ShaderGraphPreviewSettings& PreviewSettings() const noexcept { return m_PreviewSettings; }
        void AdvanceCompilation(double deltaSeconds);
        [[nodiscard]] bool CompilationPending() const noexcept;
        void ApplyLiveRevision() const;

      private:
        struct BackgroundCompilation
        {
            std::uint64_t Generation = 0;
            Keire::ShaderGraphDefinition Definition;
            Keire::ShaderGraphCompilation Compilation;
            std::vector<Keire::Ref<Keire::ShaderAsset>> DevelopmentShaders;
        };

        struct BackgroundCompilationState
        {
            std::mutex Mutex;
            std::optional<BackgroundCompilation> Result;
        };

        void QueueCompilation(const Keire::ShaderGraphDefinition& definition);
        void StartPendingCompilation();
        void ConsumeBackgroundCompilation(bool wait);
        void EnsureJobScope();
        void CancelBackgroundCompilation() noexcept;
        void ApplyCompilation(Keire::ShaderGraphDefinition definition, Keire::ShaderGraphCompilation compilation,
                              std::vector<Keire::Ref<Keire::ShaderAsset>> developmentShaders = {});
        void CompileAndPreview(const Keire::ShaderGraphDefinition& definition, bool compileDevelopmentShaders);
        void RecompileCurrent(bool compileDevelopmentShaders = false);

        ShaderGraphDocumentSpecification m_Specification;
        Host m_Host;
        Keire::ShaderGraphCompilation m_Compilation;
        std::optional<Keire::ShaderGraphCompilation> m_LastGoodCompilation;
        std::optional<Keire::ShaderGraphDefinition> m_LastGoodDefinition;
        std::vector<Keire::Ref<Keire::ShaderAsset>> m_LastGoodDevelopmentShaders;
        std::optional<Keire::ShaderGraphDefinition> m_PendingDefinition;
        Keire::Ref<Keire::JobSystem> m_JobSystem;
        Keire::Ref<Keire::JobScope> m_JobScope;
        Keire::JobHandle m_BackgroundCompilation;
        std::shared_ptr<BackgroundCompilationState> m_BackgroundCompilationState;
        ShaderGraphPreviewSettings m_PreviewSettings;
        std::string m_Diagnostic;
        double m_CompileDebounceSeconds = 0.0;
        std::uint64_t m_RequestedGeneration = 0;
        std::uint64_t m_InFlightGeneration = 0;
        bool m_OwnJobSystem = false;
        bool m_ReusableValid = false;
        std::optional<Keire::GraphFunctionDefinition> m_FunctionMetadata;
    };
} // namespace KeireEditor

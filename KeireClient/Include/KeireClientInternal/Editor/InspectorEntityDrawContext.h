#pragma once
#include "KeireClient/Editor/EditorPanels.h"
#include "KeireClient/Editor/SceneDocument.h"
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    class InspectorPropertyEditor;
    struct ReflectedComponentState;

    // One synchronous entity draw borrows panel state and controller data. Nothing escapes the UI frame.
    class InspectorPanel::EntityDrawContext final
    {
      public:
        EntityDrawContext(InspectorPanel& panel, Keire::UiFrame& ui, const Keire::Ref<Keire::Scene>& scene,
                          const Keire::Entity& entity);
        void Draw();

      private:
        void DrawMetadata();
        void PasteComponent();
        void DrawFooter();
        void DrawTransform(bool mixedComponentValues);
        void DrawDirectionalLight(bool mixedComponentValues);
        void DrawCamera(bool mixedComponentValues);
        void DrawMeshRenderer(bool mixedComponentValues);
        void DrawReflectedComponent(const Keire::Ref<Keire::Component>& displayedComponent, bool mixedComponentValues);
        void DrawReflectedProperties(InspectorPropertyEditor& propertyEditor, const ReflectedComponentState& state);
        void DrawAudioDiagnostics(const ReflectedComponentState& state);
        [[nodiscard]] bool IsCommonComponent(const Keire::Ref<Keire::Component>& component) const;
        void SetComponentEnabled(Keire::ComponentTypeId type, bool enabled);
        void SetComponentProperty(Keire::ComponentTypeId type, std::string_view property,
                                  Keire::ComponentPropertyValue value);
        void ResetComponent(Keire::ComponentTypeId type);
        [[nodiscard]] bool DrawComponentHeader(const Keire::Ref<Keire::Component>& component,
                                               const Keire::ComponentRegistration& registration, bool& expanded,
                                               std::string_view title, bool mixedValues);
        [[nodiscard]] bool& Expansion(std::string_view type);

        InspectorPanel& m_Panel;
        Keire::UiFrame& m_Ui;
        SceneDocument& m_Document;
        const Keire::Ref<Keire::Scene>& m_Scene;
        const Keire::Entity& m_Entity;
        PropertyDrawerRegistry& m_PropertyDrawers;
        const Keire::UiThemeDefinition& m_Theme;
        std::span<const Keire::AssetSourceRecord> m_Records;
        Keire::Ref<Keire::AssetSystem> m_Assets;
        std::vector<Keire::ManagedAssetTypeDescriptor> m_ManagedAssetTypes;
        bool m_MultiEditing;
        std::vector<Keire::AssetId> m_EditTargets;
        std::vector<Keire::Ref<Keire::Component>> m_ComponentOrder;
        std::size_t m_EditingComponentOrdinal = 0;
        // Expansion entries live in the panel's node-based map throughout this synchronous draw.
        bool* m_CurrentHeaderExpanded = nullptr;
        std::string m_CurrentHeaderTitle;
    };
} // namespace KeireEditor

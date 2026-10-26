#include "KeireClient/Editor/EditorPanels.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/SceneDocument.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    class RepeatedInspectorComponent final : public Keire::Component
    {
      public:
        RepeatedInspectorComponent() : Component(StaticType()) {}

        [[nodiscard]] static constexpr Keire::ComponentTypeId StaticType() noexcept
        {
            return Keire::ComponentTypeId(Keire::AssetId(0xed17000000004000ULL, 0x8000000000000191ULL));
        }

        double Value = 1.0;
    };

    class InspectorWorkflowLayer final : public Keire::Layer, public KeireEditor::IInspectorController
    {
      public:
        InspectorWorkflowLayer(bool& checked, const bool multiEditing, const bool locked, const float width,
                               const bool enabledEdit)
            : Layer("Inspector workflow"), m_Checked(checked), m_MultiEditing(multiEditing), m_Locked(locked),
              m_Width(width), m_EnabledEdit(enabledEdit), m_Panel(*this)
        {
        }

        KeireEditor::SceneDocument& InspectorSceneDocument() noexcept override { return m_Document; }

        KeireEditor::InputActionsDocument& InspectorInputDocument() noexcept override { return m_Input; }

        KeireEditor::MaterialDocument& InspectorMaterialDocument() noexcept override { return m_Material; }

        KeireEditor::PropertyDrawerRegistry& InspectorPropertyDrawers() noexcept override { return m_Drawers; }

        const Keire::UiThemeDefinition& InspectorTheme() const noexcept override { return m_Theme; }

        std::span<const std::string> InspectorLayerNames() const noexcept override { return {}; }

        Keire::Ref<Keire::AssetDatabase> InspectorAssetDatabase() const noexcept override { return {}; }

        Keire::Ref<Keire::AssetSystem> InspectorAssetSystem() const noexcept override { return {}; }

        std::span<const Keire::AssetSourceRecord> InspectorAssetRecords() const noexcept override { return {}; }

        Keire::AssetId InspectorDefaultAudioMixer() const noexcept override { return {}; }

        Keire::AssetId InspectorSelectedAsset() const noexcept override { return {}; }

        std::string_view InspectorAssetStatus() const noexcept override { return {}; }

        std::vector<Keire::ManagedAssetTypeDescriptor> InspectorManagedAssetTypes() const override { return {}; }

        std::optional<Keire::ManagedTypeId> InspectorManagedDataType(Keire::AssetId) const noexcept override
        {
            return {};
        }

        Keire::Ref<Keire::UndoContext> InspectorManagedDataHistory() const noexcept override { return {}; }

        bool InspectorPlayModeActive() const noexcept override { return false; }

        void SetInspectorSelectedAsset(Keire::AssetId) noexcept override {}

        void PreviewInspectorAudio(Keire::AssetId) override { FAIL("Unexpected audio preview."); }

        void StopInspectorAudioPreview() noexcept override {}

        void ActivateInspectorHistory() noexcept override {}

        void ActivateInspectorManagedDataHistory() noexcept override {}

        void RecordInspectorUndo(std::string_view name, std::string) override
        {
            CHECK(name == (m_EnabledEdit ? "Change Repeated" : "Change Value"));
            CHECK(m_Components[0]->Value == doctest::Approx(1.0));
            CHECK(m_Components[1]->Value == doctest::Approx(2.0));
            ++m_UndoRequests;
        }

        void ApplyInspectorTransformEdit(KeireEditor::InspectorTransformEdit) override
        {
            FAIL("Unexpected transform edit.");
        }

        void CommitInspectorMaterial() override { FAIL("Unexpected material edit."); }

        void OpenInspectorInputActions(Keire::AssetId) override { FAIL("Unexpected asset open."); }

        void OpenInspectorMaterialGraph(Keire::AssetId) override { FAIL("Unexpected graph open."); }

        void OpenInspectorUiDocument(Keire::AssetId) override { FAIL("Unexpected UI open."); }

        void PersistInspectorMaterialInstance(Keire::AssetId, std::span<const std::byte>) override
        {
            FAIL("Unexpected material persistence.");
        }

        void PersistInspectorMaterialParameterCollection(Keire::AssetId, std::span<const std::byte>) override
        {
            FAIL("Unexpected parameter persistence.");
        }

        void PersistInspectorProceduralMotionProfile(Keire::AssetId, std::span<const std::byte>) override
        {
            FAIL("Unexpected motion persistence.");
        }

        void ApplyInspectorImportSettings(Keire::AssetId, const Keire::AssetImportSettings&) override
        {
            FAIL("Unexpected import edit.");
        }

        void ImportInspectorAssets() override { FAIL("Unexpected import."); }

        void PreviewInspectorManagedData(Keire::AssetId, const Keire::ManagedDataDefinition&) override
        {
            FAIL("Unexpected data preview.");
        }

        void PersistInspectorManagedData(Keire::AssetId, std::span<const std::byte>) override
        {
            FAIL("Unexpected data persistence.");
        }

        void RenameInspectorAsset(Keire::AssetId, std::string_view) override { FAIL("Unexpected asset rename."); }

        void DuplicateInspectorAsset(Keire::AssetId, const std::filesystem::path&) override
        {
            FAIL("Unexpected asset duplicate.");
        }

        void TrashInspectorAsset(Keire::AssetId) override { FAIL("Unexpected asset deletion."); }

        void SetInspectorAssetStatus(std::string) noexcept override {}

        void ReportInspectorAssetError(std::string message) noexcept override { m_Error = std::move(message); }

      protected:
        void OnAttach() override
        {
            const auto registry = Keire::ComponentRegistry::CreateDefault();
            Keire::ComponentRegistration registration;
            registration.Type = RepeatedInspectorComponent::StaticType();
            registration.Name = "Repeated";
            registration.AllowMultiple = true;
            registration.Properties = {{"value", "Value", "Fixture", Keire::ComponentPropertyKind::Scalar}};
            registration.Factory = []
            { return Keire::Ref<Keire::Component>(Keire::CreateRef<RepeatedInspectorComponent>()); };
            registration.Serialize = [](const Keire::Component& component)
            {
                return Keire::ComponentPropertyBag{
                    {"value", dynamic_cast<const RepeatedInspectorComponent&>(component).Value}};
            };
            registration.Deserialize =
                [](Keire::Component& component, const Keire::ComponentPropertyBag& values, const std::uint32_t)
            { dynamic_cast<RepeatedInspectorComponent&>(component).Value = std::get<double>(values.at("value")); };
            registry->Register(std::move(registration));
            m_Scene = Keire::CreateRef<Keire::Scene>(
                Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Inspector workflow"), registry);
            m_Document.Open(m_Scene);
            for (std::size_t index = 0; index < m_Entities.size(); ++index)
            {
                m_Entities[index] = m_Document.CreateEntity(index == 0 ? "Primary" : "Secondary");
                for (std::size_t ordinal = 0; ordinal < 2; ++ordinal)
                {
                    auto component = Keire::DynamicRefCast<RepeatedInspectorComponent>(
                        m_Document.AddComponent(m_Entities[index], RepeatedInspectorComponent::StaticType()));
                    component->Value = static_cast<double>(ordinal + 1);
                    m_Components[index * 2 + ordinal] = std::move(component);
                }
            }
            m_Document.Select(m_Entities[0].Value());
            if (m_MultiEditing)
            {
                const std::array selection{m_Entities[1].Value(), m_Entities[0].Value()};
                m_Document.SetSelections(selection);
            }
            m_Drawers.RegisterOverride(RepeatedInspectorComponent::StaticType(), "value",
                                       [this](KeireEditor::IPropertyEditor& editor, const Keire::ComponentProperty&,
                                              Keire::ComponentPropertyValue& value)
                                       {
                                           auto displayed = std::get<double>(value);
                                           (void)editor.EditScalar("Value", displayed, 0.1, std::nullopt, std::nullopt);
                                           const auto ordinal = m_VisitedValues.size();
                                           m_VisitedValues.push_back(std::get<double>(value));
                                           if (!m_EnabledEdit && m_Frame == 2 && ordinal == 1)
                                           {
                                               value = 9.0;
                                               return true;
                                           }
                                           return false;
                                       });
            m_Panel.Attach(Owner().GetUiWorkspace());
            m_Panel.Registration().SetLocked(m_Locked);
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            m_VisitedValues.clear();
            ui.SetNextWindowPosition({10.0F, 10.0F}, false);
            ui.SetNextWindowSize({m_Width, 1250.0F}, false);
            if (m_EnabledEdit && m_Frame == 2)
            {
                const auto cardId = "ComponentCard##" + RepeatedInspectorComponent::StaticType().ToString() + ".1";
                ImGuiWindow* target = nullptr;
                for (auto* window : ImGui::GetCurrentContext()->Windows)
                {
                    if (std::string_view(window->Name).find(cardId) != std::string_view::npos)
                    {
                        REQUIRE(target == nullptr);
                        target = window;
                    }
                }
                REQUIRE(target != nullptr);
                auto* context = ImGui::GetCurrentContext();
                context->NavActivateId =
                    target->GetID(("Enabled##" + RepeatedInspectorComponent::StaticType().ToString()).c_str());
                context->NavActivateDownId = context->NavActivateId;
                context->NavActivatePressedId = context->NavActivateId;
            }
            m_Panel.Draw(ui);
            if (m_Frame >= 2 && m_Frame < 4)
            {
                REQUIRE(m_VisitedValues.size() == 2);
                CHECK(m_Components[0]->Value == doctest::Approx(1.0));
                CHECK(m_Components[1]->Value == doctest::Approx(m_EnabledEdit ? 2.0 : 9.0));
                CHECK(m_Components[2]->Value == doctest::Approx(1.0));
                CHECK(m_Components[3]->Value ==
                      doctest::Approx(!m_EnabledEdit && m_MultiEditing && !m_Locked ? 9.0 : 2.0));
                CHECK(m_Components[0]->Enabled());
                CHECK(m_Components[1]->Enabled() == !m_EnabledEdit);
                CHECK(m_Components[2]->Enabled());
                CHECK(m_Components[3]->Enabled() == !(m_EnabledEdit && m_MultiEditing && !m_Locked));
                CHECK(m_UndoRequests == 1);
                CHECK(m_Error.empty());
            }
            if (m_Frame == 3)
            {
                m_Document.MoveComponentBefore(m_Entities[0], m_Components[1], m_Components[0]);
            }
            if (m_Frame == 4)
            {
                REQUIRE(m_VisitedValues.size() == 2);
                CHECK(m_VisitedValues[0] == doctest::Approx(m_EnabledEdit ? 2.0 : 9.0));
                CHECK(m_VisitedValues[1] == doctest::Approx(1.0));
                m_Document.DeleteEntity(m_Entities[0]);
            }
            if (m_Frame == 5)
            {
                CHECK_FALSE(m_Panel.Registration().Locked());
                m_Checked = true;
                Owner().RequestExit();
            }
            ++m_Frame;
        }

        void OnDetach() noexcept override { m_Document.Close(); }

      private:
        bool& m_Checked;
        bool m_MultiEditing;
        bool m_Locked;
        float m_Width;
        bool m_EnabledEdit;
        int m_Frame = 0;
        int m_UndoRequests = 0;
        KeireEditor::SceneDocument m_Document;
        KeireEditor::InputActionsDocument m_Input;
        KeireEditor::MaterialDocument m_Material;
        KeireEditor::PropertyDrawerRegistry m_Drawers;
        Keire::UiThemeDefinition m_Theme;
        Keire::Ref<Keire::Scene> m_Scene;
        std::array<Keire::EntityId, 2> m_Entities;
        std::array<Keire::Ref<RepeatedInspectorComponent>, 4> m_Components;
        std::vector<double> m_VisitedValues;
        std::string m_Error;
        KeireEditor::InspectorPanel m_Panel;
    };
} // namespace

TEST_CASE("Inspector cards edit the displayed repeated component and preserve selection and ordering")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    for (const auto width : {280.0F, 760.0F})
    {
        for (const bool multiEditing : {false, true})
        {
            for (const bool locked : {false, true})
            {
                for (const bool enabledEdit : {false, true})
                {
                    INFO("Width: " << width << ", multi-edit: " << multiEditing << ", locked: " << locked
                                   << ", enabled edit: " << enabledEdit);
                    bool checked = false;
                    Keire::ApplicationSpecification specification;
                    specification.MainWindow.Visible = false;
                    specification.MainWindow.Height = 1400;
                    specification.TargetFrameRate = 0;
                    specification.Ui.Mode = Keire::UiMode::Headless;
                    specification.Ui.LayoutPath.clear();
                    specification.Ui.Workspace.Enabled = true;
                    specification.Ui.Workspace.Ephemeral = true;
                    Keire::Application application(specification);
                    (void)application.PushLayer(
                        std::make_unique<InspectorWorkflowLayer>(checked, multiEditing, locked, width, enabledEdit));
                    CHECK(application.Run() == 0);
                    CHECK(checked);
                }
            }
        }
    }
}

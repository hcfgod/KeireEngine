#pragma once

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Ui.h"
#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/PostProcessDocument.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace KeireEditor
{
    class IPostProcessPanelController
    {
      public:
        virtual ~IPostProcessPanelController() = default;
        [[nodiscard]] virtual PostProcessDocument& PostProcessState() noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::AssetDatabase> PostProcessDatabase() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::AssetSystem> PostProcessAssets() const noexcept { return {}; }
        virtual void ActivatePostProcessHistory() noexcept = 0;
        virtual void OpenPostProcessProfile(Keire::AssetId asset) = 0;
        virtual void CreatePostProcessProfile(std::string_view name) = 0;
        virtual void SavePostProcessProfile() = 0;
        virtual void ReloadPostProcessProfile() = 0;
    };

    class PostProcessPanel final
    {
      public:
        explicit PostProcessPanel(IPostProcessPanelController& controller) noexcept;
        void Attach(Keire::UiWorkspace& workspace);
        void Draw(Keire::UiFrame& ui);
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        void DrawContents(Keire::UiFrame& ui);
        void DrawParameters(Keire::UiFrame& ui, std::size_t first, std::size_t last, std::string_view category);
        void DrawEffect(Keire::UiFrame& ui, Keire::PostProcessEffect effect);
        void DrawColorLookup(Keire::UiFrame& ui);
        void DrawCustomEffects(Keire::UiFrame& ui);
        void DrawEffectWorkspace(Keire::UiFrame& ui);
        [[nodiscard]] bool TrackNumericEdit(std::string_view key, std::string_view label,
                                            const Keire::UiItemState& state);
        void FinishNumericEdit(std::string_view key, const Keire::UiItemState& state);
        void ApplyNumericEdit(std::string_view label, const Keire::PostProcessProfileDefinition& replacement);

        IPostProcessPanelController& m_Controller;
        Keire::UiPanelRegistration m_Registration;
        std::string m_Search;
        AssetPicker m_LookupPicker;
        AssetPicker m_CustomMaterialPicker;
        std::string m_NewName = "Post Processing Profile";
        std::string m_CreationMessage;
        std::string m_Message;
        std::size_t m_SelectedGroup = 1;
        bool m_AdvancedOverrides = false;
        std::string m_ContinuousField;
        std::string m_CancelledField;
        bool m_ContinuousFieldSeen = false;
    };
} // namespace KeireEditor

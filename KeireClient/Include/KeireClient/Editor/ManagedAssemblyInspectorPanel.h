#pragma once
#include "Keire/Scripting/ManagedAssemblyAsset.h"
#include "KeireClient/Editor/AssetPicker.h"
#include <optional>

namespace KeireEditor
{
    class IInspectorController;
    class ManagedAssemblyInspectorPanel final
    {
      public:
        explicit ManagedAssemblyInspectorPanel(IInspectorController& controller) : m_Controller(controller) {}
        void Draw(Keire::UiFrame& ui, const Keire::AssetSourceRecord& record);
        void Clear() noexcept;

      private:
        IInspectorController& m_Controller;
        AssetPicker m_Picker;
        Keire::AssetId m_Asset;
        std::optional<Keire::ManagedAssemblyDefinition> m_Definition;
        std::string m_Reference;
        std::vector<std::byte> m_Original;
        bool m_Dirty = false;
        bool m_ExcludePlatforms = false;
    };
} // namespace KeireEditor

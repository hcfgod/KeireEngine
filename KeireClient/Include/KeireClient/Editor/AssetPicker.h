#pragma once

#include "Keire/Core.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace KeireEditor
{
    struct AssetPickerOptions final
    {
        std::string_view Label;
        std::string_view EmptyLabel = "None";
        std::optional<Keire::AssetTypeId> ExpectedType;
        std::function<bool(const Keire::AssetSourceRecord&)> Filter;
        std::function<void(Keire::AssetId)> Reveal;
        bool AllowNone = true;
    };

    class AssetPicker final
    {
      public:
        [[nodiscard]] bool Draw(Keire::UiFrame& ui, std::span<const Keire::AssetSourceRecord> records,
                                Keire::AssetId& value, const AssetPickerOptions& options);

        [[nodiscard]] static bool Accepts(const Keire::AssetSourceRecord& record, const AssetPickerOptions& options);
        [[nodiscard]] static bool AcceptsEnvironmentTexture(const Keire::AssetSourceRecord& record);

        [[nodiscard]] std::string_view Diagnostic() const noexcept { return m_Diagnostic; }
        void Clear() noexcept;

      private:
        std::string m_Search;
        std::string m_Diagnostic;
    };
} // namespace KeireEditor

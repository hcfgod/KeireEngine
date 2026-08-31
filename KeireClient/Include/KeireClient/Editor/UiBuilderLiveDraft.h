#pragma once

#include "Keire/Assets/AssetSystem.h"
#include "Keire/Ui/UiToolkit.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace KeireEditor
{
    [[nodiscard]] bool PublishUiToolkitAuthoringAsset(const Keire::Ref<Keire::AssetSystem>& assets,
                                                      Keire::AssetId asset, Keire::AssetTypeId type,
                                                      std::span<const std::byte> source,
                                                      std::string& diagnostic) noexcept;

    class UiBuilderLiveDraftSession final
    {
      public:
        UiBuilderLiveDraftSession() = default;
        ~UiBuilderLiveDraftSession();

        UiBuilderLiveDraftSession(const UiBuilderLiveDraftSession&) = delete;
        UiBuilderLiveDraftSession& operator=(const UiBuilderLiveDraftSession&) = delete;

        void Synchronize(Keire::Ref<Keire::AssetSystem> assets, bool playActive, Keire::AssetId asset,
                         std::uint64_t documentGeneration, bool dirty,
                         const Keire::UiVisualTreeDefinition& definition) noexcept;
        void Commit(Keire::Ref<Keire::AssetSystem> assets, Keire::AssetId asset,
                    const Keire::UiVisualTreeDefinition& definition) noexcept;
        void Close() noexcept;

        [[nodiscard]] bool Active() const noexcept { return m_Applied; }
        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Asset; }
        [[nodiscard]] std::uint64_t DocumentGeneration() const noexcept { return m_DocumentGeneration; }
        [[nodiscard]] const std::string& Diagnostic() const noexcept { return m_Diagnostic; }

      private:
        void ClearState() noexcept;
        [[nodiscard]] bool Publish(const Keire::UiVisualTreeDefinition& definition) noexcept;

        Keire::Ref<Keire::AssetSystem> m_Assets;
        Keire::AssetId m_Asset;
        std::optional<Keire::UiVisualTreeDefinition> m_Baseline;
        std::uint64_t m_DocumentGeneration = 0;
        bool m_Applied = false;
        std::string m_Diagnostic;
    };
} // namespace KeireEditor

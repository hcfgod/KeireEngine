#pragma once

#include "Keire/Vfx/VfxSystem.h"
#include "KeireClient/Editor/AssetDocumentHost.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace KeireEditor
{
    struct VfxEffectDocumentSpecification
    {
        // Preview is transient and must not persist the asset or dirty scene state.
        std::function<void(Keire::AssetId, const Keire::VfxEffectDefinition&)> Preview;
        // Stops every effect instance owned by this document's preview session.
        std::function<void(Keire::AssetId)> StopPreview;
        std::function<void(Keire::AssetId, std::span<const std::byte>)> Persist;
    };

    class VfxEffectDocument final
    {
      public:
        using Host = AssetDocumentHost<Keire::VfxEffectDefinition>;

        explicit VfxEffectDocument(VfxEffectDocumentSpecification specification);
        ~VfxEffectDocument() = default;

        VfxEffectDocument(const VfxEffectDocument&) = delete;
        VfxEffectDocument& operator=(const VfxEffectDocument&) = delete;

        void Open(Keire::AssetId asset, std::span<const std::byte> bytes, std::uint64_t revision,
                  Keire::Ref<Keire::UndoContext> undo = {});
        void Open(Keire::AssetId asset, Keire::VfxEffectDefinition definition, std::uint64_t revision,
                  Keire::Ref<Keire::UndoContext> undo = {});
        void Create(Keire::AssetId asset,
                    Keire::VfxEffectDefinition definition = Keire::VfxEffectAsset::DefaultDefinition(),
                    Keire::Ref<Keire::UndoContext> undo = {});
        void Save();
        void Discard();
        [[nodiscard]] AssetDocumentReloadResult Reload(std::span<const std::byte> bytes, std::uint64_t revision);
        [[nodiscard]] AssetDocumentReloadResult Reload(Keire::VfxEffectDefinition definition, std::uint64_t revision);
        [[nodiscard]] bool Undo();
        [[nodiscard]] bool Redo();
        void Close() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept { return m_Host.IsOpen(); }
        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Host.Asset(); }
        [[nodiscard]] std::uint64_t Revision() const noexcept { return m_Host.Revision(); }
        [[nodiscard]] bool Dirty() const noexcept { return m_Host.Dirty(); }
        [[nodiscard]] std::string_view Diagnostic() const noexcept { return m_Host.Diagnostic(); }
        [[nodiscard]] const Keire::VfxEffectDefinition& Definition() const { return m_Host.Draft(); }
        [[nodiscard]] Keire::Ref<Keire::UndoContext> UndoContext() const noexcept { return m_Host.UndoContext(); }

        [[nodiscard]] bool Edit(std::string_view name,
                                const std::function<void(Keire::VfxEffectDefinition&)>& operation);
        [[nodiscard]] bool AddModule(Keire::VfxModuleDefinition module);
        [[nodiscard]] bool EditModule(Keire::AssetId module,
                                      const std::function<void(Keire::VfxModuleDefinition&)>& operation);
        [[nodiscard]] bool RemoveModule(Keire::AssetId module);
        [[nodiscard]] bool MoveModule(Keire::AssetId module, std::size_t destination);

      private:
        Host m_Host;
    };
} // namespace KeireEditor

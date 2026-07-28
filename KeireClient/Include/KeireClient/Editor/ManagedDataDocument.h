#pragma once

#include "KeireClient/Editor/AssetDocumentHost.h"

#include "Keire/Scripting/ManagedDataAsset.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    struct ManagedDataPropertyState final
    {
        bool Serialized = false;
        Keire::ManagedAssetValueNode Value;
        std::string RawValue;
        std::string Diagnostic;
    };

    class ManagedDataDocument final
    {
      public:
        struct Specification final
        {
            std::function<void(Keire::AssetId, const Keire::ManagedDataDefinition&)> Preview;
            std::function<void(Keire::AssetId, std::span<const std::byte>)> Persist;
        };

        explicit ManagedDataDocument(Specification specification);
        ~ManagedDataDocument();

        ManagedDataDocument(const ManagedDataDocument&) = delete;
        ManagedDataDocument& operator=(const ManagedDataDocument&) = delete;

        void Open(Keire::AssetId asset, Keire::ManagedDataDefinition definition, std::uint64_t revision,
                  std::optional<Keire::ManagedAssetTypeDescriptor> descriptor = {},
                  Keire::Ref<Keire::UndoContext> undo = {});
        void Close() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept { return m_Host.IsOpen(); }
        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Host.Asset(); }
        [[nodiscard]] std::uint64_t Revision() const noexcept { return m_Host.Revision(); }
        [[nodiscard]] bool Dirty() const noexcept { return m_Host.Dirty(); }
        [[nodiscard]] const Keire::ManagedDataDefinition& Draft() const { return m_Host.Draft(); }
        [[nodiscard]] const Keire::ManagedDataDefinition& Baseline() const { return m_Host.Baseline(); }
        [[nodiscard]] const Keire::ManagedAssetTypeDescriptor* Descriptor() const noexcept;
        [[nodiscard]] std::string_view Diagnostic() const noexcept;
        [[nodiscard]] Keire::Ref<Keire::UndoContext> UndoContext() const noexcept { return m_Host.UndoContext(); }

        [[nodiscard]] ManagedDataPropertyState
        Property(const Keire::ManagedAssetPropertyDescriptor& property) const noexcept;
        bool SetProperty(const Keire::ManagedAssetPropertyDescriptor& property, Keire::ManagedAssetValueNode value,
                         std::string_view undoName = "Edit Managed Data");
        bool ClearProperty(const Keire::ManagedAssetPropertyDescriptor& property,
                           std::string_view undoName = "Use Managed Default");

        void Save() { m_Host.Save(); }
        void Discard() { m_Host.Discard(); }
        [[nodiscard]] bool Undo() { return m_Host.Undo(); }
        [[nodiscard]] bool Redo() { return m_Host.Redo(); }
        [[nodiscard]] AssetDocumentReloadResult Reload(Keire::ManagedDataDefinition definition, std::uint64_t revision);

        [[nodiscard]] static Keire::ManagedAssetValueNode
        DefaultValue(const Keire::ManagedAssetPropertyDescriptor& property);
        [[nodiscard]] static Keire::ManagedAssetValueNode
        MaterializedDefaultValue(const Keire::ManagedAssetPropertyDescriptor& property);
        [[nodiscard]] static bool
        AcceptsAssetReference(const Keire::AssetSourceRecord& record,
                              const Keire::ManagedAssetPropertyDescriptor& property,
                              const std::optional<Keire::ManagedDataDefinition>& managedDefinition,
                              std::span<const Keire::ManagedAssetTypeDescriptor> types) noexcept;

      private:
        void RebuildDependencies(Keire::ManagedDataDefinition& definition) const;

        Specification m_Specification;
        AssetDocumentHost<Keire::ManagedDataDefinition> m_Host;
        std::optional<Keire::ManagedAssetTypeDescriptor> m_Descriptor;
        std::string m_Diagnostic;
        bool m_SuppressPreview = false;
    };
} // namespace KeireEditor

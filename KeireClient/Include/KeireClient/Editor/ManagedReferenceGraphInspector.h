#pragma once

#include "Keire/Scripting/ManagedDataAsset.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>

namespace Keire
{
    struct ManagedAssetTypeDiagnostic;
    class UiFrame;
} // namespace Keire

namespace KeireEditor
{
    class ManagedDataDocument;

    [[nodiscard]] std::string
    FormatManagedSerializationDiagnostic(std::string_view message,
                                         const Keire::ManagedSerializationDiagnostic& diagnostic);
    [[nodiscard]] std::string FormatManagedAssetTypeDiagnostic(const Keire::ManagedAssetTypeDiagnostic& diagnostic);
    [[nodiscard]] std::string FormatManagedInspectorError(const std::exception& error);

    [[nodiscard]] bool FocusManagedReferenceGraphObject(std::uint32_t& focusedObject,
                                                        const Keire::ManagedReferenceGraph& graph,
                                                        std::uint32_t object) noexcept;

    class ManagedReferenceGraphEditController final
    {
      public:
        ManagedReferenceGraphEditController() noexcept;

        ManagedReferenceGraphEditController(const ManagedReferenceGraphEditController&) = delete;
        ManagedReferenceGraphEditController& operator=(const ManagedReferenceGraphEditController&) = delete;
        ManagedReferenceGraphEditController(ManagedReferenceGraphEditController&&) = delete;
        ManagedReferenceGraphEditController& operator=(ManagedReferenceGraphEditController&&) = delete;

        void AssertOwnerThread() const;
        [[nodiscard]] bool Focus(std::uint32_t& focusedObject, const Keire::ManagedReferenceGraph& graph,
                                 std::uint32_t object) const;
        [[nodiscard]] bool CommitPersistent(ManagedDataDocument& document,
                                            const Keire::ManagedAssetPropertyDescriptor& property,
                                            Keire::ManagedReferenceGraph value,
                                            std::string_view undoName = "Edit Managed Reference Graph") const;

      private:
        std::thread::id m_OwnerThread;
    };

    class ManagedReferenceGraphActions final
    {
      public:
        [[nodiscard]] static bool LinkValue(Keire::ManagedReferenceGraphValue& value,
                                            const Keire::ManagedReferenceGraph& graph,
                                            const Keire::ManagedAssetPropertyDescriptor& property,
                                            std::uint32_t object);
        [[nodiscard]] static std::uint32_t CreateObject(Keire::ManagedReferenceGraphValue& value,
                                                        Keire::ManagedReferenceGraph& graph,
                                                        const Keire::ManagedAssetPropertyDescriptor& property,
                                                        const Keire::ManagedReferenceGraphDescriptor& descriptor,
                                                        Keire::ManagedTypeId type);
        [[nodiscard]] static std::uint32_t CreateCollection(Keire::ManagedReferenceGraphValue& value,
                                                            Keire::ManagedReferenceGraph& graph,
                                                            const Keire::ManagedAssetPropertyDescriptor& property);
        static void AddDictionaryEntry(Keire::ManagedReferenceGraphNode& node,
                                       const Keire::ManagedAssetPropertyDescriptor& property);
        static void Finalize(Keire::ManagedReferenceGraph& graph,
                             const Keire::ManagedReferenceGraphDescriptor& descriptor);
    };

    class ManagedReferenceGraphInspector final
    {
      public:
        ManagedReferenceGraphInspector(Keire::UiFrame& ui, std::uint32_t& focusedObject,
                                       const ManagedReferenceGraphEditController* controller = nullptr)
            : m_Ui(ui), m_FocusedObject(focusedObject), m_Controller(controller)
        {
        }

        [[nodiscard]] bool Draw(std::string_view label, Keire::ManagedReferenceGraph& value,
                                const Keire::ManagedReferenceGraphDescriptor& descriptor);

      private:
        [[nodiscard]] bool DrawValue(std::string_view label, Keire::ManagedReferenceGraphValue& value,
                                     const Keire::ManagedAssetPropertyDescriptor& property,
                                     Keire::ManagedReferenceGraph& graph,
                                     const Keire::ManagedReferenceGraphDescriptor& descriptor,
                                     std::set<std::uint32_t>& active);
        [[nodiscard]] bool DrawNode(Keire::ManagedReferenceGraphNode& node,
                                    const Keire::ManagedAssetPropertyDescriptor& property,
                                    Keire::ManagedReferenceGraph& graph,
                                    const Keire::ManagedReferenceGraphDescriptor& descriptor,
                                    std::set<std::uint32_t>& active);
        [[nodiscard]] bool DrawScalar(Keire::ManagedReferenceGraphValue& value,
                                      const Keire::ManagedAssetPropertyDescriptor& property,
                                      const Keire::ManagedReferenceGraph& graph);
        void BeginMutation(const Keire::ManagedReferenceGraph& graph);

        Keire::UiFrame& m_Ui;
        std::uint32_t& m_FocusedObject;
        const ManagedReferenceGraphEditController* m_Controller = nullptr;
        std::optional<Keire::ManagedReferenceGraph> m_OriginalGraph;
        bool m_CreatedNode = false;
    };
} // namespace KeireEditor

#include "KeireClient/Editor/ManagedReferenceGraphInspector.h"

#include "KeireClient/Editor/ManagedDataDocument.h"

#include "Keire/Scripting/ScriptSystem.h"
#include "Keire/Ui.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        void AppendDiagnosticField(std::string& result, const std::string_view label, const std::string_view value)
        {
            if (!value.empty())
                result += '\n' + std::string(label) + ": " + std::string(value);
        }
    } // namespace

    std::string FormatManagedSerializationDiagnostic(const std::string_view message,
                                                     const Keire::ManagedSerializationDiagnostic& diagnostic)
    {
        std::string result(message);
        AppendDiagnosticField(result, "Code", diagnostic.Code);
        AppendDiagnosticField(result, "Phase", diagnostic.Phase);
        AppendDiagnosticField(result, "Owner", diagnostic.Owner);
        AppendDiagnosticField(result, "Root field", diagnostic.RootField);
        AppendDiagnosticField(result, "Field path", diagnostic.FieldPath);
        AppendDiagnosticField(result, "Declared type", diagnostic.DeclaredType);
        AppendDiagnosticField(result, "Runtime type", diagnostic.RuntimeType);
        AppendDiagnosticField(result, "Serialized type ID", diagnostic.SerializedTypeId);
        if (diagnostic.ObjectId)
            AppendDiagnosticField(result, "Object ID", std::to_string(*diagnostic.ObjectId));
        return result;
    }

    std::string FormatManagedAssetTypeDiagnostic(const Keire::ManagedAssetTypeDiagnostic& diagnostic)
    {
        return FormatManagedSerializationDiagnostic(diagnostic.Message,
                                                    {.Code = diagnostic.Code,
                                                     .Phase = diagnostic.Phase,
                                                     .Owner = diagnostic.Owner,
                                                     .RootField = diagnostic.RootField,
                                                     .FieldPath = diagnostic.FieldPath,
                                                     .DeclaredType = diagnostic.DeclaredType,
                                                     .RuntimeType = diagnostic.RuntimeType,
                                                     .SerializedTypeId = diagnostic.SerializedTypeId,
                                                     .ObjectId = diagnostic.ObjectId});
    }

    std::string FormatManagedInspectorError(const std::exception& error)
    {
        const auto* managed = dynamic_cast<const Keire::ManagedSerializationError*>(&error);
        return managed ? FormatManagedSerializationDiagnostic(error.what(), managed->Details()) : error.what();
    }

    bool FocusManagedReferenceGraphObject(std::uint32_t& focusedObject, const Keire::ManagedReferenceGraph& graph,
                                          const std::uint32_t object) noexcept
    {
        if (object == 0)
        {
            focusedObject = 0;
            return true;
        }
        if (std::ranges::find(graph.Objects, object, &Keire::ManagedReferenceGraphNode::Id) == graph.Objects.end())
            return false;
        focusedObject = object;
        return true;
    }

    ManagedReferenceGraphEditController::ManagedReferenceGraphEditController() noexcept
        : m_OwnerThread(std::this_thread::get_id())
    {
    }

    void ManagedReferenceGraphEditController::AssertOwnerThread() const
    {
        if (std::this_thread::get_id() != m_OwnerThread)
            throw std::logic_error("Managed reference graph Inspector edits require the owner thread.");
    }

    bool ManagedReferenceGraphEditController::Focus(std::uint32_t& focusedObject,
                                                    const Keire::ManagedReferenceGraph& graph,
                                                    const std::uint32_t object) const
    {
        AssertOwnerThread();
        return FocusManagedReferenceGraphObject(focusedObject, graph, object);
    }

    bool ManagedReferenceGraphEditController::CommitPersistent(ManagedDataDocument& document,
                                                               const Keire::ManagedAssetPropertyDescriptor& property,
                                                               Keire::ManagedReferenceGraph value,
                                                               const std::string_view undoName) const
    {
        AssertOwnerThread();
        return document.SetGraphProperty(property, std::move(value), undoName);
    }

    namespace
    {
        constexpr std::size_t MaximumEditableCollectionElements = 16'384;
        constexpr std::size_t MaximumGraphObjects = 65'536;

        [[nodiscard]] Keire::ManagedReferenceGraphNode* FindNode(Keire::ManagedReferenceGraph& graph,
                                                                 const std::uint32_t id) noexcept
        {
            const auto found = std::ranges::find(graph.Objects, id, &Keire::ManagedReferenceGraphNode::Id);
            return found == graph.Objects.end() ? nullptr : std::addressof(*found);
        }

        enum class GraphValueLocationKind
        {
            Root,
            Field,
            Item,
            EntryKey,
            EntryValue
        };

        struct GraphValueLocation final
        {
            GraphValueLocationKind Kind = GraphValueLocationKind::Root;
            std::uint32_t Object = 0;
            Keire::AssetId Field;
            std::size_t Index = 0;
        };

        [[nodiscard]] GraphValueLocation LocateValue(const Keire::ManagedReferenceGraph& graph,
                                                     const Keire::ManagedReferenceGraphValue& value)
        {
            if (std::addressof(graph.Root) == std::addressof(value))
                return {};
            for (const auto& object : graph.Objects)
            {
                for (const auto& field : object.Fields)
                {
                    if (std::addressof(field.Value) == std::addressof(value))
                    {
                        return {
                            .Kind = GraphValueLocationKind::Field, .Object = object.Id, .Field = field.StableFieldId};
                    }
                }
                for (std::size_t index = 0; index < object.Items.size(); ++index)
                {
                    if (std::addressof(object.Items[index]) == std::addressof(value))
                        return {.Kind = GraphValueLocationKind::Item, .Object = object.Id, .Index = index};
                }
                for (std::size_t index = 0; index < object.Entries.size(); ++index)
                {
                    if (std::addressof(object.Entries[index].Key) == std::addressof(value))
                        return {.Kind = GraphValueLocationKind::EntryKey, .Object = object.Id, .Index = index};
                    if (std::addressof(object.Entries[index].Value) == std::addressof(value))
                        return {.Kind = GraphValueLocationKind::EntryValue, .Object = object.Id, .Index = index};
                }
            }
            throw std::logic_error("Managed reference graph edit target is not part of the graph.");
        }

        [[nodiscard]] Keire::ManagedReferenceGraphValue& ResolveValue(Keire::ManagedReferenceGraph& graph,
                                                                      const GraphValueLocation& location)
        {
            if (location.Kind == GraphValueLocationKind::Root)
                return graph.Root;
            auto* object = FindNode(graph, location.Object);
            if (!object)
                throw std::logic_error("Managed reference graph edit target object disappeared.");
            if (location.Kind == GraphValueLocationKind::Field)
            {
                const auto field = std::ranges::find(object->Fields, location.Field,
                                                     &Keire::ManagedReferenceGraphField::StableFieldId);
                if (field == object->Fields.end())
                    throw std::logic_error("Managed reference graph edit target field disappeared.");
                return field->Value;
            }
            if (location.Kind == GraphValueLocationKind::Item)
            {
                if (location.Index >= object->Items.size())
                    throw std::logic_error("Managed reference graph edit target item disappeared.");
                return object->Items[location.Index];
            }
            if (location.Index >= object->Entries.size())
                throw std::logic_error("Managed reference graph edit target entry disappeared.");
            return location.Kind == GraphValueLocationKind::EntryKey ? object->Entries[location.Index].Key
                                                                     : object->Entries[location.Index].Value;
        }

        [[nodiscard]] const Keire::ManagedAssetReferenceTypeDescriptor*
        FindType(const Keire::ManagedReferenceGraphDescriptor& descriptor, const Keire::ManagedTypeId id) noexcept
        {
            const auto found =
                std::ranges::find(descriptor.Types, id, &Keire::ManagedAssetReferenceTypeDescriptor::StableTypeId);
            return found == descriptor.Types.end() ? nullptr : std::addressof(*found);
        }

        [[nodiscard]] bool Compatible(const Keire::ManagedReferenceGraphNode& node,
                                      const Keire::ManagedAssetPropertyDescriptor& property) noexcept
        {
            switch (node.Kind)
            {
            case Keire::ManagedReferenceGraphNodeKind::Object:
                return property.ReferenceGraph && std::ranges::find(property.ReferenceTypeChoices, node.RuntimeType) !=
                                                      property.ReferenceTypeChoices.end();
            case Keire::ManagedReferenceGraphNodeKind::Array:
                return property.Kind == Keire::ManagedAssetPropertyKind::Array;
            case Keire::ManagedReferenceGraphNodeKind::List:
                return property.Kind == Keire::ManagedAssetPropertyKind::List;
            case Keire::ManagedReferenceGraphNodeKind::Dictionary:
                return property.Kind == Keire::ManagedAssetPropertyKind::Dictionary;
            }
            return false;
        }

        [[nodiscard]] std::uint32_t NextId(const Keire::ManagedReferenceGraph& graph)
        {
            std::unordered_set<std::uint32_t> used;
            used.reserve(graph.Objects.size());
            for (const auto& node : graph.Objects)
                used.emplace(node.Id);
            for (std::uint32_t candidate = 1; candidate <= MaximumGraphObjects; ++candidate)
            {
                if (!used.contains(candidate))
                    return candidate;
            }
            throw std::overflow_error("Managed reference graph object IDs are exhausted.");
        }

        [[nodiscard]] Keire::ManagedReferenceGraphValue
        DefaultValue(const Keire::ManagedAssetPropertyDescriptor& property)
        {
            if (property.ReferenceGraph || property.Kind == Keire::ManagedAssetPropertyKind::Array ||
                property.Kind == Keire::ManagedAssetPropertyKind::List ||
                property.Kind == Keire::ManagedAssetPropertyKind::Dictionary)
            {
                return {};
            }
            return {.Scalar = Keire::EncodeManagedAssetValue(ManagedDataDocument::MaterializedDefaultValue(property),
                                                             property)};
        }

        [[nodiscard]] std::uint32_t CreateObjectNode(Keire::ManagedReferenceGraph& graph,
                                                     const Keire::ManagedAssetReferenceTypeDescriptor& type)
        {
            if (graph.Objects.size() >= MaximumGraphObjects)
                throw std::invalid_argument("Managed reference graph exceeds the 65,536-object limit.");
            Keire::ManagedReferenceGraphNode node;
            node.Id = NextId(graph);
            node.Kind = Keire::ManagedReferenceGraphNodeKind::Object;
            node.RuntimeType = type.StableTypeId;
            node.Fields.reserve(type.Properties.size());
            for (const auto& property : type.Properties)
            {
                node.Fields.push_back(
                    {.StableFieldId = property.StableFieldId, .Name = property.Name, .Value = DefaultValue(property)});
            }
            const auto id = node.Id;
            graph.Objects.push_back(std::move(node));
            return id;
        }

        [[nodiscard]] std::uint32_t CreateCollectionNode(Keire::ManagedReferenceGraph& graph,
                                                         const Keire::ManagedAssetPropertyKind kind)
        {
            if (graph.Objects.size() >= MaximumGraphObjects)
                throw std::invalid_argument("Managed reference graph exceeds the 65,536-object limit.");
            Keire::ManagedReferenceGraphNode node;
            node.Id = NextId(graph);
            if (kind == Keire::ManagedAssetPropertyKind::Array)
                node.Kind = Keire::ManagedReferenceGraphNodeKind::Array;
            else if (kind == Keire::ManagedAssetPropertyKind::List)
                node.Kind = Keire::ManagedReferenceGraphNodeKind::List;
            else if (kind == Keire::ManagedAssetPropertyKind::Dictionary)
                node.Kind = Keire::ManagedReferenceGraphNodeKind::Dictionary;
            else
                throw std::logic_error("Only graph collections can create collection nodes.");
            const auto id = node.Id;
            graph.Objects.push_back(std::move(node));
            return id;
        }

        void MarkReachable(const Keire::ManagedReferenceGraphValue& value, const Keire::ManagedReferenceGraph& graph,
                           std::set<std::uint32_t>& reached)
        {
            if (value.Reference == 0 || !reached.emplace(value.Reference).second)
                return;
            const auto found = std::ranges::find(graph.Objects, value.Reference, &Keire::ManagedReferenceGraphNode::Id);
            if (found == graph.Objects.end())
                return;
            for (const auto& field : found->Fields)
                MarkReachable(field.Value, graph, reached);
            for (const auto& item : found->Items)
                MarkReachable(item, graph, reached);
            for (const auto& entry : found->Entries)
            {
                MarkReachable(entry.Key, graph, reached);
                MarkReachable(entry.Value, graph, reached);
            }
        }

        void PruneUnreachable(Keire::ManagedReferenceGraph& graph)
        {
            std::set<std::uint32_t> reached;
            MarkReachable(graph.Root, graph, reached);
            std::erase_if(graph.Objects, [&reached](const Keire::ManagedReferenceGraphNode& node)
                          { return !reached.contains(node.Id); });
        }

        [[nodiscard]] std::string NodeLabel(const Keire::ManagedReferenceGraphNode& node,
                                            const Keire::ManagedReferenceGraphDescriptor& descriptor)
        {
            if (node.Kind == Keire::ManagedReferenceGraphNodeKind::Object)
            {
                const auto* type = FindType(descriptor, node.RuntimeType);
                return "#" + std::to_string(node.Id) + " " + (type ? type->DisplayName : node.RuntimeType.ToString());
            }
            return "#" + std::to_string(node.Id) + " collection";
        }
    } // namespace

    bool ManagedReferenceGraphActions::LinkValue(Keire::ManagedReferenceGraphValue& value,
                                                 const Keire::ManagedReferenceGraph& graph,
                                                 const Keire::ManagedAssetPropertyDescriptor& property,
                                                 const std::uint32_t object)
    {
        const auto canonicalReference = [&value](const std::uint32_t reference)
        { return value.Reference == reference && (value.Scalar.empty() || value.Scalar == "null"); };
        if (object == 0)
        {
            if (canonicalReference(0))
                return false;
            value = {};
            return true;
        }
        const auto found = std::ranges::find(graph.Objects, object, &Keire::ManagedReferenceGraphNode::Id);
        if (found == graph.Objects.end())
        {
            throw std::invalid_argument("Managed reference graph link target #" + std::to_string(object) +
                                        " does not exist.");
        }
        if (!Compatible(*found, property))
        {
            throw std::invalid_argument("Managed reference graph object #" + std::to_string(object) +
                                        " is not compatible with field '" + property.Name + "'.");
        }
        if (canonicalReference(object))
            return false;
        value = {.Reference = object};
        return true;
    }

    std::uint32_t ManagedReferenceGraphActions::CreateObject(Keire::ManagedReferenceGraphValue& value,
                                                             Keire::ManagedReferenceGraph& graph,
                                                             const Keire::ManagedAssetPropertyDescriptor& property,
                                                             const Keire::ManagedReferenceGraphDescriptor& descriptor,
                                                             const Keire::ManagedTypeId type)
    {
        if (!property.ReferenceGraph ||
            std::ranges::find(property.ReferenceTypeChoices, type) == property.ReferenceTypeChoices.end())
        {
            throw std::invalid_argument("Managed reference type '" + type.ToString() +
                                        "' is not registered for field '" + property.Name + "'.");
        }
        const auto* registeredType = FindType(descriptor, type);
        if (!registeredType)
        {
            throw std::invalid_argument("Managed reference type '" + type.ToString() +
                                        "' is absent from the accepted type registry.");
        }
        const auto location = LocateValue(graph, value);
        const auto object = CreateObjectNode(graph, *registeredType);
        ResolveValue(graph, location) = {.Reference = object};
        return object;
    }

    std::uint32_t ManagedReferenceGraphActions::CreateCollection(Keire::ManagedReferenceGraphValue& value,
                                                                 Keire::ManagedReferenceGraph& graph,
                                                                 const Keire::ManagedAssetPropertyDescriptor& property)
    {
        if (property.Kind != Keire::ManagedAssetPropertyKind::Array &&
            property.Kind != Keire::ManagedAssetPropertyKind::List &&
            property.Kind != Keire::ManagedAssetPropertyKind::Dictionary)
        {
            throw std::invalid_argument("Managed field '" + property.Name + "' is not a graph collection.");
        }
        const auto location = LocateValue(graph, value);
        const auto object = CreateCollectionNode(graph, property.Kind);
        ResolveValue(graph, location) = {.Reference = object};
        return object;
    }

    void ManagedReferenceGraphActions::AddDictionaryEntry(Keire::ManagedReferenceGraphNode& node,
                                                          const Keire::ManagedAssetPropertyDescriptor& property)
    {
        if (node.Kind != Keire::ManagedReferenceGraphNodeKind::Dictionary ||
            property.Kind != Keire::ManagedAssetPropertyKind::Dictionary || property.Children.size() != 2)
        {
            throw std::invalid_argument("Managed reference dictionary descriptor is malformed.");
        }
        if (node.Entries.size() >= MaximumEditableCollectionElements)
            throw std::invalid_argument("Managed dictionary exceeds the 16,384-entry limit.");
        node.Entries.push_back({DefaultValue(property.Children[0]), DefaultValue(property.Children[1])});
    }

    void ManagedReferenceGraphActions::Finalize(Keire::ManagedReferenceGraph& graph,
                                                const Keire::ManagedReferenceGraphDescriptor& descriptor)
    {
        PruneUnreachable(graph);
        Keire::ValidateManagedReferenceGraph(graph, descriptor.Root, descriptor.Types);
    }

    bool ManagedReferenceGraphInspector::Draw(const std::string_view label, Keire::ManagedReferenceGraph& value,
                                              const Keire::ManagedReferenceGraphDescriptor& descriptor)
    {
        if (m_Controller)
            m_Controller->AssertOwnerThread();
        const auto inspectorId = m_Ui.PushId(descriptor.Root.StableFieldId.ToString());
        m_CreatedNode = false;
        m_OriginalGraph.reset();
        if (!(m_Controller ? m_Controller->Focus(m_FocusedObject, value, m_FocusedObject)
                           : FocusManagedReferenceGraphObject(m_FocusedObject, value, m_FocusedObject)))
            m_FocusedObject = 0;
        try
        {
            std::set<std::uint32_t> active;
            bool changed = DrawValue(label, value.Root, descriptor.Root, value, descriptor, active);
            if (auto* focused = FindNode(value, m_FocusedObject))
            {
                m_Ui.Separator();
                m_Ui.Text("Focused graph object: " + NodeLabel(*focused, descriptor));
                m_Ui.SameLine();
                if (m_Ui.Button("Clear focus"))
                    m_FocusedObject = 0;
                else if (focused->Kind == Keire::ManagedReferenceGraphNodeKind::Object)
                {
                    const auto focusedId = focused->Id;
                    const auto id = m_Ui.PushId("managed-graph-focus-" + std::to_string(focusedId));
                    if (m_Ui.BeginTreeNode("Object fields"))
                    {
                        active.emplace(focusedId);
                        changed = DrawNode(*focused, descriptor.Root, value, descriptor, active) || changed;
                        active.erase(focusedId);
                    }
                }
                else
                {
                    const auto count = focused->Kind == Keire::ManagedReferenceGraphNodeKind::Dictionary
                                           ? focused->Entries.size()
                                           : focused->Items.size();
                    m_Ui.Text("Collection entries: " + std::to_string(count));
                }
            }
            if (changed)
            {
                ManagedReferenceGraphActions::Finalize(value, descriptor);
                if (!(m_Controller ? m_Controller->Focus(m_FocusedObject, value, m_FocusedObject)
                                   : FocusManagedReferenceGraphObject(m_FocusedObject, value, m_FocusedObject)))
                    m_FocusedObject = 0;
            }
            m_OriginalGraph.reset();
            return changed;
        }
        catch (...)
        {
            if (m_OriginalGraph)
                value = std::move(*m_OriginalGraph);
            m_OriginalGraph.reset();
            throw;
        }
    }

    bool ManagedReferenceGraphInspector::DrawValue(const std::string_view label,
                                                   Keire::ManagedReferenceGraphValue& value,
                                                   const Keire::ManagedAssetPropertyDescriptor& property,
                                                   Keire::ManagedReferenceGraph& graph,
                                                   const Keire::ManagedReferenceGraphDescriptor& descriptor,
                                                   std::set<std::uint32_t>& active)
    {
        if (!property.ReferenceGraph && property.Kind != Keire::ManagedAssetPropertyKind::Array &&
            property.Kind != Keire::ManagedAssetPropertyKind::List &&
            property.Kind != Keire::ManagedAssetPropertyKind::Dictionary)
        {
            return DrawScalar(value, property, graph);
        }

        bool changed = false;
        auto* selected = FindNode(graph, value.Reference);
        const auto preview = selected ? NodeLabel(*selected, descriptor) : std::string("Null");
        if (auto combo = m_Ui.BeginCombo(std::string(label) + " Reference", preview); combo)
        {
            if (m_Ui.Selectable("Null", value.Reference == 0))
            {
                BeginMutation(graph);
                changed = ManagedReferenceGraphActions::LinkValue(value, graph, property, 0) || changed;
            }
            for (const auto& candidate : graph.Objects)
            {
                if (Compatible(candidate, property) &&
                    m_Ui.Selectable("Link " + NodeLabel(candidate, descriptor), candidate.Id == value.Reference))
                {
                    BeginMutation(graph);
                    changed = ManagedReferenceGraphActions::LinkValue(value, graph, property, candidate.Id) || changed;
                }
            }
            if (!m_CreatedNode && property.ReferenceGraph && !property.ReferenceTypeChoices.empty())
            {
                for (const auto typeId : property.ReferenceTypeChoices)
                {
                    const auto* type = FindType(descriptor, typeId);
                    if (type && m_Ui.Selectable("Create " + type->DisplayName))
                    {
                        BeginMutation(graph);
                        (void)ManagedReferenceGraphActions::CreateObject(value, graph, property, descriptor, typeId);
                        m_CreatedNode = true;
                        changed = true;
                        break;
                    }
                }
            }
            else if (!m_CreatedNode && m_Ui.Selectable("Create collection"))
            {
                BeginMutation(graph);
                (void)ManagedReferenceGraphActions::CreateCollection(value, graph, property);
                m_CreatedNode = true;
                changed = true;
            }
        }

        if (m_CreatedNode)
            return true;

        selected = FindNode(graph, value.Reference);
        if (!selected)
            return changed;
        m_Ui.SameLine();
        if (m_Ui.Button("Focus " + NodeLabel(*selected, descriptor)))
        {
            (void)(m_Controller ? m_Controller->Focus(m_FocusedObject, graph, selected->Id)
                                : FocusManagedReferenceGraphObject(m_FocusedObject, graph, selected->Id));
        }
        if (active.contains(selected->Id))
        {
            m_Ui.Text("Cycle link to " + NodeLabel(*selected, descriptor));
            return changed;
        }
        if (!m_Ui.BeginTreeNode(std::string(label) + " -> " + NodeLabel(*selected, descriptor)))
            return changed;
        const auto selectedId = selected->Id;
        active.emplace(selectedId);
        changed = DrawNode(*selected, property, graph, descriptor, active) || changed;
        active.erase(selectedId);
        return changed;
    }

    bool ManagedReferenceGraphInspector::DrawNode(Keire::ManagedReferenceGraphNode& node,
                                                  const Keire::ManagedAssetPropertyDescriptor& property,
                                                  Keire::ManagedReferenceGraph& graph,
                                                  const Keire::ManagedReferenceGraphDescriptor& descriptor,
                                                  std::set<std::uint32_t>& active)
    {
        bool changed = false;
        if (node.Kind == Keire::ManagedReferenceGraphNodeKind::Object)
        {
            const auto* type = FindType(descriptor, node.RuntimeType);
            if (!type)
                throw std::invalid_argument("Managed reference graph object uses an unregistered type.");
            for (const auto& childProperty : type->Properties)
            {
                auto field = std::ranges::find(node.Fields, childProperty.StableFieldId,
                                               &Keire::ManagedReferenceGraphField::StableFieldId);
                if (field == node.Fields.end())
                    continue;
                const auto id = m_Ui.PushId(childProperty.StableFieldId.ToString());
                changed =
                    DrawValue(childProperty.DisplayName, field->Value, childProperty, graph, descriptor, active) ||
                    changed;
                if (m_CreatedNode)
                    return true;
            }
            return changed;
        }

        if (node.Kind == Keire::ManagedReferenceGraphNodeKind::Array ||
            node.Kind == Keire::ManagedReferenceGraphNodeKind::List)
        {
            if (property.Children.size() != 1)
                throw std::invalid_argument("Managed reference collection descriptor is malformed.");
            for (std::size_t index = 0; index < node.Items.size();)
            {
                const auto id = m_Ui.PushId(std::to_string(index));
                changed = DrawValue("Element " + std::to_string(index), node.Items[index], property.Children.front(),
                                    graph, descriptor, active) ||
                          changed;
                if (m_CreatedNode)
                    return true;
                m_Ui.SameLine();
                if (m_Ui.Button("Remove"))
                {
                    BeginMutation(graph);
                    node.Items.erase(node.Items.begin() + static_cast<std::ptrdiff_t>(index));
                    changed = true;
                    continue;
                }
                ++index;
            }
            if (node.Items.size() < MaximumEditableCollectionElements && m_Ui.Button("Add Element"))
            {
                BeginMutation(graph);
                node.Items.push_back(DefaultValue(property.Children.front()));
                changed = true;
            }
            return changed;
        }

        if (property.Children.size() != 2)
            throw std::invalid_argument("Managed reference dictionary descriptor is malformed.");
        for (std::size_t index = 0; index < node.Entries.size();)
        {
            const auto id = m_Ui.PushId(std::to_string(index));
            auto& entry = node.Entries[index];
            changed = DrawValue("Key", entry.Key, property.Children[0], graph, descriptor, active) || changed;
            if (m_CreatedNode)
                return true;
            changed = DrawValue("Value", entry.Value, property.Children[1], graph, descriptor, active) || changed;
            if (m_CreatedNode)
                return true;
            if (m_Ui.Button("Remove Entry"))
            {
                BeginMutation(graph);
                node.Entries.erase(node.Entries.begin() + static_cast<std::ptrdiff_t>(index));
                changed = true;
                continue;
            }
            ++index;
        }
        if (node.Entries.size() < MaximumEditableCollectionElements && m_Ui.Button("Add Entry"))
        {
            BeginMutation(graph);
            ManagedReferenceGraphActions::AddDictionaryEntry(node, property);
            changed = true;
        }
        return changed;
    }

    bool ManagedReferenceGraphInspector::DrawScalar(Keire::ManagedReferenceGraphValue& value,
                                                    const Keire::ManagedAssetPropertyDescriptor& property,
                                                    const Keire::ManagedReferenceGraph& graph)
    {
        auto node = Keire::DecodeManagedAssetValue(value.Scalar, property);
        const auto label = property.DisplayName.empty() ? property.Name : property.DisplayName;
        const auto signedBound = [](const std::optional<double> candidate) -> std::optional<std::int64_t>
        {
            if (!candidate)
                return std::nullopt;
            return static_cast<std::int64_t>(
                std::clamp(static_cast<long double>(*candidate),
                           static_cast<long double>(std::numeric_limits<std::int64_t>::min()),
                           static_cast<long double>(std::numeric_limits<std::int64_t>::max())));
        };
        const auto unsignedBound = [](const std::optional<double> candidate) -> std::optional<std::uint64_t>
        {
            if (!candidate)
                return std::nullopt;
            return static_cast<std::uint64_t>(
                std::clamp(static_cast<long double>(*candidate), 0.0L,
                           static_cast<long double>(std::numeric_limits<std::uint64_t>::max())));
        };
        bool changed = false;
        switch (property.Kind)
        {
        case Keire::ManagedAssetPropertyKind::Boolean:
            changed = m_Ui.Checkbox(label, std::get<bool>(node.Value));
            break;
        case Keire::ManagedAssetPropertyKind::Integer:
        case Keire::ManagedAssetPropertyKind::Enum:
            if (property.ManagedTypeName == "System.Char")
                changed = m_Ui.InputText(label, std::get<std::string>(node.Value));
            else
                changed = m_Ui.DragInteger(label, std::get<std::int64_t>(node.Value), property.Step,
                                           signedBound(property.Minimum), signedBound(property.Maximum));
            break;
        case Keire::ManagedAssetPropertyKind::UnsignedInteger:
            changed = m_Ui.DragUnsignedInteger(label, std::get<std::uint64_t>(node.Value), property.Step,
                                               unsignedBound(property.Minimum), unsignedBound(property.Maximum));
            break;
        case Keire::ManagedAssetPropertyKind::Scalar:
            changed =
                m_Ui.DragScalar(label, std::get<double>(node.Value), property.Step, property.Minimum, property.Maximum);
            break;
        case Keire::ManagedAssetPropertyKind::Text:
            changed = m_Ui.InputText(label, std::get<std::string>(node.Value));
            break;
        case Keire::ManagedAssetPropertyKind::CustomValue:
        {
            auto candidate = std::get<std::string>(node.Value);
            changed = m_Ui.InputTextMultiline(label, candidate, 6);
            if (changed)
                node = Keire::DecodeManagedAssetValue(candidate, property);
            break;
        }
        case Keire::ManagedAssetPropertyKind::Vector2:
            changed = m_Ui.DragVector2(label, std::get<Keire::Vector2>(node.Value));
            break;
        case Keire::ManagedAssetPropertyKind::Vector3:
            changed = m_Ui.DragVector3(label, std::get<Keire::Vector3>(node.Value));
            break;
        case Keire::ManagedAssetPropertyKind::Vector4:
            changed = m_Ui.DragVector4(label, std::get<Keire::Vector4>(node.Value));
            break;
        case Keire::ManagedAssetPropertyKind::Quaternion:
            changed = m_Ui.DragQuaternion(label, std::get<Keire::Quaternion>(node.Value));
            break;
        case Keire::ManagedAssetPropertyKind::Color:
        {
            auto& color = std::get<Keire::Color>(node.Value);
            Keire::UiColor editable{color.Red, color.Green, color.Blue, color.Alpha};
            changed = m_Ui.ColorEdit(label, editable);
            if (changed)
                color = {editable.Red, editable.Green, editable.Blue, editable.Alpha};
            break;
        }
        case Keire::ManagedAssetPropertyKind::AssetReference:
        {
            auto text = std::get<Keire::AssetId>(node.Value).ToString();
            if (m_Ui.InputText(label, text))
            {
                node.Value = text.empty() ? Keire::AssetId{} : Keire::AssetId::Parse(text);
                changed = true;
            }
            break;
        }
        case Keire::ManagedAssetPropertyKind::SerializableObject:
        case Keire::ManagedAssetPropertyKind::Array:
        case Keire::ManagedAssetPropertyKind::List:
        case Keire::ManagedAssetPropertyKind::Dictionary:
            throw std::logic_error("Managed graph node values must be edited through graph links.");
        }
        if (changed)
        {
            BeginMutation(graph);
            value.Scalar = Keire::EncodeManagedAssetValue(node, property);
        }
        return changed;
    }

    void ManagedReferenceGraphInspector::BeginMutation(const Keire::ManagedReferenceGraph& graph)
    {
        // Graphs are intentionally snapshotted only after the UI reports an edit. Idle Inspector frames must not
        // duplicate the bounded 16 MiB object table.
        if (!m_OriginalGraph)
            m_OriginalGraph = graph;
    }
} // namespace KeireEditor

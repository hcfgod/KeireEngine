#include "KeireClient/Editor/ManagedDataDocument.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <ranges>
#include <set>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] Keire::ManagedAssetValueNode DefaultNode(const Keire::ManagedAssetPropertyDescriptor& property)
        {
            Keire::ManagedAssetValueNode result;
            result.StableFieldId = property.StableFieldId;
            result.Kind = property.Kind;
            if (property.Kind == Keire::ManagedAssetPropertyKind::SerializableObject)
            {
                result.Children.reserve(property.Children.size());
                for (const auto& child : property.Children)
                    result.Children.push_back(DefaultNode(child));
            }
            return result;
        }

        void Materialize(Keire::ManagedAssetValueNode& value, const Keire::ManagedAssetPropertyDescriptor& property)
        {
            switch (property.Kind)
            {
            case Keire::ManagedAssetPropertyKind::Boolean:
                value.Value = false;
                break;
            case Keire::ManagedAssetPropertyKind::Integer:
                value.Value = property.ManagedTypeName == "System.Char"
                                  ? Keire::ManagedAssetScalarValue(std::string(1, '\0'))
                                  : Keire::ManagedAssetScalarValue(std::int64_t{0});
                break;
            case Keire::ManagedAssetPropertyKind::Enum:
                value.Value = std::int64_t{0};
                break;
            case Keire::ManagedAssetPropertyKind::UnsignedInteger:
                value.Value = std::uint64_t{0};
                break;
            case Keire::ManagedAssetPropertyKind::Scalar:
                value.Value = 0.0;
                break;
            case Keire::ManagedAssetPropertyKind::Text:
                value.Value = std::string{};
                break;
            case Keire::ManagedAssetPropertyKind::CustomValue:
                if (!property.CustomValueTypeId || property.CustomValueVersion == 0)
                    throw std::invalid_argument("Managed custom value descriptor is incomplete.");
                value.Value = std::string("{\"$custom\":\"") + property.CustomValueTypeId->ToString() +
                              "\",\"payload\":null,\"version\":" + std::to_string(property.CustomValueVersion) + '}';
                break;
            case Keire::ManagedAssetPropertyKind::Vector2:
                value.Value = Keire::Vector2{};
                break;
            case Keire::ManagedAssetPropertyKind::Vector3:
                value.Value = Keire::Vector3{};
                break;
            case Keire::ManagedAssetPropertyKind::Vector4:
                value.Value = Keire::Vector4{};
                break;
            case Keire::ManagedAssetPropertyKind::Quaternion:
                value.Value = Keire::Quaternion{};
                break;
            case Keire::ManagedAssetPropertyKind::Color:
                value.Value = Keire::Color{};
                break;
            case Keire::ManagedAssetPropertyKind::AssetReference:
                value.Value = Keire::AssetId{};
                break;
            case Keire::ManagedAssetPropertyKind::SerializableObject:
            case Keire::ManagedAssetPropertyKind::Array:
            case Keire::ManagedAssetPropertyKind::List:
            case Keire::ManagedAssetPropertyKind::Dictionary:
                value.Value = true;
                break;
            }
        }

        [[nodiscard]] bool IsPresent(const Keire::ManagedAssetValueNode& value) noexcept
        {
            return !std::holds_alternative<std::monostate>(value.Value);
        }

        [[nodiscard]] bool IsNullable(const Keire::ManagedAssetPropertyKind kind) noexcept
        {
            return kind == Keire::ManagedAssetPropertyKind::Text ||
                   kind == Keire::ManagedAssetPropertyKind::SerializableObject ||
                   kind == Keire::ManagedAssetPropertyKind::Array || kind == Keire::ManagedAssetPropertyKind::List ||
                   kind == Keire::ManagedAssetPropertyKind::Dictionary;
        }

        [[nodiscard]] const Keire::ManagedAssetPropertyDescriptor*
        FindProperty(const Keire::ManagedAssetTypeDescriptor& descriptor, const Keire::AssetId stableId)
        {
            const auto found = std::ranges::find(descriptor.Properties, stableId,
                                                 &Keire::ManagedAssetPropertyDescriptor::StableFieldId);
            return found == descriptor.Properties.end() ? nullptr : std::addressof(*found);
        }

        void CollectDependencies(const Keire::ManagedAssetValueNode& value,
                                 const Keire::ManagedAssetPropertyDescriptor& property,
                                 std::map<Keire::AssetId, Keire::ManagedDataAssetDependency>& destination)
        {
            if (!IsPresent(value))
                return;
            if (property.Kind == Keire::ManagedAssetPropertyKind::AssetReference)
            {
                const auto asset = std::get<Keire::AssetId>(value.Value);
                if (!asset)
                    return;
                Keire::ManagedDataAssetDependency dependency;
                dependency.Asset = asset;
                dependency.AssetType = *property.ExpectedAssetType;
                dependency.ManagedType = property.ExpectedManagedType;
                const auto [found, inserted] = destination.emplace(asset, dependency);
                if (!inserted && !(found->second == dependency))
                    throw std::invalid_argument("Managed data references one asset through incompatible types.");
                return;
            }
            if (property.Kind == Keire::ManagedAssetPropertyKind::SerializableObject)
            {
                for (const auto& childProperty : property.Children)
                {
                    const auto child = std::ranges::find(value.Children, childProperty.StableFieldId,
                                                         &Keire::ManagedAssetValueNode::StableFieldId);
                    if (child != value.Children.end())
                        CollectDependencies(*child, childProperty, destination);
                }
                return;
            }
            if (property.Kind == Keire::ManagedAssetPropertyKind::Array ||
                property.Kind == Keire::ManagedAssetPropertyKind::List)
            {
                for (const auto& child : value.Children)
                    CollectDependencies(child, property.Children.front(), destination);
                return;
            }
            if (property.Kind == Keire::ManagedAssetPropertyKind::Dictionary)
            {
                for (const auto& entry : value.Children)
                {
                    if (entry.Children.size() != 2 || property.Children.size() != 2)
                        throw std::invalid_argument("Managed dictionary contains a malformed entry.");
                    CollectDependencies(entry.Children[0], property.Children[0], destination);
                    CollectDependencies(entry.Children[1], property.Children[1], destination);
                }
            }
        }

        [[nodiscard]] const Keire::ManagedReferenceGraphNode* FindGraphNode(const Keire::ManagedReferenceGraph& graph,
                                                                            const std::uint32_t id) noexcept
        {
            const auto found = std::ranges::find(graph.Objects, id, &Keire::ManagedReferenceGraphNode::Id);
            return found == graph.Objects.end() ? nullptr : std::addressof(*found);
        }

        [[nodiscard]] const Keire::ManagedAssetReferenceTypeDescriptor*
        FindReferenceType(const std::span<const Keire::ManagedAssetReferenceTypeDescriptor> types,
                          const Keire::ManagedTypeId id) noexcept
        {
            const auto found = std::ranges::find(types, id, &Keire::ManagedAssetReferenceTypeDescriptor::StableTypeId);
            return found == types.end() ? nullptr : std::addressof(*found);
        }

        bool CollectGraphDependencies(const Keire::ManagedReferenceGraphValue& value,
                                      const Keire::ManagedAssetPropertyDescriptor& property,
                                      const Keire::ManagedReferenceGraph& graph,
                                      const std::span<const Keire::ManagedAssetReferenceTypeDescriptor> types,
                                      std::map<Keire::AssetId, Keire::ManagedDataAssetDependency>& destination,
                                      std::set<std::pair<std::uint32_t, Keire::AssetId>>& expanded)
        {
            if (value.Reference == 0)
            {
                if (property.Kind == Keire::ManagedAssetPropertyKind::AssetReference)
                    CollectDependencies(Keire::DecodeManagedAssetValue(value.Scalar, property), property, destination);
                return false;
            }

            const auto* node = FindGraphNode(graph, value.Reference);
            if (!node || !expanded.emplace(node->Id, property.StableFieldId).second)
                return node == nullptr;
            bool unknown = false;
            if (node->Kind == Keire::ManagedReferenceGraphNodeKind::Object)
            {
                const auto* type = FindReferenceType(types, node->RuntimeType);
                if (!type)
                    return true;
                for (const auto& field : node->Fields)
                {
                    const auto descriptor = std::ranges::find(type->Properties, field.StableFieldId,
                                                              &Keire::ManagedAssetPropertyDescriptor::StableFieldId);
                    if (descriptor == type->Properties.end())
                    {
                        unknown = true;
                        continue;
                    }
                    unknown = CollectGraphDependencies(field.Value, *descriptor, graph, types, destination, expanded) ||
                              unknown;
                }
                return unknown;
            }
            if (node->Kind == Keire::ManagedReferenceGraphNodeKind::Array ||
                node->Kind == Keire::ManagedReferenceGraphNodeKind::List)
            {
                if (property.Children.size() != 1)
                    return true;
                for (const auto& item : node->Items)
                    unknown = CollectGraphDependencies(item, property.Children.front(), graph, types, destination,
                                                       expanded) ||
                              unknown;
                return unknown;
            }
            if (property.Children.size() != 2)
                return true;
            for (const auto& entry : node->Entries)
            {
                unknown =
                    CollectGraphDependencies(entry.Key, property.Children[0], graph, types, destination, expanded) ||
                    unknown;
                unknown =
                    CollectGraphDependencies(entry.Value, property.Children[1], graph, types, destination, expanded) ||
                    unknown;
            }
            return unknown;
        }

        [[nodiscard]] bool IsDerivedFrom(const Keire::ManagedTypeId actual, const Keire::ManagedTypeId expected,
                                         const std::span<const Keire::ManagedAssetTypeDescriptor> types) noexcept
        {
            auto current = actual;
            for (std::size_t depth = 0; current && depth <= types.size(); ++depth)
            {
                if (current == expected)
                    return true;
                const auto found = std::ranges::find(types, current, &Keire::ManagedAssetTypeDescriptor::StableTypeId);
                if (found == types.end() || !found->BaseTypeId)
                    return false;
                current = *found->BaseTypeId;
            }
            return false;
        }
    } // namespace

    ManagedDataDocument::ManagedDataDocument(Specification specification)
        : m_Specification(std::move(specification)),
          m_Host({.Validate = [](const Keire::ManagedDataDefinition& definition)
                  { Keire::ManagedDataAsset::Validate(definition); },
                  .Encode = [](const Keire::ManagedDataDefinition& definition)
                  { return Keire::ManagedDataAsset::Encode(definition); },
                  .Preview =
                      [this](const Keire::AssetId asset, const Keire::ManagedDataDefinition& definition)
                  {
                      if (!m_SuppressPreview && m_Specification.Preview)
                          m_Specification.Preview(asset, definition);
                  },
                  .Persist =
                      [this](const Keire::AssetId asset, const std::span<const std::byte> bytes)
                  {
                      if (!m_Specification.Persist)
                          throw std::logic_error("Managed data persistence is unavailable.");
                      m_Specification.Persist(asset, bytes);
                  }})
    {
        if (!m_Specification.Persist)
            throw std::invalid_argument("Managed data documents require a persistence callback.");
    }

    ManagedDataDocument::~ManagedDataDocument() { Close(); }

    void ManagedDataDocument::Open(const Keire::AssetId asset, Keire::ManagedDataDefinition definition,
                                   const std::uint64_t revision,
                                   std::optional<Keire::ManagedAssetTypeDescriptor> descriptor,
                                   Keire::Ref<Keire::UndoContext> undo)
    {
        Close();
        m_Diagnostic.clear();
        if (descriptor && descriptor->StableTypeId != definition.ManagedType)
        {
            m_Diagnostic = "The managed type descriptor does not match this asset's stable type ID.";
            descriptor.reset();
        }
        m_Descriptor = std::move(descriptor);
        m_SuppressPreview = true;
        try
        {
            m_Host.Open(asset, std::move(definition), revision, std::move(undo));
        }
        catch (...)
        {
            m_SuppressPreview = false;
            m_Descriptor.reset();
            throw;
        }
        m_SuppressPreview = false;
    }

    void ManagedDataDocument::Close() noexcept
    {
        if (m_Host.IsOpen() && m_Host.Dirty())
        {
            try
            {
                m_Host.Discard();
            }
            catch (...)
            {
            }
        }
        m_Host.Close();
        m_Descriptor.reset();
        m_RejectedGraphDiagnostic.reset();
        m_Diagnostic.clear();
        m_SuppressPreview = false;
    }

    const Keire::ManagedAssetTypeDescriptor* ManagedDataDocument::Descriptor() const noexcept
    {
        return m_Descriptor ? std::addressof(*m_Descriptor) : nullptr;
    }

    std::string_view ManagedDataDocument::Diagnostic() const noexcept
    {
        return m_Diagnostic.empty() ? m_Host.Diagnostic() : std::string_view(m_Diagnostic);
    }

    ManagedDataPropertyState
    ManagedDataDocument::Property(const Keire::ManagedAssetPropertyDescriptor& property) const noexcept
    {
        ManagedDataPropertyState result;
        result.Value = DefaultValue(property);
        if (!m_Host.IsOpen())
        {
            result.Diagnostic = "The managed data document is not open.";
            return result;
        }
        const auto field = std::ranges::find(m_Host.Draft().Fields, property.StableFieldId,
                                             &Keire::ManagedDataFieldState::StableFieldId);
        if (field == m_Host.Draft().Fields.end())
            return result;
        result.Serialized = true;
        result.RawValue = field->Value;
        if (property.ReferenceGraph || field->ReferenceGraph)
        {
            result.Diagnostic =
                "KEIRE-MANAGED-SERIALIZATION-0003: Reference graph fields require graph Inspector editing.";
            return result;
        }
        if (field->ManagedTypeName != property.ManagedTypeName)
        {
            result.Diagnostic = "The serialized managed type does not match the current descriptor.";
            return result;
        }
        try
        {
            result.Value = Keire::DecodeManagedAssetValue(field->Value, property);
        }
        catch (const std::exception& error)
        {
            result.Diagnostic = error.what();
        }
        return result;
    }

    ManagedDataGraphPropertyState
    ManagedDataDocument::GraphProperty(const Keire::ManagedAssetPropertyDescriptor& property) const noexcept
    {
        ManagedDataGraphPropertyState result;
        if (!property.ReferenceGraph)
        {
            result.Diagnostic =
                "KEIRE-MANAGED-SERIALIZATION-0003: The requested Inspector field is not a reference graph.";
            return result;
        }
        if (!m_Host.IsOpen())
        {
            result.Diagnostic = "The managed data document is not open.";
            return result;
        }
        const auto field = std::ranges::find(m_Host.Draft().Fields, property.StableFieldId,
                                             &Keire::ManagedDataFieldState::StableFieldId);
        if (field == m_Host.Draft().Fields.end())
            return result;
        result.Serialized = true;
        result.RawValue = field->Value;
        if (!field->ReferenceGraph)
        {
            result.Diagnostic = "KEIRE-MANAGED-SERIALIZATION-0003: Serialized field '" + property.Name +
                                "' is missing its reference-graph marker.";
            return result;
        }
        if (field->ManagedTypeName != property.ManagedTypeName)
        {
            result.Diagnostic = "The serialized managed type does not match the current descriptor.";
            return result;
        }
        try
        {
            if (!m_Host.Draft().ReferenceGraph.empty() && !field->ReferenceGraphRoot.empty())
            {
                const auto shared = Keire::DecodeManagedReferenceGraph(m_Host.Draft().ReferenceGraph);
                result.Value = Keire::ExtractManagedReferenceGraphRoot(shared, field->ReferenceGraphRoot);
            }
            else
            {
                result.Value = Keire::DecodeManagedReferenceGraph(field->Value);
            }
            if (!m_Descriptor)
                throw std::logic_error("Managed reference graph validation requires a current type descriptor.");
            Keire::ValidateManagedReferenceGraph(result.Value, property, m_Descriptor->ReferenceTypes);
            if (m_RejectedGraphDiagnostic && m_RejectedGraphDiagnostic->RootField == property.Name)
            {
                result.Diagnostic = m_Diagnostic;
                result.StructuredDiagnostic = m_RejectedGraphDiagnostic;
            }
        }
        catch (const Keire::ManagedSerializationError& error)
        {
            result.Diagnostic = error.what();
            result.StructuredDiagnostic = error.Details();
        }
        catch (const std::exception& error)
        {
            result.Diagnostic = error.what();
        }
        return result;
    }

    bool ManagedDataDocument::SetProperty(const Keire::ManagedAssetPropertyDescriptor& property,
                                          Keire::ManagedAssetValueNode value, const std::string_view undoName)
    {
        if (!m_Descriptor || !FindProperty(*m_Descriptor, property.StableFieldId))
            throw std::logic_error("Managed data property editing requires a current type descriptor.");
        if (property.ReferenceGraph)
        {
            throw std::invalid_argument(
                "KEIRE-MANAGED-SERIALIZATION-0003: Reference graph fields require SetGraphProperty().");
        }
        if (!IsPresent(value) && !IsNullable(property.Kind))
            value = MaterializedDefaultValue(property);
        const auto encoded = Keire::EncodeManagedAssetValue(value, property);
        auto candidate = m_Host.Draft();
        auto field =
            std::ranges::find(candidate.Fields, property.StableFieldId, &Keire::ManagedDataFieldState::StableFieldId);
        if (field == candidate.Fields.end())
        {
            candidate.Fields.push_back({.StableFieldId = property.StableFieldId,
                                        .Name = property.Name,
                                        .ManagedTypeName = property.ManagedTypeName,
                                        .Value = encoded});
        }
        else
        {
            if (field->ReferenceGraph && !candidate.ReferenceGraph.empty() && !field->ReferenceGraphRoot.empty())
            {
                auto shared = Keire::DecodeManagedReferenceGraph(candidate.ReferenceGraph);
                Keire::RemoveManagedReferenceGraphRoot(shared, field->ReferenceGraphRoot);
                candidate.ReferenceGraph =
                    shared.Roots.empty() ? std::string{} : Keire::EncodeManagedReferenceGraph(shared);
            }
            field->Name = property.Name;
            field->ManagedTypeName = property.ManagedTypeName;
            field->ReferenceGraph = false;
            field->ReferenceGraphRoot.clear();
            field->Value = encoded;
        }
        RebuildDependencies(candidate);
        candidate = Keire::ManagedDataAsset::Canonicalize(std::move(candidate));
        const auto edited = m_Host.Edit(undoName, std::move(candidate));
        if (edited)
        {
            m_RejectedGraphDiagnostic.reset();
            m_Diagnostic.clear();
        }
        return edited;
    }

    bool ManagedDataDocument::SetGraphProperty(const Keire::ManagedAssetPropertyDescriptor& property,
                                               Keire::ManagedReferenceGraph value, const std::string_view undoName)
    {
        if (!m_Descriptor || !FindProperty(*m_Descriptor, property.StableFieldId))
            throw std::logic_error("Managed data graph editing requires a current type descriptor.");
        if (!property.ReferenceGraph)
        {
            throw std::invalid_argument(
                "KEIRE-MANAGED-SERIALIZATION-0003: SetGraphProperty requires a SerializeReference field.");
        }

        Keire::ValidateManagedReferenceGraph(value, property, m_Descriptor->ReferenceTypes);
        const auto encoded = Keire::EncodeManagedReferenceGraph(value);
        auto candidate = m_Host.Draft();
        auto field =
            std::ranges::find(candidate.Fields, property.StableFieldId, &Keire::ManagedDataFieldState::StableFieldId);
        if (field == candidate.Fields.end())
        {
            const auto rootKey = "id:" + property.StableFieldId.ToString();
            candidate.Fields.push_back({.StableFieldId = property.StableFieldId,
                                        .Name = property.Name,
                                        .ManagedTypeName = property.ManagedTypeName,
                                        .ReferenceGraph = true,
                                        .ReferenceGraphRoot = rootKey,
                                        .Value = encoded});
            field = std::prev(candidate.Fields.end());
        }
        else
        {
            field->Name = property.Name;
            field->ManagedTypeName = property.ManagedTypeName;
            field->ReferenceGraph = true;
            if (field->ReferenceGraphRoot.empty())
                field->ReferenceGraphRoot = "id:" + property.StableFieldId.ToString();
            field->Value = encoded;
        }
        auto shared = candidate.ReferenceGraph.empty() ? Keire::ManagedReferenceGraph{.Version = 2}
                                                       : Keire::DecodeManagedReferenceGraph(candidate.ReferenceGraph);
        Keire::UpdateManagedReferenceGraphRoot(shared, field->ReferenceGraphRoot, value);
        candidate.ReferenceGraph = Keire::EncodeManagedReferenceGraph(shared);
        RebuildDependencies(candidate);
        candidate = Keire::ManagedDataAsset::Canonicalize(std::move(candidate));
        const auto edited = m_Host.Edit(undoName, std::move(candidate));
        if (edited)
        {
            m_RejectedGraphDiagnostic.reset();
            m_Diagnostic.clear();
        }
        return edited;
    }

    bool ManagedDataDocument::ClearProperty(const Keire::ManagedAssetPropertyDescriptor& property,
                                            const std::string_view undoName)
    {
        auto candidate = m_Host.Draft();
        const auto found =
            std::ranges::find(candidate.Fields, property.StableFieldId, &Keire::ManagedDataFieldState::StableFieldId);
        const auto graphRoot =
            found != candidate.Fields.end() && found->ReferenceGraph ? found->ReferenceGraphRoot : std::string{};
        const auto removed = std::erase_if(candidate.Fields, [&property](const Keire::ManagedDataFieldState& field)
                                           { return field.StableFieldId == property.StableFieldId; });
        if (removed == 0)
            return false;
        if (!graphRoot.empty() && !candidate.ReferenceGraph.empty())
        {
            auto shared = Keire::DecodeManagedReferenceGraph(candidate.ReferenceGraph);
            Keire::RemoveManagedReferenceGraphRoot(shared, graphRoot);
            candidate.ReferenceGraph =
                shared.Roots.empty() ? std::string{} : Keire::EncodeManagedReferenceGraph(shared);
        }
        RebuildDependencies(candidate);
        candidate = Keire::ManagedDataAsset::Canonicalize(std::move(candidate));
        const auto edited = m_Host.Edit(undoName, std::move(candidate));
        if (edited)
        {
            m_RejectedGraphDiagnostic.reset();
            m_Diagnostic.clear();
        }
        return edited;
    }

    AssetDocumentReloadResult ManagedDataDocument::Reload(const Keire::ManagedDataDefinition& definition,
                                                          const std::uint64_t revision)
    {
        if (definition == m_Host.Draft())
        {
            m_Host.AcknowledgeRevision(revision);
            m_RejectedGraphDiagnostic.reset();
            m_Diagnostic.clear();
            return AssetDocumentReloadResult::Unchanged;
        }
        if (m_Host.Dirty())
            return m_Host.Reload(definition, revision);
        m_SuppressPreview = true;
        try
        {
            ValidateReloadCandidate(definition);
            const auto result = m_Host.Reload(definition, revision);
            m_SuppressPreview = false;
            m_RejectedGraphDiagnostic.reset();
            m_Diagnostic.clear();
            return result;
        }
        catch (const Keire::ManagedSerializationError& error)
        {
            m_SuppressPreview = false;
            m_RejectedGraphDiagnostic = error.Details();
            m_Diagnostic = error.what();
            throw;
        }
        catch (const std::exception& error)
        {
            m_SuppressPreview = false;
            m_RejectedGraphDiagnostic.reset();
            m_Diagnostic = error.what();
            throw;
        }
        catch (...)
        {
            m_SuppressPreview = false;
            m_RejectedGraphDiagnostic.reset();
            m_Diagnostic = "Managed data reload failed with an unknown error.";
            throw;
        }
    }

    void ManagedDataDocument::ValidateReloadCandidate(const Keire::ManagedDataDefinition& definition) const
    {
        Keire::ManagedDataAsset::Validate(definition);
        if (!m_Descriptor)
            return;

        for (const auto& field : definition.Fields)
        {
            const auto* property = FindProperty(*m_Descriptor, field.StableFieldId);
            if (!property)
                continue;
            if (field.ReferenceGraph != property->ReferenceGraph)
            {
                throw std::invalid_argument("Managed field '" + property->Name +
                                            "' has a SerializeReference marker that does not match the descriptor.");
            }
            if (field.ReferenceGraph)
            {
                const auto graph = Keire::DecodeManagedReferenceGraph(field.Value);
                Keire::ValidateManagedReferenceGraph(graph, *property, m_Descriptor->ReferenceTypes);
            }
            else
            {
                (void)Keire::DecodeManagedAssetValue(field.Value, *property);
            }
        }
    }

    Keire::ManagedAssetValueNode
    ManagedDataDocument::DefaultValue(const Keire::ManagedAssetPropertyDescriptor& property)
    {
        return DefaultNode(property);
    }

    Keire::ManagedAssetValueNode
    ManagedDataDocument::MaterializedDefaultValue(const Keire::ManagedAssetPropertyDescriptor& property)
    {
        auto result = DefaultNode(property);
        Materialize(result, property);
        return result;
    }

    bool
    ManagedDataDocument::AcceptsAssetReference(const Keire::AssetSourceRecord& record,
                                               const Keire::ManagedAssetPropertyDescriptor& property,
                                               const std::optional<Keire::ManagedDataDefinition>& managedDefinition,
                                               const std::span<const Keire::ManagedAssetTypeDescriptor> types) noexcept
    {
        if (property.Kind != Keire::ManagedAssetPropertyKind::AssetReference || !property.ExpectedAssetType ||
            record.Type != *property.ExpectedAssetType)
        {
            return false;
        }
        if (!property.ExpectedManagedType)
            return true;
        if (!managedDefinition ||
            (managedDefinition->ManagedType != *property.ExpectedManagedType && !property.IncludeDerivedAssetTypes))
        {
            return false;
        }
        return managedDefinition->ManagedType == *property.ExpectedManagedType ||
               IsDerivedFrom(managedDefinition->ManagedType, *property.ExpectedManagedType, types);
    }

    void ManagedDataDocument::RebuildDependencies(Keire::ManagedDataDefinition& definition) const
    {
        if (!m_Descriptor)
            return;
        bool hasUnknownField = false;
        std::map<Keire::AssetId, Keire::ManagedDataAssetDependency> dependencies;
        for (const auto& field : definition.Fields)
        {
            const auto* property = FindProperty(*m_Descriptor, field.StableFieldId);
            if (!property)
            {
                hasUnknownField = true;
                continue;
            }
            try
            {
                if (field.ReferenceGraph || property->ReferenceGraph)
                {
                    if (field.ReferenceGraph != property->ReferenceGraph)
                        throw std::invalid_argument("Managed reference graph marker does not match the descriptor.");
                    const auto graph = Keire::DecodeManagedReferenceGraph(field.Value);
                    Keire::ValidateManagedReferenceGraph(graph, *property, m_Descriptor->ReferenceTypes);
                    std::set<std::pair<std::uint32_t, Keire::AssetId>> expanded;
                    hasUnknownField = CollectGraphDependencies(graph.Root, *property, graph,
                                                               m_Descriptor->ReferenceTypes, dependencies, expanded) ||
                                      hasUnknownField;
                }
                else
                {
                    CollectDependencies(Keire::DecodeManagedAssetValue(field.Value, *property), *property,
                                        dependencies);
                }
            }
            catch (...)
            {
                hasUnknownField = true;
            }
        }
        if (hasUnknownField)
        {
            for (const auto& dependency : definition.Dependencies)
                dependencies.try_emplace(dependency.Asset, dependency);
        }
        definition.Dependencies.clear();
        definition.Dependencies.reserve(dependencies.size());
        for (auto& [asset, dependency] : dependencies)
        {
            (void)asset;
            definition.Dependencies.push_back(dependency);
        }
    }
} // namespace KeireEditor

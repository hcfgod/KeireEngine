#include "KeireClient/Editor/ManagedDataDocument.h"

#include <algorithm>
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
                   kind == Keire::ManagedAssetPropertyKind::Array || kind == Keire::ManagedAssetPropertyKind::List;
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
            }
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

    bool ManagedDataDocument::SetProperty(const Keire::ManagedAssetPropertyDescriptor& property,
                                          Keire::ManagedAssetValueNode value, const std::string_view undoName)
    {
        if (!m_Descriptor || !FindProperty(*m_Descriptor, property.StableFieldId))
            throw std::logic_error("Managed data property editing requires a current type descriptor.");
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
            field->Name = property.Name;
            field->ManagedTypeName = property.ManagedTypeName;
            field->Value = encoded;
        }
        RebuildDependencies(candidate);
        candidate = Keire::ManagedDataAsset::Canonicalize(std::move(candidate));
        return m_Host.Edit(undoName, std::move(candidate));
    }

    bool ManagedDataDocument::ClearProperty(const Keire::ManagedAssetPropertyDescriptor& property,
                                            const std::string_view undoName)
    {
        auto candidate = m_Host.Draft();
        const auto removed = std::erase_if(candidate.Fields, [&property](const Keire::ManagedDataFieldState& field)
                                           { return field.StableFieldId == property.StableFieldId; });
        if (removed == 0)
            return false;
        RebuildDependencies(candidate);
        candidate = Keire::ManagedDataAsset::Canonicalize(std::move(candidate));
        return m_Host.Edit(undoName, std::move(candidate));
    }

    AssetDocumentReloadResult ManagedDataDocument::Reload(Keire::ManagedDataDefinition definition,
                                                          const std::uint64_t revision)
    {
        if (definition == m_Host.Draft())
        {
            m_Host.AcknowledgeRevision(revision);
            return AssetDocumentReloadResult::Unchanged;
        }
        m_SuppressPreview = true;
        try
        {
            const auto result = m_Host.Reload(std::move(definition), revision);
            m_SuppressPreview = false;
            return result;
        }
        catch (...)
        {
            m_SuppressPreview = false;
            throw;
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
            managedDefinition->ManagedType != *property.ExpectedManagedType && !property.IncludeDerivedAssetTypes)
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
                CollectDependencies(Keire::DecodeManagedAssetValue(field.Value, *property), *property, dependencies);
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
            definition.Dependencies.push_back(std::move(dependency));
        }
    }
} // namespace KeireEditor

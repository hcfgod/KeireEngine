#include "Keire/Scripting/ManagedDataAsset.h"

#include <algorithm>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <tuple>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t MaximumObjects = 65'536;
        constexpr std::size_t MaximumEdges = 131'072;
        constexpr std::size_t MaximumCollectionEntries = 16'384;
        constexpr std::size_t MaximumDepth = 32;

        [[nodiscard]] const Json* Member(const Json& value, const std::string_view primary,
                                         const std::string_view fallback)
        {
            if (const auto found = value.find(std::string(primary)); found != value.end())
                return std::addressof(*found);
            if (const auto found = value.find(std::string(fallback)); found != value.end())
                return std::addressof(*found);
            return nullptr;
        }

        [[nodiscard]] ManagedReferenceGraphValue DecodeValue(const Json& source)
        {
            if (!source.is_object())
                throw std::invalid_argument("Managed reference graph values must be objects.");
            ManagedReferenceGraphValue result;
            if (const auto* reference = Member(source, "Reference", "reference"))
            {
                if (!reference->is_number_unsigned() && !reference->is_number_integer())
                    throw std::invalid_argument("Managed reference graph object references must be integers.");
                const auto value = reference->get<std::int64_t>();
                if (value < 0 || value > std::numeric_limits<std::uint32_t>::max())
                    throw std::invalid_argument("Managed reference graph object references are out of range.");
                result.Reference = static_cast<std::uint32_t>(value);
            }
            if (const auto* scalar = Member(source, "Scalar", "scalar"))
                result.Scalar = scalar->dump();
            if (result.Reference != 0 && result.Scalar != "null")
                throw std::invalid_argument("Managed reference graph links cannot also contain scalar data.");
            return result;
        }

        [[nodiscard]] Json EncodeValue(const ManagedReferenceGraphValue& value)
        {
            Json scalar;
            try
            {
                scalar = Json::parse(value.Scalar);
            }
            catch (const Json::exception& exception)
            {
                throw std::invalid_argument(std::string("Managed reference graph scalar is malformed: ") +
                                            exception.what());
            }
            if (value.Reference != 0 && !scalar.is_null())
                throw std::invalid_argument("Managed reference graph links cannot also contain scalar data.");
            return {{"Reference", value.Reference}, {"Scalar", std::move(scalar)}};
        }

        [[nodiscard]] std::string_view KindName(const ManagedReferenceGraphNodeKind kind) noexcept
        {
            switch (kind)
            {
            case ManagedReferenceGraphNodeKind::Object:
                return "object";
            case ManagedReferenceGraphNodeKind::Array:
                return "array";
            case ManagedReferenceGraphNodeKind::List:
                return "list";
            case ManagedReferenceGraphNodeKind::Dictionary:
                return "dictionary";
            }
            return {};
        }

        [[nodiscard]] ManagedReferenceGraphNodeKind ParseKind(const std::string_view value)
        {
            if (value == "object")
                return ManagedReferenceGraphNodeKind::Object;
            if (value == "array")
                return ManagedReferenceGraphNodeKind::Array;
            if (value == "list")
                return ManagedReferenceGraphNodeKind::List;
            if (value == "dictionary")
                return ManagedReferenceGraphNodeKind::Dictionary;
            throw std::invalid_argument("Managed reference graph contains an unsupported object kind.");
        }

        using GraphNodeMap = std::map<std::uint32_t, const ManagedReferenceGraphNode*>;

        [[nodiscard]] GraphNodeMap IndexNodes(const ManagedReferenceGraph& graph)
        {
            if (graph.Objects.size() > MaximumObjects)
                throw std::invalid_argument("Managed reference graph exceeds the 65,536-object limit.");
            GraphNodeMap nodes;
            for (const auto& node : graph.Objects)
            {
                if (node.Id == 0 || !nodes.emplace(node.Id, std::addressof(node)).second)
                    throw std::invalid_argument("Managed reference graph object IDs must be unique and positive.");
                if (node.Items.size() > MaximumCollectionEntries || node.Entries.size() > MaximumCollectionEntries)
                    throw std::invalid_argument("Managed reference graph contains an oversized collection.");
            }
            return nodes;
        }

        void MarkReachable(const ManagedReferenceGraphValue& value, const GraphNodeMap& nodes,
                           std::set<std::uint32_t>& reached, std::size_t& edges, const std::size_t depth)
        {
            if (depth > MaximumDepth)
                throw std::invalid_argument("Managed reference graph exceeds the depth limit.");
            if (value.Reference == 0)
                return;
            if (++edges > MaximumEdges)
                throw std::invalid_argument("Managed reference graph exceeds the 131,072-edge limit.");
            const auto found = nodes.find(value.Reference);
            if (found == nodes.end())
                throw std::invalid_argument("Managed reference graph links to a missing object.");
            if (!reached.emplace(value.Reference).second)
                return;
            const auto& node = *found->second;
            for (const auto& field : node.Fields)
                MarkReachable(field.Value, nodes, reached, edges, depth + 1);
            for (const auto& item : node.Items)
                MarkReachable(item, nodes, reached, edges, depth + 1);
            for (const auto& entry : node.Entries)
            {
                MarkReachable(entry.Key, nodes, reached, edges, depth + 1);
                MarkReachable(entry.Value, nodes, reached, edges, depth + 1);
            }
        }

        [[nodiscard]] std::set<std::uint32_t> ReachableFrom(const ManagedReferenceGraphValue& root,
                                                            const GraphNodeMap& nodes)
        {
            std::set<std::uint32_t> reached;
            std::size_t edges = 0;
            MarkReachable(root, nodes, reached, edges, 0);
            return reached;
        }

        void RemapValue(ManagedReferenceGraphValue& value, const std::map<std::uint32_t, std::uint32_t>& remap)
        {
            if (const auto found = remap.find(value.Reference); found != remap.end())
                value.Reference = found->second;
        }

        void RemapNode(ManagedReferenceGraphNode& node, const std::map<std::uint32_t, std::uint32_t>& remap)
        {
            if (const auto found = remap.find(node.Id); found != remap.end())
                node.Id = found->second;
            for (auto& field : node.Fields)
                RemapValue(field.Value, remap);
            for (auto& item : node.Items)
                RemapValue(item, remap);
            for (auto& entry : node.Entries)
            {
                RemapValue(entry.Key, remap);
                RemapValue(entry.Value, remap);
            }
        }

        void PruneSharedGraph(ManagedReferenceGraph& graph)
        {
            const auto nodes = IndexNodes(graph);
            std::set<std::uint32_t> reached;
            std::size_t edges = 0;
            for (const auto& root : graph.Roots)
                MarkReachable(root.Value, nodes, reached, edges, 0);
            std::erase_if(graph.Objects,
                          [&reached](const ManagedReferenceGraphNode& node) { return !reached.contains(node.Id); });
            std::ranges::sort(graph.Objects, {}, &ManagedReferenceGraphNode::Id);
            std::ranges::sort(graph.Roots, {}, &ManagedReferenceGraphRoot::Key);
        }

        [[nodiscard]] ManagedSerializationError
        GraphError(std::string message, const ManagedAssetPropertyDescriptor& root,
                   const ManagedAssetPropertyDescriptor& property, std::string path, std::string serializedTypeId = {},
                   std::optional<std::uint32_t> objectId = std::nullopt, std::string runtimeType = {})
        {
            return ManagedSerializationError(std::move(message),
                                             {.Code = "KEIRE-MANAGED-SERIALIZATION-0003",
                                              .Phase = "validate",
                                              .Owner = root.ManagedTypeName,
                                              .RootField = root.Name.empty() ? std::string("Root") : root.Name,
                                              .FieldPath = std::move(path),
                                              .DeclaredType = property.ManagedTypeName,
                                              .RuntimeType = std::move(runtimeType),
                                              .SerializedTypeId = std::move(serializedTypeId),
                                              .ObjectId = objectId});
        }

        class GraphValidator final
        {
          public:
            GraphValidator(const ManagedReferenceGraph& graph, const ManagedAssetPropertyDescriptor& root,
                           const std::span<const ManagedAssetReferenceTypeDescriptor> types)
                : m_Graph(graph), m_Root(root), m_Types(types)
            {
                if (graph.Version != 1)
                    throw std::invalid_argument("Managed reference graph uses an unsupported version.");
                if (graph.Objects.size() > MaximumObjects)
                    throw std::invalid_argument("Managed reference graph exceeds the 65,536-object limit.");
                for (const auto& node : graph.Objects)
                {
                    if (node.Id == 0 || !m_Nodes.emplace(node.Id, std::addressof(node)).second)
                        throw std::invalid_argument("Managed reference graph object IDs must be unique and positive.");
                }
                for (const auto& type : types)
                {
                    if (!type.StableTypeId ||
                        !m_RegisteredTypes.emplace(type.StableTypeId, std::addressof(type)).second)
                        throw std::invalid_argument("Managed reference graph type registry contains duplicate IDs.");
                }
            }

            void Validate()
            {
                ValidateValue(m_Graph.Root, m_Root, m_Root.Name.empty() ? std::string("Root") : m_Root.Name, 0);
                if (m_Reached.size() != m_Nodes.size())
                    throw std::invalid_argument("Managed reference graph contains unreachable objects.");
            }

          private:
            void CountEdge()
            {
                if (++m_Edges > MaximumEdges)
                    throw std::invalid_argument("Managed reference graph exceeds the 131,072-edge limit.");
            }

            void ValidateValue(const ManagedReferenceGraphValue& value, const ManagedAssetPropertyDescriptor& property,
                               const std::string& path, const std::size_t depth)
            {
                if (depth > MaximumDepth)
                    throw std::invalid_argument("Managed reference graph exceeds the depth limit at '" + path + "'.");
                if (value.Reference == 0)
                {
                    if (property.ReferenceGraph && !property.ReferenceTypeChoices.empty() && value.Scalar != "null")
                        throw std::invalid_argument("Managed reference field '" + path +
                                                    "' requires a graph link or null.");
                    if (!property.ReferenceGraph || property.ReferenceTypeChoices.empty())
                        (void)DecodeManagedAssetValue(value.Scalar, property);
                    return;
                }

                CountEdge();
                const auto found = m_Nodes.find(value.Reference);
                if (found == m_Nodes.end())
                    throw GraphError("Managed reference field '" + path + "' links to a missing object.", m_Root,
                                     property, path, {}, value.Reference);
                const auto& node = *found->second;
                ValidateSlot(node, property, path);
                m_Reached.emplace(node.Id);
                if (!m_Expanded.emplace(node.Id, property.StableFieldId).second)
                    return;
                ValidateNode(node, property, path, depth + 1);
            }

            void ValidateSlot(const ManagedReferenceGraphNode& node, const ManagedAssetPropertyDescriptor& property,
                              const std::string& path) const
            {
                const auto wrongKind = [&path]
                {
                    throw std::invalid_argument("Managed reference field '" + path +
                                                "' links to an incompatible node.");
                };
                if (node.Kind == ManagedReferenceGraphNodeKind::Object)
                {
                    if (!property.ReferenceGraph ||
                        std::ranges::find(property.ReferenceTypeChoices, node.RuntimeType) ==
                            property.ReferenceTypeChoices.end())
                        wrongKind();
                    return;
                }
                if (node.Kind == ManagedReferenceGraphNodeKind::Array &&
                    property.Kind != ManagedAssetPropertyKind::Array)
                    wrongKind();
                if (node.Kind == ManagedReferenceGraphNodeKind::List && property.Kind != ManagedAssetPropertyKind::List)
                    wrongKind();
                if (node.Kind == ManagedReferenceGraphNodeKind::Dictionary &&
                    property.Kind != ManagedAssetPropertyKind::Dictionary)
                    wrongKind();
            }

            void ValidateNode(const ManagedReferenceGraphNode& node, const ManagedAssetPropertyDescriptor& property,
                              const std::string& path, const std::size_t depth)
            {
                if (node.Kind == ManagedReferenceGraphNodeKind::Object)
                {
                    const auto type = m_RegisteredTypes.find(node.RuntimeType);
                    if (type == m_RegisteredTypes.end())
                        throw GraphError(
                            "KEIRE-MANAGED-SERIALIZATION-0003: Managed reference graph field '" + path +
                                "' uses an unregistered stable type ID '" + node.RuntimeType.ToString() + "'.",
                            m_Root, property, path, node.RuntimeType.ToString(), node.Id, node.RuntimeType.ToString());
                    std::set<AssetId> fields;
                    for (const auto& field : node.Fields)
                    {
                        if (!field.StableFieldId || !fields.emplace(field.StableFieldId).second)
                            throw std::invalid_argument(
                                "KEIRE-MANAGED-SERIALIZATION-0003: Managed reference graph field '" + path +
                                "' contains duplicate stable field ID '" + field.StableFieldId.ToString() + "'.");
                        const auto descriptor = std::ranges::find(type->second->Properties, field.StableFieldId,
                                                                  &ManagedAssetPropertyDescriptor::StableFieldId);
                        if (descriptor == type->second->Properties.end())
                        {
                            ReachUnknown(field.Value, depth);
                            continue;
                        }
                        ValidateValue(field.Value, *descriptor, path + "." + descriptor->Name, depth);
                    }
                    return;
                }
                if (node.Kind == ManagedReferenceGraphNodeKind::Array ||
                    node.Kind == ManagedReferenceGraphNodeKind::List)
                {
                    if (node.Items.size() > MaximumCollectionEntries || property.Children.size() != 1)
                        throw std::invalid_argument("Managed reference collection '" + path + "' is malformed.");
                    for (std::size_t index = 0; index < node.Items.size(); ++index)
                        ValidateValue(node.Items[index], property.Children.front(),
                                      path + "[" + std::to_string(index) + "]", depth);
                    return;
                }
                if (node.Entries.size() > MaximumCollectionEntries || property.Children.size() != 2)
                    throw std::invalid_argument("Managed reference dictionary '" + path + "' is malformed.");
                std::set<std::string, std::less<>> keys;
                for (std::size_t index = 0; index < node.Entries.size(); ++index)
                {
                    const auto& entry = node.Entries[index];
                    if (entry.Key.Reference != 0)
                        throw std::invalid_argument("Managed reference dictionary keys cannot be graph links.");
                    const auto canonical = EncodeValue(entry.Key).at("Scalar").dump();
                    if (!keys.emplace(canonical).second)
                        throw std::invalid_argument(
                            "KEIRE-MANAGED-SERIALIZATION-0003: Managed reference dictionary field '" + path +
                            "' declared as '" + property.ManagedTypeName + "' contains duplicate key " + canonical +
                            ".");
                    ValidateValue(entry.Key, property.Children[0], path + "[" + std::to_string(index) + "].Key", depth);
                    ValidateValue(entry.Value, property.Children[1], path + "[" + std::to_string(index) + "]", depth);
                }
            }

            void ReachUnknown(const ManagedReferenceGraphValue& value, const std::size_t depth)
            {
                if (value.Reference == 0)
                    return;
                CountEdge();
                const auto found = m_Nodes.find(value.Reference);
                if (found == m_Nodes.end())
                    throw std::invalid_argument("Managed reference graph contains an unknown-field dangling link.");
                if (!m_Reached.emplace(value.Reference).second || depth > MaximumDepth)
                    return;
                const auto& node = *found->second;
                for (const auto& field : node.Fields)
                    ReachUnknown(field.Value, depth + 1);
                for (const auto& item : node.Items)
                    ReachUnknown(item, depth + 1);
                for (const auto& entry : node.Entries)
                {
                    ReachUnknown(entry.Key, depth + 1);
                    ReachUnknown(entry.Value, depth + 1);
                }
            }

            const ManagedReferenceGraph& m_Graph;
            const ManagedAssetPropertyDescriptor& m_Root;
            std::span<const ManagedAssetReferenceTypeDescriptor> m_Types;
            std::map<std::uint32_t, const ManagedReferenceGraphNode*> m_Nodes;
            std::map<ManagedTypeId, const ManagedAssetReferenceTypeDescriptor*> m_RegisteredTypes;
            std::set<std::uint32_t> m_Reached;
            std::set<std::pair<std::uint32_t, AssetId>> m_Expanded;
            std::size_t m_Edges = 0;
        };
    } // namespace

    ManagedSerializationError::ManagedSerializationError(std::string message, ManagedSerializationDiagnostic diagnostic)
        : std::invalid_argument(std::move(message)), m_Diagnostic(std::move(diagnostic))
    {
    }

    ManagedReferenceGraph DecodeManagedReferenceGraph(const std::string_view value)
    {
        try
        {
            const auto document = Json::parse(value.begin(), value.end());
            if (!document.is_object())
                throw std::invalid_argument("Managed reference graph root must be an object.");
            ManagedReferenceGraph result;
            const auto* version = Member(document, "Version", "version");
            const auto* root = Member(document, "Root", "root");
            const auto* roots = Member(document, "Roots", "roots");
            const auto* objects = Member(document, "Objects", "objects");
            if (!version || (!version->is_number_unsigned() && !version->is_number_integer()) || !objects ||
                !objects->is_array())
                throw std::invalid_argument("Managed reference graph document is malformed.");
            const auto parsedVersion = version->get<std::int64_t>();
            if (parsedVersion < 0 || parsedVersion > std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("Managed reference graph version is out of range.");
            result.Version = static_cast<std::uint32_t>(parsedVersion);
            if (result.Version == 1)
            {
                if (!root)
                    throw std::invalid_argument("Managed reference graph root is missing.");
                result.Root = DecodeValue(*root);
            }
            else if (result.Version == 2)
            {
                if (!roots || !roots->is_array())
                    throw std::invalid_argument("Managed shared reference graph roots are malformed.");
                result.Roots.reserve(roots->size());
                for (const auto& encoded : *roots)
                {
                    const auto* key = Member(encoded, "Key", "key");
                    const auto* rootValue = Member(encoded, "Value", "value");
                    if (!key || !key->is_string() || !rootValue)
                        throw std::invalid_argument("Managed shared reference graph root record is malformed.");
                    result.Roots.push_back({key->get<std::string>(), DecodeValue(*rootValue)});
                }
            }
            else
            {
                throw std::invalid_argument("Managed reference graph uses an unsupported version.");
            }
            result.Objects.reserve(objects->size());
            for (const auto& encoded : *objects)
            {
                if (!encoded.is_object())
                    throw std::invalid_argument("Managed reference graph objects must be records.");
                const auto* id = Member(encoded, "Id", "id");
                const auto* kind = Member(encoded, "Kind", "kind");
                if (!id || (!id->is_number_unsigned() && !id->is_number_integer()) || !kind || !kind->is_string())
                    throw std::invalid_argument("Managed reference graph object record is malformed.");
                const auto parsedId = id->get<std::int64_t>();
                if (parsedId < 0 || parsedId > std::numeric_limits<std::uint32_t>::max())
                    throw std::invalid_argument("Managed reference graph object ID is out of range.");
                ManagedReferenceGraphNode node;
                node.Id = static_cast<std::uint32_t>(parsedId);
                node.Kind = ParseKind(kind->get<std::string>());
                if (const auto* type = Member(encoded, "StableTypeId", "stableTypeId");
                    type && type->is_string() && !type->get_ref<const std::string&>().empty())
                {
                    const auto stableTypeId = type->get<std::string>();
                    try
                    {
                        node.RuntimeType = ManagedTypeId::Parse(stableTypeId);
                    }
                    catch (const std::exception&)
                    {
                        throw ManagedSerializationError(
                            "KEIRE-MANAGED-SERIALIZATION-0003: Managed reference graph object " +
                                std::to_string(node.Id) + " contains a malformed stable serialized type ID.",
                            {.Code = "KEIRE-MANAGED-SERIALIZATION-0003",
                             .Phase = "validate",
                             .RootField = "Root",
                             .FieldPath = "Objects[" + std::to_string(node.Id) + "].StableTypeId",
                             .SerializedTypeId = stableTypeId,
                             .ObjectId = node.Id});
                    }
                }
                if (const auto* fields = Member(encoded, "Fields", "fields"); fields && fields->is_array())
                {
                    node.Fields.reserve(fields->size());
                    for (const auto& field : *fields)
                    {
                        const auto* stableId = Member(field, "StableId", "stableId");
                        const auto* name = Member(field, "Name", "name");
                        const auto* fieldValue = Member(field, "Value", "value");
                        if (!stableId || !stableId->is_string() || !name || !name->is_string() || !fieldValue)
                            throw std::invalid_argument("Managed reference graph field record is malformed.");
                        node.Fields.push_back({.StableFieldId = AssetId::Parse(stableId->get<std::string>()),
                                               .Name = name->get<std::string>(),
                                               .Value = DecodeValue(*fieldValue)});
                    }
                }
                if (const auto* items = Member(encoded, "Items", "items"); items && items->is_array())
                    for (const auto& item : *items)
                        node.Items.push_back(DecodeValue(item));
                if (const auto* entries = Member(encoded, "Entries", "entries"); entries && entries->is_array())
                    for (const auto& entry : *entries)
                    {
                        const auto* key = Member(entry, "Key", "key");
                        const auto* entryValue = Member(entry, "Value", "value");
                        if (!key || !entryValue)
                            throw std::invalid_argument("Managed reference graph dictionary entry is malformed.");
                        node.Entries.push_back({DecodeValue(*key), DecodeValue(*entryValue)});
                    }
                result.Objects.push_back(std::move(node));
            }
            return result;
        }
        catch (const Json::exception& exception)
        {
            throw std::invalid_argument(std::string("Managed reference graph is malformed: ") + exception.what());
        }
    }

    std::string EncodeManagedReferenceGraph(const ManagedReferenceGraph& value)
    {
        Json objects = Json::array();
        for (const auto& node : value.Objects)
        {
            Json fields = Json::array();
            for (const auto& field : node.Fields)
                fields.push_back({{"StableId", field.StableFieldId.ToString()},
                                  {"Name", field.Name},
                                  {"Value", EncodeValue(field.Value)}});
            Json items = Json::array();
            for (const auto& item : node.Items)
                items.push_back(EncodeValue(item));
            Json entries = Json::array();
            for (const auto& entry : node.Entries)
                entries.push_back({{"Key", EncodeValue(entry.Key)}, {"Value", EncodeValue(entry.Value)}});
            objects.push_back({{"Id", node.Id},
                               {"Kind", KindName(node.Kind)},
                               {"StableTypeId", node.RuntimeType ? node.RuntimeType.ToString() : std::string{}},
                               {"Fields", std::move(fields)},
                               {"Items", std::move(items)},
                               {"Entries", std::move(entries)}});
        }
        Json document{{"Version", value.Version}, {"Objects", std::move(objects)}};
        if (value.Version == 1)
        {
            document["Root"] = EncodeValue(value.Root);
        }
        else if (value.Version == 2)
        {
            Json roots = Json::array();
            for (const auto& root : value.Roots)
                roots.push_back({{"Key", root.Key}, {"Value", EncodeValue(root.Value)}});
            document["Roots"] = std::move(roots);
        }
        else
        {
            throw std::invalid_argument("Managed reference graph uses an unsupported version.");
        }
        return document.dump();
    }

    void ValidateManagedReferenceGraphDocument(const ManagedReferenceGraph& value)
    {
        if (value.Version != 1 && value.Version != 2)
            throw std::invalid_argument("Managed reference graph uses an unsupported version.");
        const auto nodes = IndexNodes(value);
        std::set<std::uint32_t> reached;
        std::size_t edges = 0;
        if (value.Version == 1)
        {
            if (!value.Roots.empty())
                throw std::invalid_argument("Managed reference graph v1 cannot contain a root map.");
            MarkReachable(value.Root, nodes, reached, edges, 0);
        }
        else
        {
            if (value.Roots.size() > 1'024)
                throw std::invalid_argument("Managed shared reference graphs cannot exceed 1,024 roots.");
            std::set<std::string, std::less<>> keys;
            for (const auto& root : value.Roots)
            {
                if (root.Key.empty() || !keys.emplace(root.Key).second)
                    throw std::invalid_argument("Managed shared reference graph roots require unique non-empty keys.");
                MarkReachable(root.Value, nodes, reached, edges, 0);
            }
        }
        if (reached.size() != nodes.size())
            throw std::invalid_argument("Managed reference graph contains unreachable objects.");
    }

    ManagedReferenceGraph ExtractManagedReferenceGraphRoot(const ManagedReferenceGraph& value,
                                                           const std::string_view rootKey)
    {
        ValidateManagedReferenceGraphDocument(value);
        if (value.Version != 2)
            throw std::invalid_argument("Managed reference graph root extraction requires a shared graph.");
        const auto root = std::ranges::find(value.Roots, rootKey, &ManagedReferenceGraphRoot::Key);
        if (root == value.Roots.end())
            throw std::invalid_argument("Managed shared reference graph root does not exist.");
        const auto nodes = IndexNodes(value);
        const auto reached = ReachableFrom(root->Value, nodes);
        ManagedReferenceGraph result;
        result.Root = root->Value;
        result.Objects.reserve(reached.size());
        for (const auto& node : value.Objects)
            if (reached.contains(node.Id))
                result.Objects.push_back(node);
        std::ranges::sort(result.Objects, {}, &ManagedReferenceGraphNode::Id);
        return result;
    }

    void UpdateManagedReferenceGraphRoot(ManagedReferenceGraph& destination, const std::string_view rootKey,
                                         const ManagedReferenceGraph& value)
    {
        if (rootKey.empty())
            throw std::invalid_argument("Managed shared reference graph root keys cannot be empty.");
        ValidateManagedReferenceGraphDocument(destination);
        ValidateManagedReferenceGraphDocument(value);
        if (destination.Version != 2 || value.Version != 1)
            throw std::invalid_argument("Managed graph root updates require shared-v2 and standalone-v1 graphs.");

        const auto destinationNodes = IndexNodes(destination);
        const auto existingRoot = std::ranges::find(destination.Roots, rootKey, &ManagedReferenceGraphRoot::Key);
        const auto oldReach = existingRoot == destination.Roots.end()
                                  ? std::set<std::uint32_t>{}
                                  : ReachableFrom(existingRoot->Value, destinationNodes);
        std::set<std::uint32_t> otherReach;
        std::size_t otherEdges = 0;
        for (const auto& root : destination.Roots)
            if (root.Key != rootKey)
                MarkReachable(root.Value, destinationNodes, otherReach, otherEdges, 0);

        std::map<std::uint32_t, ManagedReferenceGraphNode> merged;
        for (const auto& node : destination.Objects)
            merged.emplace(node.Id, node);
        std::uint32_t nextId = merged.empty() ? 1 : merged.rbegin()->first;
        if (!merged.empty())
        {
            if (nextId == std::numeric_limits<std::uint32_t>::max())
                throw std::overflow_error("Managed reference graph object IDs are exhausted.");
            ++nextId;
        }

        std::map<std::uint32_t, std::uint32_t> remap;
        std::set<std::uint32_t> reservedIds;
        for (const auto& [id, node] : merged)
        {
            (void)node;
            reservedIds.emplace(id);
        }
        for (const auto& node : value.Objects)
            reservedIds.emplace(node.Id);
        for (const auto& node : value.Objects)
        {
            if (!merged.contains(node.Id) || oldReach.contains(node.Id))
            {
                remap.emplace(node.Id, node.Id);
                reservedIds.emplace(node.Id);
                continue;
            }
            while (reservedIds.contains(nextId))
            {
                if (nextId == std::numeric_limits<std::uint32_t>::max())
                    throw std::overflow_error("Managed reference graph object IDs are exhausted.");
                ++nextId;
            }
            remap.emplace(node.Id, nextId);
            reservedIds.emplace(nextId);
            if (nextId != std::numeric_limits<std::uint32_t>::max())
                ++nextId;
        }

        for (const auto id : oldReach)
            if (!otherReach.contains(id))
                merged.erase(id);
        for (auto node : value.Objects)
        {
            RemapNode(node, remap);
            merged.insert_or_assign(node.Id, std::move(node));
        }
        auto rootValue = value.Root;
        RemapValue(rootValue, remap);
        if (existingRoot == destination.Roots.end())
            destination.Roots.push_back({std::string(rootKey), rootValue});
        else
            existingRoot->Value = rootValue;

        destination.Objects.clear();
        destination.Objects.reserve(merged.size());
        for (auto& [id, node] : merged)
            destination.Objects.push_back(std::move(node));
        PruneSharedGraph(destination);
        ValidateManagedReferenceGraphDocument(destination);
    }

    void RemoveManagedReferenceGraphRoot(ManagedReferenceGraph& destination, const std::string_view rootKey)
    {
        ValidateManagedReferenceGraphDocument(destination);
        if (destination.Version != 2)
            throw std::invalid_argument("Managed graph root removal requires a shared-v2 graph.");
        std::erase_if(destination.Roots,
                      [rootKey](const ManagedReferenceGraphRoot& root) { return root.Key == rootKey; });
        PruneSharedGraph(destination);
        ValidateManagedReferenceGraphDocument(destination);
    }

    void ValidateManagedReferenceGraph(const ManagedReferenceGraph& value,
                                       const ManagedAssetPropertyDescriptor& property,
                                       const std::span<const ManagedAssetReferenceTypeDescriptor> types)
    {
        GraphValidator(value, property, types).Validate();
    }
} // namespace Keire

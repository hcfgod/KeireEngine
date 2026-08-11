#include "Keire/Rendering/MaterialEcosystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <concepts>
#include <cstring>
#include <mutex>
#include <ranges>
#include <set>
#include <shared_mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t MaximumGraphFunctionBytes = std::size_t{16} * 1024U * 1024U;
        constexpr std::size_t MaximumCollectionBytes = std::size_t{1024} * 1024U;
        constexpr std::size_t MaximumCollectionParameters = 256;
        constexpr std::size_t MaximumText = 512;

        [[nodiscard]] std::string Text(const std::span<const std::byte> bytes)
        {
            return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        }

        [[nodiscard]] std::vector<std::byte> TextBytes(const std::string_view text)
        {
            const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
            return {bytes.begin(), bytes.end()};
        }

        [[nodiscard]] bool ValidIdentifier(const std::string_view value)
        {
            if (value.empty() || value.size() > 128U ||
                !(std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_'))
                return false;
            return std::ranges::all_of(value.substr(1), [](const unsigned char character)
                                       { return std::isalnum(character) || character == '_'; });
        }

        [[nodiscard]] bool PropertyValueMatches(const MaterialPropertyValue& value, const ShaderPropertyType type)
        {
            return type != ShaderPropertyType::Texture2D && value.index() == static_cast<std::size_t>(type);
        }

        void ValidateFinite(const MaterialPropertyValue& value)
        {
            const auto finite = [](const float candidate)
            {
                if (!std::isfinite(candidate))
                    throw std::invalid_argument("Material parameter collection values must be finite.");
            };
            std::visit(
                [&](const auto& typed)
                {
                    using T = std::decay_t<decltype(typed)>;
                    if constexpr (std::same_as<T, float>)
                        finite(typed);
                    else if constexpr (std::same_as<T, Vector2>)
                    {
                        finite(typed.X);
                        finite(typed.Y);
                    }
                    else if constexpr (std::same_as<T, Vector3>)
                    {
                        finite(typed.X);
                        finite(typed.Y);
                        finite(typed.Z);
                    }
                    else if constexpr (std::same_as<T, Vector4>)
                    {
                        finite(typed.X);
                        finite(typed.Y);
                        finite(typed.Z);
                        finite(typed.W);
                    }
                    else if constexpr (std::same_as<T, Color>)
                    {
                        finite(typed.Red);
                        finite(typed.Green);
                        finite(typed.Blue);
                        finite(typed.Alpha);
                    }
                },
                value);
        }

        [[nodiscard]] Json EncodeMaterialValue(const MaterialPropertyValue& value)
        {
            return std::visit(
                [](const auto& typed) -> Json
                {
                    using T = std::decay_t<decltype(typed)>;
                    if constexpr (std::same_as<T, float>)
                        return typed;
                    else if constexpr (std::same_as<T, Vector2>)
                        return Json::array({typed.X, typed.Y});
                    else if constexpr (std::same_as<T, Vector3>)
                        return Json::array({typed.X, typed.Y, typed.Z});
                    else if constexpr (std::same_as<T, Vector4>)
                        return Json::array({typed.X, typed.Y, typed.Z, typed.W});
                    else if constexpr (std::same_as<T, Color>)
                        return Json::array({typed.Red, typed.Green, typed.Blue, typed.Alpha});
                    else if constexpr (std::same_as<T, AssetId>)
                        return typed ? Json(typed.ToString()) : Json(nullptr);
                },
                value);
        }

        [[nodiscard]] MaterialPropertyValue DecodeMaterialValue(const Json& value, const ShaderPropertyType type)
        {
            if (type == ShaderPropertyType::Texture2D)
                return value.is_null() ? AssetId{} : AssetId::Parse(value.get<std::string>());
            if (type == ShaderPropertyType::Scalar)
                return value.get<float>();
            const std::size_t count = type == ShaderPropertyType::Vector2   ? 2U
                                      : type == ShaderPropertyType::Vector3 ? 3U
                                                                            : 4U;
            if (!value.is_array() || value.size() != count)
                throw std::invalid_argument("Material collection vector has the wrong component count.");
            std::array<float, 4> components{};
            for (std::size_t index = 0; index < count; ++index)
                components[index] = value[index].get<float>();
            if (type == ShaderPropertyType::Vector2)
                return Vector2{components[0], components[1]};
            if (type == ShaderPropertyType::Vector3)
                return Vector3{components[0], components[1], components[2]};
            if (type == ShaderPropertyType::Color)
                return Color{components[0], components[1], components[2], components[3]};
            return Vector4{components[0], components[1], components[2], components[3]};
        }

        [[nodiscard]] Json EncodeGraphFunctionJson(const GraphFunctionDefinition& definition,
                                                   const ShaderGraphPurpose expected)
        {
            ValidateGraphFunction(definition, expected);
            const auto bodyBytes = ShaderGraphAsset::EncodeSource(definition.Body);
            return {{"schemaVersion", GraphFunctionSourceSchemaVersion},
                    {"description", definition.Description},
                    {"category", definition.Category},
                    {"sortPriority", definition.SortPriority},
                    {"exposeToLibrary", definition.ExposeToLibrary},
                    {"body", Json::parse(Text(bodyBytes))}};
        }

        [[nodiscard]] GraphFunctionDefinition DecodeGraphFunctionJson(const Json& source,
                                                                      const ShaderGraphPurpose expected)
        {
            if (!source.is_object() || source.value("schemaVersion", 0U) != GraphFunctionSourceSchemaVersion)
                throw std::invalid_argument("Reusable graph asset has an unsupported schema.");
            GraphFunctionDefinition result;
            result.Description = source.value("description", std::string{});
            result.Category = source.value("category", std::string("Project"));
            result.SortPriority = source.value("sortPriority", 0);
            result.ExposeToLibrary = source.value("exposeToLibrary", true);
            const auto encodedBody = source.at("body").dump();
            result.Body = ShaderGraphAsset::DecodeSource(TextBytes(encodedBody));
            ValidateGraphFunction(result, expected);
            return result;
        }

        [[nodiscard]] Json EncodeCollectionJson(const MaterialParameterCollectionDefinition& definition)
        {
            ValidateMaterialParameterCollection(definition);
            Json parameters = Json::array();
            for (const auto& parameter : definition.Parameters)
                parameters.push_back({{"id", parameter.Id.ToString()},
                                      {"name", parameter.Name},
                                      {"displayName", parameter.DisplayName},
                                      {"description", parameter.Description},
                                      {"category", parameter.Category},
                                      {"sortPriority", parameter.SortPriority},
                                      {"type", static_cast<std::uint8_t>(parameter.Type)},
                                      {"default", EncodeMaterialValue(parameter.DefaultValue)}});
            return {{"schemaVersion", MaterialParameterCollectionSourceSchemaVersion},
                    {"parameters", std::move(parameters)}};
        }

        [[nodiscard]] MaterialParameterCollectionDefinition DecodeCollectionJson(const Json& source)
        {
            if (!source.is_object() ||
                source.value("schemaVersion", 0U) != MaterialParameterCollectionSourceSchemaVersion)
                throw std::invalid_argument("Material Parameter Collection has an unsupported schema.");
            const auto& parameters = source.at("parameters");
            if (!parameters.is_array() || parameters.size() > MaximumCollectionParameters)
                throw std::invalid_argument("Material Parameter Collection exceeds its parameter limit.");
            MaterialParameterCollectionDefinition result;
            for (const auto& encoded : parameters)
            {
                MaterialParameterCollectionParameter parameter;
                parameter.Id = AssetId::Parse(encoded.at("id").get<std::string>());
                parameter.Name = encoded.at("name").get<std::string>();
                parameter.DisplayName = encoded.value("displayName", parameter.Name);
                parameter.Description = encoded.value("description", std::string{});
                parameter.Category = encoded.value("category", std::string("Global"));
                parameter.SortPriority = encoded.value("sortPriority", 0);
                parameter.Type = static_cast<ShaderPropertyType>(encoded.at("type").get<std::uint8_t>());
                parameter.DefaultValue = DecodeMaterialValue(encoded.at("default"), parameter.Type);
                result.Parameters.push_back(std::move(parameter));
            }
            ValidateMaterialParameterCollection(result);
            return result;
        }

        template <typename AssetType>
        [[nodiscard]] AssetImporterRegistration GraphFunctionImporter(const std::string_view name,
                                                                      const std::string_view extension,
                                                                      const ShaderGraphPurpose purpose)
        {
            AssetImporterRegistration result;
            result.Name = std::string(name);
            result.Version = 1;
            result.Type = AssetType::StaticType();
            result.Extensions = {std::string(extension)};
            result.Import = [purpose](const std::span<const std::byte> bytes)
            {
                const auto definition = DecodeGraphFunctionJson(Json::parse(Text(bytes)), purpose);
                return TextBytes(EncodeGraphFunctionJson(definition, purpose).dump(2) + '\n');
            };
            result.ContextualImport = [purpose](const AssetImportContext&, const std::span<const std::byte> bytes)
            {
                const auto definition = DecodeGraphFunctionJson(Json::parse(Text(bytes)), purpose);
                AssetImportOutput output;
                output.Bytes = TextBytes(EncodeGraphFunctionJson(definition, purpose).dump(2) + '\n');
                output.AssetDependencies = ShaderGraphReferencedAssets(definition.Body);
                return output;
            };
            return result;
        }

        template <typename AssetType> [[nodiscard]] AssetDecoderRegistration GraphFunctionDecoder()
        {
            return {AssetType::StaticType(), AssetType::Error(),
                    [](const std::span<const std::byte> bytes) -> Ref<Asset> { return AssetType::Decode(bytes); }};
        }

        [[nodiscard]] std::size_t GraphFunctionResidentBytes(const GraphFunctionDefinition& definition)
        {
            std::size_t result =
                sizeof(definition) + definition.Description.capacity() + definition.Category.capacity();
            for (const auto& node : definition.Body.Nodes)
                result += sizeof(node) + node.Name.capacity() + node.TypeId.capacity() + node.Symbol.capacity() +
                          node.Function.capacity() + node.Pins.capacity() * sizeof(ShaderGraphPin);
            result += definition.Body.Connections.capacity() * sizeof(ShaderGraphConnection);
            return result;
        }

        template <typename AssetType>
        [[nodiscard]] Ref<AssetType> DecodeGraphFunctionAsset(const std::span<const std::byte> bytes,
                                                              const ShaderGraphPurpose purpose)
        {
            if (bytes.empty() || bytes.size() > MaximumGraphFunctionBytes)
                throw std::invalid_argument("Reusable graph asset is empty or exceeds its byte limit.");
            return CreateRef<AssetType>(DecodeGraphFunctionJson(Json::parse(Text(bytes)), purpose));
        }

        template <typename AssetType>
        [[nodiscard]] std::vector<std::byte> EncodeGraphFunctionAsset(const GraphFunctionDefinition& definition,
                                                                      const ShaderGraphPurpose purpose)
        {
            return TextBytes(EncodeGraphFunctionJson(definition, purpose).dump(2) + '\n');
        }
    } // namespace

    GraphFunctionDefinition CreateDefaultGraphFunction(const ShaderGraphPurpose purpose)
    {
        if (purpose == ShaderGraphPurpose::Shader)
            throw std::invalid_argument("A Shader Graph template is not a reusable graph function.");
        GraphFunctionDefinition result;
        result.Body = CreateDefaultShaderGraph();
        result.Body.Purpose = purpose;
        result.Body.SchemaVersion = ShaderGraphSourceSchemaVersion;
        result.Body.Keywords.clear();
        auto& master = result.Body.Nodes.front();
        master.Name = purpose == ShaderGraphPurpose::MaterialLayer        ? "Material Layer Output"
                      : purpose == ShaderGraphPurpose::MaterialLayerBlend ? "Material Layer Blend Output"
                      : purpose == ShaderGraphPurpose::ShaderFunction     ? "Shader Function Output"
                                                                          : "Material Function Output";
        master.Pins.clear();

        auto input = CreateShaderGraphNode(ShaderGraphNodeKind::Parameter, ShaderGraphValueType::Color);
        input.Name = "Input";
        input.Symbol = "Input";
        input.ParameterMetadata.Category = "Inputs";
        result.Body.Nodes.resize(1);

        ShaderGraphValueType outputType = ShaderGraphValueType::Color;
        ShaderGraphNode expression;
        if (purpose == ShaderGraphPurpose::MaterialLayer || purpose == ShaderGraphPurpose::MaterialLayerBlend)
        {
            outputType = ShaderGraphValueType::MaterialAttributes;
            input = CreateShaderGraphNode(ShaderGraphNodeKind::Parameter, outputType);
            input.Name = purpose == ShaderGraphPurpose::MaterialLayerBlend ? "Bottom" : "Input";
            input.Symbol = input.Name;
            input.ParameterMetadata.Category = "Inputs";
            if (purpose == ShaderGraphPurpose::MaterialLayerBlend)
            {
                auto top = CreateShaderGraphNode(ShaderGraphNodeKind::Parameter, outputType);
                top.Name = "Top";
                top.Symbol = "Top";
                top.ParameterMetadata.Category = "Inputs";
                auto alpha = CreateShaderGraphNode(ShaderGraphNodeKind::Parameter, ShaderGraphValueType::Scalar);
                alpha.Name = "Alpha";
                alpha.Symbol = "Alpha";
                alpha.Value = 0.5F;
                alpha.Pins.front().DefaultValue = 0.5F;
                alpha.ParameterMetadata.Category = "Inputs";
                expression = CreateShaderGraphNode(ShaderGraphNodeKind::BlendMaterialAttributes);
                result.Body.Nodes.push_back(input);
                result.Body.Nodes.push_back(top);
                result.Body.Nodes.push_back(alpha);
                result.Body.Nodes.push_back(expression);
                const auto outputPin = [](const ShaderGraphNode& node)
                { return *std::ranges::find(node.Pins, ShaderGraphPinDirection::Output, &ShaderGraphPin::Direction); };
                const auto inputPin = [](const ShaderGraphNode& node, const std::string_view name)
                { return *std::ranges::find(node.Pins, name, &ShaderGraphPin::Name); };
                result.Body.Connections = {
                    {AssetId::Generate(),
                     {input.Id, outputPin(input).Id},
                     {expression.Id, inputPin(expression, "A").Id}},
                    {AssetId::Generate(), {top.Id, outputPin(top).Id}, {expression.Id, inputPin(expression, "B").Id}},
                    {AssetId::Generate(),
                     {alpha.Id, outputPin(alpha).Id},
                     {expression.Id, inputPin(expression, "Alpha").Id}}};
            }
            else
                result.Body.Nodes.push_back(input);
        }
        else
            result.Body.Nodes.push_back(input);

        auto& outputNode = result.Body.Nodes.front();
        outputNode.Pins.push_back({AssetId::Generate(), "Result", outputType, ShaderGraphPinDirection::Input,
                                   outputType == ShaderGraphValueType::Color
                                       ? ShaderGraphValue(Color{0.0F, 0.0F, 0.0F, 1.0F})
                                       : ShaderGraphValue(ShaderGraphMaterialAttributesValue{})});
        const auto sourceNode = purpose == ShaderGraphPurpose::MaterialLayerBlend ? &result.Body.Nodes.back() : &input;
        const auto sourcePin =
            std::ranges::find(sourceNode->Pins, ShaderGraphPinDirection::Output, &ShaderGraphPin::Direction);
        result.Body.Connections.push_back(
            {AssetId::Generate(), {sourceNode->Id, sourcePin->Id}, {outputNode.Id, outputNode.Pins.front().Id}});
        ValidateGraphFunction(result, purpose);
        return result;
    }

    void ValidateGraphFunction(const GraphFunctionDefinition& definition, const ShaderGraphPurpose expected)
    {
        if (definition.SchemaVersion != GraphFunctionSourceSchemaVersion || expected == ShaderGraphPurpose::Shader ||
            definition.Description.size() > MaximumText * 4U || definition.Category.empty() ||
            definition.Category.size() > MaximumText || definition.Body.Purpose != expected)
            throw std::invalid_argument("Reusable graph metadata, schema, or purpose is invalid.");
        ValidateShaderGraph(definition.Body);
    }

    void ValidateMaterialParameterCollection(const MaterialParameterCollectionDefinition& definition)
    {
        if (definition.SchemaVersion != MaterialParameterCollectionSourceSchemaVersion ||
            definition.Parameters.size() > MaximumCollectionParameters)
            throw std::invalid_argument("Material Parameter Collection schema or bounds are invalid.");
        std::set<AssetId> ids;
        std::set<std::string, std::less<>> names;
        for (const auto& parameter : definition.Parameters)
        {
            if (!parameter.Id || !ids.insert(parameter.Id).second || !ValidIdentifier(parameter.Name) ||
                !names.insert(parameter.Name).second || parameter.DisplayName.size() > MaximumText ||
                parameter.Description.size() > MaximumText * 4U || parameter.Category.empty() ||
                parameter.Category.size() > MaximumText || parameter.Type > ShaderPropertyType::Color ||
                !PropertyValueMatches(parameter.DefaultValue, parameter.Type))
                throw std::invalid_argument("Material Parameter Collection contains an invalid parameter.");
            ValidateFinite(parameter.DefaultValue);
        }
    }

#define KEIRE_DEFINE_GRAPH_FUNCTION_ASSET(AssetName, Purpose)                                                          \
    AssetName::AssetName(GraphFunctionDefinition definition) : m_Definition(std::move(definition))                     \
    {                                                                                                                  \
        if (m_Definition.Body.Nodes.empty())                                                                           \
            m_Definition = CreateDefaultGraphFunction(Purpose);                                                        \
        ValidateGraphFunction(m_Definition, Purpose);                                                                  \
    }                                                                                                                  \
    std::size_t AssetName::ResidentBytes() const noexcept { return GraphFunctionResidentBytes(m_Definition); }         \
    Ref<AssetName> AssetName::Decode(const std::span<const std::byte> bytes)                                           \
    {                                                                                                                  \
        return DecodeGraphFunctionAsset<AssetName>(bytes, Purpose);                                                    \
    }                                                                                                                  \
    std::vector<std::byte> AssetName::Encode(const GraphFunctionDefinition& definition)                                \
    {                                                                                                                  \
        return EncodeGraphFunctionAsset<AssetName>(definition, Purpose);                                               \
    }                                                                                                                  \
    GraphFunctionDefinition AssetName::DecodeSource(const std::span<const std::byte> bytes)                            \
    {                                                                                                                  \
        return DecodeGraphFunctionAsset<AssetName>(bytes, Purpose)->Definition();                                      \
    }                                                                                                                  \
    std::vector<std::byte> AssetName::EncodeSource(const GraphFunctionDefinition& definition)                          \
    {                                                                                                                  \
        return Encode(definition);                                                                                     \
    }                                                                                                                  \
    Ref<AssetName> AssetName::Error() { return CreateRef<AssetName>(CreateDefaultGraphFunction(Purpose)); }

    KEIRE_DEFINE_GRAPH_FUNCTION_ASSET(MaterialFunctionAsset, ShaderGraphPurpose::MaterialFunction)
    KEIRE_DEFINE_GRAPH_FUNCTION_ASSET(ShaderFunctionAsset, ShaderGraphPurpose::ShaderFunction)
    KEIRE_DEFINE_GRAPH_FUNCTION_ASSET(MaterialLayerAsset, ShaderGraphPurpose::MaterialLayer)
    KEIRE_DEFINE_GRAPH_FUNCTION_ASSET(MaterialLayerBlendAsset, ShaderGraphPurpose::MaterialLayerBlend)

#undef KEIRE_DEFINE_GRAPH_FUNCTION_ASSET

    MaterialParameterCollectionAsset::MaterialParameterCollectionAsset(MaterialParameterCollectionDefinition definition)
        : m_Definition(std::move(definition))
    {
        ValidateMaterialParameterCollection(m_Definition);
    }

    std::size_t MaterialParameterCollectionAsset::ResidentBytes() const noexcept
    {
        std::size_t result =
            sizeof(m_Definition) + m_Definition.Parameters.capacity() * sizeof(MaterialParameterCollectionParameter);
        for (const auto& parameter : m_Definition.Parameters)
            result += parameter.Name.capacity() + parameter.DisplayName.capacity() + parameter.Description.capacity() +
                      parameter.Category.capacity();
        return result;
    }

    Ref<MaterialParameterCollectionAsset>
    MaterialParameterCollectionAsset::Decode(const std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() > MaximumCollectionBytes)
            throw std::invalid_argument("Material Parameter Collection is empty or exceeds its byte limit.");
        return CreateRef<MaterialParameterCollectionAsset>(DecodeCollectionJson(Json::parse(Text(bytes))));
    }

    std::vector<std::byte>
    MaterialParameterCollectionAsset::Encode(const MaterialParameterCollectionDefinition& definition)
    {
        return TextBytes(EncodeCollectionJson(definition).dump(2) + '\n');
    }

    MaterialParameterCollectionDefinition
    MaterialParameterCollectionAsset::DecodeSource(const std::span<const std::byte> bytes)
    {
        return Decode(bytes)->Definition();
    }

    std::vector<std::byte>
    MaterialParameterCollectionAsset::EncodeSource(const MaterialParameterCollectionDefinition& definition)
    {
        return Encode(definition);
    }

    Ref<MaterialParameterCollectionAsset> MaterialParameterCollectionAsset::Error()
    {
        return CreateRef<MaterialParameterCollectionAsset>();
    }

    class DynamicMaterialInstance::Impl final
    {
      public:
        explicit Impl(MaterialAssetDefinition definition) : Base(std::move(definition)) {}

        mutable std::shared_mutex Mutex;
        MaterialAssetDefinition Base;
        std::map<std::string, MaterialPropertyValue, std::less<>> Overrides;
        std::atomic<std::uint64_t> Revision{1};
        bool Closed = false;
    };

    DynamicMaterialInstance::DynamicMaterialInstance(MaterialAssetDefinition parent)
        : m_Impl(std::make_unique<Impl>(std::move(parent)))
    {
        (void)MaterialAsset::Encode(m_Impl->Base);
    }

    DynamicMaterialInstance::~DynamicMaterialInstance() = default;

    MaterialAssetDefinition DynamicMaterialInstance::Snapshot() const
    {
        std::shared_lock lock(m_Impl->Mutex);
        if (m_Impl->Closed)
            throw std::logic_error("Dynamic Material Instance is closed.");
        auto result = m_Impl->Base;
        for (const auto& [name, value] : m_Impl->Overrides)
            result.Properties.insert_or_assign(name, value);
        return result;
    }

    std::uint64_t DynamicMaterialInstance::Revision() const noexcept
    {
        return m_Impl->Revision.load(std::memory_order_acquire);
    }

    void DynamicMaterialInstance::SetProperty(std::string name, MaterialPropertyValue value)
    {
        std::unique_lock lock(m_Impl->Mutex);
        if (m_Impl->Closed)
            throw std::logic_error("Dynamic Material Instance is closed.");
        const auto base = m_Impl->Base.Properties.find(name);
        if (base == m_Impl->Base.Properties.end())
            throw std::invalid_argument("Dynamic Material Instance property is not declared: " + name + '.');
        if (base->second.index() != value.index())
            throw std::invalid_argument("Dynamic Material Instance property type does not match its parent.");
        ValidateFinite(value);
        if (base->second == value)
        {
            const auto existing = m_Impl->Overrides.find(name);
            if (existing == m_Impl->Overrides.end())
                return;
            m_Impl->Overrides.erase(existing);
        }
        else if (const auto existing = m_Impl->Overrides.find(name);
                 existing != m_Impl->Overrides.end() && existing->second == value)
            return;
        else
            m_Impl->Overrides.insert_or_assign(std::move(name), std::move(value));
        m_Impl->Revision.fetch_add(1, std::memory_order_release);
    }

    bool DynamicMaterialInstance::ResetProperty(const std::string_view name)
    {
        std::unique_lock lock(m_Impl->Mutex);
        if (m_Impl->Closed)
            return false;
        const auto existing = m_Impl->Overrides.find(name);
        const auto removed = existing != m_Impl->Overrides.end();
        if (removed)
            m_Impl->Overrides.erase(existing);
        if (removed)
            m_Impl->Revision.fetch_add(1, std::memory_order_release);
        return removed;
    }

    void DynamicMaterialInstance::Close() noexcept
    {
        std::unique_lock lock(m_Impl->Mutex);
        if (m_Impl->Closed)
            return;
        m_Impl->Closed = true;
        m_Impl->Overrides.clear();
        m_Impl->Revision.fetch_add(1, std::memory_order_release);
    }

    class MaterialParameterCollectionState::Impl final
    {
      public:
        explicit Impl(MaterialParameterCollectionDefinition value) : Definition(std::move(value))
        {
            for (const auto& parameter : Definition.Parameters)
                Values.emplace(parameter.Id, parameter.DefaultValue);
        }

        mutable std::shared_mutex Mutex;
        MaterialParameterCollectionDefinition Definition;
        std::map<AssetId, MaterialPropertyValue> Values;
        std::atomic<std::uint64_t> Revision{1};
        bool Closed = false;
    };

    MaterialParameterCollectionState::MaterialParameterCollectionState(MaterialParameterCollectionDefinition definition)
        : m_Impl(std::make_unique<Impl>(std::move(definition)))
    {
        ValidateMaterialParameterCollection(m_Impl->Definition);
    }

    MaterialParameterCollectionState::~MaterialParameterCollectionState() = default;

    MaterialParameterCollectionDefinition MaterialParameterCollectionState::Definition() const
    {
        std::shared_lock lock(m_Impl->Mutex);
        return m_Impl->Definition;
    }

    std::map<AssetId, MaterialPropertyValue> MaterialParameterCollectionState::Snapshot() const
    {
        std::shared_lock lock(m_Impl->Mutex);
        if (m_Impl->Closed)
            throw std::logic_error("Material Parameter Collection state is closed.");
        return m_Impl->Values;
    }

    std::uint64_t MaterialParameterCollectionState::Revision() const noexcept
    {
        return m_Impl->Revision.load(std::memory_order_acquire);
    }

    void MaterialParameterCollectionState::Set(const AssetId parameter, MaterialPropertyValue value)
    {
        std::unique_lock lock(m_Impl->Mutex);
        if (m_Impl->Closed)
            throw std::logic_error("Material Parameter Collection state is closed.");
        const auto definition =
            std::ranges::find(m_Impl->Definition.Parameters, parameter, &MaterialParameterCollectionParameter::Id);
        if (definition == m_Impl->Definition.Parameters.end())
            throw std::invalid_argument("Material Parameter Collection parameter is not declared.");
        if (!PropertyValueMatches(value, definition->Type))
            throw std::invalid_argument("Material Parameter Collection override has the wrong type.");
        ValidateFinite(value);
        const auto current = m_Impl->Values.find(parameter);
        if (current != m_Impl->Values.end() && current->second == value)
            return;
        m_Impl->Values.insert_or_assign(parameter, std::move(value));
        m_Impl->Revision.fetch_add(1, std::memory_order_release);
    }

    bool MaterialParameterCollectionState::Reset(const AssetId parameter)
    {
        std::unique_lock lock(m_Impl->Mutex);
        if (m_Impl->Closed)
            return false;
        const auto definition =
            std::ranges::find(m_Impl->Definition.Parameters, parameter, &MaterialParameterCollectionParameter::Id);
        if (definition == m_Impl->Definition.Parameters.end())
            return false;
        const auto current = m_Impl->Values.find(parameter);
        if (current != m_Impl->Values.end() && current->second == definition->DefaultValue)
            return false;
        m_Impl->Values.insert_or_assign(parameter, definition->DefaultValue);
        m_Impl->Revision.fetch_add(1, std::memory_order_release);
        return true;
    }

    void MaterialParameterCollectionState::Close() noexcept
    {
        std::unique_lock lock(m_Impl->Mutex);
        if (m_Impl->Closed)
            return;
        m_Impl->Closed = true;
        m_Impl->Values.clear();
        m_Impl->Revision.fetch_add(1, std::memory_order_release);
    }

    AssetImporterRegistration CreateMaterialFunctionAssetImporter()
    {
        return GraphFunctionImporter<MaterialFunctionAsset>("Keire.MaterialFunction", ".keirematerialfunction",
                                                            ShaderGraphPurpose::MaterialFunction);
    }

    AssetDecoderRegistration CreateMaterialFunctionAssetDecoder()
    {
        return GraphFunctionDecoder<MaterialFunctionAsset>();
    }

    AssetImporterRegistration CreateShaderFunctionAssetImporter()
    {
        return GraphFunctionImporter<ShaderFunctionAsset>("Keire.ShaderFunction", ".keireshaderfunction",
                                                          ShaderGraphPurpose::ShaderFunction);
    }

    AssetDecoderRegistration CreateShaderFunctionAssetDecoder() { return GraphFunctionDecoder<ShaderFunctionAsset>(); }

    AssetImporterRegistration CreateMaterialLayerAssetImporter()
    {
        return GraphFunctionImporter<MaterialLayerAsset>("Keire.MaterialLayer", ".keiremateriallayer",
                                                         ShaderGraphPurpose::MaterialLayer);
    }

    AssetDecoderRegistration CreateMaterialLayerAssetDecoder() { return GraphFunctionDecoder<MaterialLayerAsset>(); }

    AssetImporterRegistration CreateMaterialLayerBlendAssetImporter()
    {
        return GraphFunctionImporter<MaterialLayerBlendAsset>("Keire.MaterialLayerBlend", ".keirematerialblend",
                                                              ShaderGraphPurpose::MaterialLayerBlend);
    }

    AssetDecoderRegistration CreateMaterialLayerBlendAssetDecoder()
    {
        return GraphFunctionDecoder<MaterialLayerBlendAsset>();
    }

    AssetImporterRegistration CreateMaterialParameterCollectionAssetImporter()
    {
        return {"Keire.MaterialParameterCollection",
                1,
                MaterialParameterCollectionAsset::StaticType(),
                {".keirematerialcollection"},
                [](const std::span<const std::byte> bytes)
                {
                    return MaterialParameterCollectionAsset::Encode(
                        MaterialParameterCollectionAsset::DecodeSource(bytes));
                }};
    }

    AssetDecoderRegistration CreateMaterialParameterCollectionAssetDecoder()
    {
        return {MaterialParameterCollectionAsset::StaticType(), MaterialParameterCollectionAsset::Error(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset>
                { return MaterialParameterCollectionAsset::Decode(bytes); }};
    }
} // namespace Keire

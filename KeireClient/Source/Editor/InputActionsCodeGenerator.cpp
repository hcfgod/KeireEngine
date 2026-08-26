#include "KeireClient/Editor/InputActionsCodeGenerator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] bool Keyword(const std::string_view value)
        {
            constexpr std::array words{
                "abstract",  "as",         "base",      "bool",     "break",    "byte",      "case",    "catch",
                "char",      "checked",    "class",     "const",    "continue", "decimal",   "default", "delegate",
                "do",        "double",     "else",      "enum",     "event",    "explicit",  "extern",  "false",
                "finally",   "fixed",      "float",     "for",      "foreach",  "goto",      "if",      "implicit",
                "in",        "int",        "interface", "internal", "is",       "lock",      "long",    "namespace",
                "new",       "null",       "object",    "operator", "out",      "override",  "params",  "private",
                "protected", "public",     "readonly",  "ref",      "return",   "sbyte",     "sealed",  "short",
                "sizeof",    "stackalloc", "static",    "string",   "struct",   "switch",    "this",    "throw",
                "true",      "try",        "typeof",    "uint",     "ulong",    "unchecked", "unsafe",  "ushort",
                "using",     "virtual",    "void",      "volatile", "while"};
            return std::ranges::find(words, value) != words.end();
        }

        [[nodiscard]] std::string Identifier(const std::string_view source, const std::string_view fallback)
        {
            std::string result;
            bool uppercase = true;
            for (const auto value : source)
            {
                if (!std::isalnum(static_cast<unsigned char>(value)) && value != '_')
                {
                    uppercase = true;
                    continue;
                }
                auto output = value;
                if (uppercase && std::isalpha(static_cast<unsigned char>(output)))
                    output = static_cast<char>(std::toupper(static_cast<unsigned char>(output)));
                if (result.empty() && std::isdigit(static_cast<unsigned char>(output)))
                    result.push_back('_');
                result.push_back(output);
                uppercase = false;
            }
            if (result.empty())
                result = fallback;
            if (Keyword(result))
                result.insert(result.begin(), '@');
            return result;
        }

        [[nodiscard]] std::string UniqueIdentifier(const std::string_view source, const std::string_view fallback,
                                                   std::unordered_set<std::string>& used)
        {
            const auto base = Identifier(source, fallback);
            auto result = base;
            for (std::size_t suffix = 2; !used.insert(result).second; ++suffix)
                result = base + std::to_string(suffix);
            return result;
        }

        [[nodiscard]] std::string UniqueMapIdentifier(const std::string_view source,
                                                      std::unordered_set<std::string>& used)
        {
            const auto base = Identifier(source, "ActionMap");
            auto result = base;
            for (std::size_t suffix = 2; used.contains(result) || used.contains(result + "Actions"); ++suffix)
                result = base + std::to_string(suffix);
            used.insert(result);
            used.insert(result + "Actions");
            return result;
        }

        [[nodiscard]] bool ValidIdentifier(const std::string_view value)
        {
            if (value.empty() || Keyword(value) ||
                (!std::isalpha(static_cast<unsigned char>(value.front())) && value.front() != '_'))
                return false;
            return std::ranges::all_of(
                value.substr(1), [](const char character)
                { return std::isalnum(static_cast<unsigned char>(character)) || character == '_'; });
        }

        [[nodiscard]] std::string EscapeString(const std::string_view value)
        {
            std::string result;
            result.reserve(value.size());
            for (const auto character : value)
            {
                if (character == '\\' || character == '"')
                {
                    result.push_back('\\');
                    result.push_back(character);
                }
                else if (static_cast<unsigned char>(character) < 0x20U || character == 0x7F)
                {
                    constexpr char digits[] = "0123456789abcdef";
                    const auto byte = static_cast<unsigned char>(character);
                    result += "\\u00";
                    result.push_back(digits[byte >> 4U]);
                    result.push_back(digits[byte & 0x0FU]);
                }
                else
                    result.push_back(character);
            }
            return result;
        }

        [[nodiscard]] std::string AssetExpression(const Keire::AssetId id)
        {
            return "new AssetId(" + std::to_string(id.High()) + "UL, " + std::to_string(id.Low()) + "UL)";
        }

        void ValidateNamespace(const std::string_view value)
        {
            if (value.empty() || value.back() == '.')
                throw std::invalid_argument("Generated input wrappers require a namespace.");
            std::size_t begin = 0;
            while (begin < value.size())
            {
                const auto end = value.find('.', begin);
                const auto component =
                    value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin);
                if (!ValidIdentifier(component))
                    throw std::invalid_argument("Generated input wrapper namespaces must be C# identifiers.");
                if (end == std::string_view::npos)
                    break;
                begin = end + 1;
            }
        }
    } // namespace

    std::string GenerateInputActionsCSharp(const Keire::InputActionAssetDefinition& definition,
                                           const std::string_view className, const std::string_view nameSpace)
    {
        Keire::InputActionAsset::Validate(definition);
        ValidateNamespace(nameSpace);
        if (!ValidIdentifier(className))
            throw std::invalid_argument("Generated input wrapper class names must be C# identifiers.");

        std::ostringstream output;
        output << "// <auto-generated />\n#nullable enable\n\nusing System;\nusing Keire;\n\nnamespace " << nameSpace
               << ";\n\n"
               << "public sealed class " << className << " : IDisposable\n{\n"
               << "    private InputActionContext? _context;\n\n"
               << "    public " << className << "(InputActionAsset asset)\n    {\n"
               << "        ArgumentNullException.ThrowIfNull(asset);\n"
               << "        _context = asset.CreateContext();\n";

        std::unordered_set<std::string> mapNames{std::string(className), "Context", "Disable", "Dispose", "Enable"};
        std::vector<std::string> generatedMapNames;
        generatedMapNames.reserve(definition.ActionMaps.size());
        for (const auto& map : definition.ActionMaps)
        {
            const auto name = UniqueMapIdentifier(map.Name, mapNames);
            generatedMapNames.push_back(name);
            output << "        " << name << " = new " << name << "Actions(_context);\n";
        }
        output << "    }\n\n"
               << "    public void Enable() => Context.Enable();\n"
               << "    public void Disable() => Context.Disable();\n"
               << "    public void Dispose()\n    {\n"
               << "        _context?.Dispose();\n        _context = null;\n    }\n\n"
               << "    private InputActionContext Context => _context ?? throw new ObjectDisposedException(nameof("
               << className << "));\n\n";

        for (std::size_t mapIndex = 0; mapIndex < definition.ActionMaps.size(); ++mapIndex)
        {
            const auto& map = definition.ActionMaps[mapIndex];
            const auto& mapName = generatedMapNames[mapIndex];
            output << "    public " << mapName << "Actions " << mapName << " { get; }\n\n"
                   << "    public sealed class " << mapName << "Actions\n    {\n"
                   << "        private readonly InputActionContext _context;\n\n"
                   << "        internal " << mapName << "Actions(InputActionContext context) => _context = context;\n\n"
                   << "        public void Enable() => _context.GetActionMap(" << AssetExpression(map.Id) << ", \""
                   << EscapeString(map.Name) << "\").Enable();\n"
                   << "        public void Disable() => _context.GetActionMap(" << AssetExpression(map.Id) << ", \""
                   << EscapeString(map.Name) << "\").Disable();\n";
            std::unordered_set<std::string> actionNames{"Disable", "Enable", mapName + "Actions"};
            for (const auto& action : map.Actions)
            {
                const auto actionName = UniqueIdentifier(action.Name, "Action", actionNames);
                output << "        public InputAction " << actionName << " => _context.GetAction("
                       << AssetExpression(map.Id) << ", " << AssetExpression(action.Id) << ", \""
                       << EscapeString(action.Name) << "\");\n";
            }
            output << "    }\n\n";
        }
        output << "}\n";
        return output.str();
    }
} // namespace KeireEditor

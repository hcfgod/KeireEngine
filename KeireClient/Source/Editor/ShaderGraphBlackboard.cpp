#include "KeireClient/Editor/ShaderGraphBlackboard.h"

#include <algorithm>
#include <tuple>

namespace KeireEditor
{
    namespace
    {
        std::string SearchText(const std::string_view text)
        {
            std::string result(text);
            for (auto& character : result)
                if (character >= 'A' && character <= 'Z')
                    character = static_cast<char>(character - 'A' + 'a');
            return result;
        }
    } // namespace

    std::vector<ShaderGraphBlackboardEntry> BuildShaderGraphBlackboard(const Keire::ShaderGraphDefinition& definition,
                                                                       const std::string_view search)
    {
        std::vector<ShaderGraphBlackboardEntry> entries;
        for (const auto& node : definition.Nodes)
            if (node.Kind == Keire::ShaderGraphNodeKind::Parameter)
                entries.push_back(
                    {node.Id.ToString(), node.Id, node.Name, node.Symbol,
                     node.ParameterMetadata.Category.empty() ? "Properties" : node.ParameterMetadata.Category,
                     node.ParameterMetadata.Description, node.ParameterMetadata.SortPriority});
        for (const auto& keyword : definition.Keywords)
        {
            const auto append = [&](const std::string& token, const std::string& label)
            {
                std::optional<Keire::AssetId> nodeId;
                for (const auto& node : definition.Nodes)
                    if (node.Kind == Keire::ShaderGraphNodeKind::Keyword && node.Symbol == token &&
                        (!nodeId || node.Id < *nodeId))
                        nodeId = node.Id;
                entries.push_back({"keyword:" + token, nodeId, label, token, "Keywords",
                                   "Default: " + keyword.DefaultOption, 0, true, keyword.Exposed});
            };
            if (keyword.Options.empty())
                append(keyword.Name, keyword.Name);
            else
                for (const auto& option : keyword.Options)
                    append(keyword.Name + "_" + option, keyword.Name + " / " + option);
        }
        const auto query = SearchText(search);
        std::erase_if(entries,
                      [&](const auto& entry)
                      {
                          return !query.empty() && SearchText(entry.Name).find(query) == std::string::npos &&
                                 SearchText(entry.Symbol).find(query) == std::string::npos &&
                                 SearchText(entry.Category).find(query) == std::string::npos &&
                                 SearchText(entry.Description).find(query) == std::string::npos;
                      });
        std::ranges::sort(entries,
                          [](const auto& left, const auto& right)
                          {
                              return std::tie(left.Category, left.SortPriority, left.Name, left.Identity) <
                                     std::tie(right.Category, right.SortPriority, right.Name, right.Identity);
                          });
        return entries;
    }

    bool ShaderGraphHasKeywordToken(const Keire::ShaderGraphDefinition& definition, const std::string_view token)
    {
        return std::ranges::any_of(
            definition.Keywords,
            [&](const auto& keyword)
            {
                return keyword.Options.empty() ? keyword.Name == token
                                               : std::ranges::any_of(keyword.Options, [&](const auto& option)
                                                                     { return keyword.Name + "_" + option == token; });
            });
    }
} // namespace KeireEditor

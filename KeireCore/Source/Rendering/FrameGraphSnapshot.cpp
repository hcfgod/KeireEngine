#include "Keire/Rendering/FrameGraphSnapshot.h"

#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <sstream>
#include <string_view>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        template <typename Enum> [[nodiscard]] constexpr std::uint32_t Number(const Enum value) noexcept
        {
            return static_cast<std::uint32_t>(value);
        }

        [[nodiscard]] std::string EscapeDot(const std::string_view value)
        {
            std::string result;
            result.reserve(value.size());
            for (const auto character : value)
            {
                if (character == '"' || character == '\\')
                    result.push_back('\\');
                if (character == '\n' || character == '\r')
                    result += "\\n";
                else
                    result.push_back(character);
            }
            return result;
        }
    } // namespace

    void ExportFrameGraphJson(const FrameGraphSnapshot& snapshot, const std::filesystem::path& path)
    {
        Json passes = Json::array();
        for (const auto& pass : snapshot.Passes)
        {
            Json transitions = Json::array();
            for (const auto& transition : pass.Transitions)
                transitions.push_back({{"resource", transition.Resource},
                                       {"before", Number(transition.Before)},
                                       {"after", Number(transition.After)}});
            passes.push_back({{"index", pass.Index},
                              {"order", pass.Order},
                              {"name", pass.Name},
                              {"kind", Number(pass.Kind)},
                              {"reads", pass.Reads},
                              {"writes", pass.Writes},
                              {"transitions", std::move(transitions)}});
        }
        Json resources = Json::array();
        for (const auto& resource : snapshot.Resources)
            resources.push_back({{"index", resource.Index},
                                 {"name", resource.Name},
                                 {"kind", Number(resource.Kind)},
                                 {"imported", resource.Imported},
                                 {"used", resource.Used},
                                 {"firstPass", resource.FirstPass},
                                 {"lastPass", resource.LastPass},
                                 {"physicalAliasSlot", resource.PhysicalAliasSlot},
                                 {"compatibilityKey", resource.CompatibilityKey},
                                 {"estimatedBytes", resource.EstimatedBytes}});
        const Json document{{"schemaVersion", 1},
                            {"frame", snapshot.Frame},
                            {"activeTransientBytes", snapshot.ActiveTransientBytes},
                            {"theoreticalUnaliasedBytes", snapshot.TheoreticalUnaliasedBytes},
                            {"savedAliasingBytes", snapshot.SavedAliasingBytes},
                            {"fenceRetiredBytes", snapshot.FenceRetiredBytes},
                            {"passes", std::move(passes)},
                            {"resources", std::move(resources)}};
        Detail::WriteTextFileAtomically(path, document.dump(2) + '\n');
    }

    void ExportFrameGraphDot(const FrameGraphSnapshot& snapshot, const std::filesystem::path& path)
    {
        std::ostringstream result;
        result << "digraph KeireFrameGraph {\n  rankdir=LR;\n";
        for (const auto& pass : snapshot.Passes)
            result << "  p" << pass.Index << " [shape=box,label=\"" << pass.Order << ": " << EscapeDot(pass.Name)
                   << "\"];\n";
        for (const auto& resource : snapshot.Resources)
            result << "  r" << resource.Index << " [shape=ellipse,label=\"" << EscapeDot(resource.Name) << "\\nslot "
                   << resource.PhysicalAliasSlot << "\"];\n";
        for (const auto& pass : snapshot.Passes)
        {
            for (const auto resource : pass.Reads)
                result << "  r" << resource << " -> p" << pass.Index << ";\n";
            for (const auto resource : pass.Writes)
                result << "  p" << pass.Index << " -> r" << resource << ";\n";
        }
        result << "}\n";
        Detail::WriteTextFileAtomically(path, result.str());
    }
} // namespace Keire

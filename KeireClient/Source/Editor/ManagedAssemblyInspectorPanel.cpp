#include "KeireClient/Editor/ManagedAssemblyInspectorPanel.h"
#include "Keire/Scripting/ManagedAssemblyReferenceAsset.h"
#include "KeireClient/Editor/EditorPanels.h"
#include "KeireClientInternal/Editor/InspectorFieldLayout.h"
#include "KeireInternal/FileSystem.h"
#include <algorithm>
#include <array>
#include <ranges>
#include <stdexcept>

namespace KeireEditor
{
    namespace
    {
        bool StringList(Keire::UiFrame& ui, const std::string_view title, std::vector<std::string>& values)
        {
            bool dirty = false;
            if (auto node = ui.BeginTreeNode(title, true); node)
            {
                for (std::size_t i = 0; i < values.size();)
                {
                    dirty |= ui.InputText("Value##" + std::string(title) + std::to_string(i), values[i]);
                    ui.SameLine();
                    if (ui.Button("Remove##" + std::string(title) + std::to_string(i)))
                    {
                        values.erase(values.begin() + static_cast<std::ptrdiff_t>(i));
                        dirty = true;
                        break;
                    }
                    else
                        ++i;
                }
                if (values.size() < 256 && ui.Button("Add##" + std::string(title)))
                {
                    values.emplace_back();
                    dirty = true;
                }
            }
            return dirty;
        }
    } // namespace
    void ManagedAssemblyInspectorPanel::Clear() noexcept
    {
        m_Asset = {};
        m_Definition.reset();
        m_Reference.clear();
        m_Original.clear();
        m_Dirty = false;
        m_Picker.Clear();
    }
    void ManagedAssemblyInspectorPanel::Draw(Keire::UiFrame& ui, const Keire::AssetSourceRecord& record)
    {
        const auto& theme = m_Controller.InspectorTheme();
        try
        {
            const auto database = m_Controller.InspectorAssetDatabase();
            const auto& spec = database->Specification();
            const auto source = spec.ProjectRoot / spec.SourceDirectory / record.RelativePath;
            const auto load = [&]
            {
                const auto text = Keire::Detail::ReadTextFile(source, 1024U * 1024U);
                const auto bytes = std::as_bytes(std::span(text));
                std::optional<Keire::ManagedAssemblyDefinition> definition;
                std::string reference;
                if (record.RelativePath.extension() == ".asmref")
                    reference = Keire::ManagedAssemblyReferenceAsset::Decode(bytes)->Reference();
                else
                {
                    definition = Keire::ManagedAssemblyAsset::Decode(bytes)->Definition();
                    definition->SchemaVersion = Keire::ManagedAssemblySchemaVersion;
                }
                m_Original.assign(bytes.begin(), bytes.end());
                m_Definition = std::move(definition);
                m_Reference = std::move(reference);
                m_ExcludePlatforms = m_Definition && !m_Definition->ExcludePlatforms.empty();
                m_Asset = record.Id;
                m_Dirty = false;
            };
            if (m_Asset != record.Id)
                load();
            ui.Separator();
            const auto records = m_Controller.InspectorAssetRecords();
            if (m_Definition)
            {
                ui.TextColored(theme.Accent, "ASSEMBLY DEFINITION");
                auto& definition = *m_Definition;
                const auto prepareAssemblyField = [&ui](const std::string_view label)
                {
                    const auto layout = Detail::ResolveInspectorFieldLayout(ui.ContentAvailable().Width, 0.0F,
                                                                            Detail::InspectorInlineActionSpacing,
                                                                            Detail::InspectorDescriptiveFieldWidth);
                    if (layout.Stacked)
                    {
                        ui.TextWrapped(Detail::InspectorVisibleLabel(label));
                        ui.SetNextItemWidth(layout.ControlWidth);
                    }
                    return Detail::InspectorControlLabel(label, layout.Stacked);
                };
                m_Dirty |= ui.InputText(prepareAssemblyField("Assembly Name"), definition.Name);
                m_Dirty |= ui.InputText(prepareAssemblyField("Root Namespace"), definition.RootNamespace);
                const std::array<std::string_view, 3> labels{"Runtime", "Editor", "Tests"};
                if (auto combo = ui.BeginCombo(prepareAssemblyField("Classification"),
                                               labels.at(static_cast<std::size_t>(definition.Classification)));
                    combo)
                    for (std::size_t i = 0; i < labels.size(); ++i)
                        if (ui.Selectable(labels[i], static_cast<std::size_t>(definition.Classification) == i))
                        {
                            definition.Classification = static_cast<Keire::ManagedAssemblyClassification>(i);
                            m_Dirty = true;
                        }
                m_Dirty |= ui.Checkbox("Auto Referenced", definition.AutoReferenced);
                m_Dirty |= ui.Checkbox("Allow Unsafe Code", definition.AllowUnsafe);
                if (auto node = ui.BeginTreeNode("Assembly References", true); node)
                {
                    for (std::size_t i = 0; i < definition.References.size();)
                    {
                        m_Dirty |=
                            m_Picker.Draw(ui, records, definition.References[i],
                                          {.Label = "Assembly##" + std::to_string(i),
                                           .ExpectedType = Keire::ManagedAssemblyAsset::StaticType(),
                                           .Filter = [&](const auto& candidate) { return candidate.Id != record.Id; }});
                        ui.SameLine();
                        if (ui.Button("Remove##reference" + std::to_string(i)))
                        {
                            definition.References.erase(definition.References.begin() + static_cast<std::ptrdiff_t>(i));
                            m_Dirty = true;
                            break;
                        }
                        else
                            ++i;
                    }
                    if (definition.References.size() < 256 && ui.Button("Add Assembly Reference"))
                    {
                        definition.References.emplace_back();
                        m_Dirty = true;
                    }
                }
                auto& exclude = m_ExcludePlatforms;
                if (ui.Checkbox("Exclude Selected Platforms", exclude))
                {
                    std::swap(definition.IncludePlatforms, definition.ExcludePlatforms);
                    m_Dirty = true;
                }
                auto& platforms = exclude ? definition.ExcludePlatforms : definition.IncludePlatforms;
                ui.Text("No selected platforms means all platforms.");
                for (const auto name : {"Editor", "Windows", "Linux", "macOS"})
                {
                    bool selected = std::ranges::find(platforms, name) != platforms.end();
                    if (ui.Checkbox(name, selected))
                    {
                        if (selected)
                            platforms.emplace_back(name);
                        else
                            std::erase(platforms, name);
                        m_Dirty = true;
                    }
                }
                m_Dirty |= StringList(ui, "Define Symbols", definition.DefineSymbols);
                ui.Text("Constraints: one required expression per row; use !SYMBOL and SYMBOL || OTHER.");
                m_Dirty |= StringList(ui, "Define Constraints", definition.DefineConstraints);
                if (auto node = ui.BeginTreeNode("Version Defines", true); node)
                {
                    for (std::size_t i = 0; i < definition.VersionDefines.size();)
                    {
                        auto& value = definition.VersionDefines[i];
                        const auto id = "##version" + std::to_string(i);
                        m_Dirty |= ui.InputText("Resource" + id, value.Resource);
                        m_Dirty |= ui.InputText("Expression" + id, value.Expression);
                        m_Dirty |= ui.InputText("Define" + id, value.Define);
                        if (ui.Button("Remove" + id))
                        {
                            definition.VersionDefines.erase(definition.VersionDefines.begin() +
                                                            static_cast<std::ptrdiff_t>(i));
                            m_Dirty = true;
                            break;
                        }
                        else
                            ++i;
                    }
                    if (definition.VersionDefines.size() < 256 && ui.Button("Add Version Define"))
                    {
                        definition.VersionDefines.emplace_back();
                        m_Dirty = true;
                    }
                }
                if (ui.Checkbox("Override References", definition.OverrideReferences))
                {
                    if (!definition.OverrideReferences)
                        definition.PrecompiledReferences.clear();
                    m_Dirty = true;
                }
                if (definition.OverrideReferences)
                {
                    std::vector<std::string> paths;
                    for (const auto& path : definition.PrecompiledReferences)
                        paths.push_back(Keire::Detail::PathToUtf8(path));
                    if (StringList(ui, "Precompiled DLLs (Assets/...dll)", paths))
                    {
                        definition.PrecompiledReferences.assign(paths.begin(), paths.end());
                        m_Dirty = true;
                    }
                }
                else
                    ui.Text("Managed DLLs under Assets are referenced automatically.");
            }
            else
            {
                ui.TextColored(theme.Accent, "ASSEMBLY REFERENCE");
                ui.Text("Assign this folder and its children to an existing assembly.");
                Keire::AssetId target;
                if (m_Reference.starts_with("GUID:"))
                    target = Keire::AssetId::Parse(m_Reference.substr(5));
                if (m_Picker.Draw(ui, records, target,
                                  {.Label = "Assembly", .ExpectedType = Keire::ManagedAssemblyAsset::StaticType()}))
                {
                    m_Reference = target ? "GUID:" + target.ToString() : std::string{};
                    m_Dirty = true;
                }
                ui.Text("Target: " + m_Reference);
            }
            std::string diagnostic;
            std::vector<std::byte> encoded;
            try
            {
                if (!m_Definition && m_Reference.empty())
                    throw std::invalid_argument("Choose an assembly.");
                encoded = m_Definition ? Keire::ManagedAssemblyAsset::Encode(*m_Definition)
                                       : Keire::ManagedAssemblyReferenceAsset::Encode(m_Reference);
            }
            catch (const std::exception& error)
            {
                diagnostic = error.what();
            }
            if (!diagnostic.empty())
                ui.TextColored(theme.Error, diagnostic);
            if (auto disabled = ui.BeginDisabled(!m_Dirty || !diagnostic.empty()); disabled)
                if (ui.Button("Apply Assembly Settings"))
                {
                    m_Controller.PersistInspectorManagedAssembly(record.Id, encoded, m_Original);
                    m_Original = std::move(encoded);
                    m_Dirty = false;
                }
            ui.SameLine();
            if (ui.Button("Revert Assembly Settings"))
                load();
        }
        catch (const std::exception& error)
        {
            ui.TextColored(theme.Error, error.what());
        }
    }
} // namespace KeireEditor

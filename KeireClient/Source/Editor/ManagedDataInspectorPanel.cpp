#include "KeireClient/Editor/ManagedDataInspectorPanel.h"

#include "Keire/Core.h"
#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/EditorPanels.h"
#include "KeireClient/Editor/ManagedDataDocument.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace KeireEditor
{
    namespace
    {
        constexpr std::size_t MaximumVisibleRawBytes = 8U * 1024U;
        constexpr std::size_t MaximumEditableCollectionElements = 4096;

        [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
                throw std::runtime_error("Could not open the managed data source.");
            const std::vector<char> characters{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            std::vector<std::byte> result(characters.size());
            std::ranges::transform(characters, result.begin(), [](const char value) { return std::byte(value); });
            return result;
        }

        [[nodiscard]] std::string ValueBufferKey(const Keire::ManagedAssetPropertyDescriptor& property,
                                                 const Keire::ManagedAssetValueNode& value)
        {
            return property.StableFieldId.ToString() + ':' +
                   std::to_string(reinterpret_cast<std::uintptr_t>(std::addressof(value)));
        }

        [[nodiscard]] std::int64_t MinimumInteger(const Keire::ManagedAssetPropertyDescriptor& property)
        {
            if (!property.Minimum)
                return std::numeric_limits<std::int64_t>::min();
            return static_cast<std::int64_t>(std::clamp(*property.Minimum,
                                                        static_cast<double>(std::numeric_limits<std::int64_t>::min()),
                                                        static_cast<double>(std::numeric_limits<std::int64_t>::max())));
        }

        [[nodiscard]] std::int64_t MaximumInteger(const Keire::ManagedAssetPropertyDescriptor& property)
        {
            if (!property.Maximum)
                return std::numeric_limits<std::int64_t>::max();
            return static_cast<std::int64_t>(std::clamp(*property.Maximum,
                                                        static_cast<double>(std::numeric_limits<std::int64_t>::min()),
                                                        static_cast<double>(std::numeric_limits<std::int64_t>::max())));
        }
    } // namespace

    ManagedDataInspectorPanel::ManagedDataInspectorPanel(IInspectorController& controller)
        : m_Controller(controller),
          m_Document(std::make_unique<ManagedDataDocument>(ManagedDataDocument::Specification{
              .Preview = [this](const Keire::AssetId asset, const Keire::ManagedDataDefinition& definition)
              { m_Controller.PreviewInspectorManagedData(asset, definition); },
              .Persist = [this](const Keire::AssetId asset, const std::span<const std::byte> bytes)
              { m_Controller.PersistInspectorManagedData(asset, bytes); }})),
          m_AssetPicker(std::make_unique<AssetPicker>())
    {
    }

    ManagedDataInspectorPanel::~ManagedDataInspectorPanel() = default;

    void ManagedDataInspectorPanel::Clear() noexcept
    {
        m_Document->Close();
        m_AssetPicker->Clear();
        m_TextBuffers.clear();
        m_Asset = {};
        m_Revision = 0;
    }

    void ManagedDataInspectorPanel::Draw(Keire::UiFrame& ui, const Keire::AssetSourceRecord& record)
    {
        const auto& theme = m_Controller.InspectorTheme();
        const auto assets = m_Controller.InspectorAssetSystem();
        if (!assets)
        {
            DrawRawFallback(ui, record, "The runtime asset system is unavailable.");
            return;
        }

        const auto handle = assets->Load<Keire::ManagedDataAsset>(record.Id, Keire::AssetPriority::High);
        const auto loaded = handle.TryGetLoaded();
        if (!loaded || handle.UsingFallback())
        {
            DrawRawFallback(ui, record, "The managed data asset has no valid imported revision.");
            return;
        }

        const auto types = m_Controller.InspectorManagedAssetTypes();
        const auto descriptor = std::ranges::find(types, loaded->Definition().ManagedType,
                                                  &Keire::ManagedAssetTypeDescriptor::StableTypeId);
        std::optional<Keire::ManagedAssetTypeDescriptor> currentDescriptor;
        if (descriptor != types.end())
            currentDescriptor = *descriptor;

        const auto revision = std::max<std::uint64_t>(handle.Revision(), 1);
        try
        {
            if (!m_Document->IsOpen() || m_Asset != record.Id)
            {
                Clear();
                m_Document->Open(record.Id, loaded->Definition(), revision, currentDescriptor,
                                 m_Controller.InspectorManagedDataHistory());
                m_Asset = record.Id;
                m_Revision = revision;
            }
            else if (revision != m_Revision)
            {
                const auto result = m_Document->Reload(loaded->Definition(), revision);
                m_Revision = revision;
                if (result == AssetDocumentReloadResult::LocalChanges)
                    m_Controller.SetInspectorAssetStatus(
                        "A newer imported revision is available; save or discard local managed data edits.");
            }
        }
        catch (const std::exception& error)
        {
            Clear();
            DrawRawFallback(ui, record, error.what());
            return;
        }

        m_Controller.ActivateInspectorManagedDataHistory();
        ui.Separator();
        ui.TextColored(theme.Accent, "MANAGED DATA");
        ui.Text(loaded->Definition().ManagedTypeName);
        ui.TextColored(theme.MutedText, loaded->Definition().ManagedType.ToString());
        if (m_Controller.InspectorPlayModeActive())
        {
            ui.TextColored(theme.Warning,
                           "Project asset: edits persist outside the Play Mode scene clone and hot-apply immediately.");
        }

        if (ui.Button("Save"))
        {
            try
            {
                m_Document->Save();
            }
            catch (const std::exception& error)
            {
                m_Controller.ReportInspectorAssetError(std::string("Managed data save failed: ") + error.what());
            }
        }
        ui.SameLine();
        if (ui.Button("Discard"))
        {
            try
            {
                m_Document->Discard();
            }
            catch (const std::exception& error)
            {
                m_Controller.ReportInspectorAssetError(std::string("Managed data discard failed: ") + error.what());
            }
        }
        ui.SameLine();
        if (ui.Button("Undo"))
            (void)m_Document->Undo();
        ui.SameLine();
        if (ui.Button("Redo"))
            (void)m_Document->Redo();
        ui.TextColored(m_Document->Dirty() ? theme.Warning : theme.Success,
                       m_Document->Dirty() ? "Unsaved managed data changes" : "Source is saved");

        if (!currentDescriptor)
        {
            ui.TextColored(theme.Error, "The managed type is missing from the active runtime assembly.");
            ui.TextColored(theme.Warning, "Raw stable-field data is preserved and remains read-only.");
            for (const auto& field : m_Document->Draft().Fields)
            {
                const auto node = ui.BeginTreeNode(field.Name.empty() ? field.StableFieldId.ToString() : field.Name);
                if (!node)
                    continue;
                ui.TextColored(theme.MutedText, field.ManagedTypeName);
                ui.Text(field.Value.substr(0, MaximumVisibleRawBytes));
            }
            return;
        }

        ui.Separator();
        for (const auto& property : currentDescriptor->Properties)
        {
            if (property.Hidden)
                continue;
            auto state = m_Document->Property(property);
            const auto id = ui.PushId(property.StableFieldId.ToString());
            if (!state.Diagnostic.empty())
            {
                ui.TextColored(theme.Error, property.DisplayName + ": " + state.Diagnostic);
                ui.TextColored(theme.MutedText, state.RawValue.substr(0, MaximumVisibleRawBytes));
                if (!property.ReadOnly && ui.Button("Reset to managed default"))
                {
                    try
                    {
                        (void)m_Document->ClearProperty(property);
                    }
                    catch (const std::exception& error)
                    {
                        m_Controller.ReportInspectorAssetError(error.what());
                    }
                }
                continue;
            }
            if (!state.Serialized)
            {
                ui.Text(property.DisplayName);
                ui.SameLine();
                ui.TextColored(theme.MutedText, "Managed default");
                if (!property.ReadOnly && ui.Button("Override"))
                {
                    try
                    {
                        (void)m_Document->SetProperty(property, ManagedDataDocument::MaterializedDefaultValue(property),
                                                      "Override " + property.DisplayName);
                    }
                    catch (const std::exception& error)
                    {
                        m_Controller.ReportInspectorAssetError(error.what());
                    }
                }
                continue;
            }

            auto value = std::move(state.Value);
            try
            {
                const auto disabled = ui.BeginDisabled(property.ReadOnly);
                const bool changed = DrawProperty(ui, value, property, types);
                if (changed && !property.ReadOnly)
                    (void)m_Document->SetProperty(property, std::move(value), "Edit " + property.DisplayName);
                if (!property.ReadOnly && ui.Button("Use managed default"))
                    (void)m_Document->ClearProperty(property, "Use " + property.DisplayName + " default");
            }
            catch (const std::exception& error)
            {
                m_Controller.ReportInspectorAssetError(std::string("Managed data edit failed: ") + error.what());
            }
        }
        if (!m_Document->Diagnostic().empty())
            ui.TextColored(theme.Error, m_Document->Diagnostic());
    }

    bool ManagedDataInspectorPanel::DrawProperty(Keire::UiFrame& ui, Keire::ManagedAssetValueNode& value,
                                                 const Keire::ManagedAssetPropertyDescriptor& property,
                                                 const std::span<const Keire::ManagedAssetTypeDescriptor> types)
    {
        const auto& theme = m_Controller.InspectorTheme();
        const auto label = property.DisplayName.empty() ? property.Name : property.DisplayName;
        if (!property.Header.empty())
            ui.TextColored(theme.Accent, property.Header);
        const bool nullable = property.Kind == Keire::ManagedAssetPropertyKind::Text ||
                              property.Kind == Keire::ManagedAssetPropertyKind::SerializableObject ||
                              property.Kind == Keire::ManagedAssetPropertyKind::Array ||
                              property.Kind == Keire::ManagedAssetPropertyKind::List;
        bool changed = false;
        if (nullable && std::holds_alternative<std::monostate>(value.Value))
        {
            ui.Text(label + ": Null");
            if (ui.Button("Create value"))
            {
                value = ManagedDataDocument::MaterializedDefaultValue(property);
                changed = true;
            }
        }
        else
        {
            changed = DrawValue(ui, value, property, types);
            if (nullable)
            {
                ui.SameLine();
                if (ui.Button("Set null"))
                {
                    value.Value = std::monostate{};
                    value.Children.clear();
                    changed = true;
                }
            }
        }
        if (!property.Tooltip.empty())
            ui.TextColored(theme.MutedText, property.Tooltip);
        return changed;
    }

    bool ManagedDataInspectorPanel::DrawValue(Keire::UiFrame& ui, Keire::ManagedAssetValueNode& value,
                                              const Keire::ManagedAssetPropertyDescriptor& property,
                                              const std::span<const Keire::ManagedAssetTypeDescriptor> types)
    {
        const auto label = property.DisplayName.empty() ? property.Name : property.DisplayName;
        switch (property.Kind)
        {
        case Keire::ManagedAssetPropertyKind::Boolean:
            return ui.Checkbox(label, std::get<bool>(value.Value));
        case Keire::ManagedAssetPropertyKind::Integer:
            if (property.ManagedTypeName == "System.Char")
            {
                auto candidate = std::get<std::string>(value.Value);
                if (!ui.InputText(label, candidate))
                    return false;
                if (candidate.size() != 1)
                    throw std::invalid_argument("Managed character values require exactly one character.");
                value.Value = std::move(candidate);
                return true;
            }
            [[fallthrough]];
        case Keire::ManagedAssetPropertyKind::Enum:
        {
            auto& integer = std::get<std::int64_t>(value.Value);
            return ui.DragInteger(label, integer, 1.0, MinimumInteger(property), MaximumInteger(property));
        }
        case Keire::ManagedAssetPropertyKind::UnsignedInteger:
        {
            auto& integer = std::get<std::uint64_t>(value.Value);
            const auto key = ValueBufferKey(property, value);
            auto [buffer, inserted] = m_TextBuffers.try_emplace(key, std::to_string(integer));
            if (!inserted && buffer->second != std::to_string(integer))
                buffer->second = std::to_string(integer);
            if (!ui.InputText(label, buffer->second))
                return false;
            std::uint64_t candidate = 0;
            const auto [end, error] =
                std::from_chars(buffer->second.data(), buffer->second.data() + buffer->second.size(), candidate);
            if (error != std::errc{} || end != buffer->second.data() + buffer->second.size())
                return false;
            integer = candidate;
            return true;
        }
        case Keire::ManagedAssetPropertyKind::Scalar:
        {
            auto& scalar = std::get<double>(value.Value);
            return ui.DragScalar(label, scalar, 0.1, property.Minimum, property.Maximum);
        }
        case Keire::ManagedAssetPropertyKind::Text:
            return ui.InputText(label, std::get<std::string>(value.Value));
        case Keire::ManagedAssetPropertyKind::Vector2:
            return ui.DragVector2(label, std::get<Keire::Vector2>(value.Value));
        case Keire::ManagedAssetPropertyKind::Vector3:
            return ui.DragVector3(label, std::get<Keire::Vector3>(value.Value));
        case Keire::ManagedAssetPropertyKind::Vector4:
            return ui.DragVector4(label, std::get<Keire::Vector4>(value.Value));
        case Keire::ManagedAssetPropertyKind::Quaternion:
            return ui.DragQuaternion(label, std::get<Keire::Quaternion>(value.Value));
        case Keire::ManagedAssetPropertyKind::Color:
        {
            auto& color = std::get<Keire::Color>(value.Value);
            Keire::UiColor editable{color.Red, color.Green, color.Blue, color.Alpha};
            if (!ui.ColorEdit(label, editable))
                return false;
            color = {editable.Red, editable.Green, editable.Blue, editable.Alpha};
            return true;
        }
        case Keire::ManagedAssetPropertyKind::AssetReference:
        {
            auto& asset = std::get<Keire::AssetId>(value.Value);
            AssetPickerOptions options;
            options.Label = label;
            options.ExpectedType = property.ExpectedAssetType;
            options.Filter = [this, &property, types](const Keire::AssetSourceRecord& candidate)
            {
                return ManagedDataDocument::AcceptsAssetReference(candidate, property, LoadDefinition(candidate),
                                                                  types);
            };
            options.Reveal = [this](const Keire::AssetId selected)
            { m_Controller.SetInspectorSelectedAsset(selected); };
            return m_AssetPicker->Draw(ui, m_Controller.InspectorAssetRecords(), asset, options);
        }
        case Keire::ManagedAssetPropertyKind::SerializableObject:
        {
            bool changed = false;
            const auto tree = ui.BeginTreeNode(label);
            if (!tree)
                return false;
            for (std::size_t index = 0; index < property.Children.size(); ++index)
            {
                const auto id = ui.PushId(property.Children[index].StableFieldId.ToString());
                changed = DrawProperty(ui, value.Children[index], property.Children[index], types) || changed;
            }
            return changed;
        }
        case Keire::ManagedAssetPropertyKind::Array:
        case Keire::ManagedAssetPropertyKind::List:
        {
            bool changed = false;
            const auto tree = ui.BeginTreeNode(label + " (" + std::to_string(value.Children.size()) + ')');
            if (!tree)
                return false;
            for (std::size_t index = 0; index < value.Children.size();)
            {
                const auto id = ui.PushId(std::to_string(index));
                changed = DrawValue(ui, value.Children[index], property.Children.front(), types) || changed;
                ui.SameLine();
                if (ui.Button("Remove"))
                {
                    value.Children.erase(value.Children.begin() + static_cast<std::ptrdiff_t>(index));
                    changed = true;
                    continue;
                }
                ++index;
            }
            if (value.Children.size() < MaximumEditableCollectionElements && ui.Button("Add Element"))
            {
                value.Children.push_back(ManagedDataDocument::MaterializedDefaultValue(property.Children.front()));
                changed = true;
            }
            return changed;
        }
        }
        return false;
    }

    std::optional<Keire::ManagedDataDefinition>
    ManagedDataInspectorPanel::LoadDefinition(const Keire::AssetSourceRecord& record) const
    {
        if (record.Type != Keire::ManagedDataAsset::StaticType())
            return std::nullopt;
        const auto database = m_Controller.InspectorAssetDatabase();
        if (!database)
            return std::nullopt;
        try
        {
            const auto sourceRoot = database->Specification().ProjectRoot / database->Specification().SourceDirectory;
            return Keire::ManagedDataAsset::Decode(ReadBytes(sourceRoot / record.RelativePath))->Definition();
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    void ManagedDataInspectorPanel::DrawRawFallback(Keire::UiFrame& ui, const Keire::AssetSourceRecord& record,
                                                    const std::string_view reason) const
    {
        const auto& theme = m_Controller.InspectorTheme();
        ui.Separator();
        ui.TextColored(theme.Error, "MANAGED DATA UNAVAILABLE");
        ui.TextColored(theme.Warning, reason);
        const auto database = m_Controller.InspectorAssetDatabase();
        if (database)
        {
            const auto status = database->ImportStatus(record.Id);
            for (const auto& diagnostic : status.Diagnostics)
                ui.TextColored(diagnostic.Severity == Keire::AssetDiagnosticSeverity::Error ? theme.Error
                                                                                            : theme.Warning,
                               diagnostic.Message);
            try
            {
                const auto sourceRoot =
                    database->Specification().ProjectRoot / database->Specification().SourceDirectory;
                const auto bytes = ReadBytes(sourceRoot / record.RelativePath);
                const auto count = std::min(bytes.size(), MaximumVisibleRawBytes);
                ui.TextColored(theme.MutedText, "Raw source (read-only)");
                ui.Text(std::string(reinterpret_cast<const char*>(bytes.data()), count));
                if (bytes.size() > count)
                    ui.TextColored(theme.MutedText, "Raw source display truncated.");
            }
            catch (const std::exception& error)
            {
                ui.TextColored(theme.MutedText, error.what());
            }
        }
        if (ui.Button("Reimport Managed Data"))
            m_Controller.ImportInspectorAssets();
    }
} // namespace KeireEditor

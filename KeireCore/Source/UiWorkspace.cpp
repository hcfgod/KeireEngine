#include "Keire/UiWorkspace.h"

#include "KeireInternal/FileSystem.h"
#include "KeireInternal/UiPanelRegistry.h"
#include "KeireInternal/WindowInternal.h"

#include "Keire/PlatformDirectories.h"
#include "Keire/Ui.h"

#include <SDL3/SDL_dialog.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <fstream>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::uint32_t WorkspaceSchemaVersion = 1;
        constexpr std::uintmax_t MaximumDocumentBytes = 1024ULL * 1024U;
        constexpr std::uint64_t DefaultLayoutValue = 1;
        constexpr std::uint64_t DarkThemeValue = 1;
        constexpr std::uint64_t LightThemeValue = 2;
        constexpr std::uint64_t ClassicThemeValue = 3;
        constexpr std::uint64_t FirstUserValue = 100;

        [[nodiscard]] std::filesystem::path Utf8Path(const char* value)
        {
            const auto* first = reinterpret_cast<const char8_t*>(value);
            return std::filesystem::path(std::u8string(first, first + std::char_traits<char>::length(value)));
        }

        struct LayoutRecord
        {
            UiLayoutId Id;
            std::string Name;
            bool BuiltIn = false;
            bool Modified = false;
        };

        struct ThemeRecord
        {
            UiThemeId Id;
            std::string Name;
            bool BuiltIn = false;
            UiThemeDefinition Definition;
        };

        enum class DialogAction : std::uint8_t
        {
            ImportLayout,
            ExportLayout,
            ImportTheme,
            ExportTheme
        };

        struct DialogResult
        {
            DialogAction Action = DialogAction::ImportLayout;
            std::uint64_t Id = 0;
            std::optional<std::filesystem::path> Path;
            std::string Error;
        };

        struct DialogMailbox
        {
            std::mutex Mutex;
            std::optional<DialogResult> Result;
            bool Active = false;
            bool Alive = true;
        };

        struct DialogRequest
        {
            std::weak_ptr<DialogMailbox> Mailbox;
            DialogAction Action = DialogAction::ImportLayout;
            std::uint64_t Id = 0;
        };

        [[nodiscard]] ImVec4 ToImVec4(const UiColor value) noexcept
        {
            return {value.Red, value.Green, value.Blue, value.Alpha};
        }

        [[nodiscard]] bool ValidColor(const UiColor value) noexcept
        {
            const auto valid = [](const float component)
            { return std::isfinite(component) && component >= 0.0F && component <= 1.0F; };
            return valid(value.Red) && valid(value.Green) && valid(value.Blue) && valid(value.Alpha);
        }

        void ValidateTheme(const UiThemeDefinition& theme)
        {
            const std::array colors{theme.Canvas,       theme.Panel,     theme.RaisedPanel, theme.Border,
                                    theme.Text,         theme.MutedText, theme.Accent,      theme.AccentHovered,
                                    theme.AccentActive, theme.Selection, theme.Success,     theme.Warning,
                                    theme.Error};
            if (!std::ranges::all_of(colors, ValidColor))
                throw std::invalid_argument("Theme colors must contain finite components in the range 0..1.");

            const auto validSize = [](const UiSize value)
            {
                return std::isfinite(value.Width) && std::isfinite(value.Height) && value.Width >= 0.0F &&
                       value.Width <= 32.0F && value.Height >= 0.0F && value.Height <= 32.0F;
            };
            const auto inRange = [](const float value, const float maximum)
            { return std::isfinite(value) && value >= 0.0F && value <= maximum; };
            if (!validSize(theme.WindowPadding) || !validSize(theme.FramePadding) || !validSize(theme.ItemSpacing) ||
                !inRange(theme.WindowRounding, 24.0F) || !inRange(theme.FrameRounding, 24.0F) ||
                !inRange(theme.TabRounding, 24.0F) || !inRange(theme.ScrollbarRounding, 24.0F) ||
                !inRange(theme.WindowBorderSize, 4.0F) || !inRange(theme.FrameBorderSize, 4.0F))
            {
                throw std::invalid_argument("Theme metrics are outside their supported ranges.");
            }
        }

        void ApplyThemeDefinition(const UiThemeDefinition& theme)
        {
            ValidateTheme(theme);
            auto& style = ImGui::GetStyle();
            style.WindowPadding = {theme.WindowPadding.Width, theme.WindowPadding.Height};
            style.FramePadding = {theme.FramePadding.Width, theme.FramePadding.Height};
            style.ItemSpacing = {theme.ItemSpacing.Width, theme.ItemSpacing.Height};
            style.WindowRounding = theme.WindowRounding;
            style.ChildRounding = theme.FrameRounding;
            style.FrameRounding = theme.FrameRounding;
            style.PopupRounding = theme.FrameRounding;
            style.TabRounding = theme.TabRounding;
            style.ScrollbarRounding = theme.ScrollbarRounding;
            style.GrabRounding = theme.FrameRounding;
            style.WindowBorderSize = theme.WindowBorderSize;
            style.ChildBorderSize = theme.WindowBorderSize;
            style.PopupBorderSize = theme.WindowBorderSize;
            style.FrameBorderSize = theme.FrameBorderSize;

            auto& colors = style.Colors;
            colors[ImGuiCol_Text] = ToImVec4(theme.Text);
            colors[ImGuiCol_TextDisabled] = ToImVec4(theme.MutedText);
            colors[ImGuiCol_WindowBg] = ToImVec4(theme.Canvas);
            colors[ImGuiCol_ChildBg] = ToImVec4(theme.Panel);
            colors[ImGuiCol_PopupBg] = ToImVec4(theme.RaisedPanel);
            colors[ImGuiCol_Border] = ToImVec4(theme.Border);
            colors[ImGuiCol_BorderShadow] = {0.0F, 0.0F, 0.0F, 0.0F};
            colors[ImGuiCol_FrameBg] = ToImVec4(theme.Panel);
            colors[ImGuiCol_FrameBgHovered] = ToImVec4(theme.RaisedPanel);
            colors[ImGuiCol_FrameBgActive] = ToImVec4(theme.Selection);
            colors[ImGuiCol_TitleBg] = ToImVec4(theme.Panel);
            colors[ImGuiCol_TitleBgActive] = ToImVec4(theme.RaisedPanel);
            colors[ImGuiCol_TitleBgCollapsed] = ToImVec4(theme.Canvas);
            colors[ImGuiCol_MenuBarBg] = ToImVec4(theme.Panel);
            colors[ImGuiCol_ScrollbarBg] = ToImVec4(theme.Canvas);
            colors[ImGuiCol_ScrollbarGrab] = ToImVec4(theme.Border);
            colors[ImGuiCol_ScrollbarGrabHovered] = ToImVec4(theme.AccentHovered);
            colors[ImGuiCol_ScrollbarGrabActive] = ToImVec4(theme.AccentActive);
            colors[ImGuiCol_CheckMark] = ToImVec4(theme.Accent);
            colors[ImGuiCol_SliderGrab] = ToImVec4(theme.Accent);
            colors[ImGuiCol_SliderGrabActive] = ToImVec4(theme.AccentActive);
            colors[ImGuiCol_Button] = ToImVec4(theme.RaisedPanel);
            colors[ImGuiCol_ButtonHovered] = ToImVec4(theme.AccentHovered);
            colors[ImGuiCol_ButtonActive] = ToImVec4(theme.AccentActive);
            colors[ImGuiCol_Header] = ToImVec4(theme.Selection);
            colors[ImGuiCol_HeaderHovered] = ToImVec4(theme.AccentHovered);
            colors[ImGuiCol_HeaderActive] = ToImVec4(theme.AccentActive);
            colors[ImGuiCol_Separator] = ToImVec4(theme.Border);
            colors[ImGuiCol_SeparatorHovered] = ToImVec4(theme.AccentHovered);
            colors[ImGuiCol_SeparatorActive] = ToImVec4(theme.AccentActive);
            colors[ImGuiCol_ResizeGrip] = ToImVec4(theme.Selection);
            colors[ImGuiCol_ResizeGripHovered] = ToImVec4(theme.AccentHovered);
            colors[ImGuiCol_ResizeGripActive] = ToImVec4(theme.AccentActive);
            colors[ImGuiCol_Tab] = ToImVec4(theme.Panel);
            colors[ImGuiCol_TabHovered] = ToImVec4(theme.AccentHovered);
            colors[ImGuiCol_TabSelected] = ToImVec4(theme.RaisedPanel);
            colors[ImGuiCol_TabSelectedOverline] = ToImVec4(theme.Accent);
            colors[ImGuiCol_TabDimmed] = ToImVec4(theme.Canvas);
            colors[ImGuiCol_TabDimmedSelected] = ToImVec4(theme.Panel);
            colors[ImGuiCol_DockingPreview] = ToImVec4(theme.Accent);
            colors[ImGuiCol_DockingEmptyBg] = ToImVec4(theme.Canvas);
            colors[ImGuiCol_PlotLines] = ToImVec4(theme.MutedText);
            colors[ImGuiCol_PlotHistogram] = ToImVec4(theme.Accent);
            colors[ImGuiCol_TableHeaderBg] = ToImVec4(theme.RaisedPanel);
            colors[ImGuiCol_TableBorderStrong] = ToImVec4(theme.Border);
            colors[ImGuiCol_TableBorderLight] = ToImVec4(theme.Border);
            colors[ImGuiCol_TableRowBgAlt] = ToImVec4(theme.Panel);
            colors[ImGuiCol_TextLink] = ToImVec4(theme.AccentHovered);
            colors[ImGuiCol_TextSelectedBg] = ToImVec4(theme.Selection);
            colors[ImGuiCol_NavCursor] = ToImVec4(theme.Accent);
            colors[ImGuiCol_NavWindowingHighlight] = ToImVec4(theme.AccentHovered);
            colors[ImGuiCol_ModalWindowDimBg] = {theme.Canvas.Red, theme.Canvas.Green, theme.Canvas.Blue, 0.72F};
        }

        [[nodiscard]] UiThemeDefinition LightTheme()
        {
            UiThemeDefinition theme;
            theme.Canvas = {0.88F, 0.89F, 0.91F, 1.0F};
            theme.Panel = {0.95F, 0.96F, 0.97F, 1.0F};
            theme.RaisedPanel = {1.0F, 1.0F, 1.0F, 1.0F};
            theme.Border = {0.69F, 0.72F, 0.77F, 1.0F};
            theme.Text = {0.10F, 0.12F, 0.16F, 1.0F};
            theme.MutedText = {0.38F, 0.42F, 0.49F, 1.0F};
            theme.Accent = {0.10F, 0.42F, 0.88F, 1.0F};
            theme.AccentHovered = {0.17F, 0.50F, 0.96F, 1.0F};
            theme.AccentActive = {0.06F, 0.34F, 0.78F, 1.0F};
            theme.Selection = {0.10F, 0.42F, 0.88F, 0.24F};
            return theme;
        }

        [[nodiscard]] UiThemeDefinition ClassicTheme()
        {
            UiThemeDefinition theme;
            theme.Canvas = {0.06F, 0.06F, 0.07F, 1.0F};
            theme.Panel = {0.14F, 0.14F, 0.16F, 1.0F};
            theme.RaisedPanel = {0.22F, 0.23F, 0.28F, 1.0F};
            theme.Border = {0.48F, 0.50F, 0.56F, 1.0F};
            theme.Text = {0.96F, 0.96F, 0.97F, 1.0F};
            theme.MutedText = {0.74F, 0.75F, 0.79F, 1.0F};
            theme.Accent = {0.34F, 0.56F, 0.96F, 1.0F};
            theme.AccentHovered = {0.45F, 0.66F, 1.0F, 1.0F};
            theme.AccentActive = {0.25F, 0.47F, 0.88F, 1.0F};
            theme.Selection = {0.34F, 0.56F, 0.96F, 0.42F};
            return theme;
        }

        [[nodiscard]] bool ValidProfileName(const std::string_view name) noexcept
        {
            if (name.empty() || name.size() > 64 || name.front() == ' ' || name.back() == ' ')
                return false;
            return std::ranges::none_of(name, [](const unsigned char character)
                                        { return character < 0x20U || character == '/' || character == '\\'; });
        }

        void ValidateProfileName(const std::string_view name)
        {
            if (!ValidProfileName(name))
            {
                throw std::invalid_argument(
                    "Profile names must be 1..64 UTF-8 bytes, without control characters, path separators, or "
                    "leading/trailing spaces.");
            }
        }

        [[nodiscard]] Json ColorToJson(const UiColor color)
        {
            return Json::array({color.Red, color.Green, color.Blue, color.Alpha});
        }

        [[nodiscard]] UiColor ColorFromJson(const Json& value, const char* field)
        {
            if (!value.is_array() || value.size() != 4 ||
                !std::ranges::all_of(value, [](const Json& component) { return component.is_number(); }))
                throw UiError("ParseTheme", std::string(field) + " must be an array of four numbers");
            const UiColor result{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
                                 value[3].get<float>()};
            if (!ValidColor(result))
                throw UiError("ParseTheme", std::string(field) + " contains a component outside 0..1");
            return result;
        }

        void RequireKeys(const Json& value, const std::initializer_list<std::string_view> allowed, const char* document)
        {
            if (!value.is_object())
                throw UiError("ParseWorkspace", std::string(document) + " must be a JSON object");
            for (const auto& [key, unused] : value.items())
            {
                (void)unused;
                if (std::ranges::find(allowed, key) == allowed.end())
                    throw UiError("ParseWorkspace", std::string(document) + " contains unknown field '" + key + "'");
            }
        }

        [[nodiscard]] Json ParseDocument(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error)
                throw UiError("ReadWorkspace", "cannot inspect " + path.string() + ": " + error.message());
            if (size > MaximumDocumentBytes)
                throw UiError("ReadWorkspace", "document exceeds the 1 MiB safety limit: " + path.string());

            std::ifstream input(path, std::ios::binary);
            if (!input)
                throw UiError("ReadWorkspace", "cannot open " + path.string());
            bool duplicate = false;
            std::vector<std::unordered_set<std::string>> keys;
            const auto callback = [&duplicate, &keys](const int, const Json::parse_event_t event, Json& parsed)
            {
                if (event == Json::parse_event_t::object_start)
                    keys.emplace_back();
                else if (event == Json::parse_event_t::key && !keys.empty() &&
                         !keys.back().insert(parsed.get<std::string>()).second)
                    duplicate = true;
                else if (event == Json::parse_event_t::object_end && !keys.empty())
                    keys.pop_back();
                return true;
            };
            Json result;
            try
            {
                result = Json::parse(input, callback, true, true);
            }
            catch (const Json::exception& exception)
            {
                throw UiError("ParseWorkspace", path.string() + ": " + exception.what());
            }
            if (duplicate)
                throw UiError("ParseWorkspace", path.string() + " contains a duplicate object key");
            return result;
        }

        void AtomicWrite(const std::filesystem::path& path, const std::string& contents)
        {
            if (contents.size() > MaximumDocumentBytes)
                throw UiError("WriteWorkspace", "document exceeds the 1 MiB safety limit: " + path.string());
            try
            {
                Detail::WriteTextFileAtomically(path, contents);
            }
            catch (const std::exception& exception)
            {
                throw UiError("WriteWorkspace", exception.what());
            }
        }

        void EnsureExtension(std::filesystem::path& path, const std::string_view extension)
        {
            if (path.extension().empty())
                path += extension;
        }

        void SDLCALL DialogCompleted(void* userData, const char* const* files, int)
        {
            std::unique_ptr<DialogRequest> request(static_cast<DialogRequest*>(userData));
            const auto mailbox = request->Mailbox.lock();
            if (!mailbox)
                return;

            DialogResult result{request->Action, request->Id};
            if (!files)
            {
                const char* error = SDL_GetError();
                result.Error = error && *error ? error : "native file dialog failed without a diagnostic";
            }
            else if (*files)
            {
                result.Path = Utf8Path(*files);
            }

            std::lock_guard lock(mailbox->Mutex);
            if (mailbox->Alive)
            {
                mailbox->Result = std::move(result);
                mailbox->Active = false;
            }
        }
    } // namespace

    class UiLayoutBuilder::Impl final
    {
      public:
        struct SplitOperation
        {
            std::uint32_t Source = 0;
            std::uint32_t Near = 0;
            std::uint32_t Far = 0;
            UiDockDirection Direction = UiDockDirection::Left;
            float Ratio = 0.5F;
        };

        struct DockOperation
        {
            std::string PanelId;
            std::uint32_t Region = 0;
        };

        std::unordered_set<std::uint32_t> Leaves{1};
        std::vector<SplitOperation> Splits;
        std::vector<DockOperation> Docks;
        std::uint32_t NextRegion = 2;
    };

    UiLayoutBuilder::UiLayoutBuilder() : m_Impl(std::make_unique<Impl>()) {}
    UiLayoutBuilder::~UiLayoutBuilder() = default;
    UiDockRegion UiLayoutBuilder::Root() const noexcept { return UiDockRegion(1); }

    UiDockSplit UiLayoutBuilder::Split(const UiDockRegion region, const UiDockDirection direction, const float ratio)
    {
        if (!region || !std::isfinite(ratio) || ratio <= 0.05F || ratio >= 0.95F)
            throw std::invalid_argument("Dock splits require a live region and a ratio in the range 0.05..0.95.");
        if (m_Impl->Leaves.erase(region.m_Value) != 1)
            throw std::logic_error("A dock region may be split exactly once.");
        const std::uint32_t near = m_Impl->NextRegion++;
        const std::uint32_t far = m_Impl->NextRegion++;
        m_Impl->Leaves.insert(near);
        m_Impl->Leaves.insert(far);
        m_Impl->Splits.push_back({region.m_Value, near, far, direction, ratio});
        return {UiDockRegion(near), UiDockRegion(far)};
    }

    void UiLayoutBuilder::Dock(const std::string_view panelId, const UiDockRegion region)
    {
        if (panelId.empty())
            throw std::invalid_argument("Dock panel identifiers must not be empty.");
        if (!region || !m_Impl->Leaves.contains(region.m_Value))
            throw std::logic_error("Panels may only be docked into live leaf regions.");
        m_Impl->Docks.push_back({std::string(panelId), region.m_Value});
    }

    class UiWorkspace::Impl final
    {
      public:
        Impl(UiWorkspaceSpecification value, WindowSystem& windows, Window& window, const bool nativeDialogsEnabled)
            : Specification(std::move(value)), OwnerThread(std::this_thread::get_id()),
              Panels(std::make_shared<Detail::UiPanelRegistry>()), Mailbox(std::make_shared<DialogMailbox>()),
              NativeWindow(nativeDialogsEnabled ? WindowSystemInternalAccess::NativeWindow(windows, window.Id())
                                                : nullptr)
        {
            LayoutRecords.push_back({UiLayoutId(DefaultLayoutValue), "Default", true, false});
            ThemeRecords.push_back({UiThemeId(DarkThemeValue), "Kéire Dark", true, UiThemeDefinition{}});
            ThemeRecords.push_back({UiThemeId(LightThemeValue), "Kéire Light", true, LightTheme()});
            ThemeRecords.push_back({UiThemeId(ClassicThemeValue), "Classic", true, ClassicTheme()});

            if (!Specification.Ephemeral)
            {
                Root = ResolveRoot();
                LoadCatalog();
            }
            QueueActiveTheme();
            QueueInitialLayout();
        }

        ~Impl() = default;

        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != OwnerThread)
                throw std::logic_error(std::string("UiWorkspace::") + operation + " must run on the UI owner thread.");
        }

        [[nodiscard]] std::filesystem::path ResolveRoot() const
        {
            if (!Specification.DirectoryOverride.empty())
                return Specification.DirectoryOverride;
            return GetPreferenceDirectory() / "Editor" / "Workspace";
        }

        [[nodiscard]] LayoutRecord& RequireLayout(const UiLayoutId id)
        {
            const auto found = std::ranges::find(LayoutRecords, id, &LayoutRecord::Id);
            if (found == LayoutRecords.end())
                throw std::invalid_argument("Unknown UI layout identifier.");
            return *found;
        }

        [[nodiscard]] const LayoutRecord& RequireLayout(const UiLayoutId id) const
        {
            const auto found = std::ranges::find(LayoutRecords, id, &LayoutRecord::Id);
            if (found == LayoutRecords.end())
                throw std::invalid_argument("Unknown UI layout identifier.");
            return *found;
        }

        [[nodiscard]] ThemeRecord& RequireTheme(const UiThemeId id)
        {
            const auto found = std::ranges::find(ThemeRecords, id, &ThemeRecord::Id);
            if (found == ThemeRecords.end())
                throw std::invalid_argument("Unknown UI theme identifier.");
            return *found;
        }

        [[nodiscard]] const ThemeRecord& RequireTheme(const UiThemeId id) const
        {
            const auto found = std::ranges::find(ThemeRecords, id, &ThemeRecord::Id);
            if (found == ThemeRecords.end())
                throw std::invalid_argument("Unknown UI theme identifier.");
            return *found;
        }

        [[nodiscard]] bool NameExists(const std::string_view name, const bool theme,
                                      const std::uint64_t except = 0) const
        {
            if (theme)
            {
                return std::ranges::any_of(ThemeRecords, [name, except](const ThemeRecord& record)
                                           { return record.Id.Value() != except && record.Name == name; });
            }
            return std::ranges::any_of(LayoutRecords, [name, except](const LayoutRecord& record)
                                       { return record.Id.Value() != except && record.Name == name; });
        }

        void ValidateUniqueName(const std::string_view name, const bool theme, const std::uint64_t except = 0) const
        {
            ValidateProfileName(name);
            if (NameExists(name, theme, except))
                throw std::invalid_argument("A UI profile with that exact name already exists.");
        }

        [[nodiscard]] std::filesystem::path LayoutPath(const UiLayoutId id) const
        {
            return Root / "Layouts" / (std::to_string(id.Value()) + ".keirelayout");
        }

        [[nodiscard]] std::filesystem::path ThemePath(const UiThemeId id) const
        {
            return Root / "Themes" / (std::to_string(id.Value()) + ".keiretheme");
        }

        [[nodiscard]] Json LayoutDocument(const std::string_view name) const
        {
            std::size_t iniSize = 0;
            const char* ini = ImGui::SaveIniSettingsToMemory(&iniSize);
            Json visibility = Json::object();
            for (const auto& [id, visible] : Panels->Visibility)
                visibility[id] = visible;
            return {{"schemaVersion", WorkspaceSchemaVersion},
                    {"kind", "layout"},
                    {"name", name},
                    {"backendVersion", IMGUI_VERSION},
                    {"docking", std::string(ini, iniSize)},
                    {"panels", std::move(visibility)}};
        }

        [[nodiscard]] static Json ThemeDocument(const ThemeRecord& theme)
        {
            const auto& value = theme.Definition;
            return {{"schemaVersion", WorkspaceSchemaVersion},
                    {"kind", "theme"},
                    {"name", theme.Name},
                    {"colors",
                     {{"canvas", ColorToJson(value.Canvas)},
                      {"panel", ColorToJson(value.Panel)},
                      {"raisedPanel", ColorToJson(value.RaisedPanel)},
                      {"border", ColorToJson(value.Border)},
                      {"text", ColorToJson(value.Text)},
                      {"mutedText", ColorToJson(value.MutedText)},
                      {"accent", ColorToJson(value.Accent)},
                      {"accentHovered", ColorToJson(value.AccentHovered)},
                      {"accentActive", ColorToJson(value.AccentActive)},
                      {"selection", ColorToJson(value.Selection)},
                      {"success", ColorToJson(value.Success)},
                      {"warning", ColorToJson(value.Warning)},
                      {"error", ColorToJson(value.Error)}}},
                    {"metrics",
                     {{"windowPadding", {value.WindowPadding.Width, value.WindowPadding.Height}},
                      {"framePadding", {value.FramePadding.Width, value.FramePadding.Height}},
                      {"itemSpacing", {value.ItemSpacing.Width, value.ItemSpacing.Height}},
                      {"windowRounding", value.WindowRounding},
                      {"frameRounding", value.FrameRounding},
                      {"tabRounding", value.TabRounding},
                      {"scrollbarRounding", value.ScrollbarRounding},
                      {"windowBorderSize", value.WindowBorderSize},
                      {"frameBorderSize", value.FrameBorderSize}}}};
        }

        [[nodiscard]] static UiThemeDefinition ParseThemeDocument(const Json& document, std::string* name)
        {
            RequireKeys(document, {"schemaVersion", "kind", "name", "colors", "metrics"}, "theme document");
            if (document.value("schemaVersion", 0U) != WorkspaceSchemaVersion || document.value("kind", "") != "theme")
                throw UiError("ParseTheme", "unsupported theme document identity or schema version");
            *name = document.at("name").get<std::string>();
            ValidateProfileName(*name);
            const auto& colors = document.at("colors");
            RequireKeys(colors,
                        {"canvas", "panel", "raisedPanel", "border", "text", "mutedText", "accent", "accentHovered",
                         "accentActive", "selection", "success", "warning", "error"},
                        "theme colors");
            const auto& metrics = document.at("metrics");
            RequireKeys(metrics,
                        {"windowPadding", "framePadding", "itemSpacing", "windowRounding", "frameRounding",
                         "tabRounding", "scrollbarRounding", "windowBorderSize", "frameBorderSize"},
                        "theme metrics");
            UiThemeDefinition result;
            result.Canvas = ColorFromJson(colors.at("canvas"), "canvas");
            result.Panel = ColorFromJson(colors.at("panel"), "panel");
            result.RaisedPanel = ColorFromJson(colors.at("raisedPanel"), "raisedPanel");
            result.Border = ColorFromJson(colors.at("border"), "border");
            result.Text = ColorFromJson(colors.at("text"), "text");
            result.MutedText = ColorFromJson(colors.at("mutedText"), "mutedText");
            result.Accent = ColorFromJson(colors.at("accent"), "accent");
            result.AccentHovered = ColorFromJson(colors.at("accentHovered"), "accentHovered");
            result.AccentActive = ColorFromJson(colors.at("accentActive"), "accentActive");
            result.Selection = ColorFromJson(colors.at("selection"), "selection");
            result.Success = ColorFromJson(colors.at("success"), "success");
            result.Warning = ColorFromJson(colors.at("warning"), "warning");
            result.Error = ColorFromJson(colors.at("error"), "error");
            const auto parseSize = [](const Json& value, const char* field)
            {
                if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number())
                    throw UiError("ParseTheme", std::string(field) + " must be an array of two numbers");
                return UiSize{value[0].get<float>(), value[1].get<float>()};
            };
            result.WindowPadding = parseSize(metrics.at("windowPadding"), "windowPadding");
            result.FramePadding = parseSize(metrics.at("framePadding"), "framePadding");
            result.ItemSpacing = parseSize(metrics.at("itemSpacing"), "itemSpacing");
            result.WindowRounding = metrics.at("windowRounding").get<float>();
            result.FrameRounding = metrics.at("frameRounding").get<float>();
            result.TabRounding = metrics.at("tabRounding").get<float>();
            result.ScrollbarRounding = metrics.at("scrollbarRounding").get<float>();
            result.WindowBorderSize = metrics.at("windowBorderSize").get<float>();
            result.FrameBorderSize = metrics.at("frameBorderSize").get<float>();
            ValidateTheme(result);
            return result;
        }

        [[nodiscard]] static std::pair<std::string, std::string> ParseLayoutDocument(const Json& document,
                                                                                     Detail::UiPanelRegistry& panels)
        {
            RequireKeys(document, {"schemaVersion", "kind", "name", "backendVersion", "docking", "panels"},
                        "layout document");
            if (document.value("schemaVersion", 0U) != WorkspaceSchemaVersion || document.value("kind", "") != "layout")
                throw UiError("ParseLayout", "unsupported layout document identity or schema version");
            auto name = document.at("name").get<std::string>();
            ValidateProfileName(name);
            const auto docking = document.at("docking").get<std::string>();
            if (docking.size() > MaximumDocumentBytes)
                throw UiError("ParseLayout", "docking payload exceeds the 1 MiB safety limit");
            const auto& visibility = document.at("panels");
            if (!visibility.is_object())
                throw UiError("ParseLayout", "panels must be a JSON object");
            for (const auto& [id, value] : visibility.items())
            {
                if (id.empty() || id.size() > 128 || !value.is_boolean())
                    throw UiError("ParseLayout", "panel visibility contains an invalid entry");
                panels.Visibility[id] = value.get<bool>();
                if (panels.Panels.contains(id))
                    panels.Panels.at(id).Visible = value.get<bool>();
            }
            return {std::move(name), docking};
        }

        void ApplyVisibility(const Detail::UiPanelRegistry& source)
        {
            for (const auto& [id, visible] : source.Visibility)
            {
                Panels->Visibility[id] = visible;
                if (Panels->Panels.contains(id))
                    Panels->Panels.at(id).Visible = visible;
            }
        }

        void WriteCatalog() const
        {
            if (Specification.Ephemeral)
                return;
            Json layouts = Json::array();
            for (const auto& record : LayoutRecords)
            {
                if (!record.BuiltIn)
                    layouts.push_back({{"id", record.Id.Value()}, {"name", record.Name}});
            }
            Json themes = Json::array();
            for (const auto& record : ThemeRecords)
            {
                if (!record.BuiltIn)
                    themes.push_back({{"id", record.Id.Value()}, {"name", record.Name}});
            }
            const Json catalog{{"schemaVersion", WorkspaceSchemaVersion},
                               {"nextLayoutId", NextLayoutId},
                               {"nextThemeId", NextThemeId},
                               {"activeLayout", ActiveLayoutId.Value()},
                               {"activeTheme", ActiveThemeId.Value()},
                               {"defaultModified", LayoutRecords.front().Modified},
                               {"layouts", std::move(layouts)},
                               {"themes", std::move(themes)}};
            AtomicWrite(Root / "catalog.json", catalog.dump(2));
        }

        void LoadCatalog()
        {
            const auto path = Root / "catalog.json";
            if (!std::filesystem::exists(path))
                return;
            try
            {
                const Json catalog = ParseDocument(path);
                RequireKeys(catalog,
                            {"schemaVersion", "nextLayoutId", "nextThemeId", "activeLayout", "activeTheme",
                             "defaultModified", "layouts", "themes"},
                            "workspace catalog");
                if (catalog.value("schemaVersion", 0U) != WorkspaceSchemaVersion)
                    throw UiError("ParseCatalog", "unsupported workspace catalog schema version");
                NextLayoutId = std::max(catalog.at("nextLayoutId").get<std::uint64_t>(), FirstUserValue);
                NextThemeId = std::max(catalog.at("nextThemeId").get<std::uint64_t>(), FirstUserValue);
                LayoutRecords.front().Modified = catalog.at("defaultModified").get<bool>();
                for (const auto& item : catalog.at("layouts"))
                {
                    RequireKeys(item, {"id", "name"}, "layout catalog entry");
                    const auto id = item.at("id").get<std::uint64_t>();
                    const auto name = item.at("name").get<std::string>();
                    if (id < FirstUserValue || !ValidProfileName(name) || NameExists(name, false) ||
                        !std::filesystem::exists(LayoutPath(UiLayoutId(id))))
                        throw UiError("ParseCatalog", "invalid or missing layout catalog entry");
                    LayoutRecords.push_back({UiLayoutId(id), name, false, false});
                }
                for (const auto& item : catalog.at("themes"))
                {
                    RequireKeys(item, {"id", "name"}, "theme catalog entry");
                    const auto id = item.at("id").get<std::uint64_t>();
                    const auto name = item.at("name").get<std::string>();
                    if (id < FirstUserValue || !ValidProfileName(name) || NameExists(name, true))
                        throw UiError("ParseCatalog", "invalid theme catalog entry");
                    std::string documentName;
                    const auto definition = ParseThemeDocument(ParseDocument(ThemePath(UiThemeId(id))), &documentName);
                    if (documentName != name)
                        throw UiError("ParseCatalog", "theme name does not match its catalog entry");
                    ThemeRecords.push_back({UiThemeId(id), name, false, definition});
                }
                const UiLayoutId layout(catalog.at("activeLayout").get<std::uint64_t>());
                const UiThemeId theme(catalog.at("activeTheme").get<std::uint64_t>());
                ActiveLayoutId = std::ranges::find(LayoutRecords, layout, &LayoutRecord::Id) != LayoutRecords.end()
                                     ? layout
                                     : UiLayoutId(DefaultLayoutValue);
                ActiveThemeId = std::ranges::find(ThemeRecords, theme, &ThemeRecord::Id) != ThemeRecords.end()
                                    ? theme
                                    : UiThemeId(DarkThemeValue);
            }
            catch (const std::exception& error)
            {
                LayoutRecords.erase(LayoutRecords.begin() + 1, LayoutRecords.end());
                ThemeRecords.erase(ThemeRecords.begin() + 3, ThemeRecords.end());
                ActiveLayoutId = UiLayoutId(DefaultLayoutValue);
                ActiveThemeId = UiThemeId(DarkThemeValue);
                NextLayoutId = FirstUserValue;
                NextThemeId = FirstUserValue;
                Notices.push_back({UiWorkspaceNoticeSeverity::Warning,
                                   std::string("Workspace catalog was ignored: ") + error.what()});
            }
        }

        void QueueActiveTheme() { PendingTheme = RequireTheme(ActiveThemeId).Definition; }

        void QueueInitialLayout()
        {
            if (Specification.Ephemeral)
            {
                FactoryPending = true;
                return;
            }
            const auto session = Root / "session.keirelayout";
            if (std::filesystem::exists(session))
            {
                try
                {
                    Detail::UiPanelRegistry parsed;
                    auto [unused, docking] = ParseLayoutDocument(ParseDocument(session), parsed);
                    (void)unused;
                    ApplyVisibility(parsed);
                    PendingIni = std::move(docking);
                    return;
                }
                catch (const std::exception& error)
                {
                    Notices.push_back({UiWorkspaceNoticeSeverity::Warning,
                                       std::string("The saved workspace session was ignored: ") + error.what()});
                }
            }
            try
            {
                LoadLayoutInternal(ActiveLayoutId);
            }
            catch (const std::exception& error)
            {
                ActiveLayoutId = UiLayoutId(DefaultLayoutValue);
                FactoryPending = true;
                Notices.push_back({UiWorkspaceNoticeSeverity::Warning,
                                   std::string("The active layout was ignored: ") + error.what()});
            }
        }

        void LoadLayoutInternal(const UiLayoutId id)
        {
            const auto& record = RequireLayout(id);
            if (record.BuiltIn)
            {
                ActiveLayoutId = id;
                FactoryPending = true;
                PendingIni.reset();
                Panels->Visibility.clear();
                for (auto& [unused, panel] : Panels->Panels)
                {
                    (void)unused;
                    panel.Visible = panel.DefaultVisible;
                    Panels->Visibility[panel.Id] = panel.Visible;
                }
                LayoutRecords.front().Modified = false;
            }
            else
            {
                Detail::UiPanelRegistry parsed;
                auto [unused, docking] = ParseLayoutDocument(ParseDocument(LayoutPath(id)), parsed);
                (void)unused;
                ActiveLayoutId = id;
                ApplyVisibility(parsed);
                PendingIni = std::move(docking);
                FactoryPending = false;
            }
            WriteCatalog();
        }

        void SaveLiveLayout()
        {
            if (Specification.Ephemeral)
                return;
            const auto& active = RequireLayout(ActiveLayoutId);
            const auto document = LayoutDocument(active.Name).dump(2);
            AtomicWrite(Root / "session.keirelayout", document);
            if (!active.BuiltIn)
                AtomicWrite(LayoutPath(active.Id), document);
            else
                LayoutRecords.front().Modified = true;
            WriteCatalog();
        }

        void ApplyFactory(const UiLayoutBuilder::Impl& recipe, const UiSize viewportSize)
        {
            ImGuiID root = ImHashStr("Keire.RootDockSpace");
            ImGui::DockBuilderRemoveNode(root);
            ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(root, {viewportSize.Width, viewportSize.Height});
            std::unordered_map<std::uint32_t, ImGuiID> regions{{1, root}};
            for (const auto& operation : recipe.Splits)
            {
                auto source = regions.at(operation.Source);
                ImGuiDir direction = ImGuiDir_Left;
                switch (operation.Direction)
                {
                case UiDockDirection::Right:
                    direction = ImGuiDir_Right;
                    break;
                case UiDockDirection::Up:
                    direction = ImGuiDir_Up;
                    break;
                case UiDockDirection::Down:
                    direction = ImGuiDir_Down;
                    break;
                case UiDockDirection::Left:
                default:
                    break;
                }
                ImGuiID near = 0;
                ImGuiID far = source;
                ImGui::DockBuilderSplitNode(source, direction, operation.Ratio, &near, &far);
                regions[operation.Near] = near;
                regions[operation.Far] = far;
            }
            for (const auto& operation : recipe.Docks)
            {
                const auto found = Panels->Panels.find(operation.PanelId);
                const std::string fallback = "###" + operation.PanelId;
                ImGui::DockBuilderDockWindow(found != Panels->Panels.end() ? found->second.SubmittedName.c_str()
                                                                           : fallback.c_str(),
                                             regions.at(operation.Region));
            }
            ImGui::DockBuilderFinish(root);
            FactoryPending = false;
            IgnoreNextSaveSignal = true;
        }

        static void ScaleDockTree(ImGuiDockNode& node, const ImVec2 scale) noexcept
        {
            node.Size.x *= scale.x;
            node.Size.y *= scale.y;
            node.SizeRef.x *= scale.x;
            node.SizeRef.y *= scale.y;
            for (auto* child : node.ChildNodes)
            {
                if (child)
                    ScaleDockTree(*child, scale);
            }
        }

        [[nodiscard]] ImVec2 CurrentDockspaceSize(const UiSize fallback) const noexcept
        {
            ImVec2 size{std::max(fallback.Width, 1.0F), std::max(fallback.Height, 1.0F)};
            const auto* viewport = ImGui::GetMainViewport();
            if (LastDockspaceSize && viewport && std::isfinite(viewport->WorkSize.x) &&
                std::isfinite(viewport->WorkSize.y) && viewport->WorkSize.x > 0.0F && viewport->WorkSize.y > 0.0F)
            {
                size = viewport->WorkSize;
            }
            return size;
        }

        void ResizeDockTree(const ImVec2 targetSize)
        {
            auto* root = ImGui::DockBuilderGetNode(ImHashStr("Keire.RootDockSpace"));
            if (!root)
            {
                LastDockspaceSize = targetSize;
                return;
            }

            ImVec2 previousSize = LastDockspaceSize.value_or(root->Size);
            if (previousSize.x <= 0.0F || previousSize.y <= 0.0F)
                previousSize = root->SizeRef;
            if (!std::isfinite(previousSize.x) || !std::isfinite(previousSize.y) || previousSize.x <= 0.0F ||
                previousSize.y <= 0.0F)
            {
                LastDockspaceSize = targetSize;
                return;
            }

            if (std::abs(previousSize.x - targetSize.x) < 0.5F && std::abs(previousSize.y - targetSize.y) < 0.5F)
            {
                LastDockspaceSize = targetSize;
                return;
            }

            ScaleDockTree(*root, {targetSize.x / previousSize.x, targetSize.y / previousSize.y});
            LastDockspaceSize = targetSize;
            DockTreeResizePendingSave = true;
            ImGui::MarkIniSettingsDirty();
        }

        void StartDialog(const DialogAction action, const std::uint64_t id, const bool save)
        {
            if (!NativeWindow)
                throw UiError("ShowFileDialog", "native file dialogs require a live rendered window");
            {
                std::lock_guard lock(Mailbox->Mutex);
                if (Mailbox->Active)
                    throw std::logic_error("A workspace file dialog is already open.");
                Mailbox->Active = true;
            }
            auto request = std::make_unique<DialogRequest>();
            request->Mailbox = Mailbox;
            request->Action = action;
            request->Id = id;
            static constexpr SDL_DialogFileFilter layoutFilter{"Kéire layout", "keirelayout"};
            static constexpr SDL_DialogFileFilter themeFilter{"Kéire theme", "keiretheme"};
            const auto* filter = action == DialogAction::ImportLayout || action == DialogAction::ExportLayout
                                     ? &layoutFilter
                                     : &themeFilter;
            if (save)
                SDL_ShowSaveFileDialog(DialogCompleted, request.release(), NativeWindow, filter, 1, nullptr);
            else
                SDL_ShowOpenFileDialog(DialogCompleted, request.release(), NativeWindow, filter, 1, nullptr, false);
        }

        void DrainDialog()
        {
            std::optional<DialogResult> result;
            {
                std::lock_guard lock(Mailbox->Mutex);
                result = std::move(Mailbox->Result);
                Mailbox->Result.reset();
            }
            if (!result)
                return;
            if (!result->Error.empty())
            {
                Notices.push_back({UiWorkspaceNoticeSeverity::Error, "File dialog failed: " + result->Error});
                return;
            }
            if (!result->Path)
                return;
            try
            {
                switch (result->Action)
                {
                case DialogAction::ImportLayout:
                    ImportLayout(*result->Path);
                    break;
                case DialogAction::ExportLayout:
                    ExportLayout(UiLayoutId(result->Id), *result->Path);
                    break;
                case DialogAction::ImportTheme:
                    ImportTheme(*result->Path);
                    break;
                case DialogAction::ExportTheme:
                    ExportTheme(UiThemeId(result->Id), *result->Path);
                    break;
                }
            }
            catch (const std::exception& error)
            {
                Notices.push_back({UiWorkspaceNoticeSeverity::Error, error.what()});
            }
        }

        void ImportLayout(const std::filesystem::path& path)
        {
            const auto document = ParseDocument(path);
            Detail::UiPanelRegistry parsed;
            auto [name, docking] = ParseLayoutDocument(document, parsed);
            ValidateUniqueName(name, false);
            const UiLayoutId id(NextLayoutId++);
            LayoutRecords.push_back({id, name, false, false});
            AtomicWrite(LayoutPath(id), document.dump(2));
            ApplyVisibility(parsed);
            PendingIni = std::move(docking);
            ActiveLayoutId = id;
            WriteCatalog();
            Notices.push_back({UiWorkspaceNoticeSeverity::Information, "Imported layout '" + name + "'."});
        }

        void ExportLayout(const UiLayoutId id, std::filesystem::path path) const
        {
            const auto& record = RequireLayout(id);
            EnsureExtension(path, ".keirelayout");
            if (id == ActiveLayoutId)
                AtomicWrite(path, LayoutDocument(record.Name).dump(2));
            else if (record.BuiltIn)
                throw std::invalid_argument("Load the Default layout before exporting its live state.");
            else
                AtomicWrite(path, ParseDocument(LayoutPath(id)).dump(2));
        }

        void ImportTheme(const std::filesystem::path& path)
        {
            std::string name;
            const auto definition = ParseThemeDocument(ParseDocument(path), &name);
            ValidateUniqueName(name, true);
            const UiThemeId id(NextThemeId++);
            ThemeRecords.push_back({id, name, false, definition});
            AtomicWrite(ThemePath(id), ThemeDocument(ThemeRecords.back()).dump(2));
            ActiveThemeId = id;
            PendingTheme = definition;
            WriteCatalog();
            Notices.push_back({UiWorkspaceNoticeSeverity::Information, "Imported theme '" + name + "'."});
        }

        void ExportTheme(const UiThemeId id, std::filesystem::path path) const
        {
            const auto& record = RequireTheme(id);
            EnsureExtension(path, ".keiretheme");
            AtomicWrite(path, ThemeDocument(record).dump(2));
        }

        UiWorkspaceSpecification Specification;
        std::thread::id OwnerThread;
        std::filesystem::path Root;
        std::shared_ptr<Detail::UiPanelRegistry> Panels;
        std::shared_ptr<DialogMailbox> Mailbox;
        std::vector<LayoutRecord> LayoutRecords;
        std::vector<ThemeRecord> ThemeRecords;
        std::deque<UiWorkspaceNotice> Notices;
        UiLayoutId ActiveLayoutId{DefaultLayoutValue};
        UiThemeId ActiveThemeId{DarkThemeValue};
        std::uint64_t NextLayoutId = FirstUserValue;
        std::uint64_t NextThemeId = FirstUserValue;
        std::optional<ImVec2> LastDockspaceSize;
        bool DockTreeResizePendingSave = false;
        std::optional<std::string> PendingIni;
        std::optional<UiThemeDefinition> PendingTheme;
        SDL_Window* NativeWindow = nullptr;
        bool FactoryPending = false;
        bool IgnoreNextSaveSignal = false;
        bool ShutdownComplete = false;
    };

    UiWorkspace::UiWorkspace(UiWorkspaceSpecification specification, WindowSystem& windows, Window& window,
                             const bool nativeDialogsEnabled)
        : m_Impl(std::make_unique<Impl>(std::move(specification), windows, window, nativeDialogsEnabled))
    {
    }

    UiWorkspace::~UiWorkspace() { Shutdown(); }

    std::vector<UiLayoutInfo> UiWorkspace::Layouts() const
    {
        m_Impl->RequireOwner("Layouts");
        std::vector<UiLayoutInfo> result;
        result.reserve(m_Impl->LayoutRecords.size());
        for (const auto& record : m_Impl->LayoutRecords)
        {
            result.push_back(
                {record.Id, record.Name, record.BuiltIn, record.Id == m_Impl->ActiveLayoutId, record.Modified});
        }
        return result;
    }

    std::vector<UiThemeInfo> UiWorkspace::Themes() const
    {
        m_Impl->RequireOwner("Themes");
        std::vector<UiThemeInfo> result;
        result.reserve(m_Impl->ThemeRecords.size());
        for (const auto& record : m_Impl->ThemeRecords)
            result.push_back({record.Id, record.Name, record.BuiltIn, record.Id == m_Impl->ActiveThemeId});
        return result;
    }

    UiLayoutId UiWorkspace::ActiveLayout() const noexcept { return m_Impl->ActiveLayoutId; }
    UiThemeId UiWorkspace::ActiveTheme() const noexcept { return m_Impl->ActiveThemeId; }

    UiThemeDefinition UiWorkspace::ThemeDefinition(const UiThemeId id) const
    {
        m_Impl->RequireOwner("ThemeDefinition");
        return m_Impl->RequireTheme(id).Definition;
    }

    UiPanelRegistration UiWorkspace::RegisterPanel(const UiPanelSpecification& specification)
    {
        m_Impl->RequireOwner("RegisterPanel");
        ValidateProfileName(specification.Id);
        ValidateProfileName(specification.Title);
        if (m_Impl->Panels->Panels.contains(specification.Id))
            throw std::invalid_argument("A UI panel with that stable identifier is already registered.");
        const bool visible = m_Impl->Panels->Visibility.contains(specification.Id)
                                 ? m_Impl->Panels->Visibility.at(specification.Id)
                                 : specification.DefaultVisible;
        m_Impl->Panels->Visibility[specification.Id] = visible;
        const auto id = specification.Id;
        m_Impl->Panels->Panels.emplace(id,
                                       Detail::UiPanelRecord{id, specification.Title, specification.Title + "###" + id,
                                                             visible, specification.DefaultVisible});
        return UiPanelRegistration(std::make_unique<UiPanelRegistration::Impl>(m_Impl->Panels, id));
    }

    void UiWorkspace::LoadLayout(const UiLayoutId id)
    {
        m_Impl->RequireOwner("LoadLayout");
        m_Impl->LoadLayoutInternal(id);
    }

    void UiWorkspace::SaveLayoutAs(std::string name)
    {
        m_Impl->RequireOwner("SaveLayoutAs");
        m_Impl->ValidateUniqueName(name, false);
        const UiLayoutId id(m_Impl->NextLayoutId++);
        m_Impl->LayoutRecords.push_back({id, std::move(name), false, false});
        m_Impl->ActiveLayoutId = id;
        if (!m_Impl->Specification.Ephemeral)
        {
            const auto document = m_Impl->LayoutDocument(m_Impl->LayoutRecords.back().Name).dump(2);
            AtomicWrite(m_Impl->LayoutPath(id), document);
            AtomicWrite(m_Impl->Root / "session.keirelayout", document);
            m_Impl->WriteCatalog();
        }
    }

    void UiWorkspace::RenameLayout(const UiLayoutId id, std::string name)
    {
        m_Impl->RequireOwner("RenameLayout");
        auto& record = m_Impl->RequireLayout(id);
        if (record.BuiltIn)
            throw std::invalid_argument("Built-in UI layouts cannot be renamed.");
        m_Impl->ValidateUniqueName(name, false, id.Value());
        record.Name = std::move(name);
        if (!m_Impl->Specification.Ephemeral)
        {
            Json document = ParseDocument(m_Impl->LayoutPath(id));
            document["name"] = record.Name;
            AtomicWrite(m_Impl->LayoutPath(id), document.dump(2));
            m_Impl->WriteCatalog();
        }
    }

    void UiWorkspace::DeleteLayout(const UiLayoutId id)
    {
        m_Impl->RequireOwner("DeleteLayout");
        const auto found = std::ranges::find(m_Impl->LayoutRecords, id, &LayoutRecord::Id);
        if (found == m_Impl->LayoutRecords.end())
            throw std::invalid_argument("Unknown UI layout identifier.");
        if (found->BuiltIn)
            throw std::invalid_argument("Built-in UI layouts cannot be deleted.");
        if (!m_Impl->Specification.Ephemeral)
        {
            std::error_code error;
            std::filesystem::remove(m_Impl->LayoutPath(id), error);
            if (error)
                throw UiError("DeleteLayout", error.message());
        }
        const bool active = id == m_Impl->ActiveLayoutId;
        m_Impl->LayoutRecords.erase(found);
        if (active)
            m_Impl->LoadLayoutInternal(UiLayoutId(DefaultLayoutValue));
        else
            m_Impl->WriteCatalog();
    }

    void UiWorkspace::ResetFactoryLayout()
    {
        m_Impl->RequireOwner("ResetFactoryLayout");
        m_Impl->LoadLayoutInternal(UiLayoutId(DefaultLayoutValue));
    }
    void UiWorkspace::ImportLayout(const std::filesystem::path& path)
    {
        m_Impl->RequireOwner("ImportLayout");
        m_Impl->ImportLayout(path);
    }
    void UiWorkspace::ExportLayout(const UiLayoutId id, const std::filesystem::path& path)
    {
        m_Impl->RequireOwner("ExportLayout");
        m_Impl->ExportLayout(id, path);
    }
    void UiWorkspace::ShowImportLayoutDialog()
    {
        m_Impl->RequireOwner("ShowImportLayoutDialog");
        m_Impl->StartDialog(DialogAction::ImportLayout, 0, false);
    }
    void UiWorkspace::ShowExportLayoutDialog(const UiLayoutId id)
    {
        m_Impl->RequireOwner("ShowExportLayoutDialog");
        (void)m_Impl->RequireLayout(id);
        m_Impl->StartDialog(DialogAction::ExportLayout, id.Value(), true);
    }

    void UiWorkspace::ApplyTheme(const UiThemeId id)
    {
        m_Impl->RequireOwner("ApplyTheme");
        m_Impl->ActiveThemeId = m_Impl->RequireTheme(id).Id;
        m_Impl->PendingTheme = m_Impl->RequireTheme(id).Definition;
        m_Impl->WriteCatalog();
    }

    void UiWorkspace::PreviewTheme(UiThemeDefinition definition)
    {
        m_Impl->RequireOwner("PreviewTheme");
        ValidateTheme(definition);
        m_Impl->PendingTheme = definition;
    }

    void UiWorkspace::CancelThemePreview()
    {
        m_Impl->RequireOwner("CancelThemePreview");
        m_Impl->PendingTheme = m_Impl->RequireTheme(m_Impl->ActiveThemeId).Definition;
    }

    UiThemeId UiWorkspace::SaveThemeAs(std::string name, UiThemeDefinition definition)
    {
        m_Impl->RequireOwner("SaveThemeAs");
        m_Impl->ValidateUniqueName(name, true);
        ValidateTheme(definition);
        const UiThemeId id(m_Impl->NextThemeId++);
        m_Impl->ThemeRecords.push_back({id, std::move(name), false, definition});
        m_Impl->ActiveThemeId = id;
        m_Impl->PendingTheme = m_Impl->ThemeRecords.back().Definition;
        if (!m_Impl->Specification.Ephemeral)
        {
            AtomicWrite(m_Impl->ThemePath(id), Impl::ThemeDocument(m_Impl->ThemeRecords.back()).dump(2));
            m_Impl->WriteCatalog();
        }
        return id;
    }

    void UiWorkspace::UpdateTheme(const UiThemeId id, UiThemeDefinition definition)
    {
        m_Impl->RequireOwner("UpdateTheme");
        auto& record = m_Impl->RequireTheme(id);
        if (record.BuiltIn)
            throw std::invalid_argument("Built-in UI themes cannot be overwritten.");
        ValidateTheme(definition);
        record.Definition = definition;
        if (id == m_Impl->ActiveThemeId)
            m_Impl->PendingTheme = record.Definition;
        if (!m_Impl->Specification.Ephemeral)
            AtomicWrite(m_Impl->ThemePath(id), Impl::ThemeDocument(record).dump(2));
    }

    void UiWorkspace::RenameTheme(const UiThemeId id, std::string name)
    {
        m_Impl->RequireOwner("RenameTheme");
        auto& record = m_Impl->RequireTheme(id);
        if (record.BuiltIn)
            throw std::invalid_argument("Built-in UI themes cannot be renamed.");
        m_Impl->ValidateUniqueName(name, true, id.Value());
        record.Name = std::move(name);
        if (!m_Impl->Specification.Ephemeral)
        {
            AtomicWrite(m_Impl->ThemePath(id), Impl::ThemeDocument(record).dump(2));
            m_Impl->WriteCatalog();
        }
    }

    void UiWorkspace::DeleteTheme(const UiThemeId id)
    {
        m_Impl->RequireOwner("DeleteTheme");
        const auto found = std::ranges::find(m_Impl->ThemeRecords, id, &ThemeRecord::Id);
        if (found == m_Impl->ThemeRecords.end())
            throw std::invalid_argument("Unknown UI theme identifier.");
        if (found->BuiltIn)
            throw std::invalid_argument("Built-in UI themes cannot be deleted.");
        if (!m_Impl->Specification.Ephemeral)
        {
            std::error_code error;
            std::filesystem::remove(m_Impl->ThemePath(id), error);
            if (error)
                throw UiError("DeleteTheme", error.message());
        }
        const bool active = id == m_Impl->ActiveThemeId;
        m_Impl->ThemeRecords.erase(found);
        if (active)
            ApplyTheme(UiThemeId(DarkThemeValue));
        else
            m_Impl->WriteCatalog();
    }

    void UiWorkspace::ImportTheme(const std::filesystem::path& path)
    {
        m_Impl->RequireOwner("ImportTheme");
        m_Impl->ImportTheme(path);
    }
    void UiWorkspace::ExportTheme(const UiThemeId id, const std::filesystem::path& path)
    {
        m_Impl->RequireOwner("ExportTheme");
        m_Impl->ExportTheme(id, path);
    }
    void UiWorkspace::ShowImportThemeDialog()
    {
        m_Impl->RequireOwner("ShowImportThemeDialog");
        m_Impl->StartDialog(DialogAction::ImportTheme, 0, false);
    }
    void UiWorkspace::ShowExportThemeDialog(const UiThemeId id)
    {
        m_Impl->RequireOwner("ShowExportThemeDialog");
        (void)m_Impl->RequireTheme(id);
        m_Impl->StartDialog(DialogAction::ExportTheme, id.Value(), true);
    }

    std::optional<UiWorkspaceNotice> UiWorkspace::ConsumeNotice()
    {
        m_Impl->RequireOwner("ConsumeNotice");
        if (m_Impl->Notices.empty())
            return std::nullopt;
        auto result = std::move(m_Impl->Notices.front());
        m_Impl->Notices.pop_front();
        return result;
    }

    void UiWorkspace::BeforeNewFrame()
    {
        m_Impl->DrainDialog();
        if (m_Impl->PendingTheme)
        {
            ApplyThemeDefinition(*m_Impl->PendingTheme);
            m_Impl->PendingTheme.reset();
        }
        if (m_Impl->PendingIni)
        {
            ImGui::ClearIniSettings();
            ImGui::LoadIniSettingsFromMemory(m_Impl->PendingIni->data(), m_Impl->PendingIni->size());
            m_Impl->PendingIni.reset();
            m_Impl->LastDockspaceSize.reset();
            m_Impl->IgnoreNextSaveSignal = true;
        }
    }

    void UiWorkspace::AfterNewFrame(const UiSize viewportSize)
    {
        const ImVec2 dockspaceSize = m_Impl->CurrentDockspaceSize(viewportSize);
        if (!m_Impl->FactoryPending)
        {
            m_Impl->ResizeDockTree(dockspaceSize);
            return;
        }
        ImGui::ClearIniSettings();
        UiLayoutBuilder builder;
        if (m_Impl->Specification.BuildFactoryLayout)
            m_Impl->Specification.BuildFactoryLayout(builder);
        m_Impl->ApplyFactory(*builder.m_Impl, {dockspaceSize.x, dockspaceSize.y});
        m_Impl->LastDockspaceSize = dockspaceSize;
    }

    void UiWorkspace::AfterFrame()
    {
        auto& io = ImGui::GetIO();
        const bool backendRequested = io.WantSaveIniSettings;
        const bool visibilityRequested = m_Impl->Panels->VisibilityDirty;
        const bool requested = backendRequested || visibilityRequested;
        io.WantSaveIniSettings = false;
        m_Impl->Panels->VisibilityDirty = false;
        if (!requested)
            return;
        if (m_Impl->IgnoreNextSaveSignal)
        {
            m_Impl->IgnoreNextSaveSignal = false;
            if (!visibilityRequested && !m_Impl->DockTreeResizePendingSave)
                return;
        }
        try
        {
            m_Impl->SaveLiveLayout();
            m_Impl->DockTreeResizePendingSave = false;
        }
        catch (const std::exception& error)
        {
            m_Impl->Notices.push_back(
                {UiWorkspaceNoticeSeverity::Error, std::string("Unable to autosave the workspace: ") + error.what()});
        }
    }

    void UiWorkspace::Shutdown() noexcept
    {
        if (!m_Impl || m_Impl->ShutdownComplete)
            return;
        m_Impl->ShutdownComplete = true;
        try
        {
            std::lock_guard lock(m_Impl->Mailbox->Mutex);
            m_Impl->Mailbox->Alive = false;
        }
        catch (...)
        {
        }
        m_Impl->Panels->Alive = false;
    }

    std::uint32_t UiWorkspace::DockspaceId() const noexcept { return ImHashStr("Keire.RootDockSpace"); }
} // namespace Keire

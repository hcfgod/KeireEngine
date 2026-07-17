#include "Keire/Ui.h"

#include "UiInternal.h"
#include "WindowInternal.h"

#include "Keire/Log.h"

#include <SDL3/SDL_gpu.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef IMGUI_HAS_DOCK
#error "The Kéire UI runtime requires the Dear ImGui docking branch."
#endif

static_assert(IMGUI_VERSION_NUM == 19280, "The Kéire UI runtime must use the dependency-lock ImGui version.");

namespace Keire
{
    namespace
    {
        constexpr std::uintmax_t MaximumLayoutBytes = 1024U * 1024U;

        [[nodiscard]] std::string LastSdlError()
        {
            const char* error = SDL_GetError();
            return error && *error ? std::string(error) : std::string("SDL did not provide a diagnostic");
        }

        [[nodiscard]] std::string BuildUiErrorMessage(const std::string& operation, const std::string& diagnostic)
        {
            return "UI operation '" + operation + "' failed: " + diagnostic;
        }

        [[nodiscard]] bool ValidColor(const UiColor color) noexcept
        {
            const auto valid = [](const float component)
            { return std::isfinite(component) && component >= 0.0F && component <= 1.0F; };
            return valid(color.Red) && valid(color.Green) && valid(color.Blue) && valid(color.Alpha);
        }

        [[nodiscard]] ImGuiWindowFlags ToImGuiWindowFlags(const UiWindowOptions options) noexcept
        {
            ImGuiWindowFlags flags = ImGuiWindowFlags_None;
            if (options.MenuBar)
                flags |= ImGuiWindowFlags_MenuBar;
            if (options.NoTitleBar)
                flags |= ImGuiWindowFlags_NoTitleBar;
            if (options.NoResize)
                flags |= ImGuiWindowFlags_NoResize;
            if (options.NoMove)
                flags |= ImGuiWindowFlags_NoMove;
            if (options.NoCollapse)
                flags |= ImGuiWindowFlags_NoCollapse;
            if (options.NoSavedSettings)
                flags |= ImGuiWindowFlags_NoSavedSettings;
            return flags;
        }

        [[nodiscard]] SDL_GPUPresentMode ToSdlPresentMode(const UiPresentMode mode) noexcept
        {
            switch (mode)
            {
            case UiPresentMode::Mailbox:
                return SDL_GPU_PRESENTMODE_MAILBOX;
            case UiPresentMode::Immediate:
                return SDL_GPU_PRESENTMODE_IMMEDIATE;
            case UiPresentMode::VSync:
            default:
                return SDL_GPU_PRESENTMODE_VSYNC;
            }
        }

        void ApplyTheme(const UiTheme theme)
        {
            switch (theme)
            {
            case UiTheme::Light:
                ImGui::StyleColorsLight();
                break;
            case UiTheme::Classic:
                ImGui::StyleColorsClassic();
                break;
            case UiTheme::Dark:
            default:
                ImGui::StyleColorsDark();
                break;
            }
        }

        void LoadLayout(const std::filesystem::path& path)
        {
            if (path.empty() || !std::filesystem::exists(path))
                return;

            const auto size = std::filesystem::file_size(path);
            if (size > MaximumLayoutBytes)
            {
                throw UiError("LoadLayout", "layout file exceeds the 1 MiB safety limit: " + path.string());
            }

            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                throw UiError("LoadLayout", "cannot open " + path.string());
            }
            const std::string contents{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            if (!input.good() && !input.eof())
            {
                throw UiError("LoadLayout", "cannot read " + path.string());
            }
            ImGui::LoadIniSettingsFromMemory(contents.data(), contents.size());
        }

        void SaveLayout(const std::filesystem::path& path)
        {
            if (path.empty())
                return;

            std::size_t size = 0;
            const char* contents = ImGui::SaveIniSettingsToMemory(&size);
            if (size > MaximumLayoutBytes)
            {
                throw UiError("SaveLayout", "layout data exceeds the 1 MiB safety limit: " + path.string());
            }

            if (const auto parent = path.parent_path(); !parent.empty())
                std::filesystem::create_directories(parent);

            auto temporary = path;
            temporary += ".tmp";
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (!output || (size > 0 && !output.write(contents, static_cast<std::streamsize>(size))))
                    throw UiError("SaveLayout", "cannot write " + temporary.string());
            }

            std::error_code error;
            std::filesystem::rename(temporary, path, error);
            if (error)
            {
                auto backup = path;
                backup += ".bak";
                std::filesystem::remove(backup, error);
                error.clear();
                if (std::filesystem::exists(path))
                    std::filesystem::rename(path, backup, error);
                if (!error)
                    std::filesystem::rename(temporary, path, error);
                if (error)
                {
                    std::error_code ignored;
                    if (std::filesystem::exists(backup) && !std::filesystem::exists(path))
                        std::filesystem::rename(backup, path, ignored);
                    std::filesystem::remove(temporary, ignored);
                    throw UiError("SaveLayout", "cannot replace " + path.string() + ": " + error.message());
                }
                std::filesystem::remove(backup, error);
            }
        }
    } // namespace

    UiError::UiError(std::string operation, std::string diagnostic)
        : std::runtime_error(BuildUiErrorMessage(operation, diagnostic)), m_Operation(std::move(operation)),
          m_Diagnostic(std::move(diagnostic))
    {
    }

    class UiFrame::Impl final
    {
      public:
        void Activate(const std::thread::id owner)
        {
            Owner = owner;
            Active.store(true, std::memory_order_release);
            ++Generation;
            Scopes.clear();
        }

        void Deactivate() noexcept
        {
            Active.store(false, std::memory_order_release);
            Scopes.clear();
        }

        void RequireActive(const char* operation) const
        {
            if (std::this_thread::get_id() != Owner)
                throw std::logic_error(std::string("UiFrame::") + operation +
                                       " must be called on the UI owner thread.");
            if (!Active.load(std::memory_order_acquire))
                throw std::logic_error(std::string("UiFrame::") + operation + " called outside Layer::OnUi.");
        }

        void OpenScope(const UiScope::Kind kind) { Scopes.push_back(kind); }

        void CloseScope(const UiScope::Kind kind, const std::uint64_t generation) noexcept
        {
            if (std::this_thread::get_id() != Owner || !Active.load(std::memory_order_acquire) ||
                generation != Generation || Scopes.empty() || Scopes.back() != kind)
                return;

            switch (kind)
            {
            case UiScope::Kind::Window:
                ImGui::End();
                break;
            case UiScope::Kind::Child:
                ImGui::EndChild();
                break;
            case UiScope::Kind::MenuBar:
                ImGui::EndMenuBar();
                break;
            case UiScope::Kind::Menu:
                ImGui::EndMenu();
                break;
            case UiScope::Kind::TabBar:
                ImGui::EndTabBar();
                break;
            case UiScope::Kind::TabItem:
                ImGui::EndTabItem();
                break;
            case UiScope::Kind::TreeNode:
                ImGui::TreePop();
                break;
            case UiScope::Kind::Disabled:
                ImGui::EndDisabled();
                break;
            case UiScope::Kind::Id:
                ImGui::PopID();
                break;
            case UiScope::Kind::MainMenuBar:
                ImGui::EndMainMenuBar();
                break;
            case UiScope::Kind::Combo:
                ImGui::EndCombo();
                break;
            case UiScope::Kind::Popup:
                ImGui::EndPopup();
                break;
            }
            Scopes.pop_back();
        }

        [[nodiscard]] bool Balanced() const noexcept { return Scopes.empty(); }

        std::shared_ptr<void> Lifetime = std::make_shared<int>(0);
        std::thread::id Owner;
        std::vector<UiScope::Kind> Scopes;
        std::uint64_t Generation = 0;
        std::atomic<bool> Active{false};
    };

    UiScope::UiScope(UiFrame& frame, const Kind kind, const bool visible, const bool closeRequired) noexcept
        : m_Frame(&frame), m_Lifetime(frame.Lifetime()), m_Generation(frame.Generation()), m_Kind(kind),
          m_Visible(visible), m_CloseRequired(closeRequired)
    {
    }

    UiScope::UiScope(UiScope&& other) noexcept
        : m_Frame(std::exchange(other.m_Frame, nullptr)), m_Lifetime(std::move(other.m_Lifetime)),
          m_Generation(other.m_Generation), m_Kind(other.m_Kind), m_Visible(other.m_Visible),
          m_CloseRequired(std::exchange(other.m_CloseRequired, false))
    {
    }

    UiScope& UiScope::operator=(UiScope&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_Frame = std::exchange(other.m_Frame, nullptr);
            m_Lifetime = std::move(other.m_Lifetime);
            m_Generation = other.m_Generation;
            m_Kind = other.m_Kind;
            m_Visible = other.m_Visible;
            m_CloseRequired = std::exchange(other.m_CloseRequired, false);
        }
        return *this;
    }

    UiScope::~UiScope() { Reset(); }

    void UiScope::Reset() noexcept
    {
        if (m_Frame && m_CloseRequired && !m_Lifetime.expired())
            m_Frame->CloseScope(m_Kind, m_Generation);
        m_Frame = nullptr;
        m_CloseRequired = false;
    }

    UiFrame::UiFrame() : m_Impl(std::make_unique<Impl>()) {}
    UiFrame::~UiFrame() = default;

    UiWindowScope UiFrame::BeginWindow(std::string_view title, bool* open, const UiWindowOptions options)
    {
        m_Impl->RequireActive("BeginWindow");
        const std::string safeTitle(title);
        const bool visible = ImGui::Begin(safeTitle.c_str(), open, ToImGuiWindowFlags(options));
        m_Impl->OpenScope(UiScope::Kind::Window);
        return UiWindowScope(*this, visible);
    }

    UiChildScope UiFrame::BeginChild(std::string_view id, const UiSize size, const bool border)
    {
        m_Impl->RequireActive("BeginChild");
        const std::string safeId(id);
        const auto flags = border ? ImGuiChildFlags_Borders : ImGuiChildFlags_None;
        const bool visible = ImGui::BeginChild(safeId.c_str(), {size.Width, size.Height}, flags);
        m_Impl->OpenScope(UiScope::Kind::Child);
        return UiChildScope(*this, visible);
    }

    UiMenuBarScope UiFrame::BeginMenuBar()
    {
        m_Impl->RequireActive("BeginMenuBar");
        const bool visible = ImGui::BeginMenuBar();
        if (visible)
            m_Impl->OpenScope(UiScope::Kind::MenuBar);
        return UiMenuBarScope(*this, visible);
    }

    UiMenuScope UiFrame::BeginMenu(std::string_view label, const bool enabled)
    {
        m_Impl->RequireActive("BeginMenu");
        const std::string safeLabel(label);
        const bool visible = ImGui::BeginMenu(safeLabel.c_str(), enabled);
        if (visible)
            m_Impl->OpenScope(UiScope::Kind::Menu);
        return UiMenuScope(*this, visible);
    }

    UiTabBarScope UiFrame::BeginTabBar(std::string_view id)
    {
        m_Impl->RequireActive("BeginTabBar");
        const std::string safeId(id);
        const bool visible = ImGui::BeginTabBar(safeId.c_str());
        if (visible)
            m_Impl->OpenScope(UiScope::Kind::TabBar);
        return UiTabBarScope(*this, visible);
    }

    UiTabItemScope UiFrame::BeginTabItem(std::string_view label, bool* open)
    {
        m_Impl->RequireActive("BeginTabItem");
        const std::string safeLabel(label);
        const bool visible = ImGui::BeginTabItem(safeLabel.c_str(), open);
        if (visible)
            m_Impl->OpenScope(UiScope::Kind::TabItem);
        return UiTabItemScope(*this, visible);
    }

    UiTreeNodeScope UiFrame::BeginTreeNode(std::string_view label)
    {
        m_Impl->RequireActive("BeginTreeNode");
        const std::string safeLabel(label);
        const bool visible = ImGui::TreeNode(safeLabel.c_str());
        if (visible)
            m_Impl->OpenScope(UiScope::Kind::TreeNode);
        return UiTreeNodeScope(*this, visible);
    }

    UiDisabledScope UiFrame::BeginDisabled(const bool disabled)
    {
        m_Impl->RequireActive("BeginDisabled");
        ImGui::BeginDisabled(disabled);
        m_Impl->OpenScope(UiScope::Kind::Disabled);
        return UiDisabledScope(*this);
    }

    UiIdScope UiFrame::PushId(std::string_view id)
    {
        m_Impl->RequireActive("PushId");
        const std::string safeId(id);
        ImGui::PushID(safeId.c_str());
        m_Impl->OpenScope(UiScope::Kind::Id);
        return UiIdScope(*this);
    }

    UiMainMenuBarScope UiFrame::BeginMainMenuBar()
    {
        m_Impl->RequireActive("BeginMainMenuBar");
        const bool visible = ImGui::BeginMainMenuBar();
        if (visible)
            m_Impl->OpenScope(UiScope::Kind::MainMenuBar);
        return UiMainMenuBarScope(*this, visible);
    }

    UiComboScope UiFrame::BeginCombo(std::string_view label, std::string_view preview)
    {
        m_Impl->RequireActive("BeginCombo");
        const std::string safeLabel(label);
        const std::string safePreview(preview);
        const bool visible = ImGui::BeginCombo(safeLabel.c_str(), safePreview.c_str());
        if (visible)
            m_Impl->OpenScope(UiScope::Kind::Combo);
        return UiComboScope(*this, visible);
    }

    UiPopupScope UiFrame::BeginPopupModal(std::string_view id, bool* open)
    {
        m_Impl->RequireActive("BeginPopupModal");
        const std::string safeId(id);
        const bool visible = ImGui::BeginPopupModal(safeId.c_str(), open, ImGuiWindowFlags_AlwaysAutoResize);
        if (visible)
            m_Impl->OpenScope(UiScope::Kind::Popup);
        return UiPopupScope(*this, visible);
    }

    UiPanelScope UiFrame::BeginPanel(UiPanelRegistration& panel, const UiWindowOptions options)
    {
        m_Impl->RequireActive("BeginPanel");
        if (!panel.Visible())
            return UiPanelScope(*this, false, false);
        bool* visible = panel.VisibilityAddress();
        const bool previous = *visible;
        const bool submitted = ImGui::Begin(panel.SubmittedName().c_str(), visible, ToImGuiWindowFlags(options));
        panel.NotifyVisibilityChanged(previous);
        m_Impl->OpenScope(UiScope::Kind::Window);
        return UiPanelScope(*this, submitted, true);
    }

    void UiFrame::OpenPopup(std::string_view id)
    {
        m_Impl->RequireActive("OpenPopup");
        const std::string safeId(id);
        ImGui::OpenPopup(safeId.c_str());
    }

    void UiFrame::CloseCurrentPopup()
    {
        m_Impl->RequireActive("CloseCurrentPopup");
        ImGui::CloseCurrentPopup();
    }

    void UiFrame::Text(std::string_view text)
    {
        m_Impl->RequireActive("Text");
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
    }

    void UiFrame::TextColored(const UiColor color, std::string_view text)
    {
        m_Impl->RequireActive("TextColored");
        if (!ValidColor(color))
            throw std::invalid_argument("UI color components must be finite values in the range 0..1.");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color.Red, color.Green, color.Blue, color.Alpha));
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopStyleColor();
    }

    void UiFrame::Separator()
    {
        m_Impl->RequireActive("Separator");
        ImGui::Separator();
    }

    void UiFrame::SameLine()
    {
        m_Impl->RequireActive("SameLine");
        ImGui::SameLine();
    }

    void UiFrame::Spacing()
    {
        m_Impl->RequireActive("Spacing");
        ImGui::Spacing();
    }

    bool UiFrame::Button(std::string_view label, const UiSize size)
    {
        m_Impl->RequireActive("Button");
        const std::string safeLabel(label);
        return ImGui::Button(safeLabel.c_str(), {size.Width, size.Height});
    }

    bool UiFrame::Checkbox(std::string_view label, bool& value)
    {
        m_Impl->RequireActive("Checkbox");
        const std::string safeLabel(label);
        return ImGui::Checkbox(safeLabel.c_str(), &value);
    }

    bool UiFrame::SliderFloat(std::string_view label, float& value, const float minimum, const float maximum)
    {
        m_Impl->RequireActive("SliderFloat");
        if (!(minimum < maximum))
            throw std::invalid_argument("SliderFloat minimum must be less than maximum.");
        const std::string safeLabel(label);
        return ImGui::SliderFloat(safeLabel.c_str(), &value, minimum, maximum);
    }

    bool UiFrame::SliderInt(std::string_view label, int& value, const int minimum, const int maximum)
    {
        m_Impl->RequireActive("SliderInt");
        if (minimum >= maximum)
            throw std::invalid_argument("SliderInt minimum must be less than maximum.");
        const std::string safeLabel(label);
        return ImGui::SliderInt(safeLabel.c_str(), &value, minimum, maximum);
    }

    bool UiFrame::InputText(std::string_view label, std::string& value)
    {
        m_Impl->RequireActive("InputText");
        const std::string safeLabel(label);
        return ImGui::InputText(safeLabel.c_str(), &value);
    }

    bool UiFrame::Selectable(std::string_view label, const bool selected)
    {
        m_Impl->RequireActive("Selectable");
        const std::string safeLabel(label);
        return ImGui::Selectable(safeLabel.c_str(), selected);
    }

    bool UiFrame::MenuItem(std::string_view label, const bool selected, const bool enabled)
    {
        m_Impl->RequireActive("MenuItem");
        const std::string safeLabel(label);
        return ImGui::MenuItem(safeLabel.c_str(), nullptr, selected, enabled);
    }

    bool UiFrame::ColorEdit(std::string_view label, UiColor& color)
    {
        m_Impl->RequireActive("ColorEdit");
        if (!ValidColor(color))
            throw std::invalid_argument("UI color components must be finite values in the range 0..1.");
        const std::string safeLabel(label);
        float values[]{color.Red, color.Green, color.Blue, color.Alpha};
        const bool changed = ImGui::ColorEdit4(safeLabel.c_str(), values, ImGuiColorEditFlags_AlphaBar);
        if (changed)
            color = {values[0], values[1], values[2], values[3]};
        return changed;
    }

    void UiFrame::SetTooltip(std::string_view text)
    {
        m_Impl->RequireActive("SetTooltip");
        if (ImGui::BeginTooltip())
        {
            ImGui::TextUnformatted(text.data(), text.data() + text.size());
            ImGui::EndTooltip();
        }
    }

    void UiFrame::SetNextWindowSize(const UiSize size, const bool firstUseOnly)
    {
        m_Impl->RequireActive("SetNextWindowSize");
        if (size.Width < 0.0F || size.Height < 0.0F)
            throw std::invalid_argument("UI window dimensions must not be negative.");
        ImGui::SetNextWindowSize({size.Width, size.Height}, firstUseOnly ? ImGuiCond_FirstUseEver : ImGuiCond_Always);
    }

    void UiFrame::SetNextWindowPosition(const UiPosition position, const bool firstUseOnly)
    {
        m_Impl->RequireActive("SetNextWindowPosition");
        if (!std::isfinite(position.X) || !std::isfinite(position.Y))
            throw std::invalid_argument("UI window positions must be finite.");
        ImGui::SetNextWindowPos({position.X, position.Y}, firstUseOnly ? ImGuiCond_FirstUseEver : ImGuiCond_Always);
    }

    void UiFrame::CloseScope(const UiScope::Kind kind, const std::uint64_t generation) noexcept
    {
        m_Impl->CloseScope(kind, generation);
    }

    std::weak_ptr<void> UiFrame::Lifetime() const noexcept { return m_Impl->Lifetime; }
    std::uint64_t UiFrame::Generation() const noexcept { return m_Impl->Generation; }

    class UiSystem::Impl final
    {
      public:
        Impl(UiSpecification value, WindowSystem& windows, Window& window)
            : Specification(std::move(value)), OwnerThread(std::this_thread::get_id()), Frame(new UiFrame())
        {
            if (!ValidColor(Specification.ClearColor))
                throw std::invalid_argument("UI clear color components must be finite values in the range 0..1.");
            if (Specification.Workspace.Enabled && !Specification.LayoutPath.empty())
                throw std::invalid_argument("UiSpecification::LayoutPath and Workspace cannot be enabled together.");

            PreviousContext = ImGui::GetCurrentContext();
            Context = ImGui::CreateContext();
            if (!Context)
                throw UiError("ImGui::CreateContext", "Dear ImGui did not create a context");

            try
            {
                ConfigureContext();
                if (Specification.Workspace.Enabled)
                    Workspace = std::unique_ptr<UiWorkspace>(new UiWorkspace(Specification.Workspace, windows, window,
                                                                             Specification.Mode == UiMode::Rendered));
                else
                    LoadLayout(Specification.LayoutPath);
                if (Specification.Mode == UiMode::Rendered)
                    InitializeRenderer(windows, window);
                InitializationComplete = true;
            }
            catch (...)
            {
                Shutdown();
                throw;
            }
        }

        ~Impl() { Shutdown(); }

        void ConfigureContext()
        {
            ImGui::SetCurrentContext(Context);
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            if (Specification.EnableKeyboardNavigation)
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            if (Specification.EnableDocking)
                io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
            io.ConfigDpiScaleFonts = true;
            ApplyTheme(Specification.Theme);
            if (Specification.Mode == UiMode::Headless)
            {
                unsigned char* pixels = nullptr;
                int width = 0;
                int height = 0;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
                if (!pixels || width <= 0 || height <= 0)
                    throw UiError("BuildHeadlessFontAtlas", "Dear ImGui did not produce font atlas pixels");
            }
        }

        void InitializeRenderer(WindowSystem& windows, Window& window)
        {
            NativeWindow = WindowSystemInternalAccess::NativeWindow(windows, window.Id());
            if (!NativeWindow)
                throw UiError("ResolveNativeWindow", "the primary window is not available");

            constexpr SDL_GPUShaderFormat formats = static_cast<SDL_GPUShaderFormat>(
                SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_DXIL |
                SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB);
            Device = SDL_CreateGPUDevice(formats, Specification.EnableGpuValidation, nullptr);
            if (!Device)
                throw UiError("SDL_CreateGPUDevice", LastSdlError());

            if (!SDL_ClaimWindowForGPUDevice(Device, NativeWindow))
                throw UiError("SDL_ClaimWindowForGPUDevice", LastSdlError());
            WindowClaimed = true;

            PresentMode = ToSdlPresentMode(Specification.PresentMode);
            if (!SDL_WindowSupportsGPUPresentMode(Device, NativeWindow, PresentMode))
            {
                KEIRE_CORE_WARN("Requested UI present mode is unavailable; falling back to VSync.");
                PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
            }
            if (!SDL_SetGPUSwapchainParameters(Device, NativeWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, PresentMode))
                throw UiError("SDL_SetGPUSwapchainParameters", LastSdlError());

            ImGui::SetCurrentContext(Context);
            if (!ImGui_ImplSDL3_InitForSDLGPU(NativeWindow))
                throw UiError("ImGui_ImplSDL3_InitForSDLGPU", LastSdlError());
            PlatformInitialized = true;

            ImGui_ImplSDLGPU3_InitInfo information{};
            information.Device = Device;
            information.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(Device, NativeWindow);
            information.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
            information.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
            information.PresentMode = PresentMode;
            if (!ImGui_ImplSDLGPU3_Init(&information))
                throw UiError("ImGui_ImplSDLGPU3_Init", LastSdlError());
            RendererInitialized = true;

            Windowing = &windows;
            WindowSystemInternalAccess::SetEventSink(windows, this, ProcessEvent);
            EventSinkInstalled = true;
        }

        static void ProcessEvent(void* context, const SDL_Event& event) noexcept
        {
            auto& self = *static_cast<Impl*>(context);
            if (!self.Context || !self.PlatformInitialized)
                return;
            ImGui::SetCurrentContext(self.Context);
            (void)ImGui_ImplSDL3_ProcessEvent(&event);
        }

        void BeginFrame(const TimeStep deltaTime, const LogicalExtent displaySize)
        {
            if (FrameActive)
                throw std::logic_error("A Kéire UI frame is already active.");

            ImGui::SetCurrentContext(Context);
            if (Workspace)
                Workspace->BeforeNewFrame();
            if (Specification.Mode == UiMode::Rendered)
            {
                ImGui_ImplSDLGPU3_NewFrame();
                ImGui_ImplSDL3_NewFrame();
            }
            else
            {
                auto& io = ImGui::GetIO();
                io.DisplaySize = {static_cast<float>(std::max(displaySize.Width, 1U)),
                                  static_cast<float>(std::max(displaySize.Height, 1U))};
                io.DeltaTime = std::max(static_cast<float>(deltaTime.Seconds()), 1.0F / 1000.0F);
            }
            ImGui::NewFrame();
            if (Workspace)
                Workspace->AfterNewFrame({static_cast<float>(std::max(displaySize.Width, 1U)),
                                          static_cast<float>(std::max(displaySize.Height, 1U))});
            if (Specification.EnableDocking)
                (void)ImGui::DockSpaceOverViewport(Workspace ? Workspace->DockspaceId() : 0);
            Frame->m_Impl->Activate(OwnerThread);
            FrameActive = true;
        }

        void EndFrame()
        {
            if (!FrameActive)
                throw std::logic_error("No Kéire UI frame is active.");
            if (!Frame->m_Impl->Balanced())
                throw std::logic_error("A UI scope escaped Layer::OnUi or was destroyed out of nesting order.");

            Frame->m_Impl->Deactivate();
            ImGui::SetCurrentContext(Context);
            ImGui::Render();
            const auto& io = ImGui::GetIO();
            CaptureState = {io.WantCaptureMouse, io.WantCaptureKeyboard, io.WantTextInput};
            FrameActive = false;

            if (Workspace)
                Workspace->AfterFrame();

            if (Specification.Mode == UiMode::Rendered)
                Render();
        }

        void Render()
        {
            ImDrawData* drawData = ImGui::GetDrawData();
            SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(Device);
            if (!commands)
                throw UiError("SDL_AcquireGPUCommandBuffer", LastSdlError());

            SDL_GPUTexture* swapchain = nullptr;
            if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, NativeWindow, &swapchain, nullptr, nullptr))
            {
                (void)SDL_CancelGPUCommandBuffer(commands);
                throw UiError("SDL_WaitAndAcquireGPUSwapchainTexture", LastSdlError());
            }

            if (swapchain && drawData->DisplaySize.x > 0.0F && drawData->DisplaySize.y > 0.0F)
            {
                ImGui_ImplSDLGPU3_PrepareDrawData(drawData, commands);
                SDL_GPUColorTargetInfo target{};
                target.texture = swapchain;
                target.clear_color = {Specification.ClearColor.Red, Specification.ClearColor.Green,
                                      Specification.ClearColor.Blue, Specification.ClearColor.Alpha};
                target.load_op = SDL_GPU_LOADOP_CLEAR;
                target.store_op = SDL_GPU_STOREOP_STORE;
                SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
                if (!pass)
                {
                    const auto diagnostic = LastSdlError();
                    (void)SDL_SubmitGPUCommandBuffer(commands);
                    throw UiError("SDL_BeginGPURenderPass", diagnostic);
                }
                ImGui_ImplSDLGPU3_RenderDrawData(drawData, commands, pass);
                SDL_EndGPURenderPass(pass);
            }

            if (!SDL_SubmitGPUCommandBuffer(commands))
                throw UiError("SDL_SubmitGPUCommandBuffer", LastSdlError());
        }

        void Shutdown() noexcept
        {
            if (ShutdownComplete)
                return;
            ShutdownComplete = true;

            if (Context)
                ImGui::SetCurrentContext(Context);
            if (FrameActive)
            {
                Frame->m_Impl->Deactivate();
                ImGui::EndFrame();
                FrameActive = false;
            }
            if (EventSinkInstalled && Windowing)
            {
                WindowSystemInternalAccess::SetEventSink(*Windowing, nullptr, nullptr);
                EventSinkInstalled = false;
            }
            if (Device)
                (void)SDL_WaitForGPUIdle(Device);
            if (RendererInitialized)
            {
                ImGui_ImplSDLGPU3_Shutdown();
                RendererInitialized = false;
            }
            if (PlatformInitialized)
            {
                ImGui_ImplSDL3_Shutdown();
                PlatformInitialized = false;
            }
            if (Context)
            {
                if (Workspace)
                {
                    Workspace->Shutdown();
                    Workspace.reset();
                }
                try
                {
                    if (InitializationComplete && !Specification.Workspace.Enabled)
                        SaveLayout(Specification.LayoutPath);
                }
                catch (const std::exception& error)
                {
                    try
                    {
                        KEIRE_CORE_WARN("Unable to persist UI layout: {}", error.what());
                    }
                    catch (...)
                    {
                    }
                }
                ImGui::DestroyContext(Context);
                Context = nullptr;
            }
            if (WindowClaimed && Device && NativeWindow)
            {
                SDL_ReleaseWindowFromGPUDevice(Device, NativeWindow);
                WindowClaimed = false;
            }
            if (Device)
            {
                SDL_DestroyGPUDevice(Device);
                Device = nullptr;
            }
            Frame->m_Impl->Lifetime.reset();
            ImGui::SetCurrentContext(PreviousContext);
        }

        UiSpecification Specification;
        std::thread::id OwnerThread;
        std::unique_ptr<UiFrame> Frame;
        UiCaptureState CaptureState;
        std::unique_ptr<UiWorkspace> Workspace;
        ImGuiContext* PreviousContext = nullptr;
        ImGuiContext* Context = nullptr;
        WindowSystem* Windowing = nullptr;
        SDL_Window* NativeWindow = nullptr;
        SDL_GPUDevice* Device = nullptr;
        SDL_GPUPresentMode PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
        bool WindowClaimed = false;
        bool PlatformInitialized = false;
        bool RendererInitialized = false;
        bool EventSinkInstalled = false;
        bool FrameActive = false;
        bool InitializationComplete = false;
        bool ShutdownComplete = false;
    };

    UiSystem::UiSystem(const UiSpecification& specification, WindowSystem& windows, Window& window)
        : m_Impl(std::make_unique<Impl>(specification, windows, window))
    {
    }

    UiSystem::~UiSystem() = default;
    void UiSystem::BeginFrame(const TimeStep deltaTime, const LogicalExtent displaySize)
    {
        m_Impl->BeginFrame(deltaTime, displaySize);
    }
    UiFrame& UiSystem::Frame() noexcept { return *m_Impl->Frame; }
    UiWorkspace* UiSystem::Workspace() noexcept { return m_Impl->Workspace.get(); }
    const UiWorkspace* UiSystem::Workspace() const noexcept { return m_Impl->Workspace.get(); }
    void UiSystem::EndFrame() { m_Impl->EndFrame(); }
    void UiSystem::Shutdown() noexcept { m_Impl->Shutdown(); }
    UiCaptureState UiSystem::Capture() const noexcept { return m_Impl->CaptureState; }
} // namespace Keire

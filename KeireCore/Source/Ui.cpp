#include "Keire/Ui.h"

#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/UiFontInternal.h"
#include "KeireInternal/UiInputInternal.h"
#include "KeireInternal/UiInternal.h"
#include "KeireInternal/UiLayoutInternal.h"
#include "KeireInternal/UiRenderBackendInternal.h"
#include "KeireInternal/UiThemeInternal.h"
#include "KeireInternal/WindowInternal.h"

#include "Keire/Log.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <mutex>
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
    class UiImage::Impl final
    {
      public:
        Impl(const std::shared_ptr<Detail::UiImageOwner>& owner, ImTextureData* texture, const std::uint32_t width,
             const std::uint32_t height)
            : Owner(owner), Texture(texture), Width(width), Height(height)
        {
        }

        ~Impl()
        {
            if (const auto owner = Owner.lock())
                owner->Retire(Texture);
        }

        std::weak_ptr<Detail::UiImageOwner> Owner;
        ImTextureData* Texture = nullptr;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
    };

    UiImage::UiImage(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}
    UiImage::~UiImage() = default;
    std::uint32_t UiImage::Width() const noexcept { return m_Impl->Width; }
    std::uint32_t UiImage::Height() const noexcept { return m_Impl->Height; }
    namespace
    {
        [[nodiscard]] std::string LastSdlError()
        {
            const char* error = SDL_GetError();
            return error && *error ? std::string(error) : std::string("SDL did not provide a diagnostic");
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
    } // namespace

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

        void RequireScope(const UiScope::Kind kind, const char* operation, const char* requiredScope) const
        {
            RequireActive(operation);
            if (Scopes.empty() || Scopes.back() != kind)
                throw std::logic_error(std::string("UiFrame::") + operation + " requires an active " + requiredScope +
                                       " scope.");
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
            case UiScope::Kind::Table:
                ImGui::EndTable();
                break;
            case UiScope::Kind::DragSource:
                ImGui::EndDragDropSource();
                break;
            case UiScope::Kind::DragTarget:
                ImGui::EndDragDropTarget();
                break;
            case UiScope::Kind::Clip:
                ImGui::GetWindowDrawList()->PopClipRect();
                break;
            case UiScope::Kind::Font:
                ImGui::PopFont();
                break;
            case UiScope::Kind::StyleColor:
                ImGui::PopStyleColor();
                break;
            case UiScope::Kind::StyleVariable:
                ImGui::PopStyleVar();
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
        std::shared_ptr<Detail::UiImageOwner> Images;
    };

    UiFrame::UiFrame() : m_Impl(std::make_unique<Impl>()) {}
    UiFrame::~UiFrame() = default;

    void UiFrame::RequireActive(const char* operation) const { m_Impl->RequireActive(operation); }
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
    UiTreeNodeScope UiFrame::BeginTreeNode(std::string_view label, const bool selected)
    {
        m_Impl->RequireActive("BeginTreeNode");
        const std::string safeLabel(label);
        ImGuiTreeNodeFlags flags =
            ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (selected)
            flags |= ImGuiTreeNodeFlags_Selected;
        const bool visible = ImGui::TreeNodeEx(safeLabel.c_str(), flags);
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
    UiWindowScope UiFrame::BeginMainToolbar(const std::string_view id, const float height)
    {
        m_Impl->RequireActive("BeginMainToolbar");
        if (id.empty() || !std::isfinite(height) || height <= 0.0F)
            throw std::invalid_argument("BeginMainToolbar requires an identifier and positive finite height.");
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        const bool visible = ImGui::Begin("##KeireMainToolbar", nullptr, flags);
        if (visible)
            m_Impl->OpenScope(UiScope::Kind::Window);
        return UiWindowScope(*this, visible);
    }
    UiWindowScope UiFrame::BeginMainStatusBar(const std::string_view id, const float height)
    {
        m_Impl->RequireActive("BeginMainStatusBar");
        if (id.empty() || !std::isfinite(height) || height <= 0.0F)
            throw std::invalid_argument("BeginMainStatusBar requires an identifier and positive finite height.");
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        const bool visible = ImGui::Begin("##KeireMainStatusBar", nullptr, flags);
        if (visible)
            m_Impl->OpenScope(UiScope::Kind::Window);
        return UiWindowScope(*this, visible);
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

    UiPopupScope UiFrame::BeginPopupModal(const std::string_view id, bool* open, const UiWindowOptions options,
                                          const bool autoResize)
    {
        m_Impl->RequireActive("BeginPopupModal");
        const std::string safeId(id);
        auto flags = ToImGuiWindowFlags(options);
        if (autoResize)
            flags |= ImGuiWindowFlags_AlwaysAutoResize;
        const bool visible = ImGui::BeginPopupModal(safeId.c_str(), open, flags);
        if (visible)
            m_Impl->OpenScope(UiScope::Kind::Popup);
        return UiPopupScope(*this, visible);
    }

    UiPopupScope UiFrame::BeginPopup(const std::string_view id)
    {
        m_Impl->RequireActive("BeginPopup");
        const std::string safeId(id);
        const bool visible = ImGui::BeginPopup(safeId.c_str());
        if (visible)
            m_Impl->OpenScope(UiScope::Kind::Popup);
        return UiPopupScope(*this, visible);
    }

    UiPopupScope UiFrame::BeginItemContextMenu(const std::string_view id)
    {
        m_Impl->RequireActive("BeginItemContextMenu");
        const std::string safeId(id);
        const bool visible = ImGui::BeginPopupContextItem(safeId.empty() ? nullptr : safeId.c_str());
        if (visible)
            m_Impl->OpenScope(UiScope::Kind::Popup);
        return UiPopupScope(*this, visible);
    }

    UiPopupScope UiFrame::BeginWindowContextMenu(const std::string_view id)
    {
        m_Impl->RequireActive("BeginWindowContextMenu");
        const std::string safeId(id);
        const bool visible =
            ImGui::BeginPopupContextWindow(safeId.empty() ? nullptr : safeId.c_str(),
                                           ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems);
        if (visible)
            m_Impl->OpenScope(UiScope::Kind::Popup);
        return UiPopupScope(*this, visible);
    }

    UiTableScope UiFrame::BeginTable(const std::string_view id, const std::size_t columns, const UiTableOptions options)
    {
        m_Impl->RequireActive("BeginTable");
        if (columns == 0 || columns > 64)
            throw std::invalid_argument("UI tables require between 1 and 64 columns.");
        const std::string safeId(id);
        ImGuiTableFlags flags = options.Sizing == UiTableSizing::Equal ? ImGuiTableFlags_SizingStretchSame
                                                                       : ImGuiTableFlags_SizingStretchProp;
        if (options.Borders)
            flags |= ImGuiTableFlags_Borders;
        if (options.Resizable)
            flags |= ImGuiTableFlags_Resizable;
        if (options.RowBackground)
            flags |= ImGuiTableFlags_RowBg;
        if (!options.PersistSettings)
            flags |= ImGuiTableFlags_NoSavedSettings;
        const bool visible = ImGui::BeginTable(safeId.c_str(), static_cast<int>(columns), flags);
        if (visible)
        {
            if (options.Sizing == UiTableSizing::Equal)
                for (std::size_t column = 0; column < columns; ++column)
                    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, 1.0F);
            m_Impl->OpenScope(UiScope::Kind::Table);
        }
        return UiTableScope(*this, visible);
    }

    UiDragSourceScope UiFrame::BeginDragSource()
    {
        m_Impl->RequireActive("BeginDragSource");
        // The Kéire facade permits display-only items as drag handles. ImGui requires this flag to synthesize a
        // temporary identifier for Text/Image items instead of asserting inside third-party code.
        const bool visible = ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID);
        if (visible)
            m_Impl->OpenScope(UiScope::Kind::DragSource);
        return UiDragSourceScope(*this, visible);
    }

    UiDragTargetScope UiFrame::BeginDragTarget()
    {
        m_Impl->RequireActive("BeginDragTarget");
        const bool visible = ImGui::BeginDragDropTarget();
        if (visible)
            m_Impl->OpenScope(UiScope::Kind::DragTarget);
        return UiDragTargetScope(*this, visible);
    }

    UiDragTargetScope UiFrame::BeginDragTarget(const UiItemRect area, const std::string_view id)
    {
        m_Impl->RequireActive("BeginDragTarget");
        if (id.empty() || area.Maximum.X < area.Minimum.X || area.Maximum.Y < area.Minimum.Y)
            throw std::invalid_argument("Custom drag targets require an ID and valid bounds.");
        const std::string safeId(id);
        const ImRect bounds(ImVec2(area.Minimum.X, area.Minimum.Y), ImVec2(area.Maximum.X, area.Maximum.Y));
        const bool visible = ImGui::BeginDragDropTargetCustom(bounds, ImGui::GetID(safeId.c_str()));
        if (visible)
            m_Impl->OpenScope(UiScope::Kind::DragTarget);
        return UiDragTargetScope(*this, visible);
    }

    UiClipScope UiFrame::PushClipRect(const UiItemRect rectangle)
    {
        m_Impl->RequireActive("PushClipRect");
        if (!std::isfinite(rectangle.Minimum.X) || !std::isfinite(rectangle.Minimum.Y) ||
            !std::isfinite(rectangle.Maximum.X) || !std::isfinite(rectangle.Maximum.Y) ||
            rectangle.Maximum.X <= rectangle.Minimum.X || rectangle.Maximum.Y <= rectangle.Minimum.Y)
        {
            throw std::invalid_argument("UI clip rectangle must be finite with positive extents.");
        }
        ImGui::GetWindowDrawList()->PushClipRect({rectangle.Minimum.X, rectangle.Minimum.Y},
                                                 {rectangle.Maximum.X, rectangle.Maximum.Y}, true);
        m_Impl->OpenScope(UiScope::Kind::Clip);
        return UiClipScope(*this);
    }

    UiFontScope UiFrame::PushFont(const UiFontRole role)
    {
        m_Impl->RequireActive("PushFont");
        if (role < UiFontRole::Body || role > UiFontRole::Icons)
            throw std::invalid_argument("The UI font role is invalid.");
        const auto name =
            std::string("Keire.") + std::array{"Body", "Heading", "Monospace", "Icons"}[static_cast<std::size_t>(role)];
        const auto& fonts = ImGui::GetIO().Fonts->Fonts;
        const auto found =
            std::find_if(fonts.begin(), fonts.end(), [&](const ImFont* font) { return name == font->GetDebugName(); });
        if (found == fonts.end())
            return UiFontScope(*this, false);
        ImGui::PushFont(*found, (*found)->LegacySize);
        m_Impl->OpenScope(UiScope::Kind::Font);
        return UiFontScope(*this, true);
    }

    UiPanelScope UiFrame::BeginPanel(UiPanelRegistration& panel, const UiWindowOptions options)
    {
        m_Impl->RequireActive("BeginPanel");
        if (!panel.Visible())
            return UiPanelScope(*this, false, false);
        if (panel.ConsumeFocusRequest())
            ImGui::SetNextWindowFocus();
        auto effectiveOptions = options;
        const bool maximized = panel.PrepareWindow();
        if (maximized)
        {
            effectiveOptions.NoResize = true;
            effectiveOptions.NoMove = true;
            effectiveOptions.NoCollapse = true;
            effectiveOptions.NoSavedSettings = true;
        }
        if (panel.Locked())
        {
            effectiveOptions.NoResize = true;
            effectiveOptions.NoMove = true;
            effectiveOptions.NoCollapse = true;
        }
        bool* visible = panel.VisibilityAddress();
        const bool previous = *visible;
        const bool submitted = ImGui::Begin(panel.SubmittedName().c_str(), visible,
                                            ToImGuiWindowFlags(effectiveOptions) |
                                                (maximized ? ImGuiWindowFlags_NoDocking : ImGuiWindowFlags_None));
        panel.NotifyWindowSubmitted();
        panel.NotifyVisibilityChanged(previous);
        m_Impl->OpenScope(UiScope::Kind::Window);
        if (submitted)
        {
            const float cursorX = ImGui::GetCursorPosX();
            const float available = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(cursorX + std::max(available - 28.0F, 0.0F));
            const auto lockId = "PanelViewLock##" + std::string(panel.Id());
            if (IconButton(lockId, panel.Locked() ? UiIcon::Lock : UiIcon::Unlock, panel.Locked(), {28.0F, 24.0F}))
                panel.SetLocked(!panel.Locked());
            if (LastItemState().Hovered)
            {
                SetTooltip(panel.Locked() ? "Unlock this panel view"
                                          : "Lock this panel view, placement, and current context",
                           {.Delayed = true});
            }
            ImGui::Separator();
        }
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

    void UiFrame::TableNextRow()
    {
        m_Impl->RequireActive("TableNextRow");
        ImGui::TableNextRow();
    }

    bool UiFrame::TableNextColumn()
    {
        m_Impl->RequireActive("TableNextColumn");
        return ImGui::TableNextColumn();
    }

    void UiFrame::SetDragPayload(const std::string_view type, const std::span<const std::byte> bytes)
    {
        m_Impl->RequireScope(UiScope::Kind::DragSource, "SetDragPayload", "drag source");
        if (type.empty() || type.size() > 32)
            throw std::invalid_argument("UI drag payload types must contain between 1 and 32 bytes.");
        const std::string safeType(type);
        (void)ImGui::SetDragDropPayload(safeType.c_str(), bytes.data(), bytes.size());
    }

    bool UiFrame::AcceptDragPayload(const std::string_view type, std::vector<std::byte>& bytes)
    {
        m_Impl->RequireScope(UiScope::Kind::DragTarget, "AcceptDragPayload", "drag target");
        if (type.empty() || type.size() > 32)
            throw std::invalid_argument("UI drag payload types must contain between 1 and 32 bytes.");
        const std::string safeType(type);
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(safeType.c_str());
        if (!payload)
            return false;
        const auto* begin = static_cast<const std::byte*>(payload->Data);
        bytes.assign(begin, begin + payload->DataSize);
        return true;
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

    void UiFrame::ProgressBar(const float fraction, const UiSize size, const std::string_view overlay)
    {
        m_Impl->RequireActive("ProgressBar");
        if (!std::isfinite(fraction))
            throw std::invalid_argument("UI progress fractions must be finite.");
        const std::string safeOverlay(overlay);
        ImGui::ProgressBar(std::clamp(fraction, 0.0F, 1.0F), {size.Width, size.Height},
                           safeOverlay.empty() ? nullptr : safeOverlay.c_str());
    }

    bool UiFrame::Splitter(const UiAxis axis, const std::string_view id, float& leadingSize, float& trailingSize,
                           const float minimumLeading, const float minimumTrailing, const float thickness)
    {
        m_Impl->RequireActive("Splitter");
        if (id.empty() || minimumLeading < 0.0F || minimumTrailing < 0.0F || thickness <= 0.0F ||
            !std::isfinite(leadingSize) || !std::isfinite(trailingSize))
            throw std::invalid_argument("UI splitter dimensions and identifier are invalid.");
        const std::string safeId(id);
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 size = axis == UiAxis::Horizontal ? ImVec2(thickness, std::max(available.y, thickness))
                                                       : ImVec2(std::max(available.x, thickness), thickness);
        (void)ImGui::InvisibleButton(safeId.c_str(), size);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (hovered || active)
            ImGui::SetMouseCursor(axis == UiAxis::Horizontal ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
        const auto color = ImGui::GetColorU32(active    ? ImGuiCol_SeparatorActive
                                              : hovered ? ImGuiCol_SeparatorHovered
                                                        : ImGuiCol_Separator);
        const auto minimum = ImGui::GetItemRectMin();
        const auto maximum = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(minimum, maximum, color);
        if (!active)
            return false;
        const float delta = axis == UiAxis::Horizontal ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;
        const float adjusted = std::clamp(delta, minimumLeading - leadingSize, trailingSize - minimumTrailing);
        if (adjusted == 0.0F)
            return false;
        leadingSize += adjusted;
        trailingSize -= adjusted;
        return true;
    }

    bool UiFrame::InvisibleButton(const std::string_view id, const UiSize size)
    {
        m_Impl->RequireActive("InvisibleButton");
        if (id.empty() || size.Width < 0.0F || size.Height < 0.0F || !std::isfinite(size.Width) ||
            !std::isfinite(size.Height))
            throw std::invalid_argument("UI invisible button dimensions and identifier are invalid.");
        const std::string safeId(id);
        return ImGui::InvisibleButton(safeId.c_str(), {size.Width, size.Height});
    }

    bool UiFrame::Shortcut(const UiShortcut shortcut)
    {
        m_Impl->RequireActive("Shortcut");
        ImGuiKeyChord chord = ImGuiKey_None;
        switch (shortcut.Key)
        {
        case UiKey::Enter:
            chord = ImGuiKey_Enter;
            break;
        case UiKey::Escape:
            chord = ImGuiKey_Escape;
            break;
        case UiKey::Tab:
            chord = ImGuiKey_Tab;
            break;
        case UiKey::Delete:
            chord = ImGuiKey_Delete;
            break;
        case UiKey::F2:
            chord = ImGuiKey_F2;
            break;
        case UiKey::A:
            chord = ImGuiKey_A;
            break;
        case UiKey::B:
            chord = ImGuiKey_B;
            break;
        case UiKey::Backspace:
            chord = ImGuiKey_Backspace;
            break;
        case UiKey::C:
            chord = ImGuiKey_C;
            break;
        case UiKey::D:
            chord = ImGuiKey_D;
            break;
        case UiKey::Down:
            chord = ImGuiKey_DownArrow;
            break;
        case UiKey::E:
            chord = ImGuiKey_E;
            break;
        case UiKey::F:
            chord = ImGuiKey_F;
            break;
        case UiKey::Left:
            chord = ImGuiKey_LeftArrow;
            break;
        case UiKey::Q:
            chord = ImGuiKey_Q;
            break;
        case UiKey::R:
            chord = ImGuiKey_R;
            break;
        case UiKey::P:
            chord = ImGuiKey_P;
            break;
        case UiKey::Right:
            chord = ImGuiKey_RightArrow;
            break;
        case UiKey::S:
            chord = ImGuiKey_S;
            break;
        case UiKey::Up:
            chord = ImGuiKey_UpArrow;
            break;
        case UiKey::V:
            chord = ImGuiKey_V;
            break;
        case UiKey::W:
            chord = ImGuiKey_W;
            break;
        case UiKey::X:
            chord = ImGuiKey_X;
            break;
        case UiKey::Y:
            chord = ImGuiKey_Y;
            break;
        case UiKey::Z:
            chord = ImGuiKey_Z;
            break;
        }
        if (shortcut.Control)
            chord |= ImGuiMod_Ctrl;
        if (shortcut.Shift)
            chord |= ImGuiMod_Shift;
        if (shortcut.Alt)
            chord |= ImGuiMod_Alt;
        if (shortcut.Primary)
            chord |= ImGuiMod_Shortcut;
        const ImGuiInputFlags flags =
            shortcut.Global ? ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverFocused : ImGuiInputFlags_None;
        return ImGui::Shortcut(chord, flags);
    }

    UiItemState UiFrame::LastItemState() const
    {
        m_Impl->RequireActive("LastItemState");
        const bool hovered = ImGui::IsItemHovered();
        return {hovered,
                ImGui::IsItemActive(),
                ImGui::IsItemActivated(),
                ImGui::IsItemEdited(),
                ImGui::IsItemDeactivatedAfterEdit(),
                hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)};
    }

    UiItemRect UiFrame::LastItemRect() const
    {
        m_Impl->RequireActive("LastItemRect");
        const auto minimum = ImGui::GetItemRectMin();
        const auto maximum = ImGui::GetItemRectMax();
        return {{minimum.x, minimum.y}, {maximum.x, maximum.y}};
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

    bool UiFrame::DragInteger(const std::string_view label, std::int64_t& value, const double speed,
                              const std::optional<std::int64_t> minimum, const std::optional<std::int64_t> maximum)
    {
        m_Impl->RequireActive("DragInteger");
        if (label.empty() || !std::isfinite(speed) || speed <= 0.0 || (minimum && maximum && *minimum > *maximum))
            throw std::invalid_argument("DragInteger requires a label, positive speed, and ordered bounds.");
        const std::string safeLabel(label);
        const auto* minimumValue = minimum ? &*minimum : nullptr;
        const auto* maximumValue = maximum ? &*maximum : nullptr;
        return ImGui::DragScalar(safeLabel.c_str(), ImGuiDataType_S64, &value, static_cast<float>(speed), minimumValue,
                                 maximumValue, "%lld");
    }

    bool UiFrame::DragScalar(const std::string_view label, double& value, const double speed,
                             const std::optional<double> minimum, const std::optional<double> maximum)
    {
        m_Impl->RequireActive("DragScalar");
        if (label.empty() || !std::isfinite(speed) || speed <= 0.0 || (minimum && maximum && *minimum > *maximum))
            throw std::invalid_argument("DragScalar requires a label, positive speed, and ordered bounds.");
        const std::string safeLabel(label);
        const auto* minimumValue = minimum ? &*minimum : nullptr;
        const auto* maximumValue = maximum ? &*maximum : nullptr;
        return ImGui::DragScalar(safeLabel.c_str(), ImGuiDataType_Double, &value, static_cast<float>(speed),
                                 minimumValue, maximumValue, "%.6g");
    }

    bool UiFrame::DragVector2(const std::string_view label, Vector2& value, const float speed)
    {
        m_Impl->RequireActive("DragVector2");
        if (label.empty() || !std::isfinite(speed) || speed <= 0.0F)
            throw std::invalid_argument("DragVector2 requires a label and a finite positive speed.");
        const std::string safeLabel(label);
        return ImGui::DragFloat2(safeLabel.c_str(), &value.X, speed, 0.0F, 0.0F, "%.3f");
    }

    bool UiFrame::DragVector3(const std::string_view label, Vector3& value, const float speed)
    {
        m_Impl->RequireActive("DragVector3");
        if (label.empty() || !std::isfinite(speed) || speed <= 0.0F)
            throw std::invalid_argument("DragVector3 requires a label and a finite positive speed.");

        const std::string safeLabel(label);
        ImGui::PushID(safeLabel.c_str());
        ImGui::BeginGroup();

        const float rowStart = ImGui::GetCursorPosX();
        const float available = std::max(ImGui::GetContentRegionAvail().x, 1.0F);
        const bool compact = available < 300.0F;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(safeLabel.c_str());
        if (!compact)
        {
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rowStart + 72.0F));
        }

        const auto drawAxis = [speed](const char* axis, float& component, const ImVec4 color, const float width)
        {
            ImGui::TextColored(color, "%s", axis);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(width);
            return ImGui::DragFloat((std::string("##") + axis).c_str(), &component, speed, 0.0F, 0.0F, "%.3f");
        };

        const auto& style = ImGui::GetStyle();
        const float controlsWidth = std::max(ImGui::GetContentRegionAvail().x, 90.0F);
        const float axisLabelWidth = ImGui::CalcTextSize("X").x;
        const float fieldWidth =
            std::max(24.0F, (controlsWidth - axisLabelWidth * 3.0F - style.ItemSpacing.x * 5.0F) / 3.0F);
        bool changed = drawAxis("X", value.X, {0.95F, 0.35F, 0.35F, 1.0F}, fieldWidth);
        ImGui::SameLine();
        changed |= drawAxis("Y", value.Y, {0.40F, 0.85F, 0.45F, 1.0F}, fieldWidth);
        ImGui::SameLine();
        changed |= drawAxis("Z", value.Z, {0.35F, 0.60F, 1.0F, 1.0F}, fieldWidth);

        ImGui::EndGroup();
        ImGui::PopID();
        return changed;
    }

    bool UiFrame::DragVector4(const std::string_view label, Vector4& value, const float speed)
    {
        m_Impl->RequireActive("DragVector4");
        if (label.empty() || !std::isfinite(speed) || speed <= 0.0F)
            throw std::invalid_argument("DragVector4 requires a label and a finite positive speed.");
        const std::string safeLabel(label);
        return ImGui::DragFloat4(safeLabel.c_str(), &value.X, speed, 0.0F, 0.0F, "%.3f");
    }
    bool UiFrame::DragQuaternion(const std::string_view label, Quaternion& value, const float speed)
    {
        m_Impl->RequireActive("DragQuaternion");
        if (label.empty() || !std::isfinite(speed) || speed <= 0.0F)
            throw std::invalid_argument("DragQuaternion requires a label and a finite positive speed.");
        const std::string safeLabel(label);
        return ImGui::DragFloat4(safeLabel.c_str(), &value.X, speed, -1.0F, 1.0F, "%.4f");
    }
    bool UiFrame::InputText(std::string_view label, std::string& value, const bool selectAllOnFocus)
    {
        m_Impl->RequireActive("InputText");
        return ImGui::InputText(std::string(label).c_str(), &value,
                                selectAllOnFocus ? ImGuiInputTextFlags_AutoSelectAll : ImGuiInputTextFlags_None);
    }

    bool UiFrame::InputPassword(std::string_view label, std::string& value)
    {
        m_Impl->RequireActive("InputPassword");
        const std::string safeLabel(label);
        return ImGui::InputText(safeLabel.c_str(), &value, ImGuiInputTextFlags_Password);
    }
    bool UiFrame::InputTextWithHint(const std::string_view label, const std::string_view hint, std::string& value)
    {
        m_Impl->RequireActive("InputTextWithHint");
        const std::string safeLabel(label);
        const std::string safeHint(hint);
        return ImGui::InputTextWithHint(safeLabel.c_str(), safeHint.c_str(), &value);
    }
    bool UiFrame::Selectable(std::string_view label, const bool selected, const bool keepPopupOpen)
    {
        m_Impl->RequireActive("Selectable");
        const std::string safeLabel(label);
        const auto flags = keepPopupOpen ? ImGuiSelectableFlags_DontClosePopups : ImGuiSelectableFlags_None;
        return ImGui::Selectable(safeLabel.c_str(), selected, flags);
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

    Ref<UiImage> UiFrame::CreateImage(const std::uint32_t width, const std::uint32_t height,
                                      const std::span<const std::byte> rgbaPixels)
    {
        m_Impl->RequireActive("CreateImage");
        if (!m_Impl->Images || width == 0 || height == 0 || width > 4096 || height > 4096 ||
            static_cast<std::uint64_t>(width) * height * 4ULL != rgbaPixels.size())
            throw std::invalid_argument("UI images require bounded dimensions and exactly four RGBA bytes per pixel.");
        auto* texture = m_Impl->Images->Create(width, height, rgbaPixels);
        return CreateRef<UiImage>(std::make_unique<UiImage::Impl>(m_Impl->Images, texture, width, height));
    }

    void UiFrame::Image(const Ref<UiImage>& image, UiSize size)
    {
        m_Impl->RequireActive("Image");
        if (!image || !image->m_Impl->Texture)
            throw std::invalid_argument("UiFrame::Image requires a valid UI image.");
        if (size.Width <= 0.0F)
            size.Width = static_cast<float>(image->Width());
        if (size.Height <= 0.0F)
            size.Height = static_cast<float>(image->Height());
        ImGui::Image(image->m_Impl->Texture->GetTexRef(), {size.Width, size.Height});
    }

    void UiFrame::Image(const Ref<RenderSurface>& surface, UiSize size)
    {
        m_Impl->RequireActive("Image(RenderSurface)");
        if (!surface)
            throw std::invalid_argument("UiFrame::Image requires a valid render surface.");
        if (size.Width <= 0.0F)
            size.Width = static_cast<float>(std::max(surface->Width(), 1U));
        if (size.Height <= 0.0F)
            size.Height = static_cast<float>(std::max(surface->Height(), 1U));

        if (auto* texture = RenderSystemInternalAccess::CaptureUiSurfaceTexture(*surface))
        {
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(texture))),
                         {size.Width, size.Height});
        }
        else
            ImGui::Dummy({size.Width, size.Height});
    }

    void UiFrame::DrawImage(const Ref<UiImage>& image, const UiItemRect rectangle)
    {
        m_Impl->RequireActive("DrawImage(UiImage)");
        if (!image || !image->m_Impl->Texture || rectangle.Maximum.X <= rectangle.Minimum.X ||
            rectangle.Maximum.Y <= rectangle.Minimum.Y)
            throw std::invalid_argument("UiFrame::DrawImage requires a valid UI image and rectangle.");
        ImGui::GetWindowDrawList()->AddImage(image->m_Impl->Texture->GetTexRef(),
                                             {rectangle.Minimum.X, rectangle.Minimum.Y},
                                             {rectangle.Maximum.X, rectangle.Maximum.Y});
    }

    void UiFrame::DrawImage(const Ref<RenderSurface>& surface, const UiItemRect rectangle)
    {
        m_Impl->RequireActive("DrawImage(RenderSurface)");
        if (!surface || rectangle.Maximum.X <= rectangle.Minimum.X || rectangle.Maximum.Y <= rectangle.Minimum.Y)
            throw std::invalid_argument("UiFrame::DrawImage requires a valid surface and rectangle.");
        if (auto* texture = RenderSystemInternalAccess::CaptureUiSurfaceTexture(*surface))
        {
            ImGui::GetWindowDrawList()->AddImage(
                ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(texture))),
                {rectangle.Minimum.X, rectangle.Minimum.Y}, {rectangle.Maximum.X, rectangle.Maximum.Y});
        }
    }

    bool UiFrame::ImageButton(const std::string_view id, const Ref<UiImage>& image, UiSize size)
    {
        m_Impl->RequireActive("ImageButton");
        if (!image || !image->m_Impl->Texture)
            throw std::invalid_argument("UiFrame::ImageButton requires a valid UI image.");
        if (size.Width <= 0.0F)
            size.Width = static_cast<float>(image->Width());
        if (size.Height <= 0.0F)
            size.Height = static_cast<float>(image->Height());
        const std::string safeId(id);
        return ImGui::ImageButton(safeId.c_str(), image->m_Impl->Texture->GetTexRef(), {size.Width, size.Height});
    }

    UiSize UiFrame::ContentAvailable() const
    {
        m_Impl->RequireActive("ContentAvailable");
        const auto available = ImGui::GetContentRegionAvail();
        return {available.x, available.y};
    }

    UiItemRect UiFrame::ContentRect() const
    {
        m_Impl->RequireActive("ContentRect");
        const auto position = ImGui::GetWindowPos();
        const auto minimum = ImGui::GetWindowContentRegionMin();
        const auto maximum = ImGui::GetWindowContentRegionMax();
        return {{position.x + minimum.x, position.y + minimum.y}, {position.x + maximum.x, position.y + maximum.y}};
    }

    bool UiFrame::WindowFocused() const
    {
        m_Impl->RequireActive("WindowFocused");
        return ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    }

    UiPointerState UiFrame::PointerState() const
    {
        m_Impl->RequireActive("PointerState");
        const auto& io = ImGui::GetIO();
        return {{io.MousePos.x, io.MousePos.y},
                {io.MouseDelta.x, io.MouseDelta.y},
                io.MouseWheel,
                ImGui::IsMouseDown(ImGuiMouseButton_Left),
                ImGui::IsMouseDown(ImGuiMouseButton_Middle),
                ImGui::IsMouseDown(ImGuiMouseButton_Right),
                ImGui::IsMouseClicked(ImGuiMouseButton_Left),
                ImGui::IsMouseClicked(ImGuiMouseButton_Middle),
                ImGui::IsMouseClicked(ImGuiMouseButton_Right),
                ImGui::IsMouseReleased(ImGuiMouseButton_Left),
                ImGui::IsMouseReleased(ImGuiMouseButton_Middle),
                ImGui::IsMouseReleased(ImGuiMouseButton_Right)};
    }
    void UiFrame::CapturePointerWheel()
    {
        m_Impl->RequireActive("CapturePointerWheel");
        (void)ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    }
    bool UiFrame::KeyDown(const UiKey key) const
    {
        m_Impl->RequireActive("KeyDown");
        return Detail::UiBackendKeyDown(key);
    }
    bool UiFrame::KeyPressed(const UiKey key) const
    {
        return (m_Impl->RequireActive(__func__), Detail::UiBackendKeyPressed(key));
    }
    std::string UiFrame::TextInput() const
    {
        return (m_Impl->RequireActive("TextInput"), Detail::UiBackendTextInput());
    }
    bool UiFrame::ControlDown() const { return (m_Impl->RequireActive(__func__), ImGui::GetIO().KeyCtrl); }
    bool UiFrame::ShiftDown() const
    {
        m_Impl->RequireActive("ShiftDown");
        return ImGui::GetIO().KeyShift;
    }
    bool UiFrame::AltDown() const
    {
        m_Impl->RequireActive("AltDown");
        return ImGui::GetIO().KeyAlt;
    }
    void UiFrame::SetTooltip(const std::string_view text) { SetTooltip(text, {}); }

    void UiFrame::SetTooltip(const std::string_view text, const UiTooltipOptions options)
    {
        m_Impl->RequireActive("SetTooltip");
        if (!ImGui::IsItemHovered(options.Delayed ? ImGuiHoveredFlags_DelayNormal : ImGuiHoveredFlags_None))
            return;
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

    void UiFrame::AlignNextItemGroup(const float alignment, const float width)
    {
        m_Impl->RequireActive("AlignNextItemGroup");
        if (!std::isfinite(alignment) || alignment < 0.0F || alignment > 1.0F || !std::isfinite(width) || width < 0.0F)
            throw std::invalid_argument("Item-group alignment and width are invalid.");
        const float target = (ImGui::GetWindowWidth() - width) * alignment;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), target));
    }

    void UiFrame::OpenScope(const UiScope::Kind kind)
    {
        m_Impl->RequireActive("OpenScope");
        m_Impl->OpenScope(kind);
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
        Impl(UiSpecification value, WindowSystem& windows, Window& window, RenderSystem& renderer)
            : Specification(std::move(value)), OwnerThread(std::this_thread::get_id()), Frame(new UiFrame()),
              Renderer(&renderer)
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
                ContextAccess = std::make_shared<Detail::UiContextAccess>(Context);
                Images = std::make_shared<Detail::UiImageOwner>();
                Images->Bind(ContextAccess, nullptr, nullptr);
                Frame->m_Impl->Images = Images;
                ConfigureContext();
                if (Specification.Workspace.Enabled)
                    Workspace = std::unique_ptr<UiWorkspace>(new UiWorkspace(Specification.Workspace, windows, window,
                                                                             Specification.Mode == UiMode::Rendered));
                else
                {
                    const auto contextLock = ContextAccess->Acquire();
                    Detail::LoadUiLayout(Specification.LayoutPath);
                }
                if (Specification.Mode == UiMode::Rendered)
                    InitializeRenderer(windows, renderer);
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
            const auto contextLock = ContextAccess->Acquire();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            if (Specification.EnableKeyboardNavigation)
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            if (Specification.EnableDocking)
                io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
            io.ConfigDpiScaleFonts = true;
            Detail::ConfigureUiFonts(Specification);
            Detail::ApplyUiTheme(Specification.Theme);
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

        void SetTheme(const UiTheme theme)
        {
            if (std::this_thread::get_id() != OwnerThread)
                throw std::logic_error("UI theme changes must run on the application owner thread.");
            const auto contextLock = ContextAccess->Acquire();
            Detail::ApplyUiTheme(theme);
            Specification.Theme = theme;
        }

        void InitializeRenderer(WindowSystem& windows, RenderSystem& renderer)
        {
            {
                const auto contextLock = ContextAccess->Acquire();
                SynchronizePlatformWindow();
            }
            RenderBackend.Initialize(renderer, ContextAccess, Images);

            Windowing = &windows;
            EventSink = WindowSystemInternalAccess::AddEventSink(windows, this, ProcessEvent);
        }

        void SynchronizePlatformWindow()
        {
            auto* nativeWindow = Renderer ? RenderSystemInternalAccess::NativeWindow(*Renderer) : nullptr;
            if (!nativeWindow)
                throw UiError("ResolveNativeWindow", "the primary window is not available");
            if (PlatformInitialized && PlatformNativeWindow == nativeWindow)
                return;

            if (PlatformInitialized)
            {
                // Recovery may replace an SDL window whose lost-device claim cannot be reused. The old main-window
                // viewport is non-owning, so shutting down the platform backend only detaches its stale handle.
                ImGui_ImplSDL3_Shutdown();
                PlatformInitialized = false;
                PlatformNativeWindow = nullptr;
            }
            if (!ImGui_ImplSDL3_InitForSDLGPU(nativeWindow))
                throw UiError("ImGui_ImplSDL3_InitForSDLGPU(recovery)", LastSdlError());
            PlatformNativeWindow = nativeWindow;
            PlatformInitialized = true;
        }

        static void ProcessEvent(void* context, const SDL_Event& event) noexcept
        {
            auto& self = *static_cast<Impl*>(context);
            if (!self.ContextAccess || !self.PlatformInitialized)
                return;
            try
            {
                const auto contextLock = self.ContextAccess->Acquire();
                (void)ImGui_ImplSDL3_ProcessEvent(&event);
            }
            catch (...)
            {
            }
        }

        void BeginFrame(const TimeStep deltaTime, const LogicalExtent displaySize)
        {
            if (FrameActive)
                throw std::logic_error("A Kéire UI frame is already active.");

            FrameContextLock = ContextAccess->Acquire();
            try
            {
                Images->ProcessRetired();
                if (Workspace)
                    Workspace->BeforeNewFrame();
                if (Specification.Mode == UiMode::Rendered)
                {
                    SynchronizePlatformWindow();
                    RenderBackend.NewFrame();
                    ImGui_ImplSDL3_NewFrame();
                    auto& io = ImGui::GetIO();
                    if (!std::isfinite(io.DisplaySize.x) || !std::isfinite(io.DisplaySize.y) ||
                        io.DisplaySize.x < 0.0F || io.DisplaySize.y < 0.0F)
                    {
                        // SDL can transiently reject a size query while the replacement window is becoming current.
                        // Keep the owner-provided logical extent authoritative until the next platform frame.
                        io.DisplaySize = {static_cast<float>(std::max(displaySize.Width, 1U)),
                                          static_cast<float>(std::max(displaySize.Height, 1U))};
                        io.DisplayFramebufferScale = {1.0F, 1.0F};
                    }
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
                {
                    // Reserve the main-menu row before allocating viewport sidebars. The client appends its menu
                    // items later in the frame; without this first reservation, the toolbar occupies the same top
                    // strip and its centered play controls are hidden behind the menu bar.
                    if (ImGui::BeginMainMenuBar())
                        ImGui::EndMainMenuBar();
                    constexpr ImGuiWindowFlags chromeFlags =
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
                    (void)ImGui::BeginViewportSideBar("##KeireMainToolbar", ImGui::GetMainViewport(), ImGuiDir_Up,
                                                      34.0F, chromeFlags);
                    ImGui::End();
                    (void)ImGui::BeginViewportSideBar("##KeireMainStatusBar", ImGui::GetMainViewport(), ImGuiDir_Down,
                                                      24.0F, chromeFlags);
                    ImGui::End();
                    (void)ImGui::DockSpaceOverViewport(Workspace ? Workspace->DockspaceId() : 0);
                }
                Frame->m_Impl->Activate(OwnerThread);
                FrameActive = true;
            }
            catch (...)
            {
                FrameContextLock = {};
                throw;
            }
        }

        void EndFrame()
        {
            if (!FrameActive)
                throw std::logic_error("No Kéire UI frame is active.");
            if (!Frame->m_Impl->Balanced())
                throw std::logic_error("A UI scope escaped Layer::OnUi or was destroyed out of nesting order.");

            Frame->m_Impl->Deactivate();
            ImGui::Render();
            const auto& io = ImGui::GetIO();
            CaptureState = {io.WantCaptureMouse, io.WantCaptureKeyboard, io.WantTextInput};
            FrameActive = false;

            if (Workspace)
                Workspace->AfterFrame();

            ImDrawData* drawData = Specification.Mode == UiMode::Rendered ? ImGui::GetDrawData() : nullptr;
            // Queue admission may need the render thread to retire an older frame. Release the authoring guard before
            // admission; the admitted packet capture reacquires it before reading this finalized draw data.
            FrameContextLock = {};
            if (Renderer)
                RenderSystemInternalAccess::EndFrame(*Renderer, drawData);
        }

        void CancelFrame() noexcept
        {
            if (!FrameActive)
            {
                FrameContextLock = {};
                return;
            }

            try
            {
                Frame->m_Impl->Deactivate();
                ImGui::EndFrame();
            }
            catch (...)
            {
            }
            FrameActive = false;
            FrameContextLock = {};
        }

        void Shutdown() noexcept
        {
            if (ShutdownComplete)
                return;
            ShutdownComplete = true;

            {
                std::unique_lock<std::recursive_mutex> contextLock;
                if (ContextAccess)
                {
                    try
                    {
                        contextLock = ContextAccess->Acquire();
                    }
                    catch (...)
                    {
                    }
                }
                else if (Context)
                {
                    ImGui::SetCurrentContext(Context);
                }
                if (FrameActive)
                {
                    Frame->m_Impl->Deactivate();
                    ImGui::EndFrame();
                    FrameActive = false;
                }
            }
            FrameContextLock = {};
            if (EventSink && Windowing)
            {
                try
                {
                    WindowSystemInternalAccess::RemoveEventSink(*Windowing, EventSink);
                }
                catch (...)
                {
                }
                EventSink = 0;
            }
            if (Renderer)
                RenderSystemInternalAccess::WaitIdle(*Renderer);
            RenderBackend.Shutdown();
            {
                std::unique_lock<std::recursive_mutex> contextLock;
                if (ContextAccess)
                {
                    try
                    {
                        contextLock = ContextAccess->Acquire();
                    }
                    catch (...)
                    {
                    }
                }
                else if (Context)
                {
                    ImGui::SetCurrentContext(Context);
                }
                if (Images)
                    Images->Close();
                if (PlatformInitialized)
                {
                    ImGui_ImplSDL3_Shutdown();
                    PlatformInitialized = false;
                    PlatformNativeWindow = nullptr;
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
                            Detail::SaveUiLayout(Specification.LayoutPath);
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
                    if (ContextAccess)
                        ContextAccess->Invalidate(PreviousContext);
                    else
                        ImGui::SetCurrentContext(PreviousContext);
                }
            }
            Renderer = nullptr;
            Frame->m_Impl->Lifetime.reset();
            Frame->m_Impl->Images.reset();
            Images.reset();
            ContextAccess.reset();
        }
        UiSpecification Specification;
        std::thread::id OwnerThread;
        std::unique_ptr<UiFrame> Frame;
        UiCaptureState CaptureState;
        std::unique_ptr<UiWorkspace> Workspace;
        ImGuiContext* PreviousContext = nullptr;
        ImGuiContext* Context = nullptr;
        std::shared_ptr<Detail::UiContextAccess> ContextAccess;
        std::unique_lock<std::recursive_mutex> FrameContextLock;
        WindowSystem* Windowing = nullptr;
        RenderSystem* Renderer = nullptr;
        Detail::UiRenderBackend RenderBackend;
        SDL_Window* PlatformNativeWindow = nullptr;
        bool PlatformInitialized = false;
        WindowSystemInternalAccess::EventSinkToken EventSink = 0;
        bool FrameActive = false;
        bool InitializationComplete = false;
        bool ShutdownComplete = false;
        std::shared_ptr<Detail::UiImageOwner> Images;
    };
    UiSystem::UiSystem(const UiSpecification& specification, WindowSystem& windows, Window& window,
                       RenderSystem& renderer)
        : m_Impl(std::make_unique<Impl>(specification, windows, window, renderer))
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
    void UiSystem::SetTheme(const UiTheme theme) { m_Impl->SetTheme(theme); }
    UiTheme UiSystem::Theme() const noexcept { return m_Impl->Specification.Theme; }
    void UiSystem::EndFrame() { m_Impl->EndFrame(); }
    void UiSystem::CancelFrame() noexcept { m_Impl->CancelFrame(); }
    void UiSystem::Shutdown() noexcept { m_Impl->Shutdown(); }
    UiCaptureState UiSystem::Capture() const noexcept { return m_Impl->CaptureState; }
} // namespace Keire

#include "KeireInternal/UiInputInternal.h"

#include <imgui.h>

namespace Keire::Detail
{
    bool UiBackendKeyDown(const UiKey key) noexcept
    {
        ImGuiKey imguiKey = ImGuiKey_None;
        switch (key)
        {
        case UiKey::Enter:
            imguiKey = ImGuiKey_Enter;
            break;
        case UiKey::Escape:
            imguiKey = ImGuiKey_Escape;
            break;
        case UiKey::Tab:
            imguiKey = ImGuiKey_Tab;
            break;
        case UiKey::Delete:
            imguiKey = ImGuiKey_Delete;
            break;
        case UiKey::F2:
            imguiKey = ImGuiKey_F2;
            break;
        case UiKey::A:
            imguiKey = ImGuiKey_A;
            break;
        case UiKey::B:
            imguiKey = ImGuiKey_B;
            break;
        case UiKey::Backspace:
            imguiKey = ImGuiKey_Backspace;
            break;
        case UiKey::C:
            imguiKey = ImGuiKey_C;
            break;
        case UiKey::D:
            imguiKey = ImGuiKey_D;
            break;
        case UiKey::Down:
            imguiKey = ImGuiKey_DownArrow;
            break;
        case UiKey::E:
            imguiKey = ImGuiKey_E;
            break;
        case UiKey::F:
            imguiKey = ImGuiKey_F;
            break;
        case UiKey::Left:
            imguiKey = ImGuiKey_LeftArrow;
            break;
        case UiKey::Q:
            imguiKey = ImGuiKey_Q;
            break;
        case UiKey::R:
            imguiKey = ImGuiKey_R;
            break;
        case UiKey::Right:
            imguiKey = ImGuiKey_RightArrow;
            break;
        case UiKey::S:
            imguiKey = ImGuiKey_S;
            break;
        case UiKey::Up:
            imguiKey = ImGuiKey_UpArrow;
            break;
        case UiKey::V:
            imguiKey = ImGuiKey_V;
            break;
        case UiKey::W:
            imguiKey = ImGuiKey_W;
            break;
        case UiKey::X:
            imguiKey = ImGuiKey_X;
            break;
        case UiKey::Y:
            imguiKey = ImGuiKey_Y;
            break;
        case UiKey::Z:
            imguiKey = ImGuiKey_Z;
            break;
        }
        return ImGui::IsKeyDown(imguiKey);
    }
} // namespace Keire::Detail

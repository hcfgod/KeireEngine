#include "KeireInternal/UiInputInternal.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdint>

namespace Keire::Detail
{
    namespace
    {
        [[nodiscard]] ImGuiKey NativeKey(const UiKey key) noexcept
        {
            switch (key)
            {
            case UiKey::Enter:
                return ImGuiKey_Enter;
            case UiKey::Escape:
                return ImGuiKey_Escape;
            case UiKey::Tab:
                return ImGuiKey_Tab;
            case UiKey::Delete:
                return ImGuiKey_Delete;
            case UiKey::F2:
                return ImGuiKey_F2;
            case UiKey::A:
                return ImGuiKey_A;
            case UiKey::B:
                return ImGuiKey_B;
            case UiKey::Backspace:
                return ImGuiKey_Backspace;
            case UiKey::C:
                return ImGuiKey_C;
            case UiKey::D:
                return ImGuiKey_D;
            case UiKey::Down:
                return ImGuiKey_DownArrow;
            case UiKey::E:
                return ImGuiKey_E;
            case UiKey::F:
                return ImGuiKey_F;
            case UiKey::Left:
                return ImGuiKey_LeftArrow;
            case UiKey::Q:
                return ImGuiKey_Q;
            case UiKey::R:
                return ImGuiKey_R;
            case UiKey::P:
                return ImGuiKey_P;
            case UiKey::Right:
                return ImGuiKey_RightArrow;
            case UiKey::S:
                return ImGuiKey_S;
            case UiKey::Up:
                return ImGuiKey_UpArrow;
            case UiKey::V:
                return ImGuiKey_V;
            case UiKey::W:
                return ImGuiKey_W;
            case UiKey::X:
                return ImGuiKey_X;
            case UiKey::Y:
                return ImGuiKey_Y;
            case UiKey::Z:
                return ImGuiKey_Z;
            }
            return ImGuiKey_None;
        }

        void AppendUtf8(std::string& destination, const std::uint32_t codePoint)
        {
            if (codePoint == 0 || codePoint > 0x10FFFFU || (codePoint >= 0xD800U && codePoint <= 0xDFFFU))
                return;
            if (codePoint <= 0x7FU)
                destination.push_back(static_cast<char>(codePoint));
            else if (codePoint <= 0x7FFU)
            {
                destination.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
                destination.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
            }
            else if (codePoint <= 0xFFFFU)
            {
                destination.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
                destination.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
                destination.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
            }
            else
            {
                destination.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
                destination.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
                destination.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
                destination.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
            }
        }
    } // namespace

    bool UiBackendKeyDown(const UiKey key) noexcept { return ImGui::IsKeyDown(NativeKey(key)); }

    bool UiBackendKeyPressed(const UiKey key) noexcept { return ImGui::IsKeyPressed(NativeKey(key), false); }

    void UiBackendRequestTextInput() noexcept
    {
        if (const auto context = ImGui::GetCurrentContext())
            context->PlatformImeData.WantTextInput = true;
    }

    std::string UiBackendTextInput()
    {
        std::string result;
        for (const auto character : ImGui::GetIO().InputQueueCharacters)
            AppendUtf8(result, static_cast<std::uint32_t>(character));
        return result;
    }
} // namespace Keire::Detail

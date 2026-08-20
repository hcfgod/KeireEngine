#include "EditorRuntimeUiInput.h"

#include "Keire/Scenes/ScenePresentationRuntime.h"
#include "Keire/Ui.h"
#include "KeireInternal/WindowInternal.h"

namespace KeireEditor
{
    void RouteRuntimeUiKeyboard(Keire::UiFrame& ui, const Keire::Ref<Keire::ScenePresentationRuntime>& presentation,
                                const Keire::Ref<Keire::WindowSystem>& windows, const Keire::Ref<Keire::Window>& window)
    {
        if (!presentation)
            return;
        const bool textInputFocused = presentation->TextInputFocused();
        if (windows && window)
            (void)Keire::WindowSystemInternalAccess::SetTextInput(*windows, window->Id(), textInputFocused);
        if (textInputFocused)
        {
            presentation->TextInput(ui.TextInput());
        }
        if (ui.KeyPressed(Keire::UiKey::Backspace))
            (void)presentation->KeyInput(Keire::RuntimeUiKey::Backspace);
        if (ui.KeyPressed(Keire::UiKey::Delete))
            (void)presentation->KeyInput(Keire::RuntimeUiKey::Delete);
        if (ui.KeyPressed(Keire::UiKey::Enter) && !presentation->KeyInput(Keire::RuntimeUiKey::Enter))
            presentation->Navigate(Keire::RuntimeUiNavigation::Accept);
        if (ui.KeyPressed(Keire::UiKey::Escape) && !presentation->KeyInput(Keire::RuntimeUiKey::Escape))
            presentation->Navigate(Keire::RuntimeUiNavigation::Cancel);
        if (ui.KeyPressed(Keire::UiKey::Tab))
        {
            presentation->Navigate(ui.ShiftDown() ? Keire::RuntimeUiNavigation::Previous
                                                  : Keire::RuntimeUiNavigation::Next);
        }
        if (presentation->TextInputFocused())
            return;
        if (ui.KeyPressed(Keire::UiKey::Left))
            presentation->Navigate(Keire::RuntimeUiNavigation::Left);
        if (ui.KeyPressed(Keire::UiKey::Right))
            presentation->Navigate(Keire::RuntimeUiNavigation::Right);
        if (ui.KeyPressed(Keire::UiKey::Up))
            presentation->Navigate(Keire::RuntimeUiNavigation::Up);
        if (ui.KeyPressed(Keire::UiKey::Down))
            presentation->Navigate(Keire::RuntimeUiNavigation::Down);
    }
} // namespace KeireEditor

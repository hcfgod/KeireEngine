#include "KeireClient/Editor/ShaderGraphCreationMenu.h"

namespace KeireEditor
{
    std::optional<Keire::ShaderGraphTemplate> DrawShaderGraphCreationMenu(Keire::UiFrame& ui)
    {
        if (auto menu = ui.BeginMenu("Shader Graph"); menu)
        {
            if (ui.MenuItem("UI"))
                return Keire::ShaderGraphTemplate::Ui;
            if (ui.MenuItem("Fullscreen Effect"))
                return Keire::ShaderGraphTemplate::Fullscreen;
            if (ui.MenuItem("VFX"))
                return Keire::ShaderGraphTemplate::Vfx;
            if (ui.MenuItem("Custom Graphics"))
                return Keire::ShaderGraphTemplate::CustomGraphics;
            (void)ui.MenuItem("Compute (not supported)", false, false);
            ui.Separator();
            if (ui.MenuItem("Surface / Lit"))
                return Keire::ShaderGraphTemplate::Lit;
            if (ui.MenuItem("Surface / Unlit"))
                return Keire::ShaderGraphTemplate::Unlit;
            if (ui.MenuItem("Surface / Transparent"))
                return Keire::ShaderGraphTemplate::Transparent;
            if (ui.MenuItem("Surface / Decal"))
                return Keire::ShaderGraphTemplate::Decal;
            if (ui.MenuItem("Surface / Hair"))
                return Keire::ShaderGraphTemplate::Hair;
            if (ui.MenuItem("Surface / Eye"))
                return Keire::ShaderGraphTemplate::Eye;
        }
        return std::nullopt;
    }
} // namespace KeireEditor

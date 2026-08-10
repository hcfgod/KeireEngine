#include "KeireClient/Editor/ShaderGraphCreationMenu.h"

namespace KeireEditor
{
    std::optional<Keire::ShaderGraphTemplate> DrawShaderGraphCreationMenu(Keire::UiFrame& ui)
    {
        if (auto menu = ui.BeginMenu("Shader Graph"); menu)
        {
            if (ui.MenuItem("Lit / PBR"))
                return Keire::ShaderGraphTemplate::Lit;
            if (ui.MenuItem("Unlit"))
                return Keire::ShaderGraphTemplate::Unlit;
            if (ui.MenuItem("Transparent"))
                return Keire::ShaderGraphTemplate::Transparent;
            if (ui.MenuItem("Decal"))
                return Keire::ShaderGraphTemplate::Decal;
            if (ui.MenuItem("Fullscreen"))
                return Keire::ShaderGraphTemplate::Fullscreen;
            ui.Separator();
            if (ui.MenuItem("Hair"))
                return Keire::ShaderGraphTemplate::Hair;
            if (ui.MenuItem("Eye"))
                return Keire::ShaderGraphTemplate::Eye;
        }
        return std::nullopt;
    }
} // namespace KeireEditor

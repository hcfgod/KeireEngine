#include "KeireClient/Editor/VfxEffectPanel.h"

#include "KeireClient/Editor/VfxEffectDocument.h"

#include <exception>
#include <functional>
#include <string>
#include <string_view>

namespace KeireEditor
{
    void VfxEffectPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.vfx-effect", "VFX Effect", false});
    }

    bool VfxEffectPanel::ApplyEdit(const std::string_view name,
                                   const std::function<void(Keire::VfxEffectDefinition&)>& operation)
    {
        try
        {
            const bool changed = m_Controller.VfxEffectState().Edit(name, operation);
            if (changed)
                m_Message = std::string(name) + ".";
            return changed;
        }
        catch (const std::exception& error)
        {
            m_Message = error.what();
            m_Controller.ReportVfxEffectError(m_Message);
            return false;
        }
    }

    bool VfxEffectPanel::ApplyAction(const std::string_view name, const std::function<bool()>& operation)
    {
        try
        {
            const bool changed = operation();
            if (changed)
                m_Message = std::string(name) + ".";
            return changed;
        }
        catch (const std::exception& error)
        {
            m_Message = error.what();
            m_Controller.ReportVfxEffectError(m_Message);
            return false;
        }
    }
} // namespace KeireEditor

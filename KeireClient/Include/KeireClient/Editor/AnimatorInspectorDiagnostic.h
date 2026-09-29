#pragma once

#include "Keire/ECS/Components/AnimatorComponent.h"
#include "Keire/Ui.h"

namespace KeireEditor
{
    inline void DrawAnimatorInspectorDiagnostic(Keire::UiFrame& ui, const Keire::AnimatorComponent& animator,
                                                const Keire::UiColor warning)
    {
        if (auto status = ui.BeginChild("##AnimatorRuntimeStatus", {0.0F, 58.0F}); status)
        {
            ui.Text("Runtime status");
            if (animator.RuntimeDiagnostic().empty())
                ui.Text("No runtime warnings.");
            else
                ui.TextColoredWrapped(warning, animator.RuntimeDiagnostic());
        }
    }
} // namespace KeireEditor

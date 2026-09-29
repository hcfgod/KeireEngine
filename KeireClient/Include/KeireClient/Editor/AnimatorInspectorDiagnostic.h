#pragma once

#include "Keire/ECS/Components/AnimatorComponent.h"
#include "Keire/Ui.h"

namespace KeireEditor
{
    inline void DrawAnimatorInspectorDiagnostic(Keire::UiFrame& ui, const Keire::AnimatorComponent& animator,
                                                const Keire::UiColor warning)
    {
        if (!animator.RuntimeDiagnostic().empty())
            ui.TextColoredWrapped(warning, animator.RuntimeDiagnostic());
    }
} // namespace KeireEditor

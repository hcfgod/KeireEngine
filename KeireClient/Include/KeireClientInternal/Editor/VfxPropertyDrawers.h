#pragma once

#include "KeireClient/Editor/PropertyDrawerRegistry.h"

#include "Keire/ECS/Components/VfxEmitterComponent.h"

namespace KeireEditor::Detail
{
    inline void RegisterVfxPropertyDrawers(PropertyDrawerRegistry& drawers)
    {
        drawers.RegisterIntegerChoices(Keire::VfxEmitterComponent::StaticType(), "quality",
                                       {"Low", "Medium", "High", "Cinematic"});
        drawers.RegisterIntegerChoices(Keire::VfxEmitterComponent::StaticType(), "culling",
                                       {"Automatic", "Fixed Bounds", "Always Simulate"});
    }
} // namespace KeireEditor::Detail

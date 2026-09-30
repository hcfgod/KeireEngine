#pragma once

#include "KeireClient/Editor/PropertyDrawerRegistry.h"

#include "Keire/ECS/Components/RigidBodyComponent.h"

namespace KeireEditor::Detail
{
    inline void RegisterPhysicsPropertyDrawers(PropertyDrawerRegistry& drawers)
    {
        drawers.RegisterIntegerChoices(Keire::RigidBodyComponent::StaticType(), "motion",
                                       {"Static", "Dynamic", "Kinematic"});
    }
} // namespace KeireEditor::Detail

#include "KeireClient/Editor/InspectorPropertyVisibility.h"

#include "Keire/ECS/Components/AudioComponents.h"

namespace KeireEditor
{
    bool IsInspectorPropertyVisible(const Keire::ComponentTypeId component, const std::string_view property) noexcept
    {
        // AudioSource::bus remains serialized as a readable fallback for schema-one scenes and renamed buses. The
        // stable busId picker is the single authoring control and commits both fields transactionally.
        return component != Keire::AudioSourceComponent::StaticType() || property != "bus";
    }

    bool IsMeshRendererBakedLightingProperty(const std::string_view property) noexcept
    {
        return property == "staticLighting" || property == "giReceive" || property == "lightmapScale" ||
               property == "preserveLightmapUvs";
    }
} // namespace KeireEditor

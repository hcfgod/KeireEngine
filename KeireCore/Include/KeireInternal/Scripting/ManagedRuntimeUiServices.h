#pragma once

#include "Keire/Scripting/ScriptSystem.h"

namespace Keire
{
    class Scene;
    class ScenePresentationRuntime;
} // namespace Keire

namespace Keire::Detail
{
    [[nodiscard]] std::optional<float> ReadManagedUiScalar(const Ref<Scene>& scene, AssetId entity,
                                                           ManagedUiScalarProperty property) noexcept;
    [[nodiscard]] bool SetManagedUiScalar(const Ref<Scene>& scene, AssetId entity, ManagedUiScalarProperty property,
                                          float value) noexcept;
    [[nodiscard]] std::optional<bool> ReadManagedUiFlag(const Ref<Scene>& scene,
                                                        const Ref<ScenePresentationRuntime>& presentation,
                                                        AssetId entity, ManagedUiFlagProperty property) noexcept;
    [[nodiscard]] bool SetManagedUiFlag(const Ref<Scene>& scene, const Ref<ScenePresentationRuntime>& presentation,
                                        AssetId entity, ManagedUiFlagProperty property, bool value) noexcept;
    [[nodiscard]] std::optional<Vector2> ReadManagedUiVector(const Ref<Scene>& scene, AssetId entity,
                                                             ManagedUiVectorProperty property) noexcept;
    [[nodiscard]] bool SetManagedUiVector(const Ref<Scene>& scene, AssetId entity, ManagedUiVectorProperty property,
                                          Vector2 value) noexcept;
    [[nodiscard]] std::optional<std::string> ReadManagedUiInputText(const Ref<Scene>& scene, AssetId entity) noexcept;
    [[nodiscard]] bool SetManagedUiInputText(const Ref<Scene>& scene, AssetId entity, std::string_view text) noexcept;
    [[nodiscard]] bool ConsumeManagedUiEvent(const Ref<ScenePresentationRuntime>& presentation, AssetId entity,
                                             RuntimeUiEventType type) noexcept;
    [[nodiscard]] bool FocusManagedUi(const Ref<ScenePresentationRuntime>& presentation, AssetId entity) noexcept;
} // namespace Keire::Detail

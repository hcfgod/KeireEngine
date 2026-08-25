#pragma once

#include "Keire/Core.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <variant>

namespace KeireEditor
{
    class SceneDocument;

    enum class InspectorTransformProperty : std::uint8_t
    {
        Position,
        Rotation,
        Scale
    };

    using InspectorTransformValue = std::variant<Keire::Vector3, Keire::Quaternion>;

    struct InspectorTransformEdit
    {
        Keire::EntityId Entity;
        InspectorTransformProperty Property = InspectorTransformProperty::Position;
        InspectorTransformValue Before = Keire::Vector3{};
        InspectorTransformValue After = Keire::Vector3{};
        std::string Name;
        std::string MergeKey;
        Keire::Ref<Keire::RefCounted> Scope;
    };

    using InspectorTransformApply =
        std::function<void(Keire::EntityId, InspectorTransformProperty, const InspectorTransformValue&)>;

    struct InspectorTransformSceneScope final
    {
        Keire::AssetId Asset;
        bool PlayMode = false;
        Keire::WeakRef<Keire::SceneRuntimeSession> PlaySession;
        Keire::WeakRef<Keire::UndoContext> EditHistory;
        Keire::WeakRef<Keire::RefCounted> EditGeneration;

        [[nodiscard]] Keire::Ref<Keire::RefCounted> Identity() const noexcept;
    };

    [[nodiscard]] InspectorTransformSceneScope CaptureInspectorTransformSceneScope(const SceneDocument& document);
    [[nodiscard]] Keire::Ref<Keire::Scene>
    ResolveInspectorTransformScene(const SceneDocument& document, const InspectorTransformSceneScope& scope) noexcept;

    [[nodiscard]] InspectorTransformEdit
    MakeInspectorTransformEdit(Keire::EntityId entity, InspectorTransformProperty property,
                               InspectorTransformValue before, InspectorTransformValue after, std::uint64_t editSerial);

    [[nodiscard]] std::unique_ptr<Keire::UndoCommand>
    CreateInspectorTransformUndoCommand(Keire::AssetId scene, bool playMode, InspectorTransformEdit edit,
                                        InspectorTransformApply apply, Keire::UndoAvailability available = {});
} // namespace KeireEditor

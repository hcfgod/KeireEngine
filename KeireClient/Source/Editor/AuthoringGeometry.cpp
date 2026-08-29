#include "KeireClient/Editor/AuthoringGeometry.h"

#include "KeireClient/Editor/AuthoringWidgets.h"
#include "KeireClient/Editor/SceneGizmoController.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace KeireEditor::Detail
{
    Keire::UiPosition Add(const Keire::UiPosition left, const Keire::UiPosition right) noexcept
    {
        return {left.X + right.X, left.Y + right.Y};
    }

    Keire::UiPosition Subtract(const Keire::UiPosition left, const Keire::UiPosition right) noexcept
    {
        return {left.X - right.X, left.Y - right.Y};
    }

    Keire::UiPosition Scale(const Keire::UiPosition value, const float scale) noexcept
    {
        return {value.X * scale, value.Y * scale};
    }

    Keire::UiColor ScaleColor(const Keire::UiColor color, const float scale, const float alpha) noexcept
    {
        return {std::clamp(color.Red * scale, 0.0F, 1.0F), std::clamp(color.Green * scale, 0.0F, 1.0F),
                std::clamp(color.Blue * scale, 0.0F, 1.0F), alpha};
    }
} // namespace KeireEditor::Detail

namespace KeireEditor
{
    std::vector<SceneTransformTarget> SceneTransformGroup::Capture(const Keire::Ref<Keire::Scene>& scene,
                                                                   const std::span<const Keire::AssetId> selections,
                                                                   const Keire::EntityId primary)
    {
        std::vector<SceneTransformTarget> result;
        if (!scene || !primary)
            return result;
        std::unordered_set<Keire::AssetId> selectedIds(selections.begin(), selections.end());
        selectedIds.insert(primary.Value());
        result.reserve(selectedIds.size());
        for (const auto id : selectedIds)
        {
            auto entity = scene->FindEntity(Keire::EntityId(id));
            if (!entity)
                continue;
            bool selectedAncestor = false;
            for (auto parent = entity.Parent(); parent; parent = parent.Parent())
            {
                if (selectedIds.contains(parent.Id().Value()))
                {
                    selectedAncestor = true;
                    break;
                }
            }
            if (selectedAncestor)
                continue;
            const auto transform = entity.GetComponent<Keire::TransformComponent>();
            if (!transform)
                continue;
            result.push_back({transform, transform->WorldMatrix(), transform->LocalPosition(),
                              transform->LocalRotation(), transform->LocalScale()});
        }
        return result;
    }

    void SceneTransformGroup::Restore(const std::span<const SceneTransformTarget> targets)
    {
        for (const auto& target : targets)
        {
            target.Transform->SetLocalPosition(target.InitialPosition);
            target.Transform->SetLocalRotation(target.InitialRotation);
            target.Transform->SetLocalScale(target.InitialScale);
        }
    }

    void SceneTransformGroup::Apply(const std::span<const SceneTransformTarget> targets, const SceneTool tool,
                                    const SceneTransformAxis axis, const float amount, const Keire::Vector3 worldAxis,
                                    const Keire::Vector3 pivot, const Keire::Quaternion pivotRotation)
    {
        if (tool == SceneTool::View)
            return;
        const Keire::Vector3 one{1.0F, 1.0F, 1.0F};
        Keire::Matrix4 delta;
        if (tool == SceneTool::Translate)
        {
            delta = Keire::Math::ComposeTransform({worldAxis.X * amount, worldAxis.Y * amount, worldAxis.Z * amount},
                                                  {}, one);
        }
        else
        {
            const auto pivotFrame = Keire::Math::ComposeTransform(pivot, pivotRotation, one);
            Keire::Matrix4 localDelta;
            if (tool == SceneTool::Rotate)
            {
                Keire::Vector3 rotation;
                if (axis == SceneTransformAxis::X)
                    rotation.X = amount;
                else if (axis == SceneTransformAxis::Y)
                    rotation.Y = amount;
                else
                    rotation.Z = amount;
                localDelta = Keire::Math::ComposeTransform({}, Keire::Math::EulerDegreesToQuaternion(rotation), one);
            }
            else
            {
                Keire::Vector3 scale = one;
                const float factor = std::max(0.0001F, 1.0F + amount);
                if (axis == SceneTransformAxis::X || axis == SceneTransformAxis::Uniform)
                    scale.X = factor;
                if (axis == SceneTransformAxis::Y || axis == SceneTransformAxis::Uniform)
                    scale.Y = factor;
                if (axis == SceneTransformAxis::Z || axis == SceneTransformAxis::Uniform)
                    scale.Z = factor;
                localDelta = Keire::Math::ComposeTransform({}, {}, scale);
            }
            delta =
                Keire::Math::Multiply(Keire::Math::Multiply(pivotFrame, localDelta), Keire::Math::Inverse(pivotFrame));
        }
        for (const auto& target : targets)
        {
            const auto desiredWorld = Keire::Math::Multiply(delta, target.InitialWorld);
            const auto parent = target.Transform->Parent();
            const auto parentTransform =
                parent ? parent.GetComponent<Keire::TransformComponent>() : Keire::Ref<Keire::TransformComponent>{};
            const auto desiredLocal =
                parentTransform
                    ? Keire::Math::Multiply(Keire::Math::Inverse(parentTransform->WorldMatrix()), desiredWorld)
                    : desiredWorld;
            Keire::Vector3 position;
            Keire::Quaternion rotation;
            Keire::Vector3 scale;
            if (!Keire::Math::DecomposeTransform(desiredLocal, position, rotation, scale))
                continue;
            if (!Keire::TransformComponent::IsValidLocalScale(scale))
                continue;
            target.Transform->SetLocalPosition(position);
            target.Transform->SetLocalRotation(rotation);
            target.Transform->SetLocalScale(scale);
        }
    }

    SceneUiRectHandleEdit CalculateSceneUiRectHandleEdit(
        const SceneUiRectHandle handle, const std::span<const Keire::Vector2, 4> projectedCorners,
        const Keire::Vector2 pointerDelta, const Keire::Vector2 anchoredPosition, const Keire::Vector2 sizeDelta,
        const Keire::Vector2 referenceResolution, const bool canvasRoot)
    {
        SceneUiRectHandleEdit result{anchoredPosition, sizeDelta, referenceResolution};
        if (handle == SceneUiRectHandle::None)
            return result;
        const Keire::Vector2 horizontal{projectedCorners[1].X - projectedCorners[0].X,
                                        projectedCorners[1].Y - projectedCorners[0].Y};
        const Keire::Vector2 vertical{projectedCorners[3].X - projectedCorners[0].X,
                                      projectedCorners[3].Y - projectedCorners[0].Y};
        const float horizontalLength =
            std::max(std::sqrt(horizontal.X * horizontal.X + horizontal.Y * horizontal.Y), 1.0F);
        const float verticalLength = std::max(std::sqrt(vertical.X * vertical.X + vertical.Y * vertical.Y), 1.0F);
        const float horizontalPixels =
            pointerDelta.X * horizontal.X / horizontalLength + pointerDelta.Y * horizontal.Y / horizontalLength;
        const float verticalPixels =
            pointerDelta.X * vertical.X / verticalLength + pointerDelta.Y * vertical.Y / verticalLength;
        if (handle == SceneUiRectHandle::Center)
        {
            result.AnchoredPosition.X += horizontalPixels / horizontalLength * referenceResolution.X;
            result.AnchoredPosition.Y += verticalPixels / verticalLength * referenceResolution.Y;
            return result;
        }
        const bool right = handle == SceneUiRectHandle::TopRight || handle == SceneUiRectHandle::BottomRight;
        const bool bottom = handle == SceneUiRectHandle::BottomRight || handle == SceneUiRectHandle::BottomLeft;
        const float widthDelta = horizontalPixels / horizontalLength * referenceResolution.X * (right ? 1.0F : -1.0F);
        const float heightDelta = verticalPixels / verticalLength * referenceResolution.Y * (bottom ? 1.0F : -1.0F);
        if (canvasRoot)
        {
            result.ReferenceResolution.X = std::max(1.0F, referenceResolution.X + widthDelta);
            result.ReferenceResolution.Y = std::max(1.0F, referenceResolution.Y + heightDelta);
            return result;
        }
        result.SizeDelta.X = std::max(1.0F, sizeDelta.X + widthDelta);
        result.SizeDelta.Y = std::max(1.0F, sizeDelta.Y + heightDelta);
        result.AnchoredPosition.X += widthDelta * (right ? 0.5F : -0.5F);
        result.AnchoredPosition.Y += heightDelta * (bottom ? 0.5F : -0.5F);
        return result;
    }

    Keire::UiPosition StableNodeGraphCanvas::ToScreen(const Keire::Vector2 position,
                                                      const Keire::UiItemRect canvas) const noexcept
    {
        return {canvas.Minimum.X + (position.X + m_Pan.X) * m_Zoom, canvas.Minimum.Y + (position.Y + m_Pan.Y) * m_Zoom};
    }

    Keire::Vector2 StableNodeGraphCanvas::ToGraph(const Keire::UiPosition position,
                                                  const Keire::UiItemRect canvas) const noexcept
    {
        return {(position.X - canvas.Minimum.X) / m_Zoom - m_Pan.X, (position.Y - canvas.Minimum.Y) / m_Zoom - m_Pan.Y};
    }
} // namespace KeireEditor

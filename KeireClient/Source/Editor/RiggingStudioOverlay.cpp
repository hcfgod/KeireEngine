#include "KeireClient/Editor/RiggingStudioPanel.h"

#include "KeireClient/Editor/RigChainInspection.h"
#include "KeireClient/EditorWorkspaceLayer.h"

#include <cmath>
#include <optional>

namespace KeireEditor
{
    namespace
    {
        std::optional<Keire::UiPosition> ProjectJoint(const Keire::Vector3 point, const Keire::Matrix4& camera,
                                                      const Keire::UiItemRect viewport)
        {
            const auto& m = camera.Elements;
            const float x = m[0] * point.X + m[4] * point.Y + m[8] * point.Z + m[12];
            const float y = m[1] * point.X + m[5] * point.Y + m[9] * point.Z + m[13];
            const float w = m[3] * point.X + m[7] * point.Y + m[11] * point.Z + m[15];
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || w <= 0.0001F)
                return std::nullopt;
            return Keire::UiPosition{viewport.Minimum.X + (x / w * 0.5F + 0.5F) * viewport.Size().Width,
                                     viewport.Minimum.Y + (0.5F - y / w * 0.5F) * viewport.Size().Height};
        }
    } // namespace

    void RiggingStudioPanel::DrawChainOverlay(Keire::UiFrame& ui, const Keire::Ref<Keire::Scene>& scene,
                                              const Keire::EntityId selected, const Keire::RenderCamera& camera,
                                              const Keire::UiItemRect viewport)
    {
        if (!m_Registration.Visible() || !m_ShowChainOverlay || !m_ChainSkeleton || !scene)
            return;
        m_ChainOverlayDiagnostic.clear();
        const auto entity = scene->FindEntity(selected);
        const auto animator =
            entity ? entity.GetComponent<Keire::AnimatorComponent>() : Keire::Ref<Keire::AnimatorComponent>{};
        const auto transform =
            entity ? entity.GetComponent<Keire::TransformComponent>() : Keire::Ref<Keire::TransformComponent>{};
        const auto assets = m_Controller.RiggingStudioAssets();
        if (!animator || !transform || !assets || !animator->SkinnedMesh() ||
            assets->TryGetType(animator->SkinnedMesh()) != Keire::SkinnedMeshAsset::StaticType())
        {
            m_ChainOverlayDiagnostic = "Select a scene entity with an Animator and this model's skinned mesh.";
            return;
        }
        const auto skin =
            assets->Load<Keire::SkinnedMeshAsset>(animator->SkinnedMesh(), Keire::AssetPriority::Normal).TryGetLoaded();
        if (!skin || skin->Skeleton() != m_ChainSkeletonId)
        {
            m_ChainOverlayDiagnostic = "The selected entity's skin does not use this skeleton, or is still loading.";
            return;
        }
        if (assets->Load<Keire::SkeletonAsset>(m_ChainSkeletonId, Keire::AssetPriority::Normal).TryGetLoaded() !=
            m_ChainSkeleton)
        {
            m_ChainOverlayDiagnostic =
                "The skeleton changed. Reopen the chain inspector and select its endpoints again.";
            return;
        }
        const auto bones = m_ChainSkeleton->Bones();
        const auto find = [&](const std::string& name)
        {
            for (std::size_t index = 0; index < bones.size(); ++index)
                if (bones[index].Name == name)
                    return index;
            return bones.size();
        };
        const auto inspection = InspectRigChain(*m_ChainSkeleton, find(m_ChainRoot), find(m_ChainTip));
        if (!inspection.Valid())
            return;
        try
        {
            const auto points = PublishedRigChainPoints(*m_ChainSkeleton, animator->SkinPalette(), inspection.Bones,
                                                        transform->PresentationWorldMatrix());
            const auto viewProjection = Keire::Math::Multiply(camera.Projection, camera.View);
            auto clip = ui.PushClipRect(viewport);
            constexpr Keire::UiColor cyan{0.1F, 0.9F, 1.0F, 1.0F};
            constexpr Keire::UiColor yellow{1.0F, 0.85F, 0.15F, 0.8F};
            std::optional<Keire::UiPosition> previous;
            for (const auto point : points)
            {
                const auto current = ProjectJoint(point, viewProjection, viewport);
                if (current)
                {
                    ui.DrawCircle(*current, 4.0F, cyan, 2.0F);
                    if (previous)
                        ui.DrawLine(*previous, *current, cyan, 2.0F);
                }
                previous = current;
            }
            const auto root = ProjectJoint(points.front(), viewProjection, viewport);
            const auto tip = ProjectJoint(points.back(), viewProjection, viewport);
            if (root && tip)
                ui.DrawLine(*root, *tip, yellow, 1.0F);
        }
        catch (const std::exception& error)
        {
            m_ChainOverlayDiagnostic = error.what();
        }
    }
} // namespace KeireEditor

void EditorWorkspaceLayer::DrawSceneViewportRigChain(Keire::UiFrame& ui, const Keire::Ref<Keire::Scene>& scene,
                                                     const Keire::EntityId selected, const Keire::RenderCamera& camera,
                                                     const Keire::UiItemRect viewport)
{
    if (m_RiggingStudioPanel)
        m_RiggingStudioPanel->DrawChainOverlay(ui, scene, selected, camera, viewport);
}

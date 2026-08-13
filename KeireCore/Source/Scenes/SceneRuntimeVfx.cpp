#include "KeireInternal/Scenes/SceneRuntimeSessionImpl.h"

namespace Keire
{
    void SceneRuntimeSession::Impl::InitializeVfx(const VfxBackend backend)
    {
        ClearVfx();
        VfxWorldSpecification specification;
        specification.Backend = backend;
        specification.MaximumParticles = backend == VfxBackend::Gpu ? 1'000'000U : VfxRenderSnapshot::MaximumParticles;
        specification.CollisionQuery = [this](const Vector3 start, const Vector3 end)
        { return QueryVfxCollision(start, end); };
        specification.ShapeSample = [this](const AssetId asset, const std::uint32_t randomValue)
        { return SampleVfxShape(asset, randomValue); };
        VfxWorldService = CreateRef<VfxWorld>(std::move(specification));
        VfxBackendMode = backend;
    }

    void SceneRuntimeSession::Impl::InitializeVfx()
    {
        InitializeVfx(DeterministicSimulation || !Assets ? VfxBackend::Cpu : VfxBackend::Gpu);
    }

    void SceneRuntimeSession::Impl::SynchronizeVfx(const float deltaSeconds)
    {
        if (!VfxWorldService || !Runtime)
            return;

        std::set<EntityId> seen;
        bool fallbackToCpu = false;
        std::string fallbackDiagnostic;
        for (const auto& entity : Runtime->Query<VfxEmitterComponent>())
        {
            const auto emitter = entity.GetComponent<VfxEmitterComponent>();
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!emitter || !transform)
                continue;
            seen.emplace(entity.Id());
            auto& state = VfxEmitters[entity.Id()];
            if (state.Effect != emitter->Effect())
            {
                if (state.Handle)
                    VfxWorldService->Stop(state.Handle);
                state = {};
                state.Effect = emitter->Effect();
                if (state.Effect && Assets)
                    state.EffectHandle = Assets->Load<VfxEffectAsset>(state.Effect, AssetPriority::High);
            }
            if (!entity.ActiveInHierarchy() || !emitter->Enabled() || !emitter->PlayOnAwake() || !state.Effect)
            {
                if (state.Handle)
                {
                    VfxWorldService->Stop(state.Handle);
                    state.Handle = {};
                }
                continue;
            }

            const auto effect = state.EffectHandle.TryGetLoaded();
            if (!effect)
                continue;
            const auto overrides = CompatibleVfxOverrides(effect->Definition(), emitter->ParameterOverrides());
            Vector3 position;
            Quaternion rotation;
            Vector3 scale;
            if (!Math::DecomposeTransform(transform->WorldMatrix(), position, rotation, scale))
            {
                constexpr std::string_view diagnostic = "VFX Emitter Transform cannot be decomposed.";
                if (state.Diagnostic != diagnostic)
                {
                    KEIRE_CORE_ERROR("VFX emitter '{}' (entity={}) is disabled: {}", entity.Name(),
                                     entity.Id().Value().ToString(), diagnostic);
                    state.Diagnostic = diagnostic;
                }
                continue;
            }
            const auto revision = state.EffectHandle.Revision();
            if (state.RejectedRevision == revision && state.RejectedOverrides == overrides)
                continue;
            try
            {
                if (!state.Handle || !VfxWorldService->IsAlive(state.Handle))
                {
                    state.Handle = VfxWorldService->Activate(
                        {effect, revision, position, rotation, emitter->SeedOffset(), overrides});
                    state.Revision = revision;
                    state.Overrides = overrides;
                }
                else
                {
                    if (revision != state.Revision)
                    {
                        const auto reloadOverrides = CompatibleVfxOverrides(effect->Definition(), state.Overrides);
                        (void)VfxWorldService->Reload(state.Handle, effect, revision);
                        state.Revision = revision;
                        if (overrides == reloadOverrides)
                            state.Overrides = overrides;
                    }
                    if (state.Overrides != overrides)
                    {
                        VfxWorldService->SetParameterOverrides(state.Handle, overrides);
                        state.Overrides = overrides;
                    }
                    VfxWorldService->SetTransform(state.Handle, position, rotation);
                }
                if (state.Handle)
                    VfxWorldService->SetSimulationSpeed(state.Handle, emitter->SimulationSpeed());
                state.RejectedRevision = 0;
                state.RejectedOverrides.clear();
                state.Diagnostic.clear();
            }
            catch (const std::exception& exception)
            {
                if (VfxBackendMode == VfxBackend::Gpu)
                {
                    const auto gpuPrograms = CompileVfxEffectSystems(effect->Definition(), VfxBackend::Gpu);
                    const auto cpuPrograms = CompileVfxEffectSystems(effect->Definition(), VfxBackend::Cpu);
                    const auto allValid = [](const std::vector<VfxCompiledProgram>& programs)
                    { return !programs.empty() && std::ranges::all_of(programs, &VfxCompiledProgram::Valid); };
                    if (!allValid(gpuPrograms) && allValid(cpuPrograms))
                    {
                        fallbackToCpu = true;
                        fallbackDiagnostic = exception.what();
                        break;
                    }
                }
                if (state.Handle)
                {
                    VfxWorldService->Stop(state.Handle);
                    state.Handle = {};
                }
                state.RejectedRevision = revision;
                state.RejectedOverrides = overrides;
                if (state.Diagnostic != exception.what())
                {
                    KEIRE_CORE_ERROR("VFX emitter '{}' (entity={}, effect={}) is disabled: {}", entity.Name(),
                                     entity.Id().Value().ToString(), state.Effect.ToString(), exception.what());
                    state.Diagnostic = exception.what();
                }
            }
            catch (...)
            {
                if (state.Handle)
                {
                    VfxWorldService->Stop(state.Handle);
                    state.Handle = {};
                }
                state.RejectedRevision = revision;
                state.RejectedOverrides = overrides;
                constexpr std::string_view diagnostic = "VFX activation failed with a non-standard exception.";
                if (state.Diagnostic != diagnostic)
                {
                    KEIRE_CORE_ERROR("VFX emitter '{}' (entity={}, effect={}) is disabled: {}", entity.Name(),
                                     entity.Id().Value().ToString(), state.Effect.ToString(), diagnostic);
                    state.Diagnostic = diagnostic;
                }
            }
        }

        if (fallbackToCpu)
        {
            KEIRE_CORE_WARN("Scene VFX is falling back to the CPU backend because a GPU effect is unsupported: {}",
                            fallbackDiagnostic);
            InitializeVfx(VfxBackend::Cpu);
            SynchronizeVfx(deltaSeconds);
            return;
        }

        for (auto iterator = VfxEmitters.begin(); iterator != VfxEmitters.end();)
        {
            if (!seen.contains(iterator->first))
            {
                if (iterator->second.Handle)
                    VfxWorldService->Stop(iterator->second.Handle);
                iterator = VfxEmitters.erase(iterator);
            }
            else
                ++iterator;
        }

        std::set<EntityId> autoDestroy;
        for (const auto& [entityId, state] : VfxEmitters)
        {
            const auto entity = Runtime->FindEntity(entityId);
            const auto emitter = entity ? entity.GetComponent<VfxEmitterComponent>() : Ref<VfxEmitterComponent>{};
            if (emitter && emitter->AutoDestroy() && state.Handle && VfxWorldService->IsAlive(state.Handle))
                autoDestroy.emplace(entityId);
        }
        VfxWorldService->Update(deltaSeconds);
        for (const auto entityId : autoDestroy)
        {
            const auto found = VfxEmitters.find(entityId);
            if (found != VfxEmitters.end() && !VfxWorldService->IsAlive(found->second.Handle))
            {
                (void)Runtime->DestroyEntity(entityId);
                VfxEmitters.erase(found);
            }
        }
    }

    void SceneRuntimeSession::Impl::ClearVfx() noexcept
    {
        VfxEmitters.clear();
        VfxMeshShapes.clear();
        VfxVolumes.clear();
        if (VfxWorldService)
        {
            VfxWorldService->Clear();
            VfxWorldService.Reset();
        }
    }
} // namespace Keire

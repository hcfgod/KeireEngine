#include "KeireClient/Editor/ManagedRuntimeDiagnostics.h"

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] std::string FormatDiagnostic(const Keire::ManagedRuntimeDiagnostic& diagnostic)
        {
            const auto typeName =
                diagnostic.TypeName.empty() ? std::string_view("<unknown>") : std::string_view(diagnostic.TypeName);
            const auto message = diagnostic.Message.empty() ? std::string_view("<no diagnostic message>")
                                                            : std::string_view(diagnostic.Message);
            const auto entity = diagnostic.Entity ? diagnostic.Entity.ToString() : std::string("<none>");

            std::string result;
            result.reserve(typeName.size() + entity.size() + message.size() + 96);
            result.append("Type: ");
            result.append(typeName);
            result.append(" | Callback: ");
            result.append(ManagedBehaviourCallbackDisplayName(diagnostic.Callback));
            result.append(" | Entity: ");
            result.append(entity);
            result.append(" | Generation: ");
            result.append(std::to_string(diagnostic.Generation));
            result.append(" | Message: ");
            result.append(message);
            return result;
        }
    } // namespace

    std::string_view ManagedBehaviourCallbackDisplayName(const Keire::ManagedBehaviourCallback callback) noexcept
    {
        switch (callback)
        {
        case Keire::ManagedBehaviourCallback::Awake:
            return "Awake";
        case Keire::ManagedBehaviourCallback::Enable:
            return "OnEnable";
        case Keire::ManagedBehaviourCallback::Start:
            return "Start";
        case Keire::ManagedBehaviourCallback::FixedUpdate:
            return "FixedUpdate";
        case Keire::ManagedBehaviourCallback::Update:
            return "Update";
        case Keire::ManagedBehaviourCallback::LateUpdate:
            return "LateUpdate";
        case Keire::ManagedBehaviourCallback::AnimationEvent:
            return "OnAnimationEvent";
        case Keire::ManagedBehaviourCallback::PhysicsContact:
            return "Physics Contact";
        case Keire::ManagedBehaviourCallback::Disable:
            return "OnDisable";
        case Keire::ManagedBehaviourCallback::Destroy:
            return "OnDestroy";
        case Keire::ManagedBehaviourCallback::BeforeReload:
            return "OnBeforeReload";
        case Keire::ManagedBehaviourCallback::AfterReload:
            return "OnAfterReload";
        case Keire::ManagedBehaviourCallback::AnimatorIk:
            return "OnAnimatorIk";
        case Keire::ManagedBehaviourCallback::ProceduralMotionEvent:
            return "OnProceduralMotionEvent";
        }
        return "Unknown";
    }

    std::vector<ManagedRuntimeConsoleEntry>
    ManagedRuntimeDiagnosticsBridge::Collect(const std::span<const Keire::ManagedRuntimeDiagnostic> diagnostics)
    {
        const auto first = m_ConsumedCount <= diagnostics.size() ? m_ConsumedCount : std::size_t{0};
        std::vector<ManagedRuntimeConsoleEntry> result;
        result.reserve(diagnostics.size() - first);
        for (const auto& diagnostic : diagnostics.subspan(first))
        {
            result.push_back({diagnostic.Severity, diagnostic.Generation, FormatDiagnostic(diagnostic)});
        }
        m_ConsumedCount = diagnostics.size();
        return result;
    }

    void ManagedRuntimeDiagnosticsBridge::Reset() noexcept { m_ConsumedCount = 0; }
} // namespace KeireEditor

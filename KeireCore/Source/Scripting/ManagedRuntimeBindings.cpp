#include "KeireInternal/Scripting/ManagedRuntimeBindings.h"

namespace Keire::Detail
{
    ManagedRuntimeBindingsScope::ManagedRuntimeBindingsScope(IScriptRuntimeServices* services) noexcept
        : m_Assets(services), m_Foundation(services), m_Input(services), m_Physics(services), m_World(services),
          m_Rendering(services), m_Ui(services)
    {
    }

    void RegisterManagedRuntimeBindings(Coral::ManagedAssembly& assembly)
    {
        RegisterManagedRuntimeAssets(assembly);
        RegisterManagedRuntimeFoundation(assembly);
        RegisterManagedRuntimeInput(assembly);
        RegisterManagedRuntimePhysics(assembly);
        RegisterManagedRuntimeWorld(assembly);
        RegisterManagedRuntimeRendering(assembly);
        RegisterManagedRuntimeUi(assembly);
    }
} // namespace Keire::Detail

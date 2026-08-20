#include "KeireInternal/Scripting/ManagedRuntimeBindings.h"

namespace Keire::Detail
{
    ManagedRuntimeBindingsScope::ManagedRuntimeBindingsScope(IScriptRuntimeServices* services) noexcept
        : m_Foundation(services), m_World(services), m_Rendering(services), m_Ui(services)
    {
    }

    void RegisterManagedRuntimeBindings(Coral::ManagedAssembly& assembly)
    {
        RegisterManagedRuntimeFoundation(assembly);
        RegisterManagedRuntimeWorld(assembly);
        RegisterManagedRuntimeRendering(assembly);
        RegisterManagedRuntimeUi(assembly);
    }
} // namespace Keire::Detail

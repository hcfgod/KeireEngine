#pragma once

#include "Keire/Scripting/ScriptSystem.h"

namespace Coral
{
    class ManagedAssembly;
}

namespace Keire::Detail
{
    class ManagedRuntimeWorldScope final
    {
      public:
        explicit ManagedRuntimeWorldScope(IScriptRuntimeServices* services) noexcept;
        ~ManagedRuntimeWorldScope();

        ManagedRuntimeWorldScope(const ManagedRuntimeWorldScope&) = delete;
        ManagedRuntimeWorldScope& operator=(const ManagedRuntimeWorldScope&) = delete;

      private:
        IScriptRuntimeServices* m_Previous = nullptr;
    };

    void RegisterManagedRuntimeWorld(Coral::ManagedAssembly& assembly);
} // namespace Keire::Detail

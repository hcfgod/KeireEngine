#pragma once

#include "Keire/Scripting/ScriptSystem.h"

namespace Coral
{
    class ManagedAssembly;
}

namespace Keire::Detail
{
    class ManagedRuntimeFoundationScope final
    {
      public:
        explicit ManagedRuntimeFoundationScope(IScriptRuntimeServices* services) noexcept;
        ~ManagedRuntimeFoundationScope();

        ManagedRuntimeFoundationScope(const ManagedRuntimeFoundationScope&) = delete;
        ManagedRuntimeFoundationScope& operator=(const ManagedRuntimeFoundationScope&) = delete;

      private:
        IScriptRuntimeServices* m_Previous = nullptr;
    };

    void RegisterManagedRuntimeFoundation(Coral::ManagedAssembly& assembly);
} // namespace Keire::Detail

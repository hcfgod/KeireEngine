#pragma once

#include "Keire/Scripting/ScriptSystem.h"

namespace Coral
{
    class ManagedAssembly;
}

namespace Keire::Detail
{
    class ManagedRuntimeUiScope final
    {
      public:
        explicit ManagedRuntimeUiScope(IScriptRuntimeServices* services) noexcept;
        ~ManagedRuntimeUiScope();

        ManagedRuntimeUiScope(const ManagedRuntimeUiScope&) = delete;
        ManagedRuntimeUiScope& operator=(const ManagedRuntimeUiScope&) = delete;

      private:
        IScriptRuntimeServices* m_Previous = nullptr;
    };

    void RegisterManagedRuntimeUi(Coral::ManagedAssembly& assembly);
} // namespace Keire::Detail

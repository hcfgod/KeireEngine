#pragma once

#include "Keire/Scripting/ScriptSystem.h"

namespace Coral
{
    class ManagedAssembly;
}

namespace Keire::Detail
{
    class ManagedRuntimeRenderingScope final
    {
      public:
        explicit ManagedRuntimeRenderingScope(IScriptRuntimeServices* services) noexcept;
        ~ManagedRuntimeRenderingScope();

        ManagedRuntimeRenderingScope(const ManagedRuntimeRenderingScope&) = delete;
        ManagedRuntimeRenderingScope& operator=(const ManagedRuntimeRenderingScope&) = delete;

      private:
        IScriptRuntimeServices* m_Previous = nullptr;
    };

    void RegisterManagedRuntimeRendering(Coral::ManagedAssembly& assembly);
} // namespace Keire::Detail

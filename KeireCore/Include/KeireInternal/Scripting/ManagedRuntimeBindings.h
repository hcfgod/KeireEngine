#pragma once

#include "KeireInternal/Scripting/ManagedRuntimeFoundation.h"
#include "KeireInternal/Scripting/ManagedRuntimeRendering.h"
#include "KeireInternal/Scripting/ManagedRuntimeUi.h"
#include "KeireInternal/Scripting/ManagedRuntimeWorld.h"

namespace Keire::Detail
{
    class ManagedRuntimeBindingsScope final
    {
      public:
        explicit ManagedRuntimeBindingsScope(IScriptRuntimeServices* services) noexcept;

        ManagedRuntimeBindingsScope(const ManagedRuntimeBindingsScope&) = delete;
        ManagedRuntimeBindingsScope& operator=(const ManagedRuntimeBindingsScope&) = delete;

      private:
        ManagedRuntimeFoundationScope m_Foundation;
        ManagedRuntimeWorldScope m_World;
        ManagedRuntimeRenderingScope m_Rendering;
        ManagedRuntimeUiScope m_Ui;
    };

    void RegisterManagedRuntimeBindings(Coral::ManagedAssembly& assembly);
} // namespace Keire::Detail

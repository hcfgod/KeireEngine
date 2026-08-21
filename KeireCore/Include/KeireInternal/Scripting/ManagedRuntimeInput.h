#pragma once

#include "Keire/Input/Input.h"
#include "Keire/Scripting/ScriptSystem.h"

#include <memory>

namespace Coral
{
    class ManagedAssembly;
}

namespace Keire::Detail
{
    class ManagedInputOperationStore final
    {
      public:
        ManagedInputOperationStore();
        ~ManagedInputOperationStore();

        ManagedInputOperationStore(const ManagedInputOperationStore&) = delete;
        ManagedInputOperationStore& operator=(const ManagedInputOperationStore&) = delete;

        [[nodiscard]] std::uint64_t Begin(const Ref<InputSystem>& input, const Ref<InputActionContext>& context,
                                          AssetId binding, ManagedInputRebindOptions options) noexcept;
        [[nodiscard]] std::optional<ManagedInputRebindSnapshot> Status(std::uint64_t operation) const noexcept;
        [[nodiscard]] bool Resolve(std::uint64_t operation, ManagedInputRebindResolution resolution) noexcept;
        [[nodiscard]] bool Cancel(std::uint64_t operation) noexcept;
        void CancelAll() noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    class ManagedRuntimeInputScope final
    {
      public:
        explicit ManagedRuntimeInputScope(IScriptRuntimeServices* services) noexcept;
        ~ManagedRuntimeInputScope();

        ManagedRuntimeInputScope(const ManagedRuntimeInputScope&) = delete;
        ManagedRuntimeInputScope& operator=(const ManagedRuntimeInputScope&) = delete;

      private:
        IScriptRuntimeServices* m_Previous = nullptr;
    };

    void RegisterManagedRuntimeInput(Coral::ManagedAssembly& assembly);
} // namespace Keire::Detail

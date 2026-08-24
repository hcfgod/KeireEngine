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
    class ManagedInputContextStore final
    {
      public:
        ManagedInputContextStore();
        ~ManagedInputContextStore();

        ManagedInputContextStore(const ManagedInputContextStore&) = delete;
        ManagedInputContextStore& operator=(const ManagedInputContextStore&) = delete;

        [[nodiscard]] std::uint64_t Create(std::uint64_t generation, const Ref<InputSystem>& input, InputUserId user,
                                           AssetId asset) noexcept;
        [[nodiscard]] bool Release(std::uint64_t handle) noexcept;
        void ReleaseGeneration(std::uint64_t generation) noexcept;
        void ReleaseAll() noexcept;
        [[nodiscard]] bool Operate(std::uint64_t handle, ManagedInputContextOperation operation,
                                   AssetId target) noexcept;
        [[nodiscard]] AssetId FindMap(std::uint64_t handle, std::string_view name) const noexcept;
        [[nodiscard]] AssetId FindAction(std::uint64_t handle, AssetId map, std::string_view name) const noexcept;
        [[nodiscard]] std::optional<ManagedInputActionSnapshot> Action(std::uint64_t handle,
                                                                       AssetId action) const noexcept;
        [[nodiscard]] Ref<InputActionContext> Context(std::uint64_t handle) const noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

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

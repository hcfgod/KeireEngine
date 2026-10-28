#pragma once

#include "Keire/Assets/Asset.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace KeireEditor
{
    enum class SceneTransitionKind : std::uint8_t
    {
        Create,
        Open,
        Close,
        Exit
    };

    enum class SceneTransitionState : std::uint8_t
    {
        Idle,
        Queued,
        Committing,
        Failed
    };

    struct SceneTransitionRequest final
    {
        SceneTransitionKind Kind = SceneTransitionKind::Open;
        Keire::AssetId Asset;
    };

    class SceneTransitionCoordinator final
    {
      public:
        [[nodiscard]] bool Request(SceneTransitionRequest request);
        [[nodiscard]] std::optional<SceneTransitionRequest> BeginCommit();
        void Complete() noexcept;
        void Fail(std::string diagnostic) noexcept;
        void Cancel() noexcept;

        void DeferStartupScene(Keire::AssetId asset) noexcept;
        [[nodiscard]] Keire::AssetId PendingStartupScene() const noexcept;
        [[nodiscard]] Keire::AssetId TakeStartupScene() noexcept;

        [[nodiscard]] bool Pending() const noexcept;
        [[nodiscard]] SceneTransitionState State() const noexcept;
        [[nodiscard]] std::string_view Diagnostic() const noexcept;

      private:
        std::optional<SceneTransitionRequest> m_Request;
        Keire::AssetId m_StartupScene;
        SceneTransitionState m_State = SceneTransitionState::Idle;
        std::string m_Diagnostic;
    };
} // namespace KeireEditor

#pragma once

#include "Keire/Assets/Asset.h"
#include "KeireInternal/Process.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace KeireEditor
{
    enum class PlayerBuildState : std::uint8_t
    {
        Idle,
        Running,
        Succeeded,
        Failed,
        Cancelled
    };

    struct PlayerBuildStatus
    {
        PlayerBuildState State = PlayerBuildState::Idle;
        std::string Phase;
        float Progress = 0.0F;
        std::string Message;
        std::filesystem::path Output;
        std::filesystem::path Executable;
    };

    class PlayerBuildService final
    {
      public:
        PlayerBuildService(std::filesystem::path editorExecutable, std::filesystem::path projectRoot);
        ~PlayerBuildService();

        PlayerBuildService(const PlayerBuildService&) = delete;
        PlayerBuildService& operator=(const PlayerBuildService&) = delete;

        void Start(Keire::AssetId profile, bool runAfterBuild);
        void Update();
        void Cancel() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool Busy() const noexcept;
        [[nodiscard]] const PlayerBuildStatus& Status() const noexcept { return m_Status; }

        [[nodiscard]] static std::filesystem::path
        ResolveBuilderExecutable(const std::filesystem::path& editorExecutable);

      private:
        void ReadStatus(bool required);
        void Finish(bool cancelled = false);

        std::filesystem::path m_BuilderExecutable;
        std::filesystem::path m_ProjectRoot;
        std::filesystem::path m_OperationDirectory;
        std::filesystem::path m_StatusPath;
        std::filesystem::path m_StagingPath;
        std::optional<Keire::Detail::ChildProcess> m_Process;
        PlayerBuildStatus m_Status;
        std::string m_Output;
        bool m_RunAfterBuild = false;
        bool m_ShuttingDown = false;
    };
} // namespace KeireEditor

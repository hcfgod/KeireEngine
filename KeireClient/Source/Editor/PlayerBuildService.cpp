#include "KeireClient/Editor/PlayerBuildService.h"

#include "KeireInternal/Build/PlayerPackage.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <vector>

namespace KeireEditor
{
    namespace
    {
        constexpr auto ShutdownTimeout = std::chrono::milliseconds(500);

        [[nodiscard]] PlayerBuildState ParseState(const std::string_view state)
        {
            if (state == "running")
                return PlayerBuildState::Running;
            if (state == "succeeded")
                return PlayerBuildState::Succeeded;
            if (state == "failed")
                return PlayerBuildState::Failed;
            throw std::runtime_error("Player builder returned an unknown state.");
        }
    } // namespace

    PlayerBuildService::PlayerBuildService(std::filesystem::path editorExecutable, std::filesystem::path projectRoot)
        : m_BuilderExecutable(ResolveBuilderExecutable(editorExecutable)),
          m_ProjectRoot(std::filesystem::absolute(std::move(projectRoot)).lexically_normal())
    {
        if (!std::filesystem::is_regular_file(m_BuilderExecutable))
            throw std::runtime_error("Kéire player builder was not found: " +
                                     Keire::Detail::PathToUtf8(m_BuilderExecutable));
        if (!std::filesystem::is_directory(m_ProjectRoot / "ProjectSettings"))
            throw std::invalid_argument("Player build service requires a valid project root.");
    }

    PlayerBuildService::~PlayerBuildService() { Shutdown(); }

    void PlayerBuildService::Start(const Keire::AssetId profile, const bool runAfterBuild)
    {
        if (m_ShuttingDown)
            throw std::logic_error("Player build service is shutting down.");
        if (m_Process)
            throw std::logic_error("A player build is already running.");
        if (!profile)
            throw std::invalid_argument("Player build requires a profile ID.");

        auto operationId = Keire::AssetId::Generate().ToString();
        auto stagingId = operationId;
        std::erase(stagingId, '-');
        stagingId.resize(12);
        m_OperationDirectory = m_ProjectRoot / "Library" / "PlayerBuild" / operationId;
        m_StatusPath = m_OperationDirectory / "status.json";
        m_StagingPath = m_ProjectRoot / "Build" / ".staging" / stagingId;
        std::filesystem::create_directories(m_OperationDirectory);
        const std::vector<std::string> arguments{
            "build-player",     "--project", Keire::Detail::PathToUtf8(m_ProjectRoot), "--profile",
            profile.ToString(), "--status",  Keire::Detail::PathToUtf8(m_StatusPath),  "--staging-id",
            stagingId};
        m_Process.emplace(Keire::Detail::ChildProcess::Start(m_BuilderExecutable, arguments, m_ProjectRoot));
        m_Status = {.State = PlayerBuildState::Running,
                    .Phase = "start",
                    .Progress = 0.0F,
                    .Message = "Starting the isolated player builder."};
        m_Output.clear();
        m_RunAfterBuild = runAfterBuild;
    }

    void PlayerBuildService::Update()
    {
        if (!m_Process)
            return;
        try
        {
            ReadStatus(false);
        }
        catch (const std::exception&)
        {
            // Status files are atomically replaced. The final document is required after process exit.
        }
        if (m_Process->Poll())
            Finish();
    }

    void PlayerBuildService::Cancel() noexcept
    {
        if (!m_Process)
            return;
        try
        {
            m_Process->Terminate();
            (void)m_Process->WaitFor(ShutdownTimeout);
            Finish(true);
        }
        catch (...)
        {
            m_Process.reset();
            m_Status = {.State = PlayerBuildState::Cancelled,
                        .Phase = "cancelled",
                        .Progress = 1.0F,
                        .Message = "Player build was cancelled."};
        }
    }

    void PlayerBuildService::Shutdown() noexcept
    {
        if (m_ShuttingDown)
            return;
        m_ShuttingDown = true;
        Cancel();
    }

    bool PlayerBuildService::Busy() const noexcept { return m_Process.has_value(); }

    std::filesystem::path PlayerBuildService::ResolveBuilderExecutable(const std::filesystem::path& editorExecutable)
    {
        const auto editor = std::filesystem::absolute(editorExecutable).lexically_normal();
        try
        {
            return Keire::Detail::ResolveCompanionExecutable(editor, "KeireAssetTool");
        }
        catch (const std::exception&)
        {
#if defined(_WIN32)
            constexpr std::string_view name = "KeireAssetTool.exe";
#else
            constexpr std::string_view name = "KeireAssetTool";
#endif
            return editor.parent_path() / Keire::Detail::PathFromUtf8(name);
        }
    }

    void PlayerBuildService::ReadStatus(const bool required)
    {
        if (!std::filesystem::is_regular_file(m_StatusPath))
        {
            if (required)
                throw std::runtime_error("Player builder exited without a final status document.");
            return;
        }
        const auto document = Keire::Detail::ReadPlayerBuildStatusDocument(m_StatusPath);
        m_Status.State = ParseState(document.State);
        m_Status.Phase = document.Phase;
        m_Status.Progress = document.Progress;
        m_Status.Message = document.Message;
        m_Status.Output = document.Output;
        m_Status.Executable = document.Executable;
    }

    void PlayerBuildService::Finish(const bool cancelled)
    {
        auto process = std::move(*m_Process);
        m_Process.reset();
        m_Output += process.TakeOutput();
        const auto exitCode = process.ExitCode().value_or(cancelled ? 1 : 127);
        try
        {
            Keire::Detail::WriteTextFileAtomically(m_OperationDirectory / "builder.log", m_Output);
            if (cancelled)
            {
                m_Status = {.State = PlayerBuildState::Cancelled,
                            .Phase = "cancelled",
                            .Progress = 1.0F,
                            .Message = "Player build was cancelled."};
            }
            else
            {
                ReadStatus(true);
                if (exitCode != 0)
                    m_Status.State = PlayerBuildState::Failed;
                if (m_Status.State == PlayerBuildState::Succeeded && m_RunAfterBuild)
                {
                    std::string diagnostic;
                    if (!Keire::Detail::LaunchDetachedProcess(m_Status.Executable, {}, m_Status.Output, diagnostic))
                    {
                        m_Status.State = PlayerBuildState::Failed;
                        m_Status.Message = "Player built successfully but could not launch: " + diagnostic;
                    }
                }
            }
        }
        catch (const std::exception& error)
        {
            m_Status = {.State = cancelled ? PlayerBuildState::Cancelled : PlayerBuildState::Failed,
                        .Phase = cancelled ? "cancelled" : "failed",
                        .Progress = 1.0F,
                        .Message = error.what()};
        }
        std::error_code ignored;
        std::filesystem::remove_all(m_StagingPath, ignored);
        m_RunAfterBuild = false;
    }
} // namespace KeireEditor

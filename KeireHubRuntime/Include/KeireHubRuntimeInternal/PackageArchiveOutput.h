#pragma once

#include "KeireHubRuntime/HubError.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace KeireHub::Detail
{
    struct ExclusivePackageOutputTestHooks final
    {
        // Tests use this instance-local hook to exercise the failure boundary immediately after the final name is
        // atomically committed. Production callers leave it empty.
        std::function<bool()> FailAfterCommit;
    };

    class ExclusivePackageOutput final
    {
      public:
        [[nodiscard]] static HubResult<std::unique_ptr<ExclusivePackageOutput>>
        Create(const std::filesystem::path& output, std::string item, ExclusivePackageOutputTestHooks testHooks = {});

        ~ExclusivePackageOutput();

        ExclusivePackageOutput(const ExclusivePackageOutput&) = delete;
        ExclusivePackageOutput& operator=(const ExclusivePackageOutput&) = delete;

        [[nodiscard]] HubStatus Write(std::span<const char> bytes);
        [[nodiscard]] HubStatus Finish();

        // The atomic final-name change is the commit point. No-replace publication reports a post-commit failure only
        // after it has safely removed that final name, so an immediate retry cannot collide with its own output.
        // Replace-existing publication cannot restore the displaced file and therefore reports success after commit;
        // its caller must perform any additional journal synchronization or reconciliation.
        [[nodiscard]] HubStatus Publish(const std::filesystem::path& output, bool replaceExisting = false);

      private:
        struct Implementation;

        explicit ExclusivePackageOutput(std::unique_ptr<Implementation> implementation);

        std::unique_ptr<Implementation> m_Implementation;
    };
} // namespace KeireHub::Detail

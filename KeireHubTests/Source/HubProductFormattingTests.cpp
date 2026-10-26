#include "KeireHubInternal/HubProductFormatting.h"

#include "KeireHub/HubProductUi.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>

TEST_CASE("Hub byte formatting preserves binary units and display precision")
{
    using KeireHub::Detail::FormatHubBytes;
    CHECK(FormatHubBytes(0) == "0 B");
    CHECK(FormatHubBytes(1023) == "1023 B");
    CHECK(FormatHubBytes(1024) == "1.0 KiB");
    CHECK(FormatHubBytes(1536) == "1.5 KiB");
    CHECK(FormatHubBytes(10240) == "10 KiB");
    CHECK(FormatHubBytes(1024ULL * 1024ULL) == "1.0 MiB");
    CHECK(FormatHubBytes(1024ULL * 1024ULL * 1024ULL) == "1.0 GiB");
    CHECK(FormatHubBytes(1024ULL * 1024ULL * 1024ULL * 1024ULL) == "1.0 TiB");
    CHECK(FormatHubBytes((std::numeric_limits<std::uint64_t>::max)()) == "16777216 TiB");
}

TEST_CASE("Hub task details preserve transfer rate package and remaining component information")
{
    using KeireHub::Detail::TaskActivityDetails;
    KeireHub::HubTaskUiRecord task;
    CHECK(TaskActivityDetails(task).empty());
    task.BytesTransferred = 1024;
    task.BytesPerSecond = 512;
    CHECK(TaskActivityDetails(task).empty());
    task.TotalBytes = 2048;
    CHECK(TaskActivityDetails(task) == "1.0 KiB / 2.0 KiB  •  512 B/s");
    task.CurrentPackage = "Editor";
    task.RemainingComponents = 2;
    CHECK(TaskActivityDetails(task) == "1.0 KiB / 2.0 KiB  •  512 B/s  •  Editor  •  2 remaining");
    task.BytesPerSecond = 0;
    CHECK(TaskActivityDetails(task) == "1.0 KiB / 2.0 KiB  •  Editor  •  2 remaining");
    task.TotalBytes = 0;
    CHECK(TaskActivityDetails(task) == "Editor  •  2 remaining");
    task.CurrentPackage.clear();
    CHECK(TaskActivityDetails(task) == "2 remaining");
}

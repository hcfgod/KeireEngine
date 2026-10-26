#include "KeireHubInternal/HubProductFormatting.h"

#include "KeireHub/HubProductUi.h"

#include <array>
#include <cstddef>
#include <sstream>

namespace KeireHub::Detail
{
    [[nodiscard]] std::string FormatHubBytes(const std::uint64_t bytes)
    {
        constexpr std::array suffixes{"B", "KiB", "MiB", "GiB", "TiB"};
        double value = static_cast<double>(bytes);
        std::size_t suffix = 0;
        while (value >= 1024.0 && suffix + 1 < suffixes.size())
        {
            value /= 1024.0;
            ++suffix;
        }
        std::ostringstream stream;
        if (suffix == 0)
        {
            stream << bytes;
        }
        else
        {
            stream.setf(std::ios::fixed);
            stream.precision(value < 10.0 ? 1 : 0);
            stream << value;
        }
        stream << ' ' << suffixes[suffix];
        return stream.str();
    }

    [[nodiscard]] std::string TaskActivityDetails(const HubTaskUiRecord& task)
    {
        std::string details;
        if (task.TotalBytes != 0)
        {
            details = FormatHubBytes(task.BytesTransferred) + " / " + FormatHubBytes(task.TotalBytes);
            if (task.BytesPerSecond != 0)
            {
                details += "  •  " + FormatHubBytes(task.BytesPerSecond) + "/s";
            }
        }
        if (!task.CurrentPackage.empty())
        {
            if (!details.empty())
            {
                details += "  •  ";
            }
            details += task.CurrentPackage;
        }
        if (task.RemainingComponents != 0)
        {
            if (!details.empty())
            {
                details += "  •  ";
            }
            details += std::to_string(task.RemainingComponents) + " remaining";
        }
        return details;
    }

} // namespace KeireHub::Detail

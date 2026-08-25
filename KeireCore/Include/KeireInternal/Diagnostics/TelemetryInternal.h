#pragma once

#if defined(KEIRE_PROFILE_TELEMETRY)
#include <tracy/Tracy.hpp>

#define KEIRE_TELEMETRY_ZONE_SCOPED(Name) ZoneScopedN(Name)

namespace Keire::Internal
{
    inline void TelemetryFrameMark() noexcept { FrameMark; }

    inline void TelemetrySetThreadName(const char* const name) noexcept { tracy::SetThreadName(name); }

    inline void TelemetryPlot(const char* const name, const double value) noexcept { TracyPlot(name, value); }
} // namespace Keire::Internal
#else
#define KEIRE_TELEMETRY_ZONE_SCOPED(Name) static_cast<void>(sizeof(Name))

namespace Keire::Internal
{
    inline void TelemetryFrameMark() noexcept {}

    inline void TelemetrySetThreadName(const char*) noexcept {}

    inline void TelemetryPlot(const char*, double) noexcept {}
} // namespace Keire::Internal
#endif

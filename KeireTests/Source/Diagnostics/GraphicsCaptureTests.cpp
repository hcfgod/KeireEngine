#include "KeireInternal/Diagnostics/GraphicsCaptureInternal.h"

#include <doctest/doctest.h>

TEST_CASE("graphics capture seam never injects an unavailable provider")
{
    const auto before = Keire::Internal::QueryGraphicsCaptureStatus();
    if (before.State == Keire::Internal::GraphicsCaptureState::Unavailable)
    {
        CHECK(Keire::Internal::QueueGraphicsCaptureNextFrame() ==
              Keire::Internal::GraphicsCaptureRequestResult::Unavailable);
        CHECK(Keire::Internal::QueryGraphicsCaptureStatus() == before);
    }
    else
    {
        CHECK(before.Provider == Keire::Internal::GraphicsCaptureProvider::RenderDoc);
        CHECK(before.Available());
    }
}

#pragma once

#include <stdexcept>
#include <utility>

namespace Keire::RenderBackend
{
    // The owning render-thread cache retains successful handles through device shutdown/recovery.
    // Failed creation leaves the slot empty so a subsequent attempt cannot observe a partial handle.
    template <typename Pipeline, typename Factory>
    [[nodiscard]] Pipeline* ResolveOnDemandPipeline(Pipeline*& slot, Factory&& create)
    {
        if (!slot)
        {
            auto* created = std::forward<Factory>(create)();
            if (!created)
            {
                throw std::runtime_error("On-demand graphics pipeline creation returned no handle.");
            }
            slot = created;
        }
        return slot;
    }
} // namespace Keire::RenderBackend

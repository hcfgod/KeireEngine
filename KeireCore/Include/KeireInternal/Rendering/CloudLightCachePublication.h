#pragma once

#include "KeireInternal/Rendering/CloudLightCacheKey.h"

#include <memory>
#include <optional>
#include <vector>

namespace Keire::RenderBackend
{
    struct CloudLightCacheValidity final
    {
        std::optional<CloudLightCacheKey> SubmittedKey;
        std::optional<CloudLightCacheKey> RecordedKey;

        void Submit() noexcept
        {
            if (RecordedKey)
            {
                SubmittedKey = RecordedKey;
                RecordedKey.reset();
            }
        }

        void Invalidate() noexcept
        {
            SubmittedKey.reset();
            RecordedKey.reset();
        }
    };

    // Shared validity survives resource moves/retirement without retaining any GPU handles.
    class CloudLightCachePublications final
    {
      public:
        CloudLightCachePublications() = default;

        CloudLightCachePublications(const CloudLightCachePublications&) = delete;
        CloudLightCachePublications& operator=(const CloudLightCachePublications&) = delete;

        ~CloudLightCachePublications() { Invalidate(); }

        void Track(const std::shared_ptr<CloudLightCacheValidity>& state)
        {
            try
            {
                m_Pending.push_back(state);
            }
            catch (...)
            {
                state->Invalidate();
                throw;
            }
        }

        void Submit() noexcept
        {
            for (const auto& state : m_Pending)
            {
                state->Submit();
            }
            m_Pending.clear();
        }

        void Invalidate() noexcept
        {
            for (const auto& state : m_Pending)
            {
                state->Invalidate();
            }
            m_Pending.clear();
        }

      private:
        std::vector<std::shared_ptr<CloudLightCacheValidity>> m_Pending;
    };
} // namespace Keire::RenderBackend

#include "Keire/Rendering/MaterialEcosystem.h"

#include <algorithm>
#include <stdexcept>

namespace Keire
{
    MaterialNumericUniformCache::MaterialNumericUniformCache(const std::size_t maximumUniforms)
        : m_MaximumUniforms(maximumUniforms)
    {
        if (maximumUniforms == 0 || maximumUniforms > MaximumMaterialNumericUniforms)
            throw std::invalid_argument("Material numeric uniform cache bound must be between 1 and 256.");
    }

    MaterialNumericUniformUpdate MaterialNumericUniformCache::Update(const MaterialNumericUniformSnapshot& snapshot)
    {
        if (snapshot.Values.size() > m_MaximumUniforms)
            throw std::invalid_argument("Material numeric uniform snapshot exceeds the cache bound.");
        if (std::ranges::any_of(snapshot.Values, [](const Vector4 value) { return !Math::IsFinite(value); }))
            throw std::invalid_argument("Material numeric uniform snapshot contains a non-finite value.");

        MaterialNumericUniformUpdate result;
        result.Revision = snapshot.Revision;
        result.Values = snapshot.Values;
        if (!m_Initialized || snapshot.Values.size() != m_Values.size())
        {
            result.FullUpload = true;
            if (!snapshot.Values.empty())
                result.DirtyRanges.push_back({0, snapshot.Values.size()});
        }
        else
        {
            std::size_t index = 0;
            while (index < snapshot.Values.size())
            {
                if (snapshot.Values[index] == m_Values[index])
                {
                    ++index;
                    continue;
                }
                const auto first = index;
                do
                    ++index;
                while (index < snapshot.Values.size() && snapshot.Values[index] != m_Values[index]);
                result.DirtyRanges.push_back({first, index - first});
            }
        }

        m_Revision = snapshot.Revision;
        m_Values = snapshot.Values;
        m_Initialized = true;
        return result;
    }

    void MaterialNumericUniformCache::Reset() noexcept
    {
        m_Revision = 0;
        m_Values.clear();
        m_Initialized = false;
    }
} // namespace Keire

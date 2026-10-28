#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Keire::Detail
{
    [[nodiscard]] std::string ShaderGraphVolumeHlsl(bool unlit, std::uint32_t noiseSlot);
    [[nodiscard]] std::string ShaderGraphVolumeEvaluationHlsl(std::string_view depthOffset, bool unlit);
    [[nodiscard]] std::string ShaderGraphFragmentTailHlsl(bool screenSpace, bool premultiplied,
                                                          std::string_view depthOffset);
} // namespace Keire::Detail

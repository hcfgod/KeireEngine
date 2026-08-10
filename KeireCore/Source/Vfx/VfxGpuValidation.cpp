#include "KeireInternal/Vfx/VfxGpuValidationInternal.h"

#include <algorithm>
#include <ranges>

namespace Keire::Internal
{
    bool ValidVfxExtendedGpuInstructionSignature(const VfxValueOpcode opcode, const VfxValueType output,
                                                 const std::uint32_t outputIndex,
                                                 const std::span<const VfxValueType> inputs) noexcept
    {
        const auto allScalar = [&inputs]()
        { return std::ranges::all_of(inputs, [](const VfxValueType type) { return type == VfxValueType::Scalar; }); };
        switch (opcode)
        {
        case VfxValueOpcode::Passthrough:
            return outputIndex < inputs.size() && inputs[outputIndex] == output;
        case VfxValueOpcode::AttributeAngularVelocity:
        case VfxValueOpcode::AttributeDirection:
        case VfxValueOpcode::AttributePivot:
        case VfxValueOpcode::AttributeScale:
        case VfxValueOpcode::AttributeTargetPosition:
            return output == VfxValueType::Vector3 && inputs.empty();
        case VfxValueOpcode::AttributeMass:
            return output == VfxValueType::Scalar && inputs.empty();
        case VfxValueOpcode::AttributeTextureIndex:
            return output == VfxValueType::UnsignedInteger && inputs.empty();
        case VfxValueOpcode::WeightedSelect:
            return output == VfxValueType::Vector3 && inputs.size() == 5 && inputs[0] == VfxValueType::Vector3 &&
                   inputs[1] == VfxValueType::Vector3 && inputs[2] == VfxValueType::Scalar &&
                   inputs[3] == VfxValueType::Scalar && inputs[4] == VfxValueType::Scalar;
        case VfxValueOpcode::LookAtDirection:
            return output == VfxValueType::Vector3 && inputs.size() == 2 && inputs[0] == VfxValueType::Vector3 &&
                   inputs[1] == VfxValueType::Vector3;
        case VfxValueOpcode::SampleBezier:
            return output == VfxValueType::Vector3 && inputs.size() == 5 && inputs[0] == VfxValueType::Vector3 &&
                   inputs[1] == VfxValueType::Vector3 && inputs[2] == VfxValueType::Vector3 &&
                   inputs[3] == VfxValueType::Vector3 && inputs[4] == VfxValueType::Scalar;
        case VfxValueOpcode::Swizzle:
            return output == VfxValueType::Vector4 && inputs.size() == 5 && inputs[0] == VfxValueType::Vector4 &&
                   inputs[1] == VfxValueType::UnsignedInteger && inputs[2] == VfxValueType::UnsignedInteger &&
                   inputs[3] == VfxValueType::UnsignedInteger && inputs[4] == VfxValueType::UnsignedInteger;
        case VfxValueOpcode::AreaCircle:
            return output == VfxValueType::Scalar && inputs.size() == 1 && inputs[0] == VfxValueType::Scalar;
        case VfxValueOpcode::DistanceLine:
        case VfxValueOpcode::DistancePlane:
            return output == VfxValueType::Scalar && inputs.size() == 3 && inputs[0] == VfxValueType::Vector3 &&
                   inputs[1] == VfxValueType::Vector3 && inputs[2] == VfxValueType::Vector3;
        case VfxValueOpcode::DistanceSphere:
            return output == VfxValueType::Scalar && inputs.size() == 3 && inputs[0] == VfxValueType::Vector3 &&
                   inputs[1] == VfxValueType::Vector3 && inputs[2] == VfxValueType::Scalar;
        case VfxValueOpcode::VolumeAxisAlignedBox:
        case VfxValueOpcode::VolumeOrientedBox:
            return output == VfxValueType::Scalar && inputs.size() == 1 && inputs[0] == VfxValueType::Vector3;
        case VfxValueOpcode::VolumeSphere:
            return output == VfxValueType::Scalar && inputs.size() == 1 && inputs[0] == VfxValueType::Scalar;
        case VfxValueOpcode::VolumeCone:
        case VfxValueOpcode::VolumeCylinder:
        case VfxValueOpcode::VolumeTorus:
            return output == VfxValueType::Scalar && inputs.size() == 2 && allScalar();
        case VfxValueOpcode::SpawnState:
            return inputs.empty() && ((outputIndex == 0 && output == VfxValueType::Boolean) ||
                                      (outputIndex == 1 && output == VfxValueType::Scalar) ||
                                      (outputIndex == 2 && output == VfxValueType::UnsignedInteger));
        default:
            return false;
        }
    }
} // namespace Keire::Internal

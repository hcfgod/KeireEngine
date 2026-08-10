#include "KeireInternal/Vfx/VfxExpressionInternal.h"

#include "Keire/Math/Math.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace Keire::Internal
{
    namespace
    {
        constexpr std::uint32_t MaximumValueRegisters = 4096;

        enum class WaveOperatorKind : std::uint8_t
        {
            Sawtooth,
            Sine,
            Square,
            Triangle
        };

        [[nodiscard]] std::optional<WaveOperatorKind> WaveOperator(const std::string_view typeId) noexcept
        {
            if (typeId == "keire.operator.sawtooth-wave")
                return WaveOperatorKind::Sawtooth;
            if (typeId == "keire.operator.sine-wave")
                return WaveOperatorKind::Sine;
            if (typeId == "keire.operator.square-wave")
                return WaveOperatorKind::Square;
            if (typeId == "keire.operator.triangle-wave")
                return WaveOperatorKind::Triangle;
            return std::nullopt;
        }
        [[nodiscard]] const VfxGraphProperty* FindProperty(const VfxGraphNode& node, const std::string_view name)
        {
            const auto found = std::ranges::find(node.Properties, name, &VfxGraphProperty::Name);
            return found == node.Properties.end() ? nullptr : std::addressof(*found);
        }
        template <typename T> [[nodiscard]] T Property(const VfxGraphNode& node, const std::string_view name)
        {
            const auto* property = FindProperty(node, name);
            if (!property || !std::holds_alternative<T>(property->Value))
                throw std::invalid_argument("VFX operator property '" + std::string(name) + "' is missing or invalid.");
            return std::get<T>(property->Value);
        }
        [[nodiscard]] VfxEvaluationDomain DomainForContext(const VfxContextType context) noexcept
        {
            switch (context)
            {
            case VfxContextType::Spawn:
            case VfxContextType::Initialize:
                return VfxEvaluationDomain::PerSpawn;
            case VfxContextType::Update:
                return VfxEvaluationDomain::PerParticleUpdate;
            case VfxContextType::Output:
            case VfxContextType::Event:
                return VfxEvaluationDomain::PerOutputEvent;
            }
            return VfxEvaluationDomain::PerEffect;
        }
        [[nodiscard]] bool IsParticleAttributeOpcode(const VfxValueOpcode opcode) noexcept
        {
            return opcode >= VfxValueOpcode::AttributeAlive && opcode <= VfxValueOpcode::RatioOverStrip;
        }

        [[nodiscard]] VfxEvaluationDomain
        SourceDomain(const VfxCompiledValueSource& source,
                     const std::map<std::uint32_t, VfxEvaluationDomain>& registerDomains) noexcept
        {
            switch (source.Kind)
            {
            case VfxCompiledValueSourceKind::Literal:
                return VfxEvaluationDomain::CompileTimeConstant;
            case VfxCompiledValueSourceKind::Parameter:
                return VfxEvaluationDomain::PerEffect;
            case VfxCompiledValueSourceKind::Register:
            {
                const auto found = registerDomains.find(source.Index);
                return found == registerDomains.end() ? VfxEvaluationDomain::PerParticleUpdate : found->second;
            }
            }
            return VfxEvaluationDomain::PerParticleUpdate;
        }

        [[nodiscard]] float FiniteOrZero(const float value) noexcept { return std::isfinite(value) ? value : 0.0F; }

        [[nodiscard]] float RoundToEven(const float value) noexcept
        {
            if (!std::isfinite(value))
                return 0.0F;
            const auto lower = std::floor(value);
            const auto fraction = value - lower;
            if (fraction < 0.5F)
                return lower;
            if (fraction > 0.5F)
                return lower + 1.0F;
            return std::fmod(std::abs(lower), 2.0F) == 0.0F ? lower : lower + 1.0F;
        }

        [[nodiscard]] Vector3 Cross(const Vector3 left, const Vector3 right) noexcept
        {
            return {left.Y * right.Z - left.Z * right.Y, left.Z * right.X - left.X * right.Z,
                    left.X * right.Y - left.Y * right.X};
        }

        [[nodiscard]] float Dot(const Vector3 left, const Vector3 right) noexcept
        {
            return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
        }

        [[nodiscard]] Vector3 NormalizeOrZero(const Vector3 value) noexcept
        {
            const auto length = FiniteOrZero(std::sqrt(Dot(value, value)));
            if (length <= std::numeric_limits<float>::epsilon())
                return {};
            return {FiniteOrZero(value.X / length), FiniteOrZero(value.Y / length), FiniteOrZero(value.Z / length)};
        }

        [[nodiscard]] Vector3 Rotate(const Quaternion rotation, const Vector3 value) noexcept
        {
            const Vector3 imaginary{rotation.X, rotation.Y, rotation.Z};
            const auto twiceCross = Cross(imaginary, value);
            const Vector3 scaledCross{twiceCross.X * 2.0F, twiceCross.Y * 2.0F, twiceCross.Z * 2.0F};
            const auto secondCross = Cross(imaginary, scaledCross);
            return {FiniteOrZero(value.X + rotation.W * scaledCross.X + secondCross.X),
                    FiniteOrZero(value.Y + rotation.W * scaledCross.Y + secondCross.Y),
                    FiniteOrZero(value.Z + rotation.W * scaledCross.Z + secondCross.Z)};
        }

        enum class NoiseKind : std::uint8_t
        {
            Value,
            Perlin,
            Cellular
        };

        [[nodiscard]] std::uint32_t HashNoiseWord(std::uint32_t value) noexcept
        {
            value ^= value >> 16U;
            value *= 0x7feb352dU;
            value ^= value >> 15U;
            value *= 0x846ca68bU;
            value ^= value >> 16U;
            return value;
        }

        [[nodiscard]] std::int32_t NoiseCell(const float value) noexcept
        {
            constexpr auto Limit = 1'000'000.0F;
            return static_cast<std::int32_t>(std::floor(std::clamp(FiniteOrZero(value), -Limit, Limit)));
        }

        [[nodiscard]] std::uint32_t HashNoiseLattice(const std::int32_t x, const std::int32_t y, const std::int32_t z,
                                                     const std::uint32_t salt) noexcept
        {
            auto state = HashNoiseWord(salt ^ 0x9e3779b9U);
            state = HashNoiseWord(state ^ std::bit_cast<std::uint32_t>(x));
            state = HashNoiseWord(state ^ std::bit_cast<std::uint32_t>(y));
            return HashNoiseWord(state ^ std::bit_cast<std::uint32_t>(z));
        }

        [[nodiscard]] float NoiseUnit(const std::uint32_t value) noexcept
        {
            return static_cast<float>(value >> 8U) * (1.0F / 16777215.0F);
        }

        [[nodiscard]] float NoiseFade(const float value) noexcept
        {
            return value * value * value * (value * (value * 6.0F - 15.0F) + 10.0F);
        }

        [[nodiscard]] float NoiseLerp(const float left, const float right, const float factor) noexcept
        {
            return left + (right - left) * factor;
        }

        [[nodiscard]] Vector3 NoiseGradient(const std::uint32_t value) noexcept
        {
            constexpr float InverseRootTwo = 0.70710678118654752440F;
            switch (value % 12U)
            {
            case 0:
                return {InverseRootTwo, InverseRootTwo, 0.0F};
            case 1:
                return {-InverseRootTwo, InverseRootTwo, 0.0F};
            case 2:
                return {InverseRootTwo, -InverseRootTwo, 0.0F};
            case 3:
                return {-InverseRootTwo, -InverseRootTwo, 0.0F};
            case 4:
                return {InverseRootTwo, 0.0F, InverseRootTwo};
            case 5:
                return {-InverseRootTwo, 0.0F, InverseRootTwo};
            case 6:
                return {InverseRootTwo, 0.0F, -InverseRootTwo};
            case 7:
                return {-InverseRootTwo, 0.0F, -InverseRootTwo};
            case 8:
                return {0.0F, InverseRootTwo, InverseRootTwo};
            case 9:
                return {0.0F, -InverseRootTwo, InverseRootTwo};
            case 10:
                return {0.0F, InverseRootTwo, -InverseRootTwo};
            default:
                return {0.0F, -InverseRootTwo, -InverseRootTwo};
            }
        }

        [[nodiscard]] float SampleValueNoise(const Vector3 position, const std::uint32_t salt) noexcept
        {
            constexpr auto Limit = 1'000'000.0F;
            const Vector3 bounded{std::clamp(FiniteOrZero(position.X), -Limit, Limit),
                                  std::clamp(FiniteOrZero(position.Y), -Limit, Limit),
                                  std::clamp(FiniteOrZero(position.Z), -Limit, Limit)};
            const auto x = NoiseCell(bounded.X);
            const auto y = NoiseCell(bounded.Y);
            const auto z = NoiseCell(bounded.Z);
            const auto tx = NoiseFade(bounded.X - static_cast<float>(x));
            const auto ty = NoiseFade(bounded.Y - static_cast<float>(y));
            const auto tz = NoiseFade(bounded.Z - static_cast<float>(z));
            std::array<float, 8> values{};
            for (std::uint32_t corner = 0; corner < values.size(); ++corner)
            {
                values[corner] = NoiseUnit(HashNoiseLattice(x + static_cast<std::int32_t>(corner & 1U),
                                                            y + static_cast<std::int32_t>((corner >> 1U) & 1U),
                                                            z + static_cast<std::int32_t>((corner >> 2U) & 1U), salt));
            }
            const auto x00 = NoiseLerp(values[0], values[1], tx);
            const auto x10 = NoiseLerp(values[2], values[3], tx);
            const auto x01 = NoiseLerp(values[4], values[5], tx);
            const auto x11 = NoiseLerp(values[6], values[7], tx);
            return NoiseLerp(NoiseLerp(x00, x10, ty), NoiseLerp(x01, x11, ty), tz);
        }

        [[nodiscard]] float SamplePerlinNoise(const Vector3 position, const std::uint32_t salt) noexcept
        {
            constexpr auto Limit = 1'000'000.0F;
            const Vector3 bounded{std::clamp(FiniteOrZero(position.X), -Limit, Limit),
                                  std::clamp(FiniteOrZero(position.Y), -Limit, Limit),
                                  std::clamp(FiniteOrZero(position.Z), -Limit, Limit)};
            const auto x = NoiseCell(bounded.X);
            const auto y = NoiseCell(bounded.Y);
            const auto z = NoiseCell(bounded.Z);
            const auto tx = bounded.X - static_cast<float>(x);
            const auto ty = bounded.Y - static_cast<float>(y);
            const auto tz = bounded.Z - static_cast<float>(z);
            std::array<float, 8> values{};
            for (std::uint32_t corner = 0; corner < values.size(); ++corner)
            {
                const auto ox = static_cast<std::int32_t>(corner & 1U);
                const auto oy = static_cast<std::int32_t>((corner >> 1U) & 1U);
                const auto oz = static_cast<std::int32_t>((corner >> 2U) & 1U);
                const auto gradient = NoiseGradient(HashNoiseLattice(x + ox, y + oy, z + oz, salt));
                values[corner] = Dot(
                    gradient, {tx - static_cast<float>(ox), ty - static_cast<float>(oy), tz - static_cast<float>(oz)});
            }
            const auto fadeX = NoiseFade(tx);
            const auto fadeY = NoiseFade(ty);
            const auto fadeZ = NoiseFade(tz);
            const auto x00 = NoiseLerp(values[0], values[1], fadeX);
            const auto x10 = NoiseLerp(values[2], values[3], fadeX);
            const auto x01 = NoiseLerp(values[4], values[5], fadeX);
            const auto x11 = NoiseLerp(values[6], values[7], fadeX);
            return std::clamp(0.5F + NoiseLerp(NoiseLerp(x00, x10, fadeY), NoiseLerp(x01, x11, fadeY), fadeZ), 0.0F,
                              1.0F);
        }

        [[nodiscard]] float SampleCellularNoise(const Vector3 position, const std::uint32_t salt) noexcept
        {
            constexpr auto Limit = 1'000'000.0F;
            const Vector3 bounded{std::clamp(FiniteOrZero(position.X), -Limit, Limit),
                                  std::clamp(FiniteOrZero(position.Y), -Limit, Limit),
                                  std::clamp(FiniteOrZero(position.Z), -Limit, Limit)};
            const auto x = NoiseCell(bounded.X);
            const auto y = NoiseCell(bounded.Y);
            const auto z = NoiseCell(bounded.Z);
            const Vector3 local{bounded.X - static_cast<float>(x), bounded.Y - static_cast<float>(y),
                                bounded.Z - static_cast<float>(z)};
            const std::int32_t baseX = x - (local.X < 0.5F ? 1 : 0);
            const std::int32_t baseY = y - (local.Y < 0.5F ? 1 : 0);
            const std::int32_t baseZ = z - (local.Z < 0.5F ? 1 : 0);
            auto nearestSquared = std::numeric_limits<float>::max();
            for (std::uint32_t corner = 0; corner < 8; ++corner)
            {
                const auto cellX = baseX + static_cast<std::int32_t>(corner & 1U);
                const auto cellY = baseY + static_cast<std::int32_t>((corner >> 1U) & 1U);
                const auto cellZ = baseZ + static_cast<std::int32_t>((corner >> 2U) & 1U);
                const auto hash = HashNoiseLattice(cellX, cellY, cellZ, salt);
                const Vector3 feature{static_cast<float>(cellX) + NoiseUnit(hash),
                                      static_cast<float>(cellY) + NoiseUnit(HashNoiseWord(hash ^ 0x68bc21ebU)),
                                      static_cast<float>(cellZ) + NoiseUnit(HashNoiseWord(hash ^ 0x02e5be93U))};
                const Vector3 delta{bounded.X - feature.X, bounded.Y - feature.Y, bounded.Z - feature.Z};
                nearestSquared = std::min(nearestSquared, Dot(delta, delta));
            }
            return std::clamp(FiniteOrZero(std::sqrt(nearestSquared) * 0.57735026918962576451F), 0.0F, 1.0F);
        }

        [[nodiscard]] float SampleNoise(const NoiseKind kind, const Vector3 position, const std::uint32_t salt) noexcept
        {
            if (kind == NoiseKind::Value)
                return SampleValueNoise(position, salt);
            if (kind == NoiseKind::Perlin)
                return SamplePerlinNoise(position, salt);
            return SampleCellularNoise(position, salt);
        }

        [[nodiscard]] float SampleFractalNoise(const NoiseKind kind, const Vector3 coordinate, const float frequency,
                                               const std::int64_t octaves, const float roughness,
                                               const float lacunarity, const std::uint32_t salt) noexcept
        {
            auto position = Vector3{coordinate.X * std::max(frequency, 0.0F), coordinate.Y * std::max(frequency, 0.0F),
                                    coordinate.Z * std::max(frequency, 0.0F)};
            auto amplitude = 1.0F;
            auto sum = 0.0F;
            auto normalization = 0.0F;
            const auto octaveCount = std::clamp<std::int64_t>(octaves, 1, 8);
            const auto persistence = std::clamp(roughness, 0.0F, 1.0F);
            const auto frequencyMultiplier = std::max(lacunarity, 0.0F);
            for (std::int64_t octave = 0; octave < octaveCount; ++octave)
            {
                sum += SampleNoise(kind, position, salt + static_cast<std::uint32_t>(octave) * 0x9e3779b9U) * amplitude;
                normalization += amplitude;
                amplitude *= persistence;
                position = {position.X * frequencyMultiplier, position.Y * frequencyMultiplier,
                            position.Z * frequencyMultiplier};
            }
            return normalization <= 0.0F ? 0.0F : std::clamp(FiniteOrZero(sum / normalization), 0.0F, 1.0F);
        }

        [[nodiscard]] float RemapNoise(const float sample, const Vector2 range) noexcept
        {
            return FiniteOrZero(range.X + std::clamp(sample, 0.0F, 1.0F) * (range.Y - range.X));
        }

        struct NoiseSample
        {
            float Value = 0.0F;
            Vector3 Derivative{};
        };

        [[nodiscard]] NoiseSample SampleNoiseWithDerivative(const NoiseKind kind, const Vector3 position,
                                                            const std::uint32_t salt) noexcept
        {
            constexpr auto Limit = 1'000'000.0F;
            const Vector3 bounded{std::clamp(FiniteOrZero(position.X), -Limit, Limit),
                                  std::clamp(FiniteOrZero(position.Y), -Limit, Limit),
                                  std::clamp(FiniteOrZero(position.Z), -Limit, Limit)};
            const auto x = NoiseCell(bounded.X);
            const auto y = NoiseCell(bounded.Y);
            const auto z = NoiseCell(bounded.Z);
            const Vector3 local{bounded.X - static_cast<float>(x), bounded.Y - static_cast<float>(y),
                                bounded.Z - static_cast<float>(z)};
            const Vector3 factor{NoiseFade(local.X), NoiseFade(local.Y), NoiseFade(local.Z)};
            const auto fadeDerivative = [](const float value)
            {
                const auto offset = value - 1.0F;
                return 30.0F * value * value * offset * offset;
            };
            const Vector3 factorDerivative{fadeDerivative(local.X), fadeDerivative(local.Y), fadeDerivative(local.Z)};
            const std::int32_t baseX = x - (local.X < 0.5F ? 1 : 0);
            const std::int32_t baseY = y - (local.Y < 0.5F ? 1 : 0);
            const std::int32_t baseZ = z - (local.Z < 0.5F ? 1 : 0);
            auto sample = 0.0F;
            Vector3 derivative{};
            auto nearestSquared = std::numeric_limits<float>::max();
            Vector3 nearestDelta{};
            for (std::uint32_t corner = 0; corner < 8; ++corner)
            {
                const auto ox = static_cast<std::int32_t>(corner & 1U);
                const auto oy = static_cast<std::int32_t>((corner >> 1U) & 1U);
                const auto oz = static_cast<std::int32_t>((corner >> 2U) & 1U);
                const auto hash = HashNoiseLattice(x + ox, y + oy, z + oz, salt);
                const Vector3 weights{ox == 0 ? 1.0F - factor.X : factor.X, oy == 0 ? 1.0F - factor.Y : factor.Y,
                                      oz == 0 ? 1.0F - factor.Z : factor.Z};
                const Vector3 derivativeWeights{ox == 0 ? -factorDerivative.X : factorDerivative.X,
                                                oy == 0 ? -factorDerivative.Y : factorDerivative.Y,
                                                oz == 0 ? -factorDerivative.Z : factorDerivative.Z};
                const auto weight = weights.X * weights.Y * weights.Z;
                const Vector3 weightDerivative{derivativeWeights.X * weights.Y * weights.Z,
                                               weights.X * derivativeWeights.Y * weights.Z,
                                               weights.X * weights.Y * derivativeWeights.Z};
                if (kind == NoiseKind::Value)
                {
                    const auto value = NoiseUnit(hash);
                    sample += value * weight;
                    derivative = {derivative.X + value * weightDerivative.X, derivative.Y + value * weightDerivative.Y,
                                  derivative.Z + value * weightDerivative.Z};
                }
                else if (kind == NoiseKind::Perlin)
                {
                    const auto gradient = NoiseGradient(hash);
                    const auto contribution =
                        Dot(gradient, {local.X - static_cast<float>(ox), local.Y - static_cast<float>(oy),
                                       local.Z - static_cast<float>(oz)});
                    sample += contribution * weight;
                    derivative = {derivative.X + gradient.X * weight + contribution * weightDerivative.X,
                                  derivative.Y + gradient.Y * weight + contribution * weightDerivative.Y,
                                  derivative.Z + gradient.Z * weight + contribution * weightDerivative.Z};
                }
                else
                {
                    const auto cellX = baseX + ox;
                    const auto cellY = baseY + oy;
                    const auto cellZ = baseZ + oz;
                    const auto cellularHash = HashNoiseLattice(cellX, cellY, cellZ, salt);
                    const Vector3 feature{
                        static_cast<float>(cellX) + NoiseUnit(cellularHash),
                        static_cast<float>(cellY) + NoiseUnit(HashNoiseWord(cellularHash ^ 0x68bc21ebU)),
                        static_cast<float>(cellZ) + NoiseUnit(HashNoiseWord(cellularHash ^ 0x02e5be93U))};
                    const Vector3 delta{bounded.X - feature.X, bounded.Y - feature.Y, bounded.Z - feature.Z};
                    const auto distanceSquared = Dot(delta, delta);
                    if (distanceSquared < nearestSquared)
                    {
                        nearestSquared = distanceSquared;
                        nearestDelta = delta;
                    }
                }
            }
            if (kind == NoiseKind::Perlin)
                sample += 0.5F;
            else if (kind == NoiseKind::Cellular)
            {
                const auto distance = std::sqrt(nearestSquared);
                sample = distance * 0.57735026918962576451F;
                derivative = distance <= 0.0000001F ? Vector3{}
                                                    : Vector3{nearestDelta.X * (0.57735026918962576451F / distance),
                                                              nearestDelta.Y * (0.57735026918962576451F / distance),
                                                              nearestDelta.Z * (0.57735026918962576451F / distance)};
            }
            if (sample <= 0.0F || sample >= 1.0F)
                derivative = {};
            return {std::clamp(FiniteOrZero(sample), 0.0F, 1.0F),
                    {FiniteOrZero(derivative.X), FiniteOrZero(derivative.Y), FiniteOrZero(derivative.Z)}};
        }

        [[nodiscard]] NoiseSample SampleFractalNoiseWithDerivative(const NoiseKind kind, const Vector3 coordinate,
                                                                   const float frequency, const std::int64_t octaves,
                                                                   const float roughness, const float lacunarity,
                                                                   const std::uint32_t salt) noexcept
        {
            const auto boundedFrequency = std::max(frequency, 0.0F);
            auto position = Vector3{coordinate.X * boundedFrequency, coordinate.Y * boundedFrequency,
                                    coordinate.Z * boundedFrequency};
            auto derivativeScale = boundedFrequency;
            auto amplitude = 1.0F;
            auto sampleSum = 0.0F;
            Vector3 derivativeSum{};
            auto normalization = 0.0F;
            const auto octaveCount = std::clamp<std::int64_t>(octaves, 1, 8);
            const auto persistence = std::clamp(roughness, 0.0F, 1.0F);
            const auto frequencyMultiplier = std::max(lacunarity, 0.0F);
            for (std::int64_t octave = 0; octave < octaveCount; ++octave)
            {
                const auto sampled =
                    SampleNoiseWithDerivative(kind, position, salt + static_cast<std::uint32_t>(octave) * 0x9e3779b9U);
                sampleSum += sampled.Value * amplitude;
                derivativeSum = {derivativeSum.X + sampled.Derivative.X * amplitude * derivativeScale,
                                 derivativeSum.Y + sampled.Derivative.Y * amplitude * derivativeScale,
                                 derivativeSum.Z + sampled.Derivative.Z * amplitude * derivativeScale};
                normalization += amplitude;
                amplitude *= persistence;
                position = {position.X * frequencyMultiplier, position.Y * frequencyMultiplier,
                            position.Z * frequencyMultiplier};
                derivativeScale *= frequencyMultiplier;
            }
            if (normalization <= 0.0F)
                return {};
            const auto sample = FiniteOrZero(sampleSum / normalization);
            auto derivative = sample <= 0.0F || sample >= 1.0F
                                  ? Vector3{}
                                  : Vector3{derivativeSum.X / normalization, derivativeSum.Y / normalization,
                                            derivativeSum.Z / normalization};
            derivative = {FiniteOrZero(derivative.X), FiniteOrZero(derivative.Y), FiniteOrZero(derivative.Z)};
            return {std::clamp(sample, 0.0F, 1.0F), derivative};
        }

        [[nodiscard]] Vector3 SampleNoiseDerivative(const NoiseKind kind, const Vector3 coordinate,
                                                    const float frequency, const std::int64_t octaves,
                                                    const float roughness, const float lacunarity, const Vector2 range,
                                                    const std::uint32_t salt) noexcept
        {
            const auto sampled =
                SampleFractalNoiseWithDerivative(kind, coordinate, frequency, octaves, roughness, lacunarity, salt);
            const auto scale = range.Y - range.X;
            return {FiniteOrZero(sampled.Derivative.X * scale), FiniteOrZero(sampled.Derivative.Y * scale),
                    FiniteOrZero(sampled.Derivative.Z * scale)};
        }

        [[nodiscard]] Vector3 SampleCurlNoise(const NoiseKind kind, const Vector3 coordinate, const float frequency,
                                              const std::int64_t octaves, const float roughness, const float lacunarity,
                                              const float amplitude) noexcept
        {
            const auto x = SampleFractalNoiseWithDerivative(kind, coordinate, frequency, octaves, roughness, lacunarity,
                                                            0x243f6a88U)
                               .Derivative;
            const auto y = SampleFractalNoiseWithDerivative(kind, coordinate, frequency, octaves, roughness, lacunarity,
                                                            0x85a308d3U)
                               .Derivative;
            const auto z = SampleFractalNoiseWithDerivative(kind, coordinate, frequency, octaves, roughness, lacunarity,
                                                            0x13198a2eU)
                               .Derivative;
            return {FiniteOrZero((z.Y - y.Z) * amplitude), FiniteOrZero((x.Z - z.X) * amplitude),
                    FiniteOrZero((y.X - x.Y) * amplitude)};
        }

        template <typename T>
        [[nodiscard]] std::optional<VfxParameterValue>
        ConstructRange(const std::span<const VfxParameterValue* const> inputs) noexcept
        {
            if (inputs.size() != 2 || !inputs[0] || !inputs[1] || !std::holds_alternative<T>(*inputs[0]) ||
                !std::holds_alternative<T>(*inputs[1]))
            {
                return std::nullopt;
            }
            return VfxRange<T>{std::get<T>(*inputs[0]), std::get<T>(*inputs[1])};
        }

        [[nodiscard]] std::optional<VfxParameterValue>
        ExecutePure(const VfxValueOpcode opcode, const std::span<const VfxParameterValue* const> inputs,
                    const VfxValueType outputType, const bool clampRemap, const VfxComparisonCondition comparison,
                    const std::uint32_t outputIndex = 0) noexcept
        {
            const auto scalar = [&inputs](const std::size_t index) -> std::optional<float>
            {
                if (index >= inputs.size() || !inputs[index] || !std::holds_alternative<float>(*inputs[index]))
                    return std::nullopt;
                return std::get<float>(*inputs[index]);
            };
            const auto boolean = [&inputs](const std::size_t index) -> std::optional<bool>
            {
                if (index >= inputs.size() || !inputs[index] || !std::holds_alternative<bool>(*inputs[index]))
                    return std::nullopt;
                return std::get<bool>(*inputs[index]);
            };
            const auto unsignedInteger = [&inputs](const std::size_t index) -> std::optional<std::uint64_t>
            {
                if (index >= inputs.size() || !inputs[index] || !std::holds_alternative<std::uint64_t>(*inputs[index]))
                {
                    return std::nullopt;
                }
                return std::get<std::uint64_t>(*inputs[index]);
            };
            const auto integer = [&inputs](const std::size_t index) -> std::optional<std::int64_t>
            {
                if (index >= inputs.size() || !inputs[index] || !std::holds_alternative<std::int64_t>(*inputs[index]))
                    return std::nullopt;
                return std::get<std::int64_t>(*inputs[index]);
            };
            const auto vector2 = [&inputs](const std::size_t index) -> std::optional<Vector2>
            {
                if (index >= inputs.size() || !inputs[index] || !std::holds_alternative<Vector2>(*inputs[index]))
                    return std::nullopt;
                return std::get<Vector2>(*inputs[index]);
            };
            const auto vector3 = [&inputs](const std::size_t index) -> std::optional<Vector3>
            {
                if (index >= inputs.size() || !inputs[index] || !std::holds_alternative<Vector3>(*inputs[index]))
                    return std::nullopt;
                return std::get<Vector3>(*inputs[index]);
            };
            switch (opcode)
            {
            case VfxValueOpcode::Constant:
                if (inputs.size() == 1 && inputs[0] && VfxValueMatchesType(outputType, *inputs[0]))
                    return *inputs[0];
                return std::nullopt;
            case VfxValueOpcode::Range:
            {
                if (const auto result = ConstructRange<float>(inputs))
                    return result;
                if (const auto result = ConstructRange<std::int64_t>(inputs))
                    return result;
                if (const auto result = ConstructRange<std::uint64_t>(inputs))
                    return result;
                if (const auto result = ConstructRange<Vector2>(inputs))
                    return result;
                if (const auto result = ConstructRange<Vector3>(inputs))
                    return result;
                if (const auto result = ConstructRange<Vector4>(inputs))
                    return result;
                if (const auto result = ConstructRange<Color>(inputs))
                    return result;
                return std::nullopt;
            }
            case VfxValueOpcode::Remap:
            {
                const auto input = scalar(0);
                if (!input || inputs.size() < 3 || !std::holds_alternative<VfxScalarRange>(*inputs[1]) ||
                    !std::holds_alternative<VfxScalarRange>(*inputs[2]))
                {
                    return std::nullopt;
                }
                const auto source = std::get<VfxScalarRange>(*inputs[1]);
                const auto destination = std::get<VfxScalarRange>(*inputs[2]);
                const auto width = source.Maximum - source.Minimum;
                auto factor = width == 0.0F ? 0.0F : (*input - source.Minimum) / width;
                if (clampRemap)
                    factor = std::clamp(factor, 0.0F, 1.0F);
                return FiniteOrZero(destination.Minimum + factor * (destination.Maximum - destination.Minimum));
            }
            case VfxValueOpcode::Add:
            case VfxValueOpcode::Subtract:
            case VfxValueOpcode::Multiply:
            case VfxValueOpcode::Divide:
            case VfxValueOpcode::Minimum:
            case VfxValueOpcode::Maximum:
            {
                const auto left = scalar(0);
                const auto right = scalar(1);
                if (!left || !right)
                    return std::nullopt;
                if (opcode == VfxValueOpcode::Add)
                    return FiniteOrZero(*left + *right);
                if (opcode == VfxValueOpcode::Subtract)
                    return FiniteOrZero(*left - *right);
                if (opcode == VfxValueOpcode::Multiply)
                    return FiniteOrZero(*left * *right);
                if (opcode == VfxValueOpcode::Divide)
                    return *right == 0.0F ? 0.0F : FiniteOrZero(*left / *right);
                if (opcode == VfxValueOpcode::Minimum)
                    return std::min(*left, *right);
                return std::max(*left, *right);
            }
            case VfxValueOpcode::Clamp:
            {
                const auto input = scalar(0);
                const auto minimum = scalar(1);
                const auto maximum = scalar(2);
                if (!input || !minimum || !maximum)
                    return std::nullopt;
                const auto low = std::min(*minimum, *maximum);
                const auto high = std::max(*minimum, *maximum);
                return std::clamp(*input, low, high);
            }
            case VfxValueOpcode::Saturate:
            {
                const auto input = scalar(0);
                return input ? std::optional<VfxParameterValue>(std::clamp(*input, 0.0F, 1.0F)) : std::nullopt;
            }
            case VfxValueOpcode::Absolute:
            {
                const auto input = scalar(0);
                return input ? std::optional<VfxParameterValue>(std::abs(*input)) : std::nullopt;
            }
            case VfxValueOpcode::Sine:
            case VfxValueOpcode::Cosine:
            case VfxValueOpcode::Tangent:
            case VfxValueOpcode::ArcSine:
            case VfxValueOpcode::ArcCosine:
            case VfxValueOpcode::ArcTangent:
            {
                const auto input = scalar(0);
                if (!input)
                    return std::nullopt;
                if ((opcode == VfxValueOpcode::ArcSine || opcode == VfxValueOpcode::ArcCosine) &&
                    (*input < -1.0F || *input > 1.0F))
                {
                    return 0.0F;
                }
                if (opcode == VfxValueOpcode::Sine)
                    return FiniteOrZero(std::sin(*input));
                if (opcode == VfxValueOpcode::Cosine)
                    return FiniteOrZero(std::cos(*input));
                if (opcode == VfxValueOpcode::Tangent)
                    return FiniteOrZero(std::tan(*input));
                if (opcode == VfxValueOpcode::ArcSine)
                    return FiniteOrZero(std::asin(*input));
                if (opcode == VfxValueOpcode::ArcCosine)
                    return FiniteOrZero(std::acos(*input));
                return FiniteOrZero(std::atan(*input));
            }
            case VfxValueOpcode::Atan2:
            {
                const auto y = scalar(0);
                const auto x = scalar(1);
                if (!y || !x)
                    return std::nullopt;
                return *y == 0.0F && *x == 0.0F ? 0.0F : FiniteOrZero(std::atan2(*y, *x));
            }
            case VfxValueOpcode::Power:
            {
                const auto base = scalar(0);
                const auto exponent = scalar(1);
                return base && exponent ? std::optional<VfxParameterValue>(FiniteOrZero(std::pow(*base, *exponent)))
                                        : std::nullopt;
            }
            case VfxValueOpcode::SquareRoot:
            {
                const auto input = scalar(0);
                if (!input)
                    return std::nullopt;
                return *input < 0.0F ? 0.0F : FiniteOrZero(std::sqrt(*input));
            }
            case VfxValueOpcode::Exponential:
            {
                const auto input = scalar(0);
                return input ? std::optional<VfxParameterValue>(FiniteOrZero(std::exp(*input))) : std::nullopt;
            }
            case VfxValueOpcode::Logarithm:
            case VfxValueOpcode::LogarithmBase2:
            case VfxValueOpcode::LogarithmBase10:
            {
                const auto input = scalar(0);
                if (!input)
                    return std::nullopt;
                if (*input <= 0.0F)
                    return 0.0F;
                if (opcode == VfxValueOpcode::Logarithm)
                    return FiniteOrZero(std::log(*input));
                if (opcode == VfxValueOpcode::LogarithmBase2)
                    return FiniteOrZero(std::log2(*input));
                return FiniteOrZero(std::log10(*input));
            }
            case VfxValueOpcode::Ceiling:
            case VfxValueOpcode::Floor:
            case VfxValueOpcode::Round:
            case VfxValueOpcode::Fractional:
            case VfxValueOpcode::Negate:
            case VfxValueOpcode::Sign:
            {
                const auto input = scalar(0);
                if (!input)
                    return std::nullopt;
                if (opcode == VfxValueOpcode::Ceiling)
                    return FiniteOrZero(std::ceil(*input));
                if (opcode == VfxValueOpcode::Floor)
                    return FiniteOrZero(std::floor(*input));
                if (opcode == VfxValueOpcode::Round)
                    return RoundToEven(*input);
                if (opcode == VfxValueOpcode::Fractional)
                    return FiniteOrZero(*input - std::floor(*input));
                if (opcode == VfxValueOpcode::Negate)
                    return FiniteOrZero(-*input);
                return *input > 0.0F ? 1.0F : *input < 0.0F ? -1.0F : 0.0F;
            }
            case VfxValueOpcode::Lerp:
            {
                const auto left = scalar(0);
                const auto right = scalar(1);
                const auto factor = scalar(2);
                return left && right && factor
                           ? std::optional<VfxParameterValue>(FiniteOrZero(std::lerp(*left, *right, *factor)))
                           : std::nullopt;
            }
            case VfxValueOpcode::Smoothstep:
            {
                const auto edge1 = scalar(0);
                const auto edge2 = scalar(1);
                const auto input = scalar(2);
                if (!edge1 || !edge2 || !input)
                    return std::nullopt;
                const auto width = *edge2 - *edge1;
                if (width == 0.0F || !std::isfinite(width))
                    return 0.0F;
                const auto factor = std::clamp(FiniteOrZero((*input - *edge1) / width), 0.0F, 1.0F);
                return FiniteOrZero(factor * factor * (3.0F - 2.0F * factor));
            }
            case VfxValueOpcode::Step:
            {
                const auto edge = scalar(0);
                const auto input = scalar(1);
                return edge && input ? std::optional<VfxParameterValue>(*input < *edge ? 0.0F : 1.0F) : std::nullopt;
            }
            case VfxValueOpcode::Compare:
            {
                const auto left = scalar(0);
                const auto right = scalar(1);
                if (!left || !right)
                    return std::nullopt;
                switch (comparison)
                {
                case VfxComparisonCondition::Less:
                    return *left < *right;
                case VfxComparisonCondition::LessOrEqual:
                    return *left <= *right;
                case VfxComparisonCondition::Equal:
                    return *left == *right;
                case VfxComparisonCondition::NotEqual:
                    return *left != *right;
                case VfxComparisonCondition::GreaterOrEqual:
                    return *left >= *right;
                case VfxComparisonCondition::Greater:
                    return *left > *right;
                }
                return std::nullopt;
            }
            case VfxValueOpcode::Select:
                if (inputs.size() == 3 && inputs[0] && std::holds_alternative<bool>(*inputs[0]) && inputs[1] &&
                    inputs[2])
                {
                    return std::get<bool>(*inputs[0]) ? *inputs[1] : *inputs[2];
                }
                return std::nullopt;
            case VfxValueOpcode::BooleanAnd:
            case VfxValueOpcode::BooleanOr:
            {
                const auto left = boolean(0);
                const auto right = boolean(1);
                if (!left || !right)
                    return std::nullopt;
                return opcode == VfxValueOpcode::BooleanAnd ? *left && *right : *left || *right;
            }
            case VfxValueOpcode::BooleanNot:
            {
                const auto input = boolean(0);
                return input ? std::optional<VfxParameterValue>(!*input) : std::nullopt;
            }
            case VfxValueOpcode::BooleanNand:
            case VfxValueOpcode::BooleanNor:
            {
                const auto left = boolean(0);
                const auto right = boolean(1);
                if (!left || !right)
                    return std::nullopt;
                return opcode == VfxValueOpcode::BooleanNand ? !(*left && *right) : !(*left || *right);
            }
            case VfxValueOpcode::BitwiseAnd:
            case VfxValueOpcode::BitwiseOr:
            case VfxValueOpcode::BitwiseXor:
            case VfxValueOpcode::BitwiseLeftShift:
            case VfxValueOpcode::BitwiseRightShift:
            {
                const auto left = unsignedInteger(0);
                const auto right = unsignedInteger(1);
                if (!left || !right)
                    return std::nullopt;
                if (opcode == VfxValueOpcode::BitwiseAnd)
                    return *left & *right;
                if (opcode == VfxValueOpcode::BitwiseOr)
                    return *left | *right;
                if (opcode == VfxValueOpcode::BitwiseXor)
                    return *left ^ *right;
                if (*right >= 64)
                    return std::uint64_t{0};
                return opcode == VfxValueOpcode::BitwiseLeftShift ? *left << *right : *left >> *right;
            }
            case VfxValueOpcode::BitwiseComplement:
            {
                const auto input = unsignedInteger(0);
                return input ? std::optional<VfxParameterValue>(~*input) : std::nullopt;
            }
            case VfxValueOpcode::Combine:
            {
                const auto x = scalar(0);
                const auto y = scalar(1);
                if (!x || !y)
                    return std::nullopt;
                if (outputType == VfxValueType::Vector2 && inputs.size() == 2)
                    return Vector2{*x, *y};
                const auto z = scalar(2);
                if (!z)
                    return std::nullopt;
                if (outputType == VfxValueType::Vector3 && inputs.size() == 3)
                    return Vector3{*x, *y, *z};
                const auto w = scalar(3);
                if (!w || inputs.size() != 4)
                    return std::nullopt;
                if (outputType == VfxValueType::Vector4)
                    return Vector4{*x, *y, *z, *w};
                if (outputType == VfxValueType::Color)
                    return Color{*x, *y, *z, *w};
                return std::nullopt;
            }
            case VfxValueOpcode::Split:
            {
                if (outputType != VfxValueType::Scalar || inputs.size() != 1 || !inputs[0])
                    return std::nullopt;
                if (const auto* input = std::get_if<Vector2>(inputs[0]); input && outputIndex < 2)
                    return outputIndex == 0 ? input->X : input->Y;
                if (const auto* input = std::get_if<Vector3>(inputs[0]); input && outputIndex < 3)
                {
                    const std::array values{input->X, input->Y, input->Z};
                    return values[outputIndex];
                }
                if (const auto* input = std::get_if<Vector4>(inputs[0]); input && outputIndex < 4)
                {
                    const std::array values{input->X, input->Y, input->Z, input->W};
                    return values[outputIndex];
                }
                if (const auto* input = std::get_if<Color>(inputs[0]); input && outputIndex < 4)
                {
                    const std::array values{input->Red, input->Green, input->Blue, input->Alpha};
                    return values[outputIndex];
                }
                return std::nullopt;
            }
            case VfxValueOpcode::Dot:
            case VfxValueOpcode::Cross:
            case VfxValueOpcode::Distance:
            {
                const auto left = vector3(0);
                const auto right = vector3(1);
                if (!left || !right)
                    return std::nullopt;
                if (opcode == VfxValueOpcode::Dot)
                    return FiniteOrZero(left->X * right->X + left->Y * right->Y + left->Z * right->Z);
                if (opcode == VfxValueOpcode::Cross)
                {
                    return Vector3{FiniteOrZero(left->Y * right->Z - left->Z * right->Y),
                                   FiniteOrZero(left->Z * right->X - left->X * right->Z),
                                   FiniteOrZero(left->X * right->Y - left->Y * right->X)};
                }
                const auto x = left->X - right->X;
                const auto y = left->Y - right->Y;
                const auto z = left->Z - right->Z;
                return FiniteOrZero(std::sqrt(x * x + y * y + z * z));
            }
            case VfxValueOpcode::Normalize:
            case VfxValueOpcode::Length:
            {
                const auto input = vector3(0);
                if (!input)
                    return std::nullopt;
                const auto length =
                    FiniteOrZero(std::sqrt(input->X * input->X + input->Y * input->Y + input->Z * input->Z));
                if (opcode == VfxValueOpcode::Length)
                    return length;
                if (length <= std::numeric_limits<float>::epsilon())
                    return Vector3{};
                return Vector3{FiniteOrZero(input->X / length), FiniteOrZero(input->Y / length),
                               FiniteOrZero(input->Z / length)};
            }
            case VfxValueOpcode::SquaredDistance:
            {
                const auto left = vector3(0);
                const auto right = vector3(1);
                if (!left || !right)
                    return std::nullopt;
                const auto x = left->X - right->X;
                const auto y = left->Y - right->Y;
                const auto z = left->Z - right->Z;
                return FiniteOrZero(x * x + y * y + z * z);
            }
            case VfxValueOpcode::SquaredLength:
            {
                const auto input = vector3(0);
                return input ? std::optional<VfxParameterValue>(
                                   FiniteOrZero(input->X * input->X + input->Y * input->Y + input->Z * input->Z))
                             : std::nullopt;
            }
            case VfxValueOpcode::Modulo:
            {
                const auto numerator = scalar(0);
                const auto denominator = scalar(1);
                if (!numerator || !denominator)
                    return std::nullopt;
                if (*denominator == 0.0F)
                    return 0.0F;
                const auto quotient = FiniteOrZero(*numerator / *denominator);
                return FiniteOrZero((quotient - std::floor(quotient)) * *denominator);
            }
            case VfxValueOpcode::OneMinus:
            {
                const auto input = scalar(0);
                return input ? std::optional<VfxParameterValue>(FiniteOrZero(1.0F - *input)) : std::nullopt;
            }
            case VfxValueOpcode::Reciprocal:
            {
                const auto input = scalar(0);
                return input ? std::optional<VfxParameterValue>(*input == 0.0F ? 0.0F : FiniteOrZero(1.0F / *input))
                             : std::nullopt;
            }
            case VfxValueOpcode::InverseLerp:
            {
                const auto start = scalar(0);
                const auto end = scalar(1);
                const auto input = scalar(2);
                if (!start || !end || !input)
                    return std::nullopt;
                const auto width = *end - *start;
                return width == 0.0F ? 0.0F : FiniteOrZero((*input - *start) / width);
            }
            case VfxValueOpcode::Discretize:
            {
                const auto input = scalar(0);
                const auto granularity = scalar(1);
                if (!input || !granularity)
                    return std::nullopt;
                return *granularity == 0.0F ? 0.0F : FiniteOrZero(std::floor(*input / *granularity) * *granularity);
            }
            case VfxValueOpcode::ColorLuma:
            {
                if (inputs.size() != 1 || !inputs[0] || !std::holds_alternative<Color>(*inputs[0]))
                    return std::nullopt;
                const auto color = std::get<Color>(*inputs[0]);
                return FiniteOrZero(0.299F * color.Red + 0.587F * color.Green + 0.114F * color.Blue);
            }
            case VfxValueOpcode::HsvToRgb:
            {
                const auto hsv = vector3(0);
                if (!hsv)
                    return std::nullopt;
                const auto hue = hsv->X - std::floor(hsv->X);
                const auto channel = [hue, saturation = hsv->Y, value = hsv->Z](const float offset) noexcept
                {
                    const auto wrapped = hue + offset - std::floor(hue + offset);
                    const auto triangle = std::clamp(std::abs(wrapped * 6.0F - 3.0F) - 1.0F, 0.0F, 1.0F);
                    return FiniteOrZero(value * (1.0F + (triangle - 1.0F) * saturation));
                };
                return Vector4{channel(1.0F), channel(2.0F / 3.0F), channel(1.0F / 3.0F), 1.0F};
            }
            case VfxValueOpcode::RgbToHsv:
            {
                if (inputs.size() != 1 || !inputs[0] || !std::holds_alternative<Color>(*inputs[0]))
                    return std::nullopt;
                const auto color = std::get<Color>(*inputs[0]);
                const auto minimum = std::min({color.Red, color.Green, color.Blue});
                const auto maximum = std::max({color.Red, color.Green, color.Blue});
                const auto delta = maximum - minimum;
                auto hue = 0.0F;
                if (delta != 0.0F)
                {
                    if (maximum == color.Red)
                        hue = (color.Green - color.Blue) / delta;
                    else if (maximum == color.Green)
                        hue = 2.0F + (color.Blue - color.Red) / delta;
                    else
                        hue = 4.0F + (color.Red - color.Green) / delta;
                    hue /= 6.0F;
                    hue -= std::floor(hue);
                }
                const auto saturation = maximum == 0.0F ? 0.0F : FiniteOrZero(delta / maximum);
                return Vector3{FiniteOrZero(hue), saturation, FiniteOrZero(maximum)};
            }
            case VfxValueOpcode::ToFloat:
                if (inputs.size() == 1 && inputs[0])
                {
                    if (std::holds_alternative<std::int64_t>(*inputs[0]))
                        return FiniteOrZero(static_cast<float>(std::get<std::int64_t>(*inputs[0])));
                    if (std::holds_alternative<std::uint64_t>(*inputs[0]))
                        return FiniteOrZero(static_cast<float>(std::get<std::uint64_t>(*inputs[0])));
                }
                return std::nullopt;
            case VfxValueOpcode::ToInteger:
            {
                const auto input = scalar(0);
                if (!input)
                    return std::nullopt;
                if (*input <= static_cast<float>(std::numeric_limits<std::int64_t>::lowest()))
                    return std::numeric_limits<std::int64_t>::lowest();
                if (*input >= static_cast<float>(std::numeric_limits<std::int64_t>::max()))
                    return std::numeric_limits<std::int64_t>::max();
                return static_cast<std::int64_t>(*input);
            }
            case VfxValueOpcode::ToUnsignedInteger:
            {
                const auto input = scalar(0);
                if (!input || *input <= 0.0F)
                    return std::uint64_t{0};
                if (*input >= static_cast<float>(std::numeric_limits<std::uint64_t>::max()))
                    return std::numeric_limits<std::uint64_t>::max();
                return static_cast<std::uint64_t>(*input);
            }
            case VfxValueOpcode::Epsilon:
                return inputs.empty() && outputIndex == 0 ? std::optional<VfxParameterValue>(0.00001F) : std::nullopt;
            case VfxValueOpcode::Pi:
            {
                if (!inputs.empty() || outputIndex >= 4)
                    return std::nullopt;
                constexpr std::array Values{3.14159265358979323846F, 6.28318530717958647692F, 1.57079632679489661923F,
                                            1.04719755119659774615F};
                return Values[outputIndex];
            }
            case VfxValueOpcode::PolarToRectangular:
            {
                const auto angle = scalar(0);
                const auto distance = scalar(1);
                if (!angle || !distance)
                    return std::nullopt;
                constexpr float DegreesToRadians = 0.01745329251994329577F;
                const auto radians = *angle * DegreesToRadians;
                return Vector2{FiniteOrZero(std::cos(radians) * *distance),
                               FiniteOrZero(std::sin(radians) * *distance)};
            }
            case VfxValueOpcode::RectangularToPolar:
            {
                const auto input = vector2(0);
                if (!input || outputIndex >= 2)
                    return std::nullopt;
                if (outputIndex == 0)
                    return input->X == 0.0F && input->Y == 0.0F ? 0.0F : FiniteOrZero(std::atan2(input->Y, input->X));
                return FiniteOrZero(std::sqrt(input->X * input->X + input->Y * input->Y));
            }
            case VfxValueOpcode::SphericalToRectangular:
            {
                const auto distance = scalar(0);
                const auto theta = scalar(1);
                const auto phi = scalar(2);
                if (!distance || !theta || !phi)
                    return std::nullopt;
                const auto cosinePhi = std::cos(*phi);
                return Vector3{FiniteOrZero(std::cos(*theta) * cosinePhi * *distance),
                               FiniteOrZero(std::sin(*phi) * *distance),
                               FiniteOrZero(std::sin(*theta) * cosinePhi * *distance)};
            }
            case VfxValueOpcode::RectangularToSpherical:
            {
                const auto input = vector3(0);
                if (!input || outputIndex >= 3)
                    return std::nullopt;
                const auto distance = FiniteOrZero(std::sqrt(Dot(*input, *input)));
                if (distance <= std::numeric_limits<float>::epsilon())
                    return 0.0F;
                if (outputIndex == 0)
                    return distance;
                if (outputIndex == 1)
                    return FiniteOrZero(std::atan2(input->Z, input->X));
                return FiniteOrZero(std::asin(std::clamp(input->Y / distance, -1.0F, 1.0F)));
            }
            case VfxValueOpcode::Rotate2D:
            {
                const auto position = vector2(0);
                const auto center = vector2(1);
                const auto angle = scalar(2);
                if (!position || !center || !angle)
                    return std::nullopt;
                const auto cosine = std::cos(*angle);
                const auto sine = std::sin(*angle);
                const auto x = position->X - center->X;
                const auto y = position->Y - center->Y;
                return Vector2{FiniteOrZero(center->X + x * cosine - y * sine),
                               FiniteOrZero(center->Y + x * sine + y * cosine)};
            }
            case VfxValueOpcode::Rotate3D:
            {
                const auto position = vector3(0);
                const auto center = vector3(1);
                const auto axis = vector3(2);
                const auto angle = scalar(3);
                if (!position || !center || !axis || !angle)
                    return std::nullopt;
                const auto normalizedAxis = NormalizeOrZero(*axis);
                if (normalizedAxis == Vector3{})
                    return *position;
                const Vector3 offset{position->X - center->X, position->Y - center->Y, position->Z - center->Z};
                const auto projectionDistance = Dot(normalizedAxis, offset);
                const Vector3 projection{center->X + normalizedAxis.X * projectionDistance,
                                         center->Y + normalizedAxis.Y * projectionDistance,
                                         center->Z + normalizedAxis.Z * projectionDistance};
                const Vector3 tangent{position->X - projection.X, position->Y - projection.Y,
                                      position->Z - projection.Z};
                const auto bitangent = Cross(tangent, normalizedAxis);
                const auto cosine = std::cos(*angle);
                const auto sine = std::sin(*angle);
                return Vector3{FiniteOrZero(projection.X + tangent.X * cosine + bitangent.X * sine),
                               FiniteOrZero(projection.Y + tangent.Y * cosine + bitangent.Y * sine),
                               FiniteOrZero(projection.Z + tangent.Z * cosine + bitangent.Z * sine)};
            }
            case VfxValueOpcode::ValueNoise:
            case VfxValueOpcode::PerlinNoise:
            case VfxValueOpcode::CellularNoise:
            {
                const auto coordinate = vector3(0);
                const auto frequency = scalar(1);
                const auto octaves = integer(2);
                const auto roughness = scalar(3);
                const auto lacunarity = scalar(4);
                const auto range = vector2(5);
                if (!coordinate || !frequency || !octaves || !roughness || !lacunarity || !range || outputIndex >= 2)
                    return std::nullopt;
                const auto kind = opcode == VfxValueOpcode::ValueNoise    ? NoiseKind::Value
                                  : opcode == VfxValueOpcode::PerlinNoise ? NoiseKind::Perlin
                                                                          : NoiseKind::Cellular;
                if (outputIndex == 0)
                {
                    return RemapNoise(
                        SampleFractalNoise(kind, *coordinate, *frequency, *octaves, *roughness, *lacunarity, 0U),
                        *range);
                }
                return SampleNoiseDerivative(kind, *coordinate, *frequency, *octaves, *roughness, *lacunarity, *range,
                                             0U);
            }
            case VfxValueOpcode::ValueCurlNoise:
            case VfxValueOpcode::PerlinCurlNoise:
            case VfxValueOpcode::CellularCurlNoise:
            {
                const auto coordinate = vector3(0);
                const auto frequency = scalar(1);
                const auto octaves = integer(2);
                const auto roughness = scalar(3);
                const auto lacunarity = scalar(4);
                const auto amplitude = scalar(5);
                if (!coordinate || !frequency || !octaves || !roughness || !lacunarity || !amplitude ||
                    outputIndex != 0)
                {
                    return std::nullopt;
                }
                const auto kind = opcode == VfxValueOpcode::ValueCurlNoise    ? NoiseKind::Value
                                  : opcode == VfxValueOpcode::PerlinCurlNoise ? NoiseKind::Perlin
                                                                              : NoiseKind::Cellular;
                return SampleCurlNoise(kind, *coordinate, *frequency, *octaves, *roughness, *lacunarity, *amplitude);
            }
            default:
                return EvaluateVfxExtendedExpression(opcode, inputs, outputType, outputIndex, nullptr);
            }
        }
        [[nodiscard]] VfxComparisonCondition ParseComparison(const VfxGraphNode& node)
        {
            const auto value = Property<std::string>(node, "Condition");
            if (value == "Less")
                return VfxComparisonCondition::Less;
            if (value == "Less Or Equal")
                return VfxComparisonCondition::LessOrEqual;
            if (value == "Equal")
                return VfxComparisonCondition::Equal;
            if (value == "Not Equal")
                return VfxComparisonCondition::NotEqual;
            if (value == "Greater Or Equal")
                return VfxComparisonCondition::GreaterOrEqual;
            if (value == "Greater")
                return VfxComparisonCondition::Greater;
            throw std::invalid_argument("VFX Compare operator condition is unsupported.");
        }

        [[nodiscard]] std::uint32_t HashWord(std::uint32_t value) noexcept
        {
            value ^= value >> 16U;
            value *= 0x7feb352dU;
            value ^= value >> 15U;
            value *= 0x846ca68bU;
            value ^= value >> 16U;
            return value;
        }

        void Mix(std::uint32_t& state, const std::uint64_t value) noexcept
        {
            state = HashWord(state ^ static_cast<std::uint32_t>(value));
            state = HashWord(state ^ static_cast<std::uint32_t>(value >> 32U));
        }

        [[nodiscard]] std::uint32_t RandomWord(const VfxCompiledValueInstruction& instruction,
                                               const VfxExpressionEvaluationContext& context,
                                               const std::uint32_t componentChannel = 0) noexcept
        {
            auto state = HashWord(context.EffectSeed ^ context.SeedOffset ^ 0x9e3779b9U);
            Mix(state, instruction.Node.High());
            Mix(state, instruction.Node.Low());
            Mix(state, context.System.High());
            Mix(state, context.System.Low());
            Mix(state, static_cast<std::uint64_t>(instruction.ChannelSalt) + componentChannel);
            if (!instruction.ConstantRandom)
            {
                Mix(state, static_cast<std::uint32_t>(instruction.Context));
                switch (instruction.RandomScope)
                {
                case VfxRandomScope::PerParticle:
                    Mix(state, context.ParticleId);
                    Mix(state, context.SpawnIndex);
                    break;
                case VfxRandomScope::PerVfxComponent:
                    break;
                case VfxRandomScope::PerParticleStrip:
                    Mix(state, context.StripId);
                    break;
                }
                Mix(state, context.SimulationStep);
            }
            return HashWord(state);
        }

        [[nodiscard]] float RandomUnit(const VfxCompiledValueInstruction& instruction,
                                       const VfxExpressionEvaluationContext& context, const bool inclusiveMaximum,
                                       const std::uint32_t componentChannel = 0) noexcept
        {
            const auto sample = RandomWord(instruction, context, componentChannel) >> 8U;
            return static_cast<float>(sample) * (inclusiveMaximum ? (1.0F / 16'777'215.0F) : (1.0F / 16'777'216.0F));
        }

        [[nodiscard]] std::uint32_t RandomComponentChannel(const VfxCompiledValueInstruction& instruction,
                                                           const std::uint32_t component) noexcept
        {
            return instruction.IndependentRandomChannels ? component : 0;
        }

        [[nodiscard]] float RandomFloat(const VfxCompiledValueInstruction& instruction,
                                        const VfxExpressionEvaluationContext& context, const float minimum,
                                        const float maximum, const bool inclusiveMaximum,
                                        const std::uint32_t component) noexcept
        {
            const auto unit =
                RandomUnit(instruction, context, inclusiveMaximum, RandomComponentChannel(instruction, component));
            return FiniteOrZero(minimum + (maximum - minimum) * unit);
        }

        [[nodiscard]] std::int64_t RandomInteger(const VfxCompiledValueInstruction& instruction,
                                                 const VfxExpressionEvaluationContext& context, std::int64_t minimum,
                                                 std::int64_t maximum, const bool inclusiveMaximum) noexcept
        {
            if (minimum > maximum)
                std::swap(minimum, maximum);
            constexpr auto sign = std::uint64_t{1} << 63U;
            const auto orderedMinimum = static_cast<std::uint64_t>(minimum) ^ sign;
            const auto orderedMaximum = static_cast<std::uint64_t>(maximum) ^ sign;
            const auto span = orderedMaximum - orderedMinimum + (inclusiveMaximum ? 1U : 0U);
            if (span == 0 && !inclusiveMaximum)
                return minimum;
            const auto first = static_cast<std::uint64_t>(RandomWord(instruction, context));
            const auto second = static_cast<std::uint64_t>(HashWord(static_cast<std::uint32_t>(first) ^ 0xa511e9b3U));
            const auto sample = (first << 32U) | second;
            const auto offset = span == 0 ? sample : sample % span;
            return static_cast<std::int64_t>((orderedMinimum + offset) ^ sign);
        }

        [[nodiscard]] std::uint64_t RandomUnsignedInteger(const VfxCompiledValueInstruction& instruction,
                                                          const VfxExpressionEvaluationContext& context,
                                                          std::uint64_t minimum, std::uint64_t maximum,
                                                          const bool inclusiveMaximum) noexcept
        {
            if (minimum > maximum)
                std::swap(minimum, maximum);
            const auto span = maximum - minimum + (inclusiveMaximum ? 1U : 0U);
            if (span == 0 && !inclusiveMaximum)
                return minimum;
            const auto first = static_cast<std::uint64_t>(RandomWord(instruction, context));
            const auto second = static_cast<std::uint64_t>(HashWord(static_cast<std::uint32_t>(first) ^ 0xa511e9b3U));
            const auto sample = (first << 32U) | second;
            return minimum + (span == 0 ? sample : sample % span);
        }

        template <typename T>
        [[nodiscard]] std::optional<VfxRange<T>>
        RandomBounds(const VfxCompiledValueInstruction& instruction,
                     const std::span<const VfxParameterValue* const> inputs) noexcept
        {
            if (instruction.Opcode == VfxValueOpcode::Random)
            {
                if (inputs.size() != 2 || !inputs[0] || !inputs[1] || !std::holds_alternative<T>(*inputs[0]) ||
                    !std::holds_alternative<T>(*inputs[1]))
                {
                    return std::nullopt;
                }
                return VfxRange<T>{std::get<T>(*inputs[0]), std::get<T>(*inputs[1])};
            }
            if (inputs.size() != 1 || !inputs[0] || !std::holds_alternative<VfxRange<T>>(*inputs[0]))
                return std::nullopt;
            return std::get<VfxRange<T>>(*inputs[0]);
        }
    } // namespace

    VfxExpressionCompilation CompileVfxExpressions(const VfxGraphSystem& system,
                                                   const std::map<AssetId, std::uint32_t>& parameterSlots,
                                                   const std::span<const AssetId> requiredOutputPins)
    {
        std::map<AssetId, const VfxGraphNode*> nodes;
        std::map<AssetId, std::pair<const VfxGraphNode*, const VfxGraphPin*>> pins;
        std::map<AssetId, const VfxGraphConnection*> inputDrivers;
        for (const auto& node : system.Nodes)
        {
            nodes.emplace(node.Id, std::addressof(node));
            for (const auto& pin : node.Pins)
                pins.emplace(pin.Id, std::pair{std::addressof(node), std::addressof(pin)});
            for (const auto& block : node.Blocks)
                for (const auto& pin : block.Pins)
                    pins.emplace(pin.Id, std::pair{std::addressof(node), std::addressof(pin)});
        }
        for (const auto& connection : system.Connections)
            inputDrivers.emplace(connection.InputPin, std::addressof(connection));

        VfxExpressionCompilation result;
        std::map<std::uint32_t, VfxEvaluationDomain> registerDomains;
        std::set<AssetId> compiling;
        std::function<VfxCompiledValueSource(AssetId)> compileOutput;
        compileOutput = [&](const AssetId outputPin) -> VfxCompiledValueSource
        {
            if (const auto cached = result.SourcesByOutputPin.find(outputPin);
                cached != result.SourcesByOutputPin.end())
            {
                return cached->second;
            }
            const auto located = pins.find(outputPin);
            if (located == pins.end() || located->second.second->Input)
                throw std::invalid_argument("VFX value expression references an invalid output pin.");
            const auto& node = *located->second.first;
            const auto& pin = *located->second.second;
            if (node.Kind == VfxGraphNodeKind::Parameter)
            {
                const auto slot = parameterSlots.find(node.Reference);
                if (slot == parameterSlots.end())
                    throw std::invalid_argument("VFX value expression references an unknown Blackboard parameter.");
                VfxCompiledValueSource source{VfxCompiledValueSourceKind::Parameter, pin.Type, slot->second, 0.0F};
                result.SourcesByOutputPin.emplace(outputPin, source);
                return source;
            }
            if (node.Kind != VfxGraphNodeKind::Operator)
                throw std::invalid_argument("VFX data inputs require a Parameter or executable Operator source.");
            if (!compiling.insert(node.Id).second)
                throw std::invalid_argument("VFX value expression contains a cycle.");

            const auto* descriptor = FindVfxNodeDescriptor(node.TypeId.View());
            if (!descriptor || descriptor->Class != VfxNodeClass::Operator || !descriptor->Lowering)
                throw std::invalid_argument("VFX graph contains an unknown executable Operator type ID.");
            if (descriptor->SupportTier == VfxNodeSupportTier::Disabled)
                throw std::invalid_argument("VFX Operator is disabled: " + descriptor->DisabledReason);
            if (node.DefinitionVersion != descriptor->DefinitionVersion || node.Pins.size() != descriptor->Pins.size())
                throw std::invalid_argument("VFX Operator node does not match its catalog definition version.");

            std::vector<VfxCompiledValueSource> inputs;
            const VfxGraphPin* output = nullptr;
            std::uint32_t outputIndex = 0;
            std::uint32_t nextOutputIndex = 0;
            for (std::size_t index = 0; index < descriptor->Pins.size(); ++index)
            {
                const auto& expected = descriptor->Pins[index];
                const auto& actual = node.Pins[index];
                if (actual.Name != expected.Name || actual.Semantic != expected.Semantic ||
                    actual.Type != expected.Type || actual.Input != expected.Input)
                {
                    throw std::invalid_argument("VFX Operator node pin signature is not canonical.");
                }
                if (!actual.Input)
                {
                    if (actual.Id == outputPin)
                    {
                        output = std::addressof(actual);
                        outputIndex = nextOutputIndex;
                    }
                    ++nextOutputIndex;
                    continue;
                }
                const auto driver = inputDrivers.find(actual.Id);
                if (driver == inputDrivers.end())
                {
                    if (!actual.DefaultValue || !VfxValueMatchesType(actual.Type, *actual.DefaultValue) ||
                        !IsFiniteVfxValue(*actual.DefaultValue))
                    {
                        throw std::invalid_argument("VFX Operator input has no cable or compatible inline value.");
                    }
                    inputs.push_back({VfxCompiledValueSourceKind::Literal, actual.Type, 0, *actual.DefaultValue});
                }
                else
                {
                    auto source = compileOutput(driver->second->OutputPin);
                    if (source.Type != actual.Type)
                        throw std::invalid_argument("VFX Operator input source type is incompatible.");
                    inputs.push_back(std::move(source));
                }
            }
            if (!output)
                throw std::invalid_argument("VFX Operator has no output pin.");

            if (const auto wave = WaveOperator(node.TypeId.View()))
            {
                if (inputs.size() != 4 || outputIndex != 0 || output->Type != VfxValueType::Scalar)
                    throw std::invalid_argument("VFX Wave Operator signature is not canonical.");

                const auto literal = [](const float value)
                { return VfxCompiledValueSource{VfxCompiledValueSourceKind::Literal, VfxValueType::Scalar, 0, value}; };
                const auto emitPrimitive =
                    [&](const VfxValueOpcode opcode, std::vector<VfxCompiledValueSource> primitiveInputs)
                {
                    VfxCompiledValueInstruction primitive;
                    primitive.Node = node.Id;
                    primitive.Opcode = opcode;
                    primitive.Type = VfxValueType::Scalar;
                    primitive.Context = node.Context;
                    primitive.Inputs = std::move(primitiveInputs);
                    primitive.Domain = VfxEvaluationDomain::CompileTimeConstant;
                    for (const auto& input : primitive.Inputs)
                        primitive.Domain = std::max(primitive.Domain, SourceDomain(input, registerDomains));

                    std::optional<VfxParameterValue> folded;
                    if (primitive.Domain == VfxEvaluationDomain::CompileTimeConstant)
                    {
                        std::array<const VfxParameterValue*, 8> values{};
                        if (primitive.Inputs.size() <= values.size() &&
                            std::ranges::all_of(primitive.Inputs, [](const VfxCompiledValueSource& source)
                                                { return source.Kind == VfxCompiledValueSourceKind::Literal; }))
                        {
                            for (std::size_t index = 0; index < primitive.Inputs.size(); ++index)
                                values[index] = std::addressof(primitive.Inputs[index].Literal);
                            folded = ExecutePure(
                                primitive.Opcode,
                                std::span<const VfxParameterValue* const>(values.data(), primitive.Inputs.size()),
                                VfxValueType::Scalar, false, VfxComparisonCondition::Less);
                        }
                    }
                    if (folded)
                    {
                        return VfxCompiledValueSource{VfxCompiledValueSourceKind::Literal, VfxValueType::Scalar, 0,
                                                      std::move(*folded)};
                    }

                    if (result.RegisterCount >= MaximumValueRegisters)
                        throw std::invalid_argument("VFX expression exceeds the 4096-register compiler safety limit.");
                    primitive.OutputRegister = result.RegisterCount++;
                    registerDomains.emplace(primitive.OutputRegister, primitive.Domain);
                    const VfxCompiledValueSource source{VfxCompiledValueSourceKind::Register, VfxValueType::Scalar,
                                                        primitive.OutputRegister, 0.0F};
                    result.Instructions.push_back(std::move(primitive));
                    return source;
                };

                const auto phase = emitPrimitive(VfxValueOpcode::Multiply, {inputs[0], inputs[1]});
                VfxCompiledValueSource factor;
                switch (*wave)
                {
                case WaveOperatorKind::Sawtooth:
                {
                    const auto fractional = emitPrimitive(VfxValueOpcode::Fractional, {phase});
                    factor = emitPrimitive(VfxValueOpcode::Absolute, {fractional});
                    break;
                }
                case WaveOperatorKind::Sine:
                {
                    constexpr float Tau = 6.28318530717958647692F;
                    const auto angle = emitPrimitive(VfxValueOpcode::Multiply, {phase, literal(Tau)});
                    const auto cosine = emitPrimitive(VfxValueOpcode::Cosine, {angle});
                    const auto numerator = emitPrimitive(VfxValueOpcode::Subtract, {literal(1.0F), cosine});
                    factor = emitPrimitive(VfxValueOpcode::Divide, {numerator, literal(2.0F)});
                    break;
                }
                case WaveOperatorKind::Square:
                {
                    const auto fractional = emitPrimitive(VfxValueOpcode::Fractional, {phase});
                    factor = emitPrimitive(VfxValueOpcode::Round, {fractional});
                    break;
                }
                case WaveOperatorKind::Triangle:
                {
                    const auto fractional = emitPrimitive(VfxValueOpcode::Fractional, {phase});
                    const auto slope = emitPrimitive(VfxValueOpcode::Round, {fractional});
                    const auto delta = emitPrimitive(VfxValueOpcode::Subtract, {slope, fractional});
                    const auto distance = emitPrimitive(VfxValueOpcode::Absolute, {delta});
                    factor = emitPrimitive(VfxValueOpcode::Multiply, {literal(2.0F), distance});
                    break;
                }
                }

                auto source = emitPrimitive(VfxValueOpcode::Lerp, {inputs[2], inputs[3], factor});
                result.SourcesByOutputPin.emplace(output->Id, source);
                compiling.erase(node.Id);
                return source;
            }

            VfxCompiledValueInstruction instruction;
            instruction.Node = node.Id;
            instruction.Opcode = *descriptor->Lowering;
            instruction.Type = output->Type;
            instruction.Context = node.Context;
            instruction.OutputIndex = outputIndex;
            instruction.Inputs = inputs;
            instruction.ClampRemap = instruction.Opcode == VfxValueOpcode::Remap && Property<bool>(node, "Clamp");
            if (instruction.Opcode == VfxValueOpcode::Compare)
                instruction.Comparison = ParseComparison(node);
            if (instruction.Opcode == VfxValueOpcode::Random || instruction.Opcode == VfxValueOpcode::RandomRange)
            {
                const auto scope = Property<std::uint64_t>(node, "Scope");
                if (scope > static_cast<std::uint64_t>(VfxRandomScope::PerParticleStrip))
                    throw std::invalid_argument("VFX Random scope is unsupported.");
                instruction.RandomScope = static_cast<VfxRandomScope>(scope);
                instruction.ConstantRandom = Property<bool>(node, "Constant");
                instruction.IndependentRandomChannels = Property<bool>(node, "Independent Channels");
                if (instruction.Opcode == VfxValueOpcode::RandomRange)
                    instruction.InclusiveMaximum = Property<bool>(node, "Inclusive Maximum");
            }

            instruction.Domain = VfxEvaluationDomain::CompileTimeConstant;
            for (const auto& input : inputs)
                instruction.Domain = std::max(instruction.Domain, SourceDomain(input, registerDomains));
            if (instruction.Opcode == VfxValueOpcode::Time || instruction.Opcode == VfxValueOpcode::DeltaTime ||
                instruction.Opcode == VfxValueOpcode::FrameIndex)
                instruction.Domain = VfxEvaluationDomain::PerFrame;
            else if (instruction.Opcode == VfxValueOpcode::SystemSeed)
                instruction.Domain = VfxEvaluationDomain::PerEffect;
            else if (instruction.Opcode == VfxValueOpcode::Age || instruction.Opcode == VfxValueOpcode::Lifetime ||
                     instruction.Opcode == VfxValueOpcode::AgeOverLifetime ||
                     instruction.Opcode == VfxValueOpcode::ParticleId ||
                     instruction.Opcode == VfxValueOpcode::SpawnIndex || IsParticleAttributeOpcode(instruction.Opcode))
            {
                instruction.Domain = DomainForContext(node.Context);
            }
            else if (instruction.Opcode == VfxValueOpcode::Random || instruction.Opcode == VfxValueOpcode::RandomRange)
            {
                instruction.Domain =
                    instruction.ConstantRandom ? VfxEvaluationDomain::PerEffect : DomainForContext(node.Context);
            }

            std::optional<VfxParameterValue> folded;
            if (instruction.Domain == VfxEvaluationDomain::CompileTimeConstant)
            {
                std::array<const VfxParameterValue*, 8> values{};
                if (inputs.size() <= values.size() &&
                    std::ranges::all_of(inputs, [](const VfxCompiledValueSource& source)
                                        { return source.Kind == VfxCompiledValueSourceKind::Literal; }))
                {
                    for (std::size_t index = 0; index < inputs.size(); ++index)
                        values[index] = std::addressof(inputs[index].Literal);
                    folded = ExecutePure(
                        instruction.Opcode, std::span<const VfxParameterValue* const>(values.data(), inputs.size()),
                        instruction.Type, instruction.ClampRemap, instruction.Comparison, instruction.OutputIndex);
                }
            }

            VfxCompiledValueSource source;
            if (folded)
                source = {VfxCompiledValueSourceKind::Literal, output->Type, 0, std::move(*folded)};
            else
            {
                if (result.RegisterCount >= MaximumValueRegisters)
                    throw std::invalid_argument("VFX expression exceeds the 4096-register compiler safety limit.");
                instruction.OutputRegister = result.RegisterCount++;
                registerDomains.emplace(instruction.OutputRegister, instruction.Domain);
                source = {VfxCompiledValueSourceKind::Register, output->Type, instruction.OutputRegister, 0.0F};
                result.Instructions.push_back(std::move(instruction));
            }
            result.SourcesByOutputPin.emplace(output->Id, source);
            compiling.erase(node.Id);
            return source;
        };

        for (const auto outputPin : requiredOutputPins)
            (void)compileOutput(outputPin);
        return result;
    }

    const VfxParameterValue* ResolveVfxValueSource(const VfxCompiledValueSource& source,
                                                   const std::span<const VfxParameterValue> parameters,
                                                   const std::span<const VfxParameterValue> registers) noexcept
    {
        switch (source.Kind)
        {
        case VfxCompiledValueSourceKind::Literal:
            return std::addressof(source.Literal);
        case VfxCompiledValueSourceKind::Parameter:
            return source.Index < parameters.size() ? std::addressof(parameters[source.Index]) : nullptr;
        case VfxCompiledValueSourceKind::Register:
            return source.Index < registers.size() ? std::addressof(registers[source.Index]) : nullptr;
        }
        return nullptr;
    }

    bool PackVfxGpuValue(const VfxValueType type, const VfxParameterValue& value, VfxGpuValue& packed) noexcept
    {
        if (!VfxValueMatchesType(type, value) || !IsFiniteVfxValue(value))
            return false;

        packed = {};
        const auto scalar = [](std::array<std::uint32_t, 4>& lane, const float component)
        { lane[0] = std::bit_cast<std::uint32_t>(component); };
        const auto integer = [](std::array<std::uint32_t, 4>& lane, const std::uint64_t component)
        {
            lane[0] = static_cast<std::uint32_t>(component);
            lane[1] = static_cast<std::uint32_t>(component >> 32U);
        };
        const auto vector2 = [](std::array<std::uint32_t, 4>& lane, const Vector2 component)
        {
            lane[0] = std::bit_cast<std::uint32_t>(component.X);
            lane[1] = std::bit_cast<std::uint32_t>(component.Y);
        };
        const auto vector3 = [](std::array<std::uint32_t, 4>& lane, const Vector3 component)
        {
            lane[0] = std::bit_cast<std::uint32_t>(component.X);
            lane[1] = std::bit_cast<std::uint32_t>(component.Y);
            lane[2] = std::bit_cast<std::uint32_t>(component.Z);
        };
        const auto vector4 = [](std::array<std::uint32_t, 4>& lane, const Vector4 component)
        {
            lane[0] = std::bit_cast<std::uint32_t>(component.X);
            lane[1] = std::bit_cast<std::uint32_t>(component.Y);
            lane[2] = std::bit_cast<std::uint32_t>(component.Z);
            lane[3] = std::bit_cast<std::uint32_t>(component.W);
        };
        const auto quaternion = [](std::array<std::uint32_t, 4>& lane, const Quaternion component)
        {
            lane[0] = std::bit_cast<std::uint32_t>(component.X);
            lane[1] = std::bit_cast<std::uint32_t>(component.Y);
            lane[2] = std::bit_cast<std::uint32_t>(component.Z);
            lane[3] = std::bit_cast<std::uint32_t>(component.W);
        };
        const auto color = [](std::array<std::uint32_t, 4>& lane, const Color component)
        {
            lane[0] = std::bit_cast<std::uint32_t>(component.Red);
            lane[1] = std::bit_cast<std::uint32_t>(component.Green);
            lane[2] = std::bit_cast<std::uint32_t>(component.Blue);
            lane[3] = std::bit_cast<std::uint32_t>(component.Alpha);
        };
        const auto asset = [](std::array<std::uint32_t, 4>& lane, const AssetId component)
        {
            lane[0] = static_cast<std::uint32_t>(component.High());
            lane[1] = static_cast<std::uint32_t>(component.High() >> 32U);
            lane[2] = static_cast<std::uint32_t>(component.Low());
            lane[3] = static_cast<std::uint32_t>(component.Low() >> 32U);
        };

        switch (type)
        {
        case VfxValueType::Boolean:
            packed.Primary[0] = std::get<bool>(value) ? 1U : 0U;
            return true;
        case VfxValueType::Integer:
            integer(packed.Primary, std::bit_cast<std::uint64_t>(std::get<std::int64_t>(value)));
            return true;
        case VfxValueType::Scalar:
            scalar(packed.Primary, std::get<float>(value));
            return true;
        case VfxValueType::Vector2:
            vector2(packed.Primary, std::get<Vector2>(value));
            return true;
        case VfxValueType::Vector3:
            vector3(packed.Primary, std::get<Vector3>(value));
            return true;
        case VfxValueType::Color:
            color(packed.Primary, std::get<Color>(value));
            return true;
        case VfxValueType::Texture:
        case VfxValueType::Mesh:
        case VfxValueType::Asset:
        case VfxValueType::Texture2DArray:
        case VfxValueType::Texture3D:
        case VfxValueType::TextureCube:
        case VfxValueType::Buffer:
        case VfxValueType::PointCache:
        case VfxValueType::SignedDistanceField:
            asset(packed.Primary, std::get<AssetId>(value));
            return true;
        case VfxValueType::UnsignedInteger:
            integer(packed.Primary, std::get<std::uint64_t>(value));
            return true;
        case VfxValueType::Vector4:
            vector4(packed.Primary, std::get<Vector4>(value));
            return true;
        case VfxValueType::Quaternion:
            quaternion(packed.Primary, std::get<Quaternion>(value));
            return true;
        case VfxValueType::ScalarRange:
        {
            const auto range = std::get<VfxScalarRange>(value);
            scalar(packed.Primary, range.Minimum);
            scalar(packed.Secondary, range.Maximum);
            return true;
        }
        case VfxValueType::IntegerRange:
        {
            const auto range = std::get<VfxIntegerRange>(value);
            integer(packed.Primary, std::bit_cast<std::uint64_t>(range.Minimum));
            integer(packed.Secondary, std::bit_cast<std::uint64_t>(range.Maximum));
            return true;
        }
        case VfxValueType::UnsignedIntegerRange:
        {
            const auto range = std::get<VfxUnsignedIntegerRange>(value);
            integer(packed.Primary, range.Minimum);
            integer(packed.Secondary, range.Maximum);
            return true;
        }
        case VfxValueType::Vector2Range:
        {
            const auto range = std::get<VfxVector2Range>(value);
            vector2(packed.Primary, range.Minimum);
            vector2(packed.Secondary, range.Maximum);
            return true;
        }
        case VfxValueType::Vector3Range:
        {
            const auto range = std::get<VfxVector3Range>(value);
            vector3(packed.Primary, range.Minimum);
            vector3(packed.Secondary, range.Maximum);
            return true;
        }
        case VfxValueType::Vector4Range:
        {
            const auto range = std::get<VfxVector4Range>(value);
            vector4(packed.Primary, range.Minimum);
            vector4(packed.Secondary, range.Maximum);
            return true;
        }
        case VfxValueType::ColorRange:
        {
            const auto range = std::get<VfxColorRange>(value);
            color(packed.Primary, range.Minimum);
            color(packed.Secondary, range.Maximum);
            return true;
        }
        case VfxValueType::ParticleStream:
        case VfxValueType::Matrix:
        case VfxValueType::Curve:
        case VfxValueType::Gradient:
            return false;
        }
        return false;
    }

    bool EvaluateVfxExpressions(const VfxCompiledProgram& program, const std::span<const VfxParameterValue> parameters,
                                const VfxExpressionEvaluationContext& context,
                                const std::span<VfxParameterValue> registers) noexcept
    {
        if (registers.size() < program.ValueRegisterCount)
            return false;
        try
        {
            for (const auto& instruction : program.ValueInstructions)
            {
                if (instruction.Context != context.Context)
                    continue;
                if (instruction.OutputRegister >= registers.size() || instruction.Inputs.size() > 8)
                    return false;
                std::array<const VfxParameterValue*, 8> inputs{};
                for (std::size_t index = 0; index < instruction.Inputs.size(); ++index)
                {
                    inputs[index] = ResolveVfxValueSource(instruction.Inputs[index], parameters, registers);
                    if (!inputs[index])
                        return false;
                }

                std::optional<VfxParameterValue> output;
                if (instruction.Opcode == VfxValueOpcode::Random || instruction.Opcode == VfxValueOpcode::RandomRange)
                {
                    const auto inputValues =
                        std::span<const VfxParameterValue* const>(inputs.data(), instruction.Inputs.size());
                    const auto inclusiveMaximum =
                        instruction.Opcode == VfxValueOpcode::RandomRange && instruction.InclusiveMaximum;
                    switch (instruction.Type)
                    {
                    case VfxValueType::Boolean:
                        if (instruction.Opcode != VfxValueOpcode::Random || !instruction.Inputs.empty())
                            return false;
                        output = RandomUnit(instruction, context, false) > 0.5F;
                        break;
                    case VfxValueType::Integer:
                    {
                        const auto bounds = RandomBounds<std::int64_t>(instruction, inputValues);
                        if (!bounds)
                            return false;
                        output =
                            RandomInteger(instruction, context, bounds->Minimum, bounds->Maximum, inclusiveMaximum);
                        break;
                    }
                    case VfxValueType::UnsignedInteger:
                    {
                        const auto bounds = RandomBounds<std::uint64_t>(instruction, inputValues);
                        if (!bounds)
                            return false;
                        output = RandomUnsignedInteger(instruction, context, bounds->Minimum, bounds->Maximum,
                                                       inclusiveMaximum);
                        break;
                    }
                    case VfxValueType::Scalar:
                    {
                        const auto bounds = RandomBounds<float>(instruction, inputValues);
                        if (!bounds)
                            return false;
                        output =
                            RandomFloat(instruction, context, bounds->Minimum, bounds->Maximum, inclusiveMaximum, 0);
                        break;
                    }
                    case VfxValueType::Vector2:
                    {
                        const auto bounds = RandomBounds<Vector2>(instruction, inputValues);
                        if (!bounds)
                            return false;
                        output = Vector2{RandomFloat(instruction, context, bounds->Minimum.X, bounds->Maximum.X,
                                                     inclusiveMaximum, 0),
                                         RandomFloat(instruction, context, bounds->Minimum.Y, bounds->Maximum.Y,
                                                     inclusiveMaximum, 1)};
                        break;
                    }
                    case VfxValueType::Vector3:
                    {
                        const auto bounds = RandomBounds<Vector3>(instruction, inputValues);
                        if (!bounds)
                            return false;
                        output = Vector3{RandomFloat(instruction, context, bounds->Minimum.X, bounds->Maximum.X,
                                                     inclusiveMaximum, 0),
                                         RandomFloat(instruction, context, bounds->Minimum.Y, bounds->Maximum.Y,
                                                     inclusiveMaximum, 1),
                                         RandomFloat(instruction, context, bounds->Minimum.Z, bounds->Maximum.Z,
                                                     inclusiveMaximum, 2)};
                        break;
                    }
                    case VfxValueType::Vector4:
                    {
                        const auto bounds = RandomBounds<Vector4>(instruction, inputValues);
                        if (!bounds)
                            return false;
                        output = Vector4{RandomFloat(instruction, context, bounds->Minimum.X, bounds->Maximum.X,
                                                     inclusiveMaximum, 0),
                                         RandomFloat(instruction, context, bounds->Minimum.Y, bounds->Maximum.Y,
                                                     inclusiveMaximum, 1),
                                         RandomFloat(instruction, context, bounds->Minimum.Z, bounds->Maximum.Z,
                                                     inclusiveMaximum, 2),
                                         RandomFloat(instruction, context, bounds->Minimum.W, bounds->Maximum.W,
                                                     inclusiveMaximum, 3)};
                        break;
                    }
                    case VfxValueType::Color:
                    {
                        const auto bounds = RandomBounds<Color>(instruction, inputValues);
                        if (!bounds)
                            return false;
                        output = Color{RandomFloat(instruction, context, bounds->Minimum.Red, bounds->Maximum.Red,
                                                   inclusiveMaximum, 0),
                                       RandomFloat(instruction, context, bounds->Minimum.Green, bounds->Maximum.Green,
                                                   inclusiveMaximum, 1),
                                       RandomFloat(instruction, context, bounds->Minimum.Blue, bounds->Maximum.Blue,
                                                   inclusiveMaximum, 2),
                                       RandomFloat(instruction, context, bounds->Minimum.Alpha, bounds->Maximum.Alpha,
                                                   inclusiveMaximum, 3)};
                        break;
                    }
                    default:
                        return false;
                    }
                }
                else if (instruction.Opcode == VfxValueOpcode::Time)
                    output = context.EffectTime;
                else if (instruction.Opcode == VfxValueOpcode::DeltaTime)
                    output = context.DeltaTime;
                else if (instruction.Opcode == VfxValueOpcode::Age)
                    output = context.Age;
                else if (instruction.Opcode == VfxValueOpcode::Lifetime)
                    output = context.Lifetime;
                else if (instruction.Opcode == VfxValueOpcode::AgeOverLifetime)
                    output = context.Lifetime == 0.0F ? 0.0F : FiniteOrZero(context.Age / context.Lifetime);
                else if (instruction.Opcode == VfxValueOpcode::ParticleId)
                    output = context.ParticleId;
                else if (instruction.Opcode == VfxValueOpcode::SpawnIndex)
                    output = context.SpawnIndex;
                else if (instruction.Opcode == VfxValueOpcode::FrameIndex)
                    output = context.SimulationStep;
                else if (instruction.Opcode == VfxValueOpcode::SystemSeed)
                    output = static_cast<std::uint64_t>(context.EffectSeed ^ context.SeedOffset);
                else if (instruction.Opcode == VfxValueOpcode::AttributeAlive)
                    output = true;
                else if (instruction.Opcode == VfxValueOpcode::AttributeAlpha)
                    output = context.Tint.Alpha;
                else if (instruction.Opcode == VfxValueOpcode::AttributeAngle)
                    output = context.Rotation;
                else if (instruction.Opcode == VfxValueOpcode::AttributeAxisX ||
                         instruction.Opcode == VfxValueOpcode::AttributeAxisY ||
                         instruction.Opcode == VfxValueOpcode::AttributeAxisZ)
                {
                    const auto rotation = Math::EulerDegreesToQuaternion(context.Rotation);
                    const auto axis = instruction.Opcode == VfxValueOpcode::AttributeAxisX ? Vector3{1.0F, 0.0F, 0.0F}
                                      : instruction.Opcode == VfxValueOpcode::AttributeAxisY
                                          ? Vector3{0.0F, 1.0F, 0.0F}
                                          : Vector3{0.0F, 0.0F, 1.0F};
                    output = Rotate(rotation, axis);
                }
                else if (instruction.Opcode == VfxValueOpcode::AttributeColor)
                    output = Vector3{context.Tint.Red, context.Tint.Green, context.Tint.Blue};
                else if (instruction.Opcode == VfxValueOpcode::AttributeOldPosition)
                    output = context.PreviousPosition;
                else if (instruction.Opcode == VfxValueOpcode::AttributeParticleCountInStrip)
                    output = static_cast<std::uint64_t>(std::max(context.ParticlesPerStrip, 1U));
                else if (instruction.Opcode == VfxValueOpcode::AttributeParticleIndexInStrip)
                    output = static_cast<std::uint64_t>(context.ParticleIndexInStrip);
                else if (instruction.Opcode == VfxValueOpcode::AttributePosition)
                    output = context.Position;
                else if (instruction.Opcode == VfxValueOpcode::AttributeSeed)
                {
                    const auto identity = static_cast<std::uint32_t>(context.ParticleId) ^
                                          static_cast<std::uint32_t>(context.ParticleId >> 32U);
                    output = static_cast<std::uint64_t>(HashWord(context.EffectSeed ^ context.SeedOffset ^ identity));
                }
                else if (instruction.Opcode == VfxValueOpcode::AttributeSize)
                    output = context.Size;
                else if (instruction.Opcode == VfxValueOpcode::AttributeSpawnTime)
                    output = std::max(0.0F, FiniteOrZero(context.EffectTime - context.Age));
                else if (instruction.Opcode == VfxValueOpcode::AttributeStripIndex)
                    output = context.StripId;
                else if (instruction.Opcode == VfxValueOpcode::AttributeVelocity)
                    output = context.Velocity;
                else if (instruction.Opcode == VfxValueOpcode::RatioOverStrip)
                {
                    const auto count = std::max(context.ParticlesPerStrip, 1U);
                    output = count <= 1U ? 0.0F
                                         : static_cast<float>(std::min(context.ParticleIndexInStrip, count - 1U)) /
                                               static_cast<float>(count - 1U);
                }
                else
                {
                    output = ExecutePure(
                        instruction.Opcode,
                        std::span<const VfxParameterValue* const>(inputs.data(), instruction.Inputs.size()),
                        instruction.Type, instruction.ClampRemap, instruction.Comparison, instruction.OutputIndex);
                    if (!output)
                        output = EvaluateVfxExtendedExpression(
                            instruction.Opcode,
                            std::span<const VfxParameterValue* const>(inputs.data(), instruction.Inputs.size()),
                            instruction.Type, instruction.OutputIndex, std::addressof(context));
                }
                if (!output || !VfxValueMatchesType(instruction.Type, *output) || !IsFiniteVfxValue(*output))
                    return false;
                registers[instruction.OutputRegister] = std::move(*output);
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace Keire::Internal

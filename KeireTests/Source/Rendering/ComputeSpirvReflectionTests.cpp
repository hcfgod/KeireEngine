#include "Keire/Rendering/ProgramArtifact.h"
#include "KeireInternal/Rendering/ComputeSpirvReflection.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
    struct ReflectionFixture
    {
        // Declaration fixtures exercise ABI checking only; they are never submitted to a driver.
        std::vector<std::uint32_t> Words{0x07230203U, 0x00010000U, 0, 64, 0};

        void Op(const std::uint16_t opcode, const std::initializer_list<std::uint32_t> operands)
        {
            Words.push_back((static_cast<std::uint32_t>(operands.size() + 1) << 16U) | opcode);
            Words.insert(Words.end(), operands);
        }

        void Name(const std::uint32_t id, const std::string_view name)
        {
            const auto size = (name.size() + 4) / 4;
            Words.push_back((static_cast<std::uint32_t>(size + 2) << 16U) | 5U);
            Words.push_back(id);
            const auto start = Words.size();
            Words.resize(start + size);
            for (std::size_t index = 0; index < name.size(); ++index)
                Words[start + index / 4] |= static_cast<std::uint32_t>(static_cast<unsigned char>(name[index]))
                                            << ((index % 4) * 8U);
        }

        [[nodiscard]] std::vector<std::byte> Bytes() const
        {
            std::vector<std::byte> result;
            for (const auto word : Words)
                for (unsigned shift = 0; shift < 32; shift += 8)
                    result.push_back(static_cast<std::byte>((word >> shift) & 0xffU));
            return result;
        }
    };

    [[nodiscard]] ReflectionFixture BufferFixture(const bool readonly = false, const bool uniform = false)
    {
        ReflectionFixture result;
        result.Op(15, {5, 1, 0x614d5343U, 0x00006e69U}); // GLCompute CSMain
        result.Op(16, {1, 17, 64, 1, 1});
        result.Name(6, "Buffer");
        result.Op(71, {6, 34, uniform ? 2U : readonly ? 0U : 1U});
        result.Op(71, {6, 33, 0});
        result.Op(71, {4, uniform ? 2U : 3U}); // Block or BufferBlock
        result.Op(72, {4, 0, 35, 0});
        if (readonly)
            result.Op(72, {4, 0, 24});
        result.Op(22, {2, 32});
        result.Op(23, {3, 2, 4});
        if (!uniform)
        {
            result.Op(29, {7, 3});
            result.Op(71, {7, 6, 16});
        }
        result.Op(30, {4, uniform ? 3U : 7U});
        result.Op(32, {5, 2, 4});
        result.Op(59, {5, 6, 2});
        return result;
    }

    [[nodiscard]] Keire::ProgramReflection Expected(const bool readonly = false, const bool uniform = false)
    {
        Keire::ProgramReflection result;
        result.EntryPoints.push_back({Keire::ProgramStage::Compute, "CSMain"});
        result.ThreadGroupSizeX = 64;
        result.Resources.push_back(
            {{},
             "Buffer",
             "Buffer",
             uniform    ? Keire::ProgramResourceKind::Uniform
             : readonly ? Keire::ProgramResourceKind::StructuredBuffer
                        : Keire::ProgramResourceKind::StorageBuffer,
             readonly || uniform ? Keire::ProgramResourceAccess::ReadOnly : Keire::ProgramResourceAccess::ReadWrite,
             Keire::ProgramStage::Compute,
             uniform    ? 2U
             : readonly ? 0U
                        : 1U,
             0,
             1,
             uniform ? 0U : 16U});
        return result;
    }
} // namespace

TEST_CASE("compute SPIR-V reflection verifies buffer and uniform declarations")
{
    CHECK_NOTHROW(Keire::Detail::ValidateComputeSpirvReflection(BufferFixture().Bytes(), Expected()));
    CHECK_NOTHROW(Keire::Detail::ValidateComputeSpirvReflection(BufferFixture(true).Bytes(), Expected(true)));
    CHECK_NOTHROW(
        Keire::Detail::ValidateComputeSpirvReflection(BufferFixture(false, true).Bytes(), Expected(false, true)));
}

TEST_CASE("compute SPIR-V reflection rejects same-count resource ABI mismatches")
{
    const auto bytes = BufferFixture().Bytes();
    auto expected = Expected();
    SUBCASE("descriptor set") { expected.Resources.front().Space = 0; }
    SUBCASE("binding") { expected.Resources.front().Binding = 1; }
    SUBCASE("symbol") { expected.Resources.front().Symbol = "Wrong"; }
    SUBCASE("stride") { expected.Resources.front().StrideBytes = 32; }
    SUBCASE("access") { expected.Resources.front().Access = Keire::ProgramResourceAccess::WriteOnly; }
    SUBCASE("array") { expected.Resources.front().ArrayCount = 2; }
    SUBCASE("stage") { expected.Resources.front().Stages = Keire::ProgramStage::Fragment; }
    SUBCASE("kernel") { expected.EntryPoints.front().Name = "Other"; }
    SUBCASE("group") { expected.ThreadGroupSizeX = 32; }
    CHECK_THROWS_AS(Keire::Detail::ValidateComputeSpirvReflection(bytes, expected), std::invalid_argument);
}

TEST_CASE("compute SPIR-V reflection rejects truncated and unsupported declarations")
{
    auto fixture = BufferFixture();
    SUBCASE("truncated instruction") { fixture.Words.push_back((3U << 16U) | 71U); }
    SUBCASE("zero instruction") { fixture.Words.push_back(0); }
    SUBCASE("duplicate binding") { fixture.Op(71, {6, 33, 0}); }
    SUBCASE("unsupported decoration group") { fixture.Op(73, {30}); }
    SUBCASE("specialized local size") { fixture.Op(331, {1, 38, 10, 11, 12}); }
    SUBCASE("contradictory access")
    {
        fixture.Op(71, {6, 24});
        fixture.Op(71, {6, 25});
    }
    SUBCASE("unexpected resource")
    {
        fixture.Name(8, "Other");
        fixture.Op(71, {8, 34, 1});
        fixture.Op(71, {8, 33, 1});
        fixture.Op(59, {5, 8, 2});
    }
    CHECK_THROWS_AS(Keire::Detail::ValidateComputeSpirvReflection(fixture.Bytes(), Expected()), std::invalid_argument);
}

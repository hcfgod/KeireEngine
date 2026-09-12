#include "KeireInternal/Rendering/ComputeSpirvReflection.h"

#include "Keire/Rendering/ProgramArtifact.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    namespace
    {
        struct Decorations
        {
            std::optional<std::uint32_t> Set;
            std::optional<std::uint32_t> Binding;
            std::optional<std::uint32_t> Stride;
            std::optional<std::uint32_t> Offset;
            bool Block = false;
            bool BufferBlock = false;
            bool NonWritable = false;
            bool NonReadable = false;
        };

        struct Declaration
        {
            std::uint16_t Opcode = 0;
            std::vector<std::uint32_t> Operands;
        };

        [[noreturn]] void Invalid(const std::string& reason)
        {
            throw std::invalid_argument("Compute SPIR-V reflection: " + reason);
        }

        [[nodiscard]] std::uint32_t Word(const std::span<const std::byte> bytes, const std::size_t index)
        {
            const auto offset = index * 4;
            return std::to_integer<std::uint32_t>(bytes[offset]) |
                   (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
                   (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
                   (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
        }

        [[nodiscard]] std::string LiteralString(const std::vector<std::uint32_t>& operands, const std::size_t begin)
        {
            std::string result;
            for (std::size_t index = begin; index < operands.size(); ++index)
                for (unsigned shift = 0; shift < 32; shift += 8)
                {
                    const auto character = static_cast<char>((operands[index] >> shift) & 0xffU);
                    if (character == 0)
                        return result;
                    if (result.size() == 1024)
                        Invalid("reflection name exceeds the supported length.");
                    result.push_back(character);
                }
            Invalid("unterminated reflection name.");
        }

        void UniqueDecoration(std::optional<std::uint32_t>& slot, const std::uint32_t value)
        {
            if (slot)
                Invalid("duplicate resource decoration.");
            slot = value;
        }
    } // namespace

    void ValidateComputeSpirvReflection(const std::span<const std::byte> binary, const ProgramReflection& expected)
    {
        if (binary.size() < 20 || binary.size() % 4 != 0 || binary.size() > ProgramBinaryMaximumBytes ||
            Word(binary, 0) != 0x07230203U || Word(binary, 4) != 0 || Word(binary, 3) == 0 ||
            Word(binary, 3) > 1048576 || expected.EntryPoints.size() != 1 ||
            expected.EntryPoints.front().Stage != ProgramStage::Compute)
            Invalid("invalid module header or expected kernel contract.");
        const auto bound = Word(binary, 3);
        const auto validId = [bound](const std::uint32_t id)
        {
            if (id == 0 || id >= bound)
                Invalid("declaration ID is outside the module bound.");
        };
        std::map<std::uint32_t, Declaration> declarations;
        std::map<std::uint32_t, Decorations> decorations;
        std::map<std::pair<std::uint32_t, std::uint32_t>, Decorations> members;
        std::map<std::uint32_t, std::string> names;
        std::vector<std::uint32_t> variables;
        std::optional<std::uint32_t> kernel;
        std::map<std::uint32_t, std::vector<std::uint32_t>> localSizes;
        for (std::size_t offset = 5; offset < binary.size() / 4;)
        {
            const auto header = Word(binary, offset);
            const auto count = static_cast<std::size_t>(header >> 16U);
            const auto opcode = static_cast<std::uint16_t>(header & 0xffffU);
            if (count == 0 || count > binary.size() / 4 - offset)
                Invalid("truncated or empty instruction.");
            std::vector<std::uint32_t> operands;
            operands.reserve(count - 1);
            for (std::size_t index = 1; index < count; ++index)
                operands.push_back(Word(binary, offset + index));
            const auto require = [&](const std::size_t minimum)
            {
                if (operands.size() < minimum)
                    Invalid("truncated reflection instruction.");
            };
            if (opcode == 5) // OpName
            {
                require(2);
                validId(operands[0]);
                if (!names.emplace(operands[0], LiteralString(operands, 1)).second)
                    Invalid("duplicate reflection name.");
            }
            else if (opcode == 15) // OpEntryPoint
            {
                require(3);
                validId(operands[1]);
                if (kernel || operands[0] != 5 || LiteralString(operands, 2) != expected.EntryPoints.front().Name)
                    Invalid("module must expose exactly the expected compute kernel.");
                kernel = operands[1];
            }
            else if (opcode == 16) // OpExecutionMode
            {
                require(2);
                if (operands[1] == 17)
                {
                    if (operands.size() != 5 || !localSizes.emplace(operands[0], operands).second)
                        Invalid("invalid or duplicate local workgroup size.");
                }
            }
            else if (opcode == 331 || (opcode >= 73 && opcode <= 75))
                Invalid("specialized workgroup sizes and decoration groups are unsupported.");
            else if (opcode == 71 || opcode == 72) // OpDecorate / OpMemberDecorate
            {
                require(opcode == 71 ? 2 : 3);
                validId(operands[0]);
                const auto decorationIndex = opcode == 71 ? 1U : 2U;
                auto& target = opcode == 71 ? decorations[operands[0]] : members[{operands[0], operands[1]}];
                switch (operands[decorationIndex])
                {
                case 2:
                    target.Block = true;
                    break;
                case 3:
                    target.BufferBlock = true;
                    break;
                case 24:
                    target.NonWritable = true;
                    break;
                case 25:
                    target.NonReadable = true;
                    break;
                case 6:
                case 33:
                case 34:
                case 35:
                    require(decorationIndex + 2);
                    UniqueDecoration(operands[decorationIndex] == 6    ? target.Stride
                                     : operands[decorationIndex] == 33 ? target.Binding
                                     : operands[decorationIndex] == 34 ? target.Set
                                                                       : target.Offset,
                                     operands[decorationIndex + 1]);
                    break;
                default:
                    break;
                }
            }
            else if ((opcode >= 19 && opcode <= 39) || opcode == 43 || opcode == 59)
            {
                require(opcode == 43 || opcode == 59 ? 2 : 1);
                const auto id = operands[opcode == 43 || opcode == 59 ? 1 : 0];
                validId(id);
                if (!declarations.emplace(id, Declaration{opcode, operands}).second)
                    Invalid("duplicate type or variable declaration.");
                if (opcode == 59)
                    variables.push_back(id);
            }
            offset += count;
        }
        if (!kernel || localSizes.size() != 1 || !localSizes.contains(*kernel))
            Invalid("missing literal local workgroup size.");
        const auto& size = localSizes.at(*kernel);
        if (size[2] != expected.ThreadGroupSizeX || size[3] != expected.ThreadGroupSizeY ||
            size[4] != expected.ThreadGroupSizeZ)
            Invalid("local workgroup size differs from the program contract.");
        const auto declaration = [&](const std::uint32_t id, const std::uint16_t opcode,
                                     const std::size_t operands) -> const Declaration&
        {
            const auto found = declarations.find(id);
            if (found == declarations.end() || found->second.Opcode != opcode ||
                found->second.Operands.size() < operands)
                Invalid("unsupported or missing resource type declaration.");
            return found->second;
        };
        std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
        for (const auto id : variables)
        {
            const auto& variable = declaration(id, 59, 3).Operands;
            const auto& attributes = decorations[id];
            const bool resourceStorage = variable[2] == 0 || variable[2] == 2 || variable[2] == 12;
            if (!resourceStorage && !attributes.Set && !attributes.Binding)
                continue;
            if (!attributes.Set || !attributes.Binding || !seen.emplace(*attributes.Set, *attributes.Binding).second)
                Invalid("resource has missing or duplicate descriptor coordinates.");
            const auto found = std::ranges::find_if(
                expected.Resources, [&](const ProgramResourceBinding& resource)
                { return resource.Space == *attributes.Set && resource.Binding == *attributes.Binding; });
            if (found == expected.Resources.end() || found->ArrayCount != 1 || found->Stages != ProgramStage::Compute)
                Invalid("resource descriptor coordinates, array count or stage differ from the program contract.");
            const auto name = names.find(id);
            if (name == names.end() || name->second != found->Symbol)
                Invalid("resource symbol differs from the program contract or was stripped.");
            const auto& pointer = declaration(variable[0], 32, 3).Operands;
            if (pointer[1] != variable[2])
                Invalid("resource pointer storage class differs from the variable.");
            const auto& structure = declaration(pointer[2], 30, 1).Operands;
            const auto& layout = decorations[pointer[2]];
            const bool uniform = variable[2] == 2 && layout.Block && !layout.BufferBlock;
            if (uniform)
            {
                if (found->Kind != ProgramResourceKind::Uniform || found->Access != ProgramResourceAccess::ReadOnly)
                    Invalid("uniform block differs from the program contract.");
                continue;
            }
            if (!((variable[2] == 12 && layout.Block) || (variable[2] == 2 && layout.BufferBlock)) ||
                structure.size() != 2)
                Invalid("only single runtime-array storage blocks and uniform blocks are supported.");
            const auto& array = declaration(structure[1], 29, 2).Operands;
            const auto& stride = decorations[array[0]].Stride;
            const auto expectedStride = found->Kind == ProgramResourceKind::ByteAddressBuffer ? 4U : found->StrideBytes;
            if (!stride || *stride != expectedStride || *stride == 0 || *stride % 4 != 0)
                Invalid("storage buffer element stride differs from the program contract.");
            const auto& member = members[{pointer[2], 0}];
            if (!member.Offset || *member.Offset != 0)
                Invalid("storage buffer runtime array requires a zero member offset.");
            if (found->Kind == ProgramResourceKind::ByteAddressBuffer)
            {
                const auto& integer = declaration(array[1], 21, 3).Operands;
                if (integer[1] != 32 || integer[2] != 0)
                    Invalid("byte-address buffers require unsigned 32-bit elements.");
            }
            const bool nonWritable = attributes.NonWritable || layout.NonWritable || member.NonWritable;
            const bool nonReadable = attributes.NonReadable || layout.NonReadable || member.NonReadable;
            if (nonWritable && nonReadable)
                Invalid("storage buffer has contradictory access decorations.");
            const auto access = nonWritable   ? ProgramResourceAccess::ReadOnly
                                : nonReadable ? ProgramResourceAccess::WriteOnly
                                              : ProgramResourceAccess::ReadWrite;
            if (access != found->Access ||
                (access == ProgramResourceAccess::ReadOnly && found->Kind != ProgramResourceKind::StructuredBuffer &&
                 found->Kind != ProgramResourceKind::ByteAddressBuffer) ||
                (access != ProgramResourceAccess::ReadOnly && found->Kind != ProgramResourceKind::StorageBuffer))
                Invalid("storage buffer access differs from the program contract.");
        }
        if (seen.size() != expected.Resources.size())
            Invalid("program resource is missing from the compiled module.");
    }
} // namespace Keire::Detail

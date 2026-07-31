struct GpuParticle
{
    float4 PositionAge;
    float4 VelocityLifetime;
    float4 Tint;
    float4 SizeRotation;
    float4 AccelerationSizeEnd;
    float4 ColorStart;
    float4 ColorEnd;
    uint4 Identity;
    uint4 SequenceIdentity;
};

struct VfxGpuValue
{
    uint4 Primary;
    uint4 Secondary;
};

struct VfxGpuValueInstruction
{
    uint4 Header;
    uint4 Output;
    uint4 Settings;
    uint4 NodeIdentity;
};

struct VfxGpuCustomInstructionRecord
{
    uint4 Metadata;
    float4 Operand;
};

RWStructuredBuffer<GpuParticle> Particles : register(u0, space1);
RWStructuredBuffer<uint> FreeIndices : register(u1, space1);
RWStructuredBuffer<uint> AliveIndices : register(u2, space1);
RWByteAddressBuffer Counters : register(u3, space1);
RWByteAddressBuffer IndirectArguments : register(u4, space1);

StructuredBuffer<VfxGpuValueInstruction> ValueInstructions : register(t0, space0);
StructuredBuffer<uint4> ValueSources : register(t1, space0);
StructuredBuffer<VfxGpuValue> ValueConstants : register(t2, space0);
StructuredBuffer<VfxGpuValue> ValueParameters : register(t3, space0);
StructuredBuffer<VfxGpuCustomInstructionRecord> CustomInstructions : register(t4, space0);
StructuredBuffer<uint4> ParticleOperations : register(t5, space0);

cbuffer VfxDispatch : register(b0, space2)
{
    uint ParticleCapacity;
    uint SpawnCount;
    float DeltaSeconds;
    uint RandomSeed;
    float4 EmitterPosition;
    float4 EmitterRotation;
    float4 ShapeExtentRadius;
    float4 VelocityMinimumLifetime;
    float4 VelocityMaximumLifetime;
    float4 AccelerationShape;
    float4 ShapeRotationParameters;
    float4 ColorStart;
    float4 ColorEnd;
    float4 SizeParameters;
    float4 PreviousEmitterPosition;
    float4 PreviousEmitterRotation;
    uint4 EmitterIdentity;
    uint4 ValueProgramMetadata;
    uint4 ValueRuntimeMetadata;
    uint4 ValueSystemIdentity;
    uint4 ValueSimulationMetadata;
    float4 ValueRuntimeTime;
};

static const uint VfxContextSpawn = 0;
static const uint VfxContextInitialize = 1;
static const uint VfxContextUpdate = 2;
static const uint VfxContextOutput = 3;
static const uint VfxTargetPosition = 0;
static const uint VfxTargetVelocity = 1;
static const uint VfxTargetRotation = 2;
static const uint VfxTargetTint = 3;
static const uint VfxTargetSize = 4;
static const uint VfxOperationAssign = 0;
static const uint VfxOperationAdd = 1;
static const uint VfxOperationMultiply = 2;
static const uint VfxParticleOperationShape = 0;
static const uint VfxParticleOperationInitialize = 1;
static const uint VfxParticleOperationForce = 2;
static const uint VfxParticleOperationSize = 3;
static const uint VfxParticleOperationColor = 4;
static const uint VfxParticleOperationCollision = 5;
static const uint VfxParticleOperationRenderer = 6;
static const uint VfxParticleOperationCustomHlsl = 7;

static const uint VfxValueTypeBoolean = 0;
static const uint VfxValueTypeInteger = 1;
static const uint VfxValueTypeScalar = 2;
static const uint VfxValueTypeVector2 = 3;
static const uint VfxValueTypeVector3 = 4;
static const uint VfxValueTypeColor = 5;
static const uint VfxValueTypeUnsignedInteger = 10;
static const uint VfxValueTypeVector4 = 11;
static const uint VfxValueTypeScalarRange = 16;
static const uint VfxValueTypeIntegerRange = 17;
static const uint VfxValueTypeUnsignedIntegerRange = 18;
static const uint VfxValueTypeVector2Range = 19;
static const uint VfxValueTypeVector3Range = 20;
static const uint VfxValueTypeVector4Range = 21;
static const uint VfxValueTypeColorRange = 22;

static const uint VfxValueSourceLiteral = 0;
static const uint VfxValueSourceParameter = 1;
static const uint VfxValueSourceRegister = 2;
static const uint VfxRandomScopePerParticle = 0;
static const uint VfxRandomScopePerComponent = 1;
static const uint VfxRandomScopePerParticleStrip = 2;
static const uint VfxValueFlagConstantRandom = 1U << 0;
static const uint VfxValueFlagIndependentRandomChannels = 1U << 1;
static const uint VfxValueFlagInclusiveMaximum = 1U << 2;
static const uint VfxValueFlagClampRemap = 1U << 3;
static const uint MaximumValueRegisters = 64;

static const uint VfxOpcodeConstant = 0;
static const uint VfxOpcodeRange = 1;
static const uint VfxOpcodeRandom = 2;
static const uint VfxOpcodeRandomRange = 3;
static const uint VfxOpcodeRemap = 4;
static const uint VfxOpcodeAdd = 5;
static const uint VfxOpcodeSubtract = 6;
static const uint VfxOpcodeMultiply = 7;
static const uint VfxOpcodeDivide = 8;
static const uint VfxOpcodeMinimum = 9;
static const uint VfxOpcodeMaximum = 10;
static const uint VfxOpcodeClamp = 11;
static const uint VfxOpcodeSaturate = 12;
static const uint VfxOpcodeAbsolute = 13;
static const uint VfxOpcodeCompare = 14;
static const uint VfxOpcodeBooleanAnd = 15;
static const uint VfxOpcodeBooleanOr = 16;
static const uint VfxOpcodeBooleanNot = 17;
static const uint VfxOpcodeSelect = 18;
static const uint VfxOpcodeCombine = 19;
static const uint VfxOpcodeSplit = 20;
static const uint VfxOpcodeDot = 21;
static const uint VfxOpcodeCross = 22;
static const uint VfxOpcodeNormalize = 23;
static const uint VfxOpcodeLength = 24;
static const uint VfxOpcodeDistance = 25;
static const uint VfxOpcodeTime = 26;
static const uint VfxOpcodeDeltaTime = 27;
static const uint VfxOpcodeAge = 28;
static const uint VfxOpcodeLifetime = 29;
static const uint VfxOpcodeParticleId = 30;
static const uint VfxOpcodeSpawnIndex = 31;
static const uint VfxOpcodeToFloat = 32;
static const uint VfxOpcodeToInteger = 33;
static const uint VfxOpcodeToUnsignedInteger = 34;
static const uint VfxOpcodeSine = 35;
static const uint VfxOpcodeCosine = 36;
static const uint VfxOpcodeTangent = 37;
static const uint VfxOpcodeArcSine = 38;
static const uint VfxOpcodeArcCosine = 39;
static const uint VfxOpcodeArcTangent = 40;
static const uint VfxOpcodeAtan2 = 41;
static const uint VfxOpcodePower = 42;
static const uint VfxOpcodeSquareRoot = 43;
static const uint VfxOpcodeExponential = 44;
static const uint VfxOpcodeLogarithm = 45;
static const uint VfxOpcodeLogarithmBase2 = 46;
static const uint VfxOpcodeLogarithmBase10 = 47;
static const uint VfxOpcodeCeiling = 48;
static const uint VfxOpcodeFloor = 49;
static const uint VfxOpcodeRound = 50;
static const uint VfxOpcodeFractional = 51;
static const uint VfxOpcodeLerp = 52;
static const uint VfxOpcodeSmoothstep = 53;
static const uint VfxOpcodeStep = 54;
static const uint VfxOpcodeNegate = 55;
static const uint VfxOpcodeSign = 56;

uint Hash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    return value ^ (value >> 16);
}

uint ValueComponentCount(uint type)
{
    if (type == VfxValueTypeVector2 || type == VfxValueTypeVector2Range)
        return 2;
    if (type == VfxValueTypeVector3 || type == VfxValueTypeVector3Range)
        return 3;
    if (type == VfxValueTypeVector4 || type == VfxValueTypeColor || type == VfxValueTypeVector4Range ||
        type == VfxValueTypeColorRange)
        return 4;
    return 1;
}

bool IsFloatValueType(uint type)
{
    return type == VfxValueTypeScalar || type == VfxValueTypeVector2 || type == VfxValueTypeVector3 ||
           type == VfxValueTypeVector4 || type == VfxValueTypeColor;
}

bool IsFloatRangeType(uint type)
{
    return type == VfxValueTypeScalarRange || type == VfxValueTypeVector2Range || type == VfxValueTypeVector3Range ||
           type == VfxValueTypeVector4Range || type == VfxValueTypeColorRange;
}

bool IsIntegerValueType(uint type) { return type == VfxValueTypeInteger || type == VfxValueTypeUnsignedInteger; }

bool IsIntegerRangeType(uint type)
{
    return type == VfxValueTypeIntegerRange || type == VfxValueTypeUnsignedIntegerRange;
}

bool IsSupportedValueType(uint type)
{
    return type == VfxValueTypeBoolean || IsIntegerValueType(type) || IsFloatValueType(type) ||
           IsIntegerRangeType(type) || IsFloatRangeType(type);
}

bool IsFiniteValue(VfxGpuValue value, uint type)
{
    if (!IsFloatValueType(type) && !IsFloatRangeType(type))
        return IsSupportedValueType(type);
    const uint count = ValueComponentCount(type);
    const bool primaryFinite = all(isfinite(asfloat(value.Primary).xyzw));
    const bool secondaryFinite = !IsFloatRangeType(type) || all(isfinite(asfloat(value.Secondary).xyzw));
    if (count == 1)
        return isfinite(asfloat(value.Primary.x)) && (!IsFloatRangeType(type) || isfinite(asfloat(value.Secondary.x)));
    if (count == 2)
        return all(isfinite(asfloat(value.Primary.xy))) &&
               (!IsFloatRangeType(type) || all(isfinite(asfloat(value.Secondary.xy))));
    if (count == 3)
        return all(isfinite(asfloat(value.Primary.xyz))) &&
               (!IsFloatRangeType(type) || all(isfinite(asfloat(value.Secondary.xyz))));
    return primaryFinite && secondaryFinite;
}

float FiniteOrZero(float value) { return isfinite(value) ? value : 0.0F; }

float4 FiniteOrZero(float4 value)
{
    return float4(FiniteOrZero(value.x), FiniteOrZero(value.y), FiniteOrZero(value.z), FiniteOrZero(value.w));
}

bool Less64(uint2 left, uint2 right) { return left.y < right.y || (left.y == right.y && left.x < right.x); }

bool Equal64(uint2 left, uint2 right) { return all(left == right); }

uint2 Add64(uint2 left, uint2 right)
{
    const uint low = left.x + right.x;
    return uint2(low, left.y + right.y + (low < left.x ? 1U : 0U));
}

uint2 Subtract64(uint2 left, uint2 right)
{
    const uint low = left.x - right.x;
    return uint2(low, left.y - right.y - (left.x < right.x ? 1U : 0U));
}

uint2 Negate64(uint2 value) { return Add64(~value, uint2(1U, 0U)); }

uint2 ShiftLeftOne64(uint2 value) { return uint2(value.x << 1U, (value.y << 1U) | (value.x >> 31U)); }

uint ReadBit64(uint2 value, uint bit) { return bit < 32U ? ((value.x >> bit) & 1U) : ((value.y >> (bit - 32U)) & 1U); }

uint2 Modulo64(uint2 value, uint2 divisor)
{
    if (all(divisor == 0U))
        return value;
    uint2 remainder = 0U.xx;
    [loop] for (int bit = 63; bit >= 0; --bit)
    {
        remainder = ShiftLeftOne64(remainder);
        remainder.x |= ReadBit64(value, (uint)bit);
        if (!Less64(remainder, divisor))
            remainder = Subtract64(remainder, divisor);
    }
    return remainder;
}

float Unsigned64ToFloat(uint2 value) { return (float)value.y * 4294967296.0F + (float)value.x; }

float Signed64ToFloat(uint2 value)
{
    return (value.y & 0x80000000U) != 0U ? -Unsigned64ToFloat(Negate64(value)) : Unsigned64ToFloat(value);
}

uint2 FloatToUnsigned64(float value)
{
    if (!isfinite(value) || value <= 0.0F)
        return 0U.xx;
    if (value >= 18446744073709551616.0F)
        return 0xffffffffU.xx;
    const float highValue = floor(value * (1.0F / 4294967296.0F));
    const uint high = (uint)min(highValue, 4294967295.0F);
    const float remainder = max(0.0F, value - (float)high * 4294967296.0F);
    return uint2((uint)min(floor(remainder), 4294967295.0F), high);
}

uint2 FloatToSigned64(float value)
{
    if (!isfinite(value))
        return 0U.xx;
    if (value <= -9223372036854775808.0F)
        return uint2(0U, 0x80000000U);
    if (value >= 9223372036854775808.0F)
        return uint2(0xffffffffU, 0x7fffffffU);
    const uint2 magnitude = FloatToUnsigned64(abs(value));
    return value < 0.0F ? Negate64(magnitude) : magnitude;
}

void Mix64(inout uint state, uint2 value)
{
    state = Hash(state ^ value.x);
    state = Hash(state ^ value.y);
}

uint ValueRandomWord(VfxGpuValueInstruction instruction, GpuParticle particle, uint componentChannel)
{
    uint state = Hash(RandomSeed ^ 0x9e3779b9U);
    Mix64(state, instruction.NodeIdentity.xy);
    Mix64(state, instruction.NodeIdentity.zw);
    Mix64(state, ValueSystemIdentity.xy);
    Mix64(state, ValueSystemIdentity.zw);
    Mix64(state, uint2(instruction.Settings.x + componentChannel, 0U));
    if ((instruction.Settings.z & VfxValueFlagConstantRandom) == 0U)
    {
        Mix64(state, uint2(instruction.Header.z, 0U));
        if (instruction.Settings.y == VfxRandomScopePerParticle)
        {
            Mix64(state, particle.SequenceIdentity.zw);
            Mix64(state, particle.SequenceIdentity.xy);
        }
        else if (instruction.Settings.y == VfxRandomScopePerParticleStrip)
        {
            // Strip execution is rejected by the renderer until strips have a first-class particle identity.
            Mix64(state, 0U.xx);
        }
        Mix64(state, ValueSimulationMetadata.xy);
    }
    return Hash(state);
}

float ValueRandomUnit(VfxGpuValueInstruction instruction, GpuParticle particle, uint component)
{
    const uint channel = (instruction.Settings.z & VfxValueFlagIndependentRandomChannels) != 0U ? component : 0U;
    const uint sample = ValueRandomWord(instruction, particle, channel) >> 8U;
    const bool inclusive = (instruction.Settings.z & VfxValueFlagInclusiveMaximum) != 0U;
    return (float)sample * (inclusive ? (1.0F / 16777215.0F) : (1.0F / 16777216.0F));
}

bool ResolveValueSource(uint sourceIndex, VfxGpuValue registers[MaximumValueRegisters], out VfxGpuValue value,
                        out uint type)
{
    value = (VfxGpuValue)0;
    type = 0U;
    if (sourceIndex >= ValueProgramMetadata.y)
        return false;
    const uint4 source = ValueSources[sourceIndex];
    if (source.w != 0U || !IsSupportedValueType(source.y))
        return false;
    type = source.y;
    if (source.x == VfxValueSourceLiteral)
    {
        if (source.z >= ValueProgramMetadata.z)
            return false;
        value = ValueConstants[source.z];
    }
    else if (source.x == VfxValueSourceParameter)
    {
        if (source.z >= ValueRuntimeMetadata.x)
            return false;
        value = ValueParameters[source.z];
    }
    else if (source.x == VfxValueSourceRegister)
    {
        if (source.z >= ValueProgramMetadata.w || source.z >= MaximumValueRegisters)
            return false;
        value = registers[source.z];
    }
    else
        return false;
    return IsFiniteValue(value, type);
}

uint RangeTypeForValue(uint type)
{
    if (type == VfxValueTypeInteger)
        return VfxValueTypeIntegerRange;
    if (type == VfxValueTypeUnsignedInteger)
        return VfxValueTypeUnsignedIntegerRange;
    if (type == VfxValueTypeScalar)
        return VfxValueTypeScalarRange;
    if (type == VfxValueTypeVector2)
        return VfxValueTypeVector2Range;
    if (type == VfxValueTypeVector3)
        return VfxValueTypeVector3Range;
    if (type == VfxValueTypeVector4)
        return VfxValueTypeVector4Range;
    if (type == VfxValueTypeColor)
        return VfxValueTypeColorRange;
    return 0xffffffffU;
}

bool ExecuteRandomValue(VfxGpuValueInstruction instruction, GpuParticle particle, VfxGpuValue inputs[4],
                        uint inputTypes[4], uint inputCount, out VfxGpuValue output)
{
    output = (VfxGpuValue)0;
    const uint opcode = instruction.Header.x;
    const uint type = instruction.Header.y;
    if (type == VfxValueTypeBoolean)
    {
        if (opcode != VfxOpcodeRandom || inputCount != 0U)
            return false;
        output.Primary.x = ValueRandomUnit(instruction, particle, 0U) > 0.5F ? 1U : 0U;
        return true;
    }

    VfxGpuValue bounds = (VfxGpuValue)0;
    if (opcode == VfxOpcodeRandom)
    {
        if (inputCount != 2U || inputTypes[0] != type || inputTypes[1] != type)
            return false;
        bounds.Primary = inputs[0].Primary;
        bounds.Secondary = inputs[1].Primary;
    }
    else
    {
        if (opcode != VfxOpcodeRandomRange || inputCount != 1U || inputTypes[0] != RangeTypeForValue(type))
            return false;
        bounds = inputs[0];
    }

    if (IsFloatValueType(type))
    {
        const float4 minimum = asfloat(bounds.Primary);
        const float4 maximum = asfloat(bounds.Secondary);
        float4 result = 0.0F.xxxx;
        const uint count = ValueComponentCount(type);
        [unroll] for (uint component = 0U; component < 4U; ++component)
        {
            if (component < count)
            {
                const float factor = ValueRandomUnit(instruction, particle, component);
                result[component] =
                    FiniteOrZero(minimum[component] + (maximum[component] - minimum[component]) * factor);
            }
        }
        output.Primary = asuint(result);
        return true;
    }

    if (!IsIntegerValueType(type))
        return false;
    uint2 minimum = bounds.Primary.xy;
    uint2 maximum = bounds.Secondary.xy;
    const bool signedValue = type == VfxValueTypeInteger;
    uint2 orderedMinimum = signedValue ? uint2(minimum.x, minimum.y ^ 0x80000000U) : minimum;
    uint2 orderedMaximum = signedValue ? uint2(maximum.x, maximum.y ^ 0x80000000U) : maximum;
    if (Less64(orderedMaximum, orderedMinimum))
    {
        const uint2 temporary = orderedMinimum;
        orderedMinimum = orderedMaximum;
        orderedMaximum = temporary;
    }
    const bool inclusive = (instruction.Settings.z & VfxValueFlagInclusiveMaximum) != 0U;
    uint2 span = Subtract64(orderedMaximum, orderedMinimum);
    if (inclusive)
        span = Add64(span, uint2(1U, 0U));
    if (!inclusive && all(span == 0U))
    {
        output.Primary.xy = signedValue ? uint2(orderedMinimum.x, orderedMinimum.y ^ 0x80000000U) : orderedMinimum;
        return true;
    }
    const uint first = ValueRandomWord(instruction, particle, 0U);
    const uint second = Hash(first ^ 0xa511e9b3U);
    const uint2 sample = uint2(second, first);
    const uint2 offset = all(span == 0U) ? sample : Modulo64(sample, span);
    const uint2 orderedResult = Add64(orderedMinimum, offset);
    output.Primary.xy = signedValue ? uint2(orderedResult.x, orderedResult.y ^ 0x80000000U) : orderedResult;
    return true;
}

bool RequireScalarInputs(uint inputTypes[4], uint inputCount, uint expectedCount)
{
    if (inputCount != expectedCount)
        return false;
    [unroll] for (uint index = 0U; index < 4U; ++index)
    {
        if (index < expectedCount && inputTypes[index] != VfxValueTypeScalar)
            return false;
    }
    return true;
}

bool ExecuteValueInstruction(VfxGpuValueInstruction instruction, GpuParticle particle,
                             VfxGpuValue registers[MaximumValueRegisters], float evaluationDeltaSeconds,
                             out VfxGpuValue output)
{
    output = (VfxGpuValue)0;
    if (instruction.Output.w > 4U || instruction.Output.z > ValueProgramMetadata.y ||
        instruction.Output.w > ValueProgramMetadata.y - instruction.Output.z)
        return false;
    VfxGpuValue inputs[4];
    uint inputTypes[4];
    [unroll] for (uint index = 0U; index < 4U; ++index)
    {
        inputs[index] = (VfxGpuValue)0;
        inputTypes[index] = 0U;
        if (index < instruction.Output.w &&
            !ResolveValueSource(instruction.Output.z + index, registers, inputs[index], inputTypes[index]))
            return false;
    }

    const uint opcode = instruction.Header.x;
    const uint type = instruction.Header.y;
    const uint inputCount = instruction.Output.w;
    if (opcode == VfxOpcodeRandom || opcode == VfxOpcodeRandomRange)
        return ExecuteRandomValue(instruction, particle, inputs, inputTypes, inputCount, output);
    if (opcode == VfxOpcodeTime)
    {
        if (type != VfxValueTypeScalar || inputCount != 0U)
            return false;
        output.Primary.x = asuint(ValueRuntimeTime.x);
        return true;
    }
    if (opcode == VfxOpcodeDeltaTime)
    {
        if (type != VfxValueTypeScalar || inputCount != 0U)
            return false;
        output.Primary.x = asuint(evaluationDeltaSeconds);
        return true;
    }
    if (opcode == VfxOpcodeAge)
    {
        if (type != VfxValueTypeScalar || inputCount != 0U)
            return false;
        output.Primary.x = asuint(particle.PositionAge.w);
        return true;
    }
    if (opcode == VfxOpcodeLifetime)
    {
        if (type != VfxValueTypeScalar || inputCount != 0U)
            return false;
        output.Primary.x = asuint(particle.VelocityLifetime.w);
        return true;
    }
    if (opcode == VfxOpcodeParticleId || opcode == VfxOpcodeSpawnIndex)
    {
        if (type != VfxValueTypeUnsignedInteger || inputCount != 0U)
            return false;
        output.Primary.xy = opcode == VfxOpcodeParticleId ? particle.SequenceIdentity.zw : particle.SequenceIdentity.xy;
        return true;
    }
    if (opcode == VfxOpcodeConstant)
    {
        if (inputCount != 1U || inputTypes[0] != type)
            return false;
        output = inputs[0];
        return true;
    }
    if (opcode == VfxOpcodeRange)
    {
        if (inputCount != 2U || RangeTypeForValue(inputTypes[0]) != type || inputTypes[0] != inputTypes[1])
            return false;
        output.Primary = inputs[0].Primary;
        output.Secondary = inputs[1].Primary;
        return true;
    }
    if (opcode == VfxOpcodeSelect)
    {
        if (inputCount != 3U || inputTypes[0] != VfxValueTypeBoolean || inputTypes[1] != type || inputTypes[2] != type)
            return false;
        if (inputs[0].Primary.x != 0U)
            output = inputs[1];
        else
            output = inputs[2];
        return true;
    }
    if (opcode == VfxOpcodeBooleanAnd || opcode == VfxOpcodeBooleanOr || opcode == VfxOpcodeBooleanNot)
    {
        const uint expected = opcode == VfxOpcodeBooleanNot ? 1U : 2U;
        if (type != VfxValueTypeBoolean || inputCount != expected || inputTypes[0] != VfxValueTypeBoolean ||
            (expected == 2U && inputTypes[1] != VfxValueTypeBoolean))
            return false;
        const bool left = inputs[0].Primary.x != 0U;
        const bool right = inputs[1].Primary.x != 0U;
        output.Primary.x = opcode == VfxOpcodeBooleanNot   ? (!left ? 1U : 0U)
                           : opcode == VfxOpcodeBooleanAnd ? (left && right ? 1U : 0U)
                                                           : (left || right ? 1U : 0U);
        return true;
    }

    if (opcode == VfxOpcodeToFloat)
    {
        if (type != VfxValueTypeScalar || inputCount != 1U || !IsIntegerValueType(inputTypes[0]))
            return false;
        const uint2 integer = inputs[0].Primary.xy;
        const float converted =
            inputTypes[0] == VfxValueTypeInteger ? Signed64ToFloat(integer) : Unsigned64ToFloat(integer);
        output.Primary.x = asuint(FiniteOrZero(converted));
        return true;
    }
    if (opcode == VfxOpcodeToInteger || opcode == VfxOpcodeToUnsignedInteger)
    {
        if (!RequireScalarInputs(inputTypes, inputCount, 1U) ||
            (opcode == VfxOpcodeToInteger && type != VfxValueTypeInteger) ||
            (opcode == VfxOpcodeToUnsignedInteger && type != VfxValueTypeUnsignedInteger))
            return false;
        const float value = asfloat(inputs[0].Primary.x);
        output.Primary.xy = opcode == VfxOpcodeToInteger ? FloatToSigned64(value) : FloatToUnsigned64(value);
        return true;
    }
    if (opcode == VfxOpcodeCombine)
    {
        if (type != VfxValueTypeVector3 || !RequireScalarInputs(inputTypes, inputCount, 3U))
            return false;
        output.Primary.xyz = uint3(inputs[0].Primary.x, inputs[1].Primary.x, inputs[2].Primary.x);
        return true;
    }
    if (opcode == VfxOpcodeSplit)
    {
        if (type != VfxValueTypeScalar || inputCount != 1U || inputTypes[0] != VfxValueTypeVector3 ||
            instruction.Output.y > 2U)
            return false;
        output.Primary.x = inputs[0].Primary[instruction.Output.y];
        return true;
    }
    if (opcode == VfxOpcodeDot || opcode == VfxOpcodeCross || opcode == VfxOpcodeDistance)
    {
        if (inputCount != 2U || inputTypes[0] != VfxValueTypeVector3 || inputTypes[1] != VfxValueTypeVector3)
            return false;
        const float3 left = asfloat(inputs[0].Primary.xyz);
        const float3 right = asfloat(inputs[1].Primary.xyz);
        if (opcode == VfxOpcodeCross)
        {
            if (type != VfxValueTypeVector3)
                return false;
            output.Primary.xyz = asuint(FiniteOrZero(float4(cross(left, right), 0.0F)).xyz);
        }
        else
        {
            if (type != VfxValueTypeScalar)
                return false;
            const float value = opcode == VfxOpcodeDot ? dot(left, right) : length(left - right);
            output.Primary.x = asuint(FiniteOrZero(value));
        }
        return true;
    }
    if (opcode == VfxOpcodeNormalize || opcode == VfxOpcodeLength)
    {
        if (inputCount != 1U || inputTypes[0] != VfxValueTypeVector3)
            return false;
        const float3 value = asfloat(inputs[0].Primary.xyz);
        const float magnitude = FiniteOrZero(length(value));
        if (opcode == VfxOpcodeLength)
        {
            if (type != VfxValueTypeScalar)
                return false;
            output.Primary.x = asuint(magnitude);
        }
        else
        {
            if (type != VfxValueTypeVector3)
                return false;
            const float3 normalized = magnitude <= 1.1920928955078125e-7F ? 0.0F.xxx : value / magnitude;
            output.Primary.xyz = asuint(FiniteOrZero(float4(normalized, 0.0F)).xyz);
        }
        return true;
    }
    if (opcode == VfxOpcodeCompare)
    {
        if (type != VfxValueTypeBoolean || !RequireScalarInputs(inputTypes, inputCount, 2U) ||
            instruction.Settings.w > 5U)
            return false;
        const float left = asfloat(inputs[0].Primary.x);
        const float right = asfloat(inputs[1].Primary.x);
        bool result = false;
        if (instruction.Settings.w == 0U)
            result = left < right;
        else if (instruction.Settings.w == 1U)
            result = left <= right;
        else if (instruction.Settings.w == 2U)
            result = left == right;
        else if (instruction.Settings.w == 3U)
            result = left != right;
        else if (instruction.Settings.w == 4U)
            result = left >= right;
        else
            result = left > right;
        output.Primary.x = result ? 1U : 0U;
        return true;
    }
    if (opcode == VfxOpcodeRemap)
    {
        if (type != VfxValueTypeScalar || inputCount != 3U || inputTypes[0] != VfxValueTypeScalar ||
            inputTypes[1] != VfxValueTypeScalarRange || inputTypes[2] != VfxValueTypeScalarRange)
            return false;
        const float input = asfloat(inputs[0].Primary.x);
        const float sourceMinimum = asfloat(inputs[1].Primary.x);
        const float sourceMaximum = asfloat(inputs[1].Secondary.x);
        const float destinationMinimum = asfloat(inputs[2].Primary.x);
        const float destinationMaximum = asfloat(inputs[2].Secondary.x);
        const float width = sourceMaximum - sourceMinimum;
        float factor = width == 0.0F ? 0.0F : FiniteOrZero((input - sourceMinimum) / width);
        if ((instruction.Settings.z & VfxValueFlagClampRemap) != 0U)
            factor = clamp(factor, 0.0F, 1.0F);
        output.Primary.x =
            asuint(FiniteOrZero(destinationMinimum + factor * (destinationMaximum - destinationMinimum)));
        return true;
    }

    const bool binaryArithmetic = opcode >= VfxOpcodeAdd && opcode <= VfxOpcodeMaximum;
    if (binaryArithmetic)
    {
        if (type != VfxValueTypeScalar || !RequireScalarInputs(inputTypes, inputCount, 2U))
            return false;
        const float left = asfloat(inputs[0].Primary.x);
        const float right = asfloat(inputs[1].Primary.x);
        float result = 0.0F;
        if (opcode == VfxOpcodeAdd)
            result = left + right;
        else if (opcode == VfxOpcodeSubtract)
            result = left - right;
        else if (opcode == VfxOpcodeMultiply)
            result = left * right;
        else if (opcode == VfxOpcodeDivide)
            result = right == 0.0F ? 0.0F : left / right;
        else if (opcode == VfxOpcodeMinimum)
            result = min(left, right);
        else
            result = max(left, right);
        output.Primary.x = asuint(FiniteOrZero(result));
        return true;
    }
    if (opcode == VfxOpcodeClamp)
    {
        if (type != VfxValueTypeScalar || !RequireScalarInputs(inputTypes, inputCount, 3U))
            return false;
        const float value = asfloat(inputs[0].Primary.x);
        const float first = asfloat(inputs[1].Primary.x);
        const float second = asfloat(inputs[2].Primary.x);
        output.Primary.x = asuint(clamp(value, min(first, second), max(first, second)));
        return true;
    }
    if (opcode == VfxOpcodeLerp || opcode == VfxOpcodeSmoothstep)
    {
        if (type != VfxValueTypeScalar || !RequireScalarInputs(inputTypes, inputCount, 3U))
            return false;
        const float first = asfloat(inputs[0].Primary.x);
        const float second = asfloat(inputs[1].Primary.x);
        const float third = asfloat(inputs[2].Primary.x);
        float result = 0.0F;
        if (opcode == VfxOpcodeLerp)
            result = first + (second - first) * third;
        else
        {
            const float width = second - first;
            if (width != 0.0F && isfinite(width))
            {
                const float factor = clamp(FiniteOrZero((third - first) / width), 0.0F, 1.0F);
                result = factor * factor * (3.0F - 2.0F * factor);
            }
        }
        output.Primary.x = asuint(FiniteOrZero(result));
        return true;
    }
    if (opcode == VfxOpcodeStep || opcode == VfxOpcodeAtan2 || opcode == VfxOpcodePower)
    {
        if (type != VfxValueTypeScalar || !RequireScalarInputs(inputTypes, inputCount, 2U))
            return false;
        const float first = asfloat(inputs[0].Primary.x);
        const float second = asfloat(inputs[1].Primary.x);
        float result = 0.0F;
        if (opcode == VfxOpcodeStep)
            result = second < first ? 0.0F : 1.0F;
        else if (opcode == VfxOpcodeAtan2)
            result = first == 0.0F && second == 0.0F ? 0.0F : atan2(first, second);
        else
            result = pow(first, second);
        output.Primary.x = asuint(FiniteOrZero(result));
        return true;
    }

    const bool unaryScalar = opcode == VfxOpcodeSaturate || opcode == VfxOpcodeAbsolute ||
                             (opcode >= VfxOpcodeSine && opcode <= VfxOpcodeArcTangent) ||
                             (opcode >= VfxOpcodeSquareRoot && opcode <= VfxOpcodeFractional) ||
                             opcode == VfxOpcodeNegate || opcode == VfxOpcodeSign;
    if (unaryScalar)
    {
        if (type != VfxValueTypeScalar || !RequireScalarInputs(inputTypes, inputCount, 1U))
            return false;
        const float value = asfloat(inputs[0].Primary.x);
        float result = 0.0F;
        if (opcode == VfxOpcodeSaturate)
            result = clamp(value, 0.0F, 1.0F);
        else if (opcode == VfxOpcodeAbsolute)
            result = abs(value);
        else if (opcode == VfxOpcodeSine)
            result = sin(value);
        else if (opcode == VfxOpcodeCosine)
            result = cos(value);
        else if (opcode == VfxOpcodeTangent)
            result = tan(value);
        else if (opcode == VfxOpcodeArcSine)
            result = value < -1.0F || value > 1.0F ? 0.0F : asin(value);
        else if (opcode == VfxOpcodeArcCosine)
            result = value < -1.0F || value > 1.0F ? 0.0F : acos(value);
        else if (opcode == VfxOpcodeArcTangent)
            result = atan(value);
        else if (opcode == VfxOpcodeSquareRoot)
            result = value < 0.0F ? 0.0F : sqrt(value);
        else if (opcode == VfxOpcodeExponential)
            result = exp(value);
        else if (opcode == VfxOpcodeLogarithm)
            result = value <= 0.0F ? 0.0F : log(value);
        else if (opcode == VfxOpcodeLogarithmBase2)
            result = value <= 0.0F ? 0.0F : log2(value);
        else if (opcode == VfxOpcodeLogarithmBase10)
            result = value <= 0.0F ? 0.0F : log10(value);
        else if (opcode == VfxOpcodeCeiling)
            result = ceil(value);
        else if (opcode == VfxOpcodeFloor)
            result = floor(value);
        else if (opcode == VfxOpcodeRound)
            result = round(value);
        else if (opcode == VfxOpcodeFractional)
            result = value - floor(value);
        else if (opcode == VfxOpcodeNegate)
            result = -value;
        else
            result = value > 0.0F ? 1.0F : value < 0.0F ? -1.0F : 0.0F;
        output.Primary.x = asuint(FiniteOrZero(result));
        return true;
    }
    return false;
}

void InitializeValueRegisters(out VfxGpuValue registers[MaximumValueRegisters])
{
    [unroll] for (uint index = 0U; index < MaximumValueRegisters; ++index) registers[index] = (VfxGpuValue)0;
}

bool EvaluateValueProgram(inout GpuParticle particle, uint context, float evaluationDeltaSeconds,
                          inout VfxGpuValue registers[MaximumValueRegisters])
{
    [loop] for (uint instructionIndex = 0U; instructionIndex < ValueProgramMetadata.x; ++instructionIndex)
    {
        const VfxGpuValueInstruction instruction = ValueInstructions[instructionIndex];
        if (instruction.Header.z != context)
            continue;
        if (instruction.Output.x >= ValueProgramMetadata.w || instruction.Output.x >= MaximumValueRegisters)
            return false;
        VfxGpuValue result;
        if (!ExecuteValueInstruction(instruction, particle, registers, evaluationDeltaSeconds, result) ||
            !IsFiniteValue(result, instruction.Header.y))
            return false;
        registers[instruction.Output.x] = result;
    }
    return true;
}

float Random01(inout uint state)
{
    state = Hash(state);
    return (state & 0x00ffffffU) * (1.0F / 16777216.0F);
}

float3 RotateByQuaternion(float3 value, float4 rotation)
{
    return value + 2.0F * cross(rotation.xyz, cross(rotation.xyz, value) + rotation.w * value);
}

bool PopFreeIndex(out uint particleIndex)
{
    for (;;)
    {
        const uint current = Counters.Load(0);
        if (current == 0)
        {
            particleIndex = 0;
            return false;
        }
        uint observed = 0;
        Counters.InterlockedCompareExchange(0, current, current - 1, observed);
        if (observed == current)
        {
            particleIndex = FreeIndices[current - 1];
            return true;
        }
    }
}

void PushFreeIndex(uint particleIndex)
{
    uint destination = 0;
    Counters.InterlockedAdd(0, 1, destination);
    if (destination < ParticleCapacity)
        FreeIndices[destination] = particleIndex;
}

void AppendAlive(uint particleIndex)
{
    uint destination = 0;
    Counters.InterlockedAdd(4, 1, destination);
    if (destination < ParticleCapacity)
        AliveIndices[destination] = particleIndex;
}

float ApplyCustomScalar(float value, float operand, uint operation)
{
    if (operation == VfxOperationAssign)
        return operand;
    if (operation == VfxOperationAdd)
        return value + operand;
    if (operation == VfxOperationMultiply)
        return value * operand;
    return value;
}

float3 ApplyCustomVector(float3 value, float3 operand, uint operation)
{
    if (operation == VfxOperationAssign)
        return operand;
    if (operation == VfxOperationAdd)
        return value + operand;
    if (operation == VfxOperationMultiply)
        return value * operand;
    return value;
}

float4 ApplyCustomVector(float4 value, float4 operand, uint operation)
{
    if (operation == VfxOperationAssign)
        return operand;
    if (operation == VfxOperationAdd)
        return value + operand;
    if (operation == VfxOperationMultiply)
        return value * operand;
    return value;
}

bool ApplyCustomInstruction(inout GpuParticle particle, uint instructionIndex, float contextDeltaSeconds,
                            VfxGpuValue registers[MaximumValueRegisters])
{
    if (instructionIndex >= ValueRuntimeMetadata.y)
        return false;
    const VfxGpuCustomInstructionRecord instruction = CustomInstructions[instructionIndex];
    const uint4 control = instruction.Metadata;
    const uint operation = control.z & 0xffU;
    const uint operandType = (control.z >> 16U) & 0xffU;
    if (operation > VfxOperationMultiply)
        return false;
    float4 operand = instruction.Operand;
    if (control.w != 0xffffffffU)
    {
        if (control.w >= ValueProgramMetadata.w || control.w >= MaximumValueRegisters)
            return false;
        operand = asfloat(registers[control.w].Primary);
    }
    if (operandType == VfxValueTypeScalar)
        operand = operand.xxxx;
    else if ((control.y == VfxTargetPosition || control.y == VfxTargetVelocity) && operandType != VfxValueTypeVector3)
        return false;
    else if (control.y == VfxTargetTint && operandType != VfxValueTypeColor && operandType != VfxValueTypeVector4)
        return false;
    else if ((control.y == VfxTargetRotation || control.y == VfxTargetSize) && operandType != VfxValueTypeScalar)
        return false;
    const float scale = (control.z & (1U << 8U)) != 0U ? contextDeltaSeconds : 1.0F;
    operand *= scale;
    if (control.y == VfxTargetPosition)
    {
        if (asuint(SizeParameters.z) == 0)
        {
            const float4 inverseEmitterRotation = float4(-EmitterRotation.xyz, EmitterRotation.w);
            float3 localPosition =
                RotateByQuaternion(particle.PositionAge.xyz - EmitterPosition.xyz, inverseEmitterRotation);
            localPosition = ApplyCustomVector(localPosition, operand.xyz, operation);
            particle.PositionAge.xyz = EmitterPosition.xyz + RotateByQuaternion(localPosition, EmitterRotation);
        }
        else
        {
            particle.PositionAge.xyz = ApplyCustomVector(particle.PositionAge.xyz, operand.xyz, operation);
        }
    }
    else if (control.y == VfxTargetVelocity)
    {
        if (asuint(SizeParameters.z) == 0)
        {
            const float4 inverseEmitterRotation = float4(-EmitterRotation.xyz, EmitterRotation.w);
            float3 localVelocity = RotateByQuaternion(particle.VelocityLifetime.xyz, inverseEmitterRotation);
            localVelocity = ApplyCustomVector(localVelocity, operand.xyz, operation);
            particle.VelocityLifetime.xyz = RotateByQuaternion(localVelocity, EmitterRotation);
        }
        else
        {
            particle.VelocityLifetime.xyz = ApplyCustomVector(particle.VelocityLifetime.xyz, operand.xyz, operation);
        }
    }
    else if (control.y == VfxTargetRotation)
        particle.SizeRotation.z = ApplyCustomScalar(particle.SizeRotation.z, operand.x, operation);
    else if (control.y == VfxTargetTint)
        particle.Tint = ApplyCustomVector(particle.Tint, operand, operation);
    else if (control.y == VfxTargetSize)
        particle.SizeRotation.x = ApplyCustomScalar(particle.SizeRotation.x, operand.x, operation);
    else
        return false;
    return true;
}

bool ApplyParticleOperation(inout GpuParticle particle, uint operationIndex, float normalizedAge,
                            float contextDeltaSeconds, inout uint random, inout bool moved,
                            VfxGpuValue registers[MaximumValueRegisters])
{
    if (operationIndex >= ValueRuntimeMetadata.z)
        return false;
    const uint4 operation = ParticleOperations[operationIndex];
    if (operation.y == VfxParticleOperationShape)
    {
        float3 localPosition = 0.0F.xxx;
        const uint shape = asuint(AccelerationShape.w);
        if (shape == 1)
        {
            localPosition =
                (float3(Random01(random), Random01(random), Random01(random)) * 2.0F - 1.0F) * ShapeExtentRadius.xyz;
        }
        else if (shape == 2)
        {
            const float z = Random01(random) * 2.0F - 1.0F;
            const float azimuth = Random01(random) * 6.28318530717958647692F;
            const float radial = sqrt(max(0.0F, 1.0F - z * z));
            const float radius = pow(Random01(random), 1.0F / 3.0F) * ShapeExtentRadius.w;
            float sine = 0.0F;
            float cosine = 0.0F;
            sincos(azimuth, sine, cosine);
            localPosition = float3(cosine * radial * radius, z * radius, sine * radial * radius);
        }
        else if (shape == 3)
        {
            const float distance = Random01(random) * ShapeRotationParameters.y;
            const float maximumRadius = tan(radians(ShapeRotationParameters.x)) * distance;
            const float radius = sqrt(Random01(random)) * maximumRadius;
            const float azimuth = Random01(random) * 6.28318530717958647692F;
            float sine = 0.0F;
            float cosine = 0.0F;
            sincos(azimuth, sine, cosine);
            localPosition = float3(cosine * radius, distance, sine * radius);
        }
        particle.PositionAge.xyz = EmitterPosition.xyz + RotateByQuaternion(localPosition, EmitterRotation);
    }
    else if (operation.y == VfxParticleOperationInitialize)
    {
        const float3 localVelocity = lerp(VelocityMinimumLifetime.xyz, VelocityMaximumLifetime.xyz,
                                          float3(Random01(random), Random01(random), Random01(random)));
        const float lifetime = lerp(VelocityMinimumLifetime.w, VelocityMaximumLifetime.w, Random01(random));
        particle.VelocityLifetime = float4(RotateByQuaternion(localVelocity, EmitterRotation), max(lifetime, 0.0001F));
        particle.SizeRotation.z = lerp(ShapeRotationParameters.z, ShapeRotationParameters.w, Random01(random));
    }
    else if (operation.y == VfxParticleOperationForce)
    {
        const bool localSpace = asuint(SizeParameters.z) == 0U;
        const float3 acceleration =
            localSpace ? RotateByQuaternion(AccelerationShape.xyz, EmitterRotation) : AccelerationShape.xyz;
        particle.VelocityLifetime.xyz += acceleration * contextDeltaSeconds;
    }
    else if (operation.y == VfxParticleOperationSize)
        particle.SizeRotation.x = lerp(max(SizeParameters.x, 0.0F), max(SizeParameters.y, 0.0F), normalizedAge);
    else if (operation.y == VfxParticleOperationColor)
        particle.Tint = lerp(ColorStart, ColorEnd, normalizedAge);
    else if (operation.y == VfxParticleOperationCollision)
    {
        particle.PositionAge.xyz += particle.VelocityLifetime.xyz * contextDeltaSeconds;
        moved = true;
    }
    else if (operation.y == VfxParticleOperationRenderer)
    {
    }
    else if (operation.y == VfxParticleOperationCustomHlsl)
    {
        if (!ApplyCustomInstruction(particle, operation.z, contextDeltaSeconds, registers))
            return false;
    }
    else
        return false;
    return true;
}

bool ApplyParticleContext(inout GpuParticle particle, uint context, float normalizedAge, float evaluationDeltaSeconds,
                          float operationDeltaSeconds, inout uint random, inout bool moved,
                          inout VfxGpuValue registers[MaximumValueRegisters])
{
    if (!EvaluateValueProgram(particle, context, evaluationDeltaSeconds, registers))
        return false;
    [loop] for (uint operationIndex = 0U; operationIndex < ValueRuntimeMetadata.z; ++operationIndex)
    {
        if (ParticleOperations[operationIndex].x == context &&
            !ApplyParticleOperation(particle, operationIndex, normalizedAge, operationDeltaSeconds, random, moved,
                                    registers))
            return false;
    }
    return true;
}

bool IsFiniteParticle(GpuParticle particle)
{
    return all(isfinite(particle.PositionAge)) && all(isfinite(particle.VelocityLifetime)) &&
           all(isfinite(particle.Tint)) && all(isfinite(particle.SizeRotation)) &&
           all(isfinite(particle.AccelerationSizeEnd)) && all(isfinite(particle.ColorStart)) &&
           all(isfinite(particle.ColorEnd));
}

[numthreads(256, 1, 1)] void CSInitialize(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index < ParticleCapacity)
    {
        GpuParticle particle = (GpuParticle)0;
        particle.PositionAge.w = -1.0F;
        Particles[index] = particle;
        FreeIndices[index] = ParticleCapacity - index - 1;
    }
    if (index == 0)
    {
        Counters.Store(0, ParticleCapacity);
        Counters.Store(4, 0);
        Counters.Store(8, 0);
        Counters.Store(12, 0);
        Counters.Store(16, 0);
        IndirectArguments.Store4(0, uint4(6, 0, 0, 0));
    }
}

    [numthreads(1, 1, 1)] void CSReset(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    Counters.Store(4, 0);
    IndirectArguments.Store4(0, uint4(6, 0, 0, 0));
}

[numthreads(256, 1, 1)] void CSKill(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index >= ParticleCapacity)
        return;
    GpuParticle particle = Particles[index];
    if (particle.PositionAge.w < 0.0F || any(particle.Identity.xy != EmitterIdentity.xy))
        return;

    particle.PositionAge.w = -1.0F;
    Particles[index] = particle;
    PushFreeIndex(index);
    uint ignored = 0;
    Counters.InterlockedAdd(12, 1, ignored);
}

    [numthreads(256, 1, 1)] void CSTransform(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index >= ParticleCapacity)
        return;
    GpuParticle particle = Particles[index];
    if (particle.PositionAge.w < 0.0F || any(particle.Identity.xy != EmitterIdentity.xy))
        return;

    const float4 inversePreviousRotation = float4(-PreviousEmitterRotation.xyz, PreviousEmitterRotation.w);
    const float3 localPosition =
        RotateByQuaternion(particle.PositionAge.xyz - PreviousEmitterPosition.xyz, inversePreviousRotation);
    particle.PositionAge.xyz = EmitterPosition.xyz + RotateByQuaternion(localPosition, EmitterRotation);
    particle.VelocityLifetime.xyz =
        RotateByQuaternion(RotateByQuaternion(particle.VelocityLifetime.xyz, inversePreviousRotation), EmitterRotation);
    particle.AccelerationSizeEnd.xyz = RotateByQuaternion(
        RotateByQuaternion(particle.AccelerationSizeEnd.xyz, inversePreviousRotation), EmitterRotation);
    Particles[index] = particle;
}

[numthreads(256, 1, 1)] void CSSimulate(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index >= ParticleCapacity)
        return;
    GpuParticle particle = Particles[index];
    if (particle.PositionAge.w < 0.0F || any(particle.Identity.xy != EmitterIdentity.xy))
        return;

    if (DeltaSeconds <= 0.0F)
    {
        AppendAlive(index);
        return;
    }
    particle.PositionAge.w += DeltaSeconds;
    if (particle.PositionAge.w >= particle.VelocityLifetime.w)
    {
        particle.PositionAge.w = -1.0F;
        Particles[index] = particle;
        PushFreeIndex(index);
        uint ignored = 0;
        Counters.InterlockedAdd(12, 1, ignored);
        return;
    }

    const float age = saturate(particle.PositionAge.w / max(particle.VelocityLifetime.w, 0.0001F));
    uint random =
        Hash(index ^ EmitterIdentity.x ^ EmitterIdentity.y ^ particle.SequenceIdentity.x ^ particle.SequenceIdentity.y);
    VfxGpuValue valueRegisters[MaximumValueRegisters];
    InitializeValueRegisters(valueRegisters);
    bool moved = false;
    bool valid = ApplyParticleContext(particle, VfxContextUpdate, age, DeltaSeconds, DeltaSeconds, random, moved,
                                      valueRegisters);
    if (!moved)
        particle.PositionAge.xyz += particle.VelocityLifetime.xyz * DeltaSeconds;
    bool outputMoved = false;
    valid = valid && ApplyParticleContext(particle, VfxContextOutput, age, DeltaSeconds, DeltaSeconds, random,
                                          outputMoved, valueRegisters);
    if (!valid || !IsFiniteParticle(particle))
    {
        particle.PositionAge.w = -1.0F;
        Particles[index] = particle;
        PushFreeIndex(index);
        uint ignored = 0;
        Counters.InterlockedAdd(12, 1, ignored);
        return;
    }
    Particles[index] = particle;
    AppendAlive(index);
}

    [numthreads(256, 1, 1)] void CSSpawn(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint spawnIndex = dispatchThreadId.x;
    if (spawnIndex >= SpawnCount)
        return;
    uint particleIndex = 0;
    if (!PopFreeIndex(particleIndex))
    {
        uint ignored = 0;
        Counters.InterlockedAdd(16, 1, ignored);
        return;
    }

    GpuParticle particle = (GpuParticle)0;
    particle.PositionAge = float4(EmitterPosition.xyz, 0.0F);
    particle.VelocityLifetime = float4(0.0F, 0.0F, 0.0F, 1.0F);
    particle.Tint = ColorStart;
    particle.SizeRotation = float4(max(SizeParameters.x, 0.0F), max(SizeParameters.x, 0.0F), 0.0F, 0.0F);
    const bool localSpace = asuint(SizeParameters.z) == 0;
    const float3 acceleration =
        localSpace ? RotateByQuaternion(AccelerationShape.xyz, EmitterRotation) : AccelerationShape.xyz;
    particle.AccelerationSizeEnd = float4(acceleration, max(SizeParameters.y, 0.0F));
    particle.ColorStart = ColorStart;
    particle.ColorEnd = ColorEnd;
    const uint2 spawnIdentity = Add64(EmitterIdentity.zw, uint2(spawnIndex, 0U));
    particle.Identity = uint4(EmitterIdentity.xy, 0U, 0U);
    particle.SequenceIdentity = uint4(spawnIdentity, Add64(spawnIdentity, uint2(1U, 0U)));
    uint random = Hash(RandomSeed ^ spawnIdentity.x ^ spawnIdentity.y);
    VfxGpuValue valueRegisters[MaximumValueRegisters];
    InitializeValueRegisters(valueRegisters);
    bool moved = false;
    bool valid =
        ApplyParticleContext(particle, VfxContextSpawn, 0.0F, DeltaSeconds, 0.0F, random, moved, valueRegisters);
    valid =
        valid && ApplyParticleContext(particle, VfxContextInitialize, 0.0F, 0.0F, 0.0F, random, moved, valueRegisters);
    valid = valid && ApplyParticleContext(particle, VfxContextOutput, 0.0F, 0.0F, 0.0F, random, moved, valueRegisters);
    if (!valid || !IsFiniteParticle(particle))
    {
        particle.PositionAge.w = -1.0F;
        Particles[particleIndex] = particle;
        PushFreeIndex(particleIndex);
        uint invalidIgnored = 0;
        Counters.InterlockedAdd(16, 1, invalidIgnored);
        return;
    }
    Particles[particleIndex] = particle;
    AppendAlive(particleIndex);
    uint ignored = 0;
    Counters.InterlockedAdd(8, 1, ignored);
}

[numthreads(1, 1, 1)] void CSFinalize(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint aliveCount = min(Counters.Load(4), ParticleCapacity);
    IndirectArguments.Store4(0, uint4(6, aliveCount, 0, 0));
}

StructuredBuffer<GpuParticle> RenderParticles : register(t0, space0);
StructuredBuffer<uint> RenderAliveIndices : register(t1, space0);

cbuffer VfxCamera : register(b0, space1)
{
    float4x4 ViewProjection;
    float4 CameraRight;
    float4 CameraUp;
};

struct VfxVertexOutput
{
    float4 Tint : TEXCOORD0;
    float2 UV : TEXCOORD1;
    float4 Position : SV_Position;
};

VfxVertexOutput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    static const float2 corners[6] = {float2(-0.5F, -0.5F), float2(0.5F, -0.5F), float2(0.5F, 0.5F),
                                      float2(-0.5F, -0.5F), float2(0.5F, 0.5F),  float2(-0.5F, 0.5F)};
    const GpuParticle particle = RenderParticles[RenderAliveIndices[instanceId]];
    const float2 corner = corners[vertexId];
    const float angle = particle.SizeRotation.z * 0.01745329251994329577F;
    float sine = 0.0F;
    float cosine = 0.0F;
    sincos(angle, sine, cosine);
    const float2 rotatedCorner = float2(corner.x * cosine - corner.y * sine, corner.x * sine + corner.y * cosine);
    const float3 world =
        particle.PositionAge.xyz +
        (CameraRight.xyz * rotatedCorner.x + CameraUp.xyz * rotatedCorner.y) * max(particle.SizeRotation.x, 0.0F);
    VfxVertexOutput output;
    output.Position = mul(ViewProjection, float4(world, 1.0F));
    output.Tint = particle.Tint;
    output.UV = corner + 0.5F.xx;
    return output;
}

float4 PSMain(VfxVertexOutput input) : SV_Target0
{
    const float2 centered = input.UV * 2.0F - 1.0F;
    const float alpha = saturate((1.0F - dot(centered, centered)) * 4.0F);
    return float4(input.Tint.rgb, input.Tint.a * alpha);
}

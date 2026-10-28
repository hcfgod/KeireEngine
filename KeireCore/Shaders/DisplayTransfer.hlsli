#pragma once

// The scene remains linear; encode once for an SDR UNORM presentation consumer.
float EncodeDisplaySrgb(float value)
{
    const float bounded = clamp(value, 0.0F, 1.0F);
    return bounded <= 0.0031308F ? 12.92F * bounded : 1.055F * pow(bounded, 1.0F / 2.4F) - 0.055F;
}

float DecodeDisplaySrgb(float value)
{
    const float bounded = clamp(value, 0.0F, 1.0F);
    return bounded <= 0.04045F ? bounded / 12.92F : pow((bounded + 0.055F) / 1.055F, 2.4F);
}

// Apply before the first 8-bit sRGB target, not after its quantization has discarded the gradient.
// A shared RGB offset avoids colored speckles; stable screen-space noise avoids temporal flicker.
float DitherDisplaySrgb(float value, float centeredNoise)
{
    return clamp(EncodeDisplaySrgb(value) + centeredNoise / 255.0F, 0.0F, 1.0F);
}

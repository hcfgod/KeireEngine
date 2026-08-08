#!/usr/bin/env python3
"""Generate Kéire's Unity 6.3 LTS VFX Graph parity manifest.

The catalog authority is the user-facing reference material shipped in the
official com.unity.visualeffectgraph package. Dynamic catalog families remain
as Unity's documented label patterns (for example ``[Set/Add] <Attribute>``)
instead of inventing a finite expansion for data-driven variant providers.
"""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Iterable


GRAPHICS_COMMIT = "2d2e78cc9d6254bc6e7c9c5552cea053508e86cb"
GRAPHICS_REPOSITORY = "https://github.com/Unity-Technologies/Graphics"
PACKAGE_PATH = Path("Packages/com.unity.visualeffectgraph")
PACKAGE_VERSION = "17.3.0"
UNITY_RELEASE = "Unity 6.3 LTS"
UNITY_EDITOR_LINE = "6000.3"

IGNORED_REFERENCE_PAGES = {
    "Block-Collision-LandingPage.md",
    "Block-SetPositionShape-LandingPage.md",
    "Context-OutputLitSettings.md",
    "Context-OutputSharedSettings.md",
}

SUPPORTED_STATUSES = ("Supported", "GPU Required", "Kéire Equivalent", "Disabled")
BACKEND_TIERS = ("CPU Only", "CPU and GPU", "GPU Required")

# These mappings describe implementation presence, not parity closure. A mapped
# row remains Disabled until its Unity type rules, settings, CPU/GPU behavior,
# and differential tests are complete.
KEIRE_IMPLEMENTATIONS = {
    "Absolute": "keire.operator.absolute",
    "Acos": "keire.operator.acos",
    "Add": "keire.operator.add",
    "Age Over Lifetime": "keire.operator.age-over-lifetime",
    "Asin": "keire.operator.asin",
    "Atan": "keire.operator.atan",
    "Atan2": "keire.operator.atan2",
    "Get Attribute: alive": "keire.operator.attribute-alive",
    "Get Attribute: alpha": "keire.operator.attribute-alpha",
    "Get Attribute: angle": "keire.operator.attribute-angle",
    "Get Attribute: axisX": "keire.operator.attribute-axis-x",
    "Get Attribute: axisY": "keire.operator.attribute-axis-y",
    "Get Attribute: axisZ": "keire.operator.attribute-axis-z",
    "Get Attribute: color": "keire.operator.attribute-color",
    "Get Attribute: oldPosition": "keire.operator.attribute-old-position",
    "Get Attribute: particleCountInStrip": "keire.operator.attribute-particle-count-in-strip",
    "Get Attribute: particleIndexInStrip": "keire.operator.attribute-particle-index-in-strip",
    "Get Attribute: position": "keire.operator.attribute-position",
    "Get Attribute: seed": "keire.operator.attribute-seed",
    "Get Attribute: size": "keire.operator.attribute-size",
    "Get Attribute: spawnTime": "keire.operator.attribute-spawn-time",
    "Get Attribute: stripIndex": "keire.operator.attribute-strip-index",
    "Get Attribute: velocity": "keire.operator.attribute-velocity",
    "Ratio Over Strip": "keire.operator.ratio-over-strip",
    "Operator > Inline|Color": "keire.operator.inline-color",
    "Operator > Inline|Direction": "keire.operator.inline-direction",
    "Operator > Inline|Position": "keire.operator.inline-position",
    "Operator > Inline|Vector": "keire.operator.inline-vector",
    "Operator > Inline|Vector2": "keire.operator.inline-vector2",
    "Operator > Inline|Vector3": "keire.operator.inline-vector3",
    "Operator > Inline|Vector4": "keire.operator.inline-vector4",
    "Operator > Inline|bool": "keire.operator.inline-bool",
    "Operator > Inline|float": "keire.operator.inline-float",
    "Operator > Inline|int": "keire.operator.inline-int",
    "Operator > Inline|uint": "keire.operator.inline-uint",
    "Epsilon (Ɛ)": "keire.operator.epsilon",
    "Pi (π)": "keire.operator.pi",
    "Value Noise": "keire.operator.value-noise",
    "Perlin Noise": "keire.operator.perlin-noise",
    "Cellular Noise": "keire.operator.cellular-noise",
    "Value Curl Noise": "keire.operator.value-curl-noise",
    "Perlin Curl Noise": "keire.operator.perlin-curl-noise",
    "Cellular Curl Noise": "keire.operator.cellular-curl-noise",
    "Polar to Rectangular": "keire.operator.polar-to-rectangular",
    "Rectangular to Polar": "keire.operator.rectangular-to-polar",
    "Rectangular to Spherical": "keire.operator.rectangular-to-spherical",
    "Spherical to Rectangular": "keire.operator.spherical-to-rectangular",
    "Rotate 2D": "keire.operator.rotate-2d",
    "Rotate 3D": "keire.operator.rotate-3d",
    "Operator > Logic|And": "keire.operator.and",
    "Operator > Bitwise|And": "keire.operator.bitwise-and",
    "Operator > Bitwise|Complement": "keire.operator.bitwise-complement",
    "Operator > Bitwise|Left Shift": "keire.operator.bitwise-left-shift",
    "Operator > Bitwise|Or": "keire.operator.bitwise-or",
    "Operator > Bitwise|Right Shift": "keire.operator.bitwise-right-shift",
    "Operator > Bitwise|Xor": "keire.operator.bitwise-xor",
    "Branch": "keire.operator.branch",
    "Clamp": "keire.operator.clamp",
    "Compare": "keire.operator.compare",
    "Color Luma": "keire.operator.color-luma",
    "Cross Product": "keire.operator.cross-product",
    "Ceiling": "keire.operator.ceiling",
    "Delta Time": "keire.operator.delta-time",
    "Distance": "keire.operator.distance",
    "Discretize": "keire.operator.discretize",
    "Divide": "keire.operator.divide",
    "Dot Product": "keire.operator.dot-product",
    "Exp": "keire.operator.exponential",
    "Floor": "keire.operator.floor",
    "Fractional": "keire.operator.fractional",
    "Frame Index": "keire.operator.frame-index",
    "Get Attribute: age": "keire.operator.age",
    "Get Attribute: lifetime": "keire.operator.lifetime",
    "Get Attribute: particleID": "keire.operator.particle-id",
    "Get Attribute: spawnIndex": "keire.operator.spawn-index",
    "Length": "keire.operator.length",
    "HSV to RGB": "keire.operator.hsv-to-rgb",
    "Inverse Lerp": "keire.operator.inverse-lerp",
    "Lerp": "keire.operator.lerp",
    "Log": "keire.operator.logarithm",
    "Maximum": "keire.operator.maximum",
    "Minimum": "keire.operator.minimum",
    "Multiply": "keire.operator.multiply",
    "Modulo": "keire.operator.modulo",
    "Nand": "keire.operator.nand",
    "Negate (-x)": "keire.operator.negate",
    "Normalize": "keire.operator.normalize",
    "Operator > Logic|Not": "keire.operator.not",
    "Operator > Logic|Or": "keire.operator.or",
    "Nor": "keire.operator.nor",
    "One Minus (1-x)": "keire.operator.one-minus",
    "Random Number": "keire.operator.random",
    "Remap": "keire.operator.remap",
    "Reciprocal (1/x)": "keire.operator.reciprocal",
    "RGB to HSV": "keire.operator.rgb-to-hsv",
    "Saturate": "keire.operator.saturate",
    "Sawtooth Wave": "keire.operator.sawtooth-wave",
    "Sign": "keire.operator.sign",
    "Sine": "keire.operator.sine",
    "Sine Wave": "keire.operator.sine-wave",
    "Smoothstep": "keire.operator.smoothstep",
    "Square Wave": "keire.operator.square-wave",
    "Square Root": "keire.operator.square-root",
    "Squared Distance": "keire.operator.squared-distance",
    "Squared Length": "keire.operator.squared-length",
    "Step": "keire.operator.step",
    "Subtract": "keire.operator.subtract",
    "System Seed": "keire.operator.system-seed",
    "Tangent": "keire.operator.tangent",
    "Cosine": "keire.operator.cosine",
    "Power": "keire.operator.power",
    "Round": "keire.operator.round",
    "Total Time": "keire.operator.time",
    "Triangle Wave": "keire.operator.triangle-wave",
    "Context|Event": "keire.context.event",
    "Context|Initialize": "keire.context.initialize",
    "Context|Spawn": "keire.context.spawn",
    "Context|Update": "keire.context.update",
    "Collide with Depth Buffer": "keire.block.collision",
    "Set Position (Mesh)": "keire.block.shape",
    "Output Particle Line": "keire.output.renderer",
    "Output Particle Mesh": "keire.output.renderer",
    "Output Particle Primitive": "keire.output.renderer",
    "Output ShaderGraph Mesh": "keire.output.renderer",
}

KEIRE_ENABLED_EQUIVALENTS = {
    "keire.block.collision",
    "keire.block.shape",
    "keire.context.event",
    "keire.context.initialize",
    "keire.context.spawn",
    "keire.context.update",
    "keire.output.renderer",
    "keire.operator.absolute",
    "keire.operator.acos",
    "keire.operator.add",
    "keire.operator.age",
    "keire.operator.age-over-lifetime",
    "keire.operator.and",
    "keire.operator.asin",
    "keire.operator.atan",
    "keire.operator.atan2",
    "keire.operator.bitwise-and",
    "keire.operator.bitwise-complement",
    "keire.operator.bitwise-left-shift",
    "keire.operator.bitwise-or",
    "keire.operator.bitwise-right-shift",
    "keire.operator.bitwise-xor",
    "keire.operator.branch",
    "keire.operator.ceiling",
    "keire.operator.clamp",
    "keire.operator.color-luma",
    "keire.operator.compare",
    "keire.operator.cosine",
    "keire.operator.cross-product",
    "keire.operator.delta-time",
    "keire.operator.distance",
    "keire.operator.discretize",
    "keire.operator.divide",
    "keire.operator.dot-product",
    "keire.operator.exponential",
    "keire.operator.floor",
    "keire.operator.fractional",
    "keire.operator.frame-index",
    "keire.operator.hsv-to-rgb",
    "keire.operator.inverse-lerp",
    "keire.operator.length",
    "keire.operator.lerp",
    "keire.operator.lifetime",
    "keire.operator.logarithm",
    "keire.operator.maximum",
    "keire.operator.minimum",
    "keire.operator.modulo",
    "keire.operator.multiply",
    "keire.operator.nand",
    "keire.operator.negate",
    "keire.operator.normalize",
    "keire.operator.not",
    "keire.operator.nor",
    "keire.operator.one-minus",
    "keire.operator.or",
    "keire.operator.particle-id",
    "keire.operator.power",
    "keire.operator.reciprocal",
    "keire.operator.remap",
    "keire.operator.rgb-to-hsv",
    "keire.operator.round",
    "keire.operator.saturate",
    "keire.operator.sawtooth-wave",
    "keire.operator.sign",
    "keire.operator.sine",
    "keire.operator.sine-wave",
    "keire.operator.smoothstep",
    "keire.operator.spawn-index",
    "keire.operator.square-root",
    "keire.operator.square-wave",
    "keire.operator.squared-distance",
    "keire.operator.squared-length",
    "keire.operator.step",
    "keire.operator.subtract",
    "keire.operator.system-seed",
    "keire.operator.tangent",
    "keire.operator.time",
    "keire.operator.triangle-wave",
    "keire.operator.attribute-alive",
    "keire.operator.attribute-alpha",
    "keire.operator.attribute-angle",
    "keire.operator.attribute-axis-x",
    "keire.operator.attribute-axis-y",
    "keire.operator.attribute-axis-z",
    "keire.operator.attribute-color",
    "keire.operator.attribute-old-position",
    "keire.operator.attribute-particle-count-in-strip",
    "keire.operator.attribute-particle-index-in-strip",
    "keire.operator.attribute-position",
    "keire.operator.attribute-seed",
    "keire.operator.attribute-size",
    "keire.operator.attribute-spawn-time",
    "keire.operator.attribute-strip-index",
    "keire.operator.attribute-velocity",
    "keire.operator.ratio-over-strip",
    "keire.operator.inline-color",
    "keire.operator.inline-direction",
    "keire.operator.inline-position",
    "keire.operator.inline-vector",
    "keire.operator.inline-vector2",
    "keire.operator.inline-vector3",
    "keire.operator.inline-vector4",
    "keire.operator.inline-bool",
    "keire.operator.inline-float",
    "keire.operator.inline-int",
    "keire.operator.inline-uint",
    "keire.operator.epsilon",
    "keire.operator.pi",
    "keire.operator.value-noise",
    "keire.operator.perlin-noise",
    "keire.operator.cellular-noise",
    "keire.operator.value-curl-noise",
    "keire.operator.perlin-curl-noise",
    "keire.operator.cellular-curl-noise",
    "keire.operator.polar-to-rectangular",
    "keire.operator.rectangular-to-polar",
    "keire.operator.rectangular-to-spherical",
    "keire.operator.spherical-to-rectangular",
    "keire.operator.rotate-2d",
    "keire.operator.rotate-3d",
}

PRODUCTION_SLICES = [
    {
        "id": "core-value-modulation",
        "name": "Core value modulation",
        "backendTier": "CPU and GPU",
        "implementations": sorted(
            implementation
            for implementation in KEIRE_ENABLED_EQUIVALENTS
            if implementation.startswith("keire.operator.")
        ),
        "tests": [
            "KeireTests/Source/Vfx/VfxExpressionTests.cpp",
            "KeireTests/Source/Rendering/GpuVertexLayoutTests.cpp",
        ],
        "samples": ["Samples/KeireSandbox/Assets/Vfx/VfxEffect.keirevfx"],
        "documentation": ["Docs/Vfx.md"],
    },
    {
        "id": "context-output-pipeline",
        "name": "Context and particle-output pipeline",
        "backendTier": "CPU and GPU",
        "implementations": sorted(
            implementation
            for implementation in KEIRE_ENABLED_EQUIVALENTS
            if not implementation.startswith("keire.operator.")
        ),
        "tests": [
            "KeireTests/Source/Vfx/VfxGpuCapabilityTests.cpp",
            "KeireTests/Source/Vfx/VfxTests.cpp",
        ],
        "samples": [
            "Samples/KeireSandbox/Assets/Vfx/ArcaneSigilOrbit.keirevfx",
            "Samples/KeireSandbox/Assets/Vfx/EmberShardCyclone.keirevfx",
            "Samples/KeireSandbox/Assets/Vfx/VfxEffect.keirevfx",
        ],
        "documentation": ["Docs/Vfx.md"],
    },
]

CORE_UTILITY_TESTS = [
    "KeireTests/Source/Vfx/VfxExpressionTests.cpp",
    "KeireTests/Source/Rendering/GpuVertexLayoutTests.cpp",
]

KEIRE_TESTS = {
    "keire.block.collision": ["KeireTests/Source/Vfx/VfxGpuCapabilityTests.cpp"],
    "keire.block.shape": ["KeireTests/Source/Vfx/VfxGpuCapabilityTests.cpp"],
    "keire.context.event": ["KeireTests/Source/Vfx/VfxTests.cpp"],
    "keire.context.initialize": ["KeireTests/Source/Vfx/VfxTests.cpp"],
    "keire.context.spawn": ["KeireTests/Source/Vfx/VfxTests.cpp"],
    "keire.context.update": ["KeireTests/Source/Vfx/VfxTests.cpp"],
    "keire.output.renderer": ["KeireTests/Source/Vfx/VfxTests.cpp"],
    "keire.operator.absolute": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.acos": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.asin": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.atan": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.atan2": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.add": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.and": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.branch": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.clamp": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.compare": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.cross-product": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.ceiling": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.delta-time": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.distance": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.divide": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.dot-product": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.exponential": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.floor": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.fractional": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.length": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.lerp": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.logarithm": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.maximum": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.minimum": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.multiply": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.negate": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.normalize": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.not": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.or": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.random": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.remap": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.saturate": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.sawtooth-wave": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.sign": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.sine": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.sine-wave": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.smoothstep": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.square-wave": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.square-root": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.step": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.subtract": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.tangent": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.cosine": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.power": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.round": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.time": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.triangle-wave": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.age": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.lifetime": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.particle-id": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.spawn-index": ["KeireTests/Source/Vfx/VfxExpressionTests.cpp"],
    "keire.operator.age-over-lifetime": CORE_UTILITY_TESTS,
    "keire.operator.bitwise-and": CORE_UTILITY_TESTS,
    "keire.operator.bitwise-complement": CORE_UTILITY_TESTS,
    "keire.operator.bitwise-left-shift": CORE_UTILITY_TESTS,
    "keire.operator.bitwise-or": CORE_UTILITY_TESTS,
    "keire.operator.bitwise-right-shift": CORE_UTILITY_TESTS,
    "keire.operator.bitwise-xor": CORE_UTILITY_TESTS,
    "keire.operator.color-luma": CORE_UTILITY_TESTS,
    "keire.operator.hsv-to-rgb": CORE_UTILITY_TESTS,
    "keire.operator.rgb-to-hsv": CORE_UTILITY_TESTS,
    "keire.operator.discretize": CORE_UTILITY_TESTS,
    "keire.operator.frame-index": CORE_UTILITY_TESTS,
    "keire.operator.inverse-lerp": CORE_UTILITY_TESTS,
    "keire.operator.modulo": CORE_UTILITY_TESTS,
    "keire.operator.nand": CORE_UTILITY_TESTS,
    "keire.operator.nor": CORE_UTILITY_TESTS,
    "keire.operator.one-minus": CORE_UTILITY_TESTS,
    "keire.operator.reciprocal": CORE_UTILITY_TESTS,
    "keire.operator.squared-distance": CORE_UTILITY_TESTS,
    "keire.operator.squared-length": CORE_UTILITY_TESTS,
    "keire.operator.system-seed": CORE_UTILITY_TESTS,
}

for implementation in (
    "keire.operator.attribute-alive",
    "keire.operator.attribute-alpha",
    "keire.operator.attribute-angle",
    "keire.operator.attribute-axis-x",
    "keire.operator.attribute-axis-y",
    "keire.operator.attribute-axis-z",
    "keire.operator.attribute-color",
    "keire.operator.attribute-old-position",
    "keire.operator.attribute-particle-count-in-strip",
    "keire.operator.attribute-particle-index-in-strip",
    "keire.operator.attribute-position",
    "keire.operator.attribute-seed",
    "keire.operator.attribute-size",
    "keire.operator.attribute-spawn-time",
    "keire.operator.attribute-strip-index",
    "keire.operator.attribute-velocity",
    "keire.operator.ratio-over-strip",
    "keire.operator.inline-color",
    "keire.operator.inline-direction",
    "keire.operator.inline-position",
    "keire.operator.inline-vector",
    "keire.operator.inline-vector2",
    "keire.operator.inline-vector3",
    "keire.operator.inline-vector4",
    "keire.operator.inline-bool",
    "keire.operator.inline-float",
    "keire.operator.inline-int",
    "keire.operator.inline-uint",
    "keire.operator.epsilon",
    "keire.operator.pi",
    "keire.operator.value-noise",
    "keire.operator.perlin-noise",
    "keire.operator.cellular-noise",
    "keire.operator.value-curl-noise",
    "keire.operator.perlin-curl-noise",
    "keire.operator.cellular-curl-noise",
    "keire.operator.polar-to-rectangular",
    "keire.operator.rectangular-to-polar",
    "keire.operator.rectangular-to-spherical",
    "keire.operator.spherical-to-rectangular",
    "keire.operator.rotate-2d",
    "keire.operator.rotate-3d",
):
    KEIRE_TESTS[implementation] = CORE_UTILITY_TESTS

GPU_KEYWORDS = (
    "attribute map",
    "buffer",
    "camera",
    "custom hlsl",
    "depth",
    "mesh",
    "output",
    "point cache",
    "signed distance",
    "skinned",
    "texture",
    "vector field",
)


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--unity-source",
        required=True,
        type=Path,
        help="Pinned Unity Graphics repository checkout.",
    )
    parser.add_argument(
        "--output",
        default=Path("Docs/VfxParityManifest.json"),
        type=Path,
        help="Manifest destination.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Fail if the destination differs instead of rewriting it.",
    )
    return parser.parse_args()


_PLACEHOLDER_SENTINEL = "\ue000vfx-placeholder-{}\ue001"
_HTML_TAG_PATTERN = re.compile(
    r"</?(?:a|b|br|code|div|em|font|i|img|kbd|li|ol|p|pre|span|strong|sub|sup|table|tbody|td|th|thead|tr|ul)\b[^>]*>",
    flags=re.IGNORECASE,
)


def clean_markdown(value: str) -> str:
    placeholders: list[str] = []

    def protect_placeholder(match: re.Match[str]) -> str:
        placeholders.append(f"<{match.group(1)}>")
        return _PLACEHOLDER_SENTINEL.format(len(placeholders) - 1)

    # Unity documents dynamic VFX variants as escaped Markdown placeholders,
    # for example ``\<Attribute>`` (some pages also escape the closing bracket).
    # Protect them before removing actual HTML.
    value = re.sub(r"\\<([^<>\\\r\n]+)\\?>", protect_placeholder, value)
    value = re.sub(r"<br\s*/?>", "; ", value, flags=re.IGNORECASE)
    value = _HTML_TAG_PATTERN.sub("", value)
    value = re.sub(r"!\[([^]]*)\]\([^)]*\)", r"\1", value)
    value = re.sub(r"\[([^]]+)\]\([^)]*\)", r"\1", value)
    value = value.replace("**", "").replace("__", "").replace("`", "")
    value = value.replace("\\<", "<").replace("\\>", ">")
    value = re.sub(r"\s+", " ", html.unescape(value)).strip(" |\t\r\n")
    for index, placeholder in enumerate(placeholders):
        value = value.replace(_PLACEHOLDER_SENTINEL.format(index), placeholder)
    return value


def split_menu_path(value: str) -> list[str]:
    """Split Unity menu separators without splitting a ``<Placeholder>``."""

    return [
        clean_markdown(part)
        for part in re.split(r"\s+>\s+", value)
        if clean_markdown(part)
    ]


def slug(value: str) -> str:
    value = value.casefold().replace("é", "e")
    value = re.sub(r"[^a-z0-9]+", "-", value)
    return value.strip("-") or "unnamed"


def verify_snapshot(graphics_root: Path) -> tuple[Path, dict[str, Any]]:
    graphics_root = graphics_root.resolve()
    package_root = graphics_root / PACKAGE_PATH
    package_json_path = package_root / "package.json"
    if not package_json_path.is_file():
        raise RuntimeError(f"VFX package not found at {package_json_path}")

    try:
        commit = subprocess.run(
            ["git", "-C", str(graphics_root), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as error:
        raise RuntimeError(
            "The Unity source must be a Git checkout so its commit can be verified."
        ) from error
    if commit != GRAPHICS_COMMIT:
        raise RuntimeError(
            f"Unity Graphics commit is {commit}; expected {GRAPHICS_COMMIT}."
        )

    package_json = json.loads(package_json_path.read_text(encoding="utf-8"))
    if (
        package_json.get("version") != PACKAGE_VERSION
        or package_json.get("unity") != UNITY_EDITOR_LINE
    ):
        raise RuntimeError(
            "Unexpected VFX package identity: "
            f"version={package_json.get('version')!r}, unity={package_json.get('unity')!r}."
        )
    return package_root, package_json


def first_heading(text: str, fallback: str) -> str:
    match = re.search(r"^#\s+(.+?)\s*$", text, flags=re.MULTILINE)
    return clean_markdown(match.group(1)) if match else fallback


def menu_paths(text: str) -> list[str]:
    lines = text.splitlines()
    paths: list[str] = []
    for index, line in enumerate(lines):
        if "Menu Path" not in line:
            continue
        remainder = line.split("Menu Path", 1)[1].lstrip(" :")
        candidate = clean_markdown(remainder)
        if len(split_menu_path(candidate)) > 1:
            paths.append(candidate)
        if candidate:
            continue
        for following in lines[index + 1 :]:
            stripped = following.strip()
            if not stripped:
                if paths:
                    break
                continue
            if not stripped.startswith("-"):
                break
            candidate = clean_markdown(stripped.lstrip("- "))
            if len(split_menu_path(candidate)) > 1:
                paths.append(candidate)
    paths = list(dict.fromkeys(paths))
    if paths:
        return paths
    fallback = re.search(
        r"then\s+select\s+\*\*([^*]+)\*\*\s*>\s*\*\*([^*]+)\*\*",
        text,
        flags=re.IGNORECASE,
    )
    if fallback:
        return [
            f"{clean_markdown(fallback.group(1))} > {clean_markdown(fallback.group(2))}"
        ]
    return []


def split_table_row(line: str) -> list[str]:
    return [clean_markdown(cell) for cell in line.strip().strip("|").split("|")]


def is_separator_row(cells: Iterable[str]) -> bool:
    cells = list(cells)
    return bool(cells) and all(
        re.fullmatch(r":?-{2,}:?", cell.replace(" ", "")) for cell in cells
    )


def parse_settings(text: str) -> list[dict[str, str]]:
    lines = text.splitlines()
    settings: list[dict[str, str]] = []
    section = ""
    section_table_seen = False
    index = 0
    while index < len(lines):
        line = lines[index]
        heading = re.match(r"^#{2,4}\s+(.+?)\s*$", line)
        if heading:
            section = clean_markdown(heading.group(1))
            section_table_seen = False
            index += 1
            continue
        relevant = any(
            marker in section.casefold()
            for marker in (
                "settings",
                "configuration",
                "inspector window properties",
                "inspector properties",
            )
        )
        if not relevant or section_table_seen or not line.lstrip().startswith("|"):
            index += 1
            continue

        table: list[list[str]] = []
        while index < len(lines) and lines[index].lstrip().startswith("|"):
            table.append(split_table_row(lines[index]))
            index += 1
        section_table_seen = True
        if len(table) < 2:
            continue
        headers = table[0]
        for cells in table[1:]:
            if is_separator_row(cells) or not cells or not cells[0]:
                continue
            padded = cells + [""] * max(0, len(headers) - len(cells))
            row = dict(zip(headers, padded))
            name = padded[0]
            type_name = ""
            description = ""
            for key, value in row.items():
                normalized = key.casefold()
                if normalized == "type":
                    type_name = value
                elif normalized == "description":
                    description = value
            settings.append(
                {
                    "name": name,
                    "type": type_name,
                    "description": description,
                    "section": section,
                }
            )

    unique: dict[tuple[str, str, str, str], dict[str, str]] = {}
    for setting in settings:
        key = (
            setting["section"],
            setting["name"],
            setting["type"],
            setting["description"],
        )
        unique[key] = setting
    return list(unique.values())


def source_index(package_root: Path) -> dict[str, list[str]]:
    result: dict[str, list[str]] = {}
    pattern = re.compile(r'VFXHelpURL\("([^"]+)"\)')
    for path in sorted((package_root / "Editor").rglob("*.cs")):
        text = path.read_text(encoding="utf-8-sig", errors="strict")
        relative = path.relative_to(package_root).as_posix()
        for key in pattern.findall(text):
            result.setdefault(key, []).append(relative)
    return result


def documentation_digest(documents: list[Path], documentation_root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(documents):
        digest.update(path.relative_to(documentation_root).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def backend_tier(kind: str, label: str, category: str) -> str:
    combined = f"{label} {category}".casefold()
    if kind == "Output" or any(keyword in combined for keyword in GPU_KEYWORDS):
        return "GPU Required"
    return "CPU and GPU"


def disabled_reason(kind: str, implementation: str | None, backend: str) -> str:
    if implementation:
        return (
            "A native Kéire baseline exists, but the complete Unity 6.3 signatures, settings, "
            "dynamic variants, CPU/GPU lowering, and differential acceptance coverage are not closed."
        )
    if backend == "GPU Required":
        return (
            "No production Kéire implementation is registered; this feature requires the schema-4 "
            "resource/output ABI and cooked GPU backend before it can be enabled."
        )
    return f"No production Kéire {kind} implementation is registered; creation and compilation must reject this row."


def keire_implementation(category: str, label: str, reference_title: str) -> str | None:
    return (
        KEIRE_IMPLEMENTATIONS.get(f"{category}|{label}")
        or KEIRE_IMPLEMENTATIONS.get(label)
        or KEIRE_IMPLEMENTATIONS.get(reference_title)
    )


def catalog_documents(documentation_root: Path) -> list[Path]:
    documents: list[Path] = []
    for prefix in ("Operator-", "Block-", "Context-"):
        documents.extend(documentation_root.glob(prefix + "*.md"))
    return sorted(
        path for path in documents if path.name not in IGNORED_REFERENCE_PAGES
    )


def classify(path: Path) -> str:
    if path.name.startswith("Operator-"):
        return "Operator"
    if path.name.startswith("Block-"):
        return "Block"
    if path.name.startswith("Context-Output"):
        return "Output"
    return "Context"


def label_from_path(menu_path: str, heading: str, path_count: int) -> str:
    if not menu_path:
        return heading
    parts = split_menu_path(menu_path)
    if not parts or not parts[-1]:
        return heading
    menu_label = parts[-1]
    if path_count > 1 or heading.endswith("Block reference"):
        return menu_label
    menu_slug = slug(menu_label)
    heading_slug = slug(heading)
    if menu_slug == heading_slug:
        return heading
    if "[" not in menu_label and "<" not in menu_label and menu_slug in heading_slug:
        return menu_label
    return heading


def category_from_path(menu_path: str, kind: str) -> str:
    if not menu_path:
        return "Uncategorized"
    parts = split_menu_path(menu_path)
    if len(parts) <= 1:
        return kind
    return " > ".join(parts[:-1])


def entry_id(kind: str, document: Path, label: str, path_count: int) -> str:
    base = document.stem.split("-", 1)[1]
    suffix = "" if path_count == 1 else "." + slug(label)
    return f"unity.{kind.casefold()}.{slug(base)}{suffix}"


def make_entries(package_root: Path, documents: list[Path]) -> list[dict[str, Any]]:
    help_sources = source_index(package_root)
    entries: list[dict[str, Any]] = []
    for document in documents:
        text = document.read_text(encoding="utf-8-sig", errors="strict")
        kind = classify(document)
        heading = first_heading(text, document.stem.split("-", 1)[1])
        paths = menu_paths(text) or [""]
        settings = parse_settings(text)
        help_key = document.stem
        sources = help_sources.get(help_key, [])
        for menu_path in paths:
            label = label_from_path(menu_path, heading, len(paths))
            category = category_from_path(menu_path, kind)
            implementation = keire_implementation(category, label, heading)
            backend = (
                "CPU and GPU"
                if implementation in KEIRE_ENABLED_EQUIVALENTS
                else backend_tier(kind, label, category)
            )
            support = (
                "Kéire Equivalent"
                if implementation in KEIRE_ENABLED_EQUIVALENTS
                else "Disabled"
            )
            entries.append(
                {
                    "id": entry_id(kind, document, label, len(paths)),
                    "kind": kind,
                    "unityLabel": label,
                    "unityReferenceTitle": heading,
                    "unityCategory": category,
                    "unityMenuPath": menu_path,
                    "unitySettings": settings,
                    "unitySource": {
                        "documentation": f"{PACKAGE_PATH.as_posix()}/Documentation~/{document.name}",
                        "implementations": [
                            f"{PACKAGE_PATH.as_posix()}/{source}" for source in sources
                        ],
                    },
                    "keire": {
                        "support": support,
                        "implementation": implementation,
                        "backendTier": backend,
                        "tests": KEIRE_TESTS.get(implementation, [])
                        if implementation
                        else [],
                        "documentation": ["Docs/Vfx.md"] if implementation else [],
                        "disabledReason": None
                        if support != "Disabled"
                        else disabled_reason(kind, implementation, backend),
                    },
                }
            )
    return sorted(
        entries,
        key=lambda entry: (
            entry["kind"],
            entry["unityCategory"],
            entry["unityLabel"],
            entry["id"],
        ),
    )


def build_manifest(graphics_root: Path) -> dict[str, Any]:
    package_root, package_json = verify_snapshot(graphics_root)
    documentation_root = package_root / "Documentation~"
    documents = catalog_documents(documentation_root)
    entries = make_entries(package_root, documents)
    counts = {
        kind: sum(entry["kind"] == kind for entry in entries)
        for kind in ("Operator", "Block", "Context", "Output")
    }
    counts["Total"] = len(entries)
    counts["Disabled"] = sum(
        entry["keire"]["support"] == "Disabled" for entry in entries
    )
    counts["WithKeireImplementation"] = sum(
        bool(entry["keire"]["implementation"]) for entry in entries
    )
    return {
        "manifestSchema": 1,
        "program": "Kéire VFX Graph — Unity 6.3 LTS Production Parity",
        "snapshot": {
            "unityRelease": UNITY_RELEASE,
            "unityEditorLine": UNITY_EDITOR_LINE,
            "package": package_json["name"],
            "packageVersion": package_json["version"],
            "graphicsRepository": GRAPHICS_REPOSITORY,
            "graphicsCommit": GRAPHICS_COMMIT,
            "sourcePath": PACKAGE_PATH.as_posix(),
            "catalogAuthority": "The user-facing Operator, Block, Context, and Output reference pages shipped in Documentation~.",
            "dynamicVariantPolicy": "Data-driven variants are represented by their exact documented label pattern and settings.",
            "documentationSha256": documentation_digest(documents, documentation_root),
        },
        "policy": {
            "supportValues": list(SUPPORTED_STATUSES),
            "backendTierValues": list(BACKEND_TIERS),
            "unfinishedBehavior": "Disabled rows must be rejected with disabledReason and must never lower as no-ops.",
            "unityAssetCompatibility": False,
            "copiedUnitySourceOrIcons": False,
        },
        "tooling": {
            "generator": "Scripts/Vfx/generate_vfx_parity_manifest.py",
            "validator": "Scripts/Vfx/validate_vfx_parity_manifest.py",
            "runtimeCatalogContract": "KeireCore/Source/Vfx/VfxNodeCatalogContract.inc",
            "runtimeCatalogExporter": "Scripts/Vfx/export_vfx_runtime_catalog.py",
            "offlineReconciler": "Scripts/Vfx/reconcile_vfx_manifest.py",
            "optionalUnityCatalogExporter": "Scripts/Vfx/UnityVfxCatalogExporter.cs",
            "canonicalEncoding": "UTF-8 JSON, two-space indentation, LF newline",
        },
        "counts": counts,
        "productionSlices": PRODUCTION_SLICES,
        "entries": entries,
    }


def main() -> int:
    options = arguments()
    try:
        manifest = build_manifest(options.unity_source)
        encoded = json.dumps(manifest, ensure_ascii=False, indent=2) + "\n"
        if options.check:
            if (
                not options.output.is_file()
                or options.output.read_text(encoding="utf-8") != encoded
            ):
                print(
                    f"VFX parity manifest is stale: {options.output}", file=sys.stderr
                )
                return 1
            print(
                f"VFX parity manifest is current ({manifest['counts']['Total']} entries)."
            )
            return 0
        options.output.parent.mkdir(parents=True, exist_ok=True)
        options.output.write_text(encoded, encoding="utf-8", newline="\n")
        print(f"Wrote {options.output} ({manifest['counts']['Total']} entries).")
        return 0
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"VFX parity manifest generation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

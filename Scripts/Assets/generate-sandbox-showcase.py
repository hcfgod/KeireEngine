#!/usr/bin/env python3
"""Generate the canonical Kéire Sandbox material, scripting, VFX, and scene showcase."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SANDBOX_ROOT = REPOSITORY_ROOT / "Samples" / "KeireSandbox"
ASSETS_ROOT = SANDBOX_ROOT / "Assets"
SHOWCASE_ROOT = ASSETS_ROOT / "Examples" / "MaterialLab"
LEGACY_GRAPH_ROOT = ASSETS_ROOT / "Materials" / "MaterialGraphs"
LEGACY_GENERATED_GRAPH_ROOT = ASSETS_ROOT / "Generated" / "MaterialGraphs"
SCENE_PATH = ASSETS_ROOT / "Scenes" / "SandboxShowcase.keirescene"
LEGACY_SCENE_PATH = ASSETS_ROOT / "Scenes" / "ShaderMaterialShowcase.keirescene"
PLINTH_MATERIAL_PATH = SHOWCASE_ROOT / "Materials" / "ShowcasePlinth.keirematerial"
SCRIPT_PATH = ASSETS_ROOT / "Scripts" / "Runtime" / "Examples" / "ShowcaseOrbit.cs"
VFX_GUIDE_PATH = ASSETS_ROOT / "Vfx" / "README.md"
NAMESPACE = uuid.UUID("2fd1dd90-9e2b-54c0-a559-b654bb581a4e")

SHADER_GRAPH_TYPE = "4b454952-4553-4752-4150-480000000001"
MATERIAL_GRAPH_TYPE = "4b454952-454d-4752-4150-480000000001"
MATERIAL_TYPE = "4b454952-454d-4154-4552-49414c000001"
SCENE_TYPE = "4b454952-4553-4345-4e45-415353455401"
TEXT_TYPE = "4b454952-4554-4558-5441-535345540001"
TRANSFORM_TYPE = "4b454952-4554-5241-4e53-464f524d0001"
CAMERA_TYPE = "4b454952-4543-414d-4552-410000000001"
MESH_RENDERER_TYPE = "4b454952-454d-4553-4852-454e44455201"
DIRECTIONAL_LIGHT_TYPE = "4b454952-4544-4952-4c49-474854000001"
VFX_EMITTER_TYPE = "4b454952-4556-4658-454d-495454455201"
SHOWCASE_ORBIT_TYPE = "73616e64-626f-4078-8000-000000000060"
DEFAULT_SURFACE_SHADER = "b1b2c3d4-1000-4000-8000-000000000001"

SCALAR = 0
VECTOR2 = 1
VECTOR3 = 2
VECTOR4 = 3
COLOR = 4
TEXTURE2D = 5
MATERIAL_ATTRIBUTES = 6
BSDF = 7
INPUT = 0
OUTPUT = 1

SURFACE = 0
TRANSPARENT = 1
UNLIT = 3

TEXTURES = {
    "diffuse": "38760a1d-9dfa-4bbc-8ba1-50921ae9d748",
    "normal": "0a8ba309-c28a-4842-8949-09ff8c60a1fa",
    "stone": "c0ffee00-0000-4000-8000-000000000002",
}

BUILTIN_MESHES = [
    "4b454952-4553-5048-4552-454d45534801",  # Sphere
    "4b454952-4543-5542-454d-455348000001",  # Cube
    "4b454952-4554-4f52-5553-4d4553480001",  # Torus
    "4b454952-4543-594c-494e-4445524d5301",  # Cylinder
    "4b454952-4543-4150-5355-4c454d455301",  # Capsule
]

VFX_EXAMPLES = [
    ("Arcane Nova", "b8da08ac-aa68-44a9-859d-6d36e96f58b2"),
    ("Arcane Sigil Orbit", "c27fbc8b-ef96-40ad-a5bc-30c688438f3c"),
    ("Ember Shard Cyclone", "2cd9a41b-120c-4bbc-988d-3d719fc4d264"),
    ("Forge Sparks", "6f17db40-57f3-4e48-9d54-a4a69b1d7c01"),
]

EXAMPLE_FOCUS = {
    "paint": "Exposed PBR inputs",
    "ceramic": "Texture sampling and UV tiling",
    "pulse": "Time-driven unlit emission",
    "cutout": "Procedural opacity masking",
    "clearcoat": "Layered automotive clear coat",
    "brushed": "Anisotropic metallic response",
    "glass": "Transmission and refraction",
    "stone": "World-aligned triplanar detail",
    "dissolve": "Procedural dissolve and emissive edge",
    "hologram": "Animated scanlines and hue shift",
    "vertex_wave": "Vertex-stage displacement",
    "shield": "Fresnel iridescence and transparency",
}

TYPE_DEFAULTS: dict[int, Any] = {
    SCALAR: 0.0,
    VECTOR2: [0.0, 0.0],
    VECTOR3: [0.0, 0.0, 0.0],
    VECTOR4: [0.0, 0.0, 0.0, 0.0],
    COLOR: [0.0, 0.0, 0.0, 0.0],
    TEXTURE2D: None,
    MATERIAL_ATTRIBUTES: None,
    BSDF: None,
}


@dataclass(frozen=True)
class Property:
    symbol: str
    display_name: str
    value_type: int
    default: Any
    material_value: Any
    category: str = "Surface"
    minimum: float | None = None
    maximum: float | None = None
    step: float | None = None


@dataclass(frozen=True)
class Example:
    number: int
    level: str
    slug: str
    display_name: str
    output: int
    style: str
    properties: tuple[Property, ...]
    alpha_mode: int = 0
    double_sided: bool = False


EXAMPLES = (
    Example(1, "Foundations", "StudioPaint", "Studio Paint", SURFACE, "paint", (
        Property("BaseTint", "Base Tint", COLOR, [0.12, 0.32, 0.72, 1.0], [0.72, 0.06, 0.04, 1.0], "Surface"),
        Property("Metallic", "Metallic", SCALAR, 0.0, 0.0, "Surface", 0.0, 1.0, 0.01),
        Property("Roughness", "Roughness", SCALAR, 0.38, 0.24, "Surface", 0.0, 1.0, 0.01),
    )),
    Example(2, "Foundations", "TiledCeramic", "Tiled Ceramic", SURFACE, "ceramic", (
        Property("BaseTexture", "Base Texture", TEXTURE2D, TEXTURES["diffuse"], TEXTURES["diffuse"], "Textures"),
        Property("Tiling", "Tiling", VECTOR2, [3.0, 3.0], [5.0, 5.0], "Coordinates"),
        Property("Roughness", "Roughness", SCALAR, 0.3, 0.18, "Surface", 0.0, 1.0, 0.01),
    )),
    Example(3, "Foundations", "NeonPulse", "Neon Pulse", UNLIT, "pulse", (
        Property("BaseTint", "Base Tint", COLOR, [0.01, 0.03, 0.08, 1.0], [0.0, 0.04, 0.1, 1.0], "Surface"),
        Property("EmissionColor", "Emission Color", COLOR, [0.05, 0.65, 1.0, 1.0], [0.0, 1.0, 0.65, 1.0], "Emission"),
        Property("PulseSpeed", "Pulse Speed", SCALAR, 2.4, 3.2, "Animation", 0.0, 10.0, 0.05),
    )),
    Example(4, "Foundations", "ProceduralCutout", "Procedural Cutout", TRANSPARENT, "cutout", (
        Property("LeafTint", "Surface Tint", COLOR, [0.08, 0.45, 0.18, 1.0], [0.12, 0.72, 0.3, 1.0], "Surface"),
        Property("Cutoff", "Cutoff", SCALAR, 0.48, 0.56, "Transparency", 0.0, 1.0, 0.01),
        Property("NoiseScale", "Noise Scale", SCALAR, 7.0, 11.0, "Procedural", 0.5, 32.0, 0.1),
    ), alpha_mode=1, double_sided=True),
    Example(5, "Production", "AutomotiveClearCoat", "Automotive Clear Coat", SURFACE, "clearcoat", (
        Property("BodyColor", "Body Color", COLOR, [0.42, 0.015, 0.01, 1.0], [0.015, 0.12, 0.5, 1.0], "Paint"),
        Property("BaseRoughness", "Base Roughness", SCALAR, 0.32, 0.2, "Paint", 0.0, 1.0, 0.01),
        Property("CoatStrength", "Coat Strength", SCALAR, 1.0, 1.0, "Clear Coat", 0.0, 1.0, 0.01),
        Property("CoatRoughness", "Coat Roughness", SCALAR, 0.08, 0.04, "Clear Coat", 0.0, 1.0, 0.01),
    )),
    Example(6, "Production", "BrushedAlloy", "Brushed Alloy", SURFACE, "brushed", (
        Property("MetalTint", "Metal Tint", COLOR, [0.42, 0.48, 0.58, 1.0], [0.7, 0.45, 0.12, 1.0], "Metal"),
        Property("Roughness", "Roughness", SCALAR, 0.28, 0.2, "Metal", 0.0, 1.0, 0.01),
        Property("Anisotropy", "Anisotropy", SCALAR, 0.82, 0.94, "Metal", -1.0, 1.0, 0.01),
    )),
    Example(7, "Production", "FrostedGlass", "Frosted Glass", TRANSPARENT, "glass", (
        Property("GlassTint", "Glass Tint", COLOR, [0.18, 0.72, 0.82, 1.0], [0.55, 0.12, 0.92, 1.0], "Glass"),
        Property("Frost", "Frost", SCALAR, 0.42, 0.62, "Glass", 0.0, 1.0, 0.01),
        Property("Opacity", "Opacity", SCALAR, 0.32, 0.24, "Transparency", 0.0, 1.0, 0.01),
        Property("Refraction", "Refraction", SCALAR, 0.14, 0.22, "Glass", 0.0, 1.0, 0.01),
    ), alpha_mode=2, double_sided=True),
    Example(8, "Production", "WorldAlignedStone", "World-Aligned Stone", SURFACE, "stone", (
        Property("StoneTexture", "Stone Texture", TEXTURE2D, TEXTURES["stone"], TEXTURES["stone"], "Textures"),
        Property("WorldScale", "World Scale", SCALAR, 0.32, 0.46, "Coordinates", 0.01, 4.0, 0.01),
        Property("Roughness", "Roughness", SCALAR, 0.78, 0.68, "Surface", 0.0, 1.0, 0.01),
    )),
    Example(9, "Advanced", "EnergyDissolve", "Energy Dissolve", TRANSPARENT, "dissolve", (
        Property("SurfaceTint", "Surface Tint", COLOR, [0.04, 0.08, 0.12, 1.0], [0.06, 0.01, 0.12, 1.0], "Surface"),
        Property("EdgeColor", "Edge Color", COLOR, [1.0, 0.28, 0.02, 1.0], [0.15, 0.72, 1.0, 1.0], "Emission"),
        Property("Threshold", "Dissolve Threshold", SCALAR, 0.46, 0.58, "Dissolve", 0.0, 1.0, 0.01),
    ), alpha_mode=1),
    Example(10, "Advanced", "HologramScanlines", "Hologram Scanlines", TRANSPARENT, "hologram", (
        Property("HologramColor", "Hologram Color", COLOR, [0.02, 0.45, 1.0, 1.0], [0.55, 0.08, 1.0, 1.0], "Hologram"),
        Property("ScanSpeed", "Scan Speed", SCALAR, 0.65, 1.25, "Animation", -4.0, 4.0, 0.05),
        Property("Opacity", "Opacity", SCALAR, 0.52, 0.42, "Transparency", 0.0, 1.0, 0.01),
    ), alpha_mode=2, double_sided=True),
    Example(11, "Advanced", "VertexWave", "Vertex Wave", SURFACE, "vertex_wave", (
        Property("SurfaceTint", "Surface Tint", COLOR, [0.03, 0.28, 0.68, 1.0], [0.03, 0.75, 0.52, 1.0], "Surface"),
        Property("WaveFrequency", "Wave Frequency", SCALAR, 4.0, 7.0, "Displacement", 0.1, 20.0, 0.1),
        Property("WaveAmplitude", "Wave Amplitude", SCALAR, 0.25, 0.4, "Displacement", 0.0, 2.0, 0.01),
    )),
    Example(12, "Advanced", "IridescentShield", "Iridescent Shield", TRANSPARENT, "shield", (
        Property("CoreColor", "Core Color", COLOR, [0.01, 0.08, 0.2, 1.0], [0.08, 0.01, 0.28, 1.0], "Shield"),
        Property("EdgeColor", "Edge Color", COLOR, [0.05, 0.85, 1.0, 1.0], [1.0, 0.18, 0.72, 1.0], "Shield"),
        Property("FresnelPower", "Fresnel Power", SCALAR, 3.5, 5.0, "Shield", 0.1, 12.0, 0.05),
        Property("Opacity", "Opacity", SCALAR, 0.38, 0.48, "Transparency", 0.0, 1.0, 0.01),
    ), alpha_mode=2, double_sided=True),
)


NODE_TYPES = {
    "parameter": (1, "keire.input.parameter", "Parameter"),
    "constant": (2, "keire.input.constant", "Constant"),
    "texture_sample": (3, "keire.texture.sample_2d", "Sample Texture 2D"),
    "uv": (4, "keire.input.uv0", "UV0"),
    "uv_transform": (5, "keire.coordinates.uv_transform", "UV Transform"),
    "add": (9, "keire.math.add", "Add"),
    "multiply": (10, "keire.math.multiply", "Multiply"),
    "lerp": (11, "keire.math.lerp", "Lerp"),
    "one_minus": (12, "keire.math.one_minus", "One Minus"),
    "clamp": (13, "keire.math.saturate", "Saturate"),
    "sine": (26, "keire.math.sine", "Sine"),
    "dot": (30, "keire.vector.dot", "Dot Product"),
    "smooth_step": (32, "keire.math.smooth_step", "Smooth Step"),
    "fresnel": (34, "keire.surface.fresnel", "Fresnel"),
    "world_position": (36, "keire.input.world_position", "World Position"),
    "world_normal": (37, "keire.input.world_normal", "World Normal"),
    "view_direction": (38, "keire.input.view_direction", "View Direction"),
    "simple_noise": (40, "keire.procedural.simple_noise", "Simple Noise"),
    "time": (65, "keire.input.time", "Time"),
    "hue_shift": (73, "keire.color.hue_shift", "Hue Shift"),
    "checkerboard": (74, "keire.procedural.checkerboard", "Checkerboard"),
    "panner": (76, "keire.coordinates.panner", "Panner"),
    "linear_gradient": (80, "keire.procedural.linear_gradient", "Linear Gradient"),
    "facing_ratio": (86, "keire.surface.facing_ratio", "Facing Ratio"),
    "gradient_noise": (88, "keire.procedural.gradient_noise", "Gradient Noise"),
    "wave": (89, "keire.procedural.wave", "Wave"),
    "triplanar": (90, "keire.texture.triplanar_sample", "Triplanar Sample"),
    "height_to_normal": (92, "keire.surface.height_to_normal", "Height To Normal"),
}


def stable_id(label: str) -> str:
    return str(uuid.uuid5(NAMESPACE, label))


def derive_subasset_id(parent: str, key: str) -> str:
    digest = bytearray(hashlib.sha256(f"{parent}\n{key}".encode("utf-8")).digest()[:16])
    digest[6] = (digest[6] & 0x0F) | 0x50
    digest[8] = (digest[8] & 0x3F) | 0x80
    return str(uuid.UUID(bytes=bytes(digest)))


def default_value(value_type: int) -> Any:
    value = TYPE_DEFAULTS[value_type]
    return list(value) if isinstance(value, list) else value


def pin(name: str, value_type: int, direction: int, value: Any | None = None) -> tuple[str, int, int, Any]:
    return name, value_type, direction, default_value(value_type) if value is None else value


def node_spec(kind: str, value_type: int) -> tuple[int, str, str, int, Any, list[tuple[str, int, int, Any]]]:
    if kind == "parameter" or kind == "constant":
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, value_type, default_value(value_type), [pin("Value", value_type, OUTPUT)]
    if kind == "texture_sample":
        number, type_id, name = NODE_TYPES[kind]
        pins = [pin("Texture", TEXTURE2D, INPUT), pin("UV", VECTOR2, INPUT), pin("RGBA", COLOR, OUTPUT),
                pin("RGB", VECTOR3, OUTPUT), pin("R", SCALAR, OUTPUT), pin("G", SCALAR, OUTPUT),
                pin("B", SCALAR, OUTPUT), pin("A", SCALAR, OUTPUT)]
        return number, type_id, name, COLOR, default_value(COLOR), pins
    if kind == "uv":
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, VECTOR2, default_value(VECTOR2), [pin("UV", VECTOR2, OUTPUT)]
    if kind == "uv_transform":
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, VECTOR2, default_value(VECTOR2), [pin("UV", VECTOR2, INPUT),
            pin("Tiling", VECTOR2, INPUT, [1.0, 1.0]), pin("Offset", VECTOR2, INPUT), pin("UV", VECTOR2, OUTPUT)]
    if kind in {"add", "multiply"}:
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, value_type, default_value(value_type), [pin("A", value_type, INPUT),
            pin("B", value_type, INPUT), pin("Result", value_type, OUTPUT)]
    if kind == "lerp":
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, value_type, default_value(value_type), [pin("A", value_type, INPUT),
            pin("B", value_type, INPUT), pin("T", SCALAR, INPUT, 0.5), pin("Result", value_type, OUTPUT)]
    if kind in {"one_minus", "clamp", "sine"}:
        number, type_id, name = NODE_TYPES[kind]
        input_name = "Value"
        return number, type_id, name, value_type, default_value(value_type), [pin(input_name, value_type, INPUT),
            pin("Result", value_type, OUTPUT)]
    if kind == "dot":
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, SCALAR, 0.0, [pin("A", VECTOR3, INPUT), pin("B", VECTOR3, INPUT),
            pin("Dot", SCALAR, OUTPUT)]
    if kind == "smooth_step":
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, value_type, default_value(value_type), [pin("Edge Min", value_type, INPUT),
            pin("Edge Max", value_type, INPUT, 1.0 if value_type == SCALAR else default_value(value_type)),
            pin("Value", value_type, INPUT), pin("Result", value_type, OUTPUT)]
    if kind == "fresnel":
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, SCALAR, 0.0, [pin("Normal", VECTOR3, INPUT, [0.0, 0.0, 1.0]),
            pin("Power", SCALAR, INPUT, 5.0), pin("F0", SCALAR, INPUT, 0.04), pin("Fresnel", SCALAR, OUTPUT)]
    if kind in {"world_position", "world_normal", "view_direction"}:
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, VECTOR3, default_value(VECTOR3), [pin("Vector", VECTOR3, OUTPUT)]
    if kind == "simple_noise":
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, SCALAR, 0.0, [pin("UV", VECTOR2, INPUT), pin("Scale", SCALAR, INPUT, 5.0),
            pin("Detail", SCALAR, INPUT, 0.5), pin("Noise", SCALAR, OUTPUT)]
    if kind == "time":
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, SCALAR, 0.0, [pin("Seconds", SCALAR, OUTPUT)]
    if kind == "hue_shift":
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, COLOR, default_value(COLOR), [pin("Color", COLOR, INPUT, [1.0, 1.0, 1.0, 1.0]),
            pin("Shift", SCALAR, INPUT), pin("Color", COLOR, OUTPUT)]
    if kind == "checkerboard":
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, COLOR, default_value(COLOR), [pin("UV", VECTOR2, INPUT),
            pin("Color A", COLOR, INPUT, [0.05, 0.05, 0.05, 1.0]),
            pin("Color B", COLOR, INPUT, [0.8, 0.8, 0.8, 1.0]),
            pin("Scale", VECTOR2, INPUT, [8.0, 8.0]), pin("Color", COLOR, OUTPUT)]
    if kind == "panner":
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, VECTOR2, default_value(VECTOR2), [pin("UV", VECTOR2, INPUT),
            pin("Speed", VECTOR2, INPUT, [0.1, 0.0]), pin("Time", SCALAR, INPUT), pin("UV", VECTOR2, OUTPUT)]
    if kind == "linear_gradient":
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, SCALAR, 0.0, [pin("UV", VECTOR2, INPUT),
            pin("Direction", VECTOR2, INPUT, [1.0, 0.0]), pin("Offset", SCALAR, INPUT),
            pin("Gradient", SCALAR, OUTPUT)]
    if kind == "facing_ratio":
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, SCALAR, 0.0, [pin("Normal", VECTOR3, INPUT, [0.0, 0.0, 1.0]),
            pin("Power", SCALAR, INPUT, 1.0), pin("Ratio", SCALAR, OUTPUT)]
    if kind == "gradient_noise":
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, SCALAR, 0.0, [pin("UV", VECTOR2, INPUT), pin("Scale", SCALAR, INPUT, 5.0),
            pin("Noise", SCALAR, OUTPUT)]
    if kind == "wave":
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, SCALAR, 0.0, [pin("UV", VECTOR2, INPUT),
            pin("Direction", VECTOR2, INPUT, [1.0, 0.0]), pin("Frequency", SCALAR, INPUT, 8.0),
            pin("Phase", SCALAR, INPUT), pin("Wave", SCALAR, OUTPUT)]
    if kind == "triplanar":
        number, type_id, name = NODE_TYPES[kind]
        pins = [pin("Texture", TEXTURE2D, INPUT), pin("Position", VECTOR3, INPUT),
                pin("Normal", VECTOR3, INPUT, [0.0, 0.0, 1.0]), pin("Scale", SCALAR, INPUT, 1.0),
                pin("Blend Sharpness", SCALAR, INPUT, 4.0), pin("RGBA", COLOR, OUTPUT),
                pin("RGB", VECTOR3, OUTPUT), pin("R", SCALAR, OUTPUT), pin("G", SCALAR, OUTPUT),
                pin("B", SCALAR, OUTPUT), pin("A", SCALAR, OUTPUT)]
        return number, type_id, name, COLOR, default_value(COLOR), pins
    if kind == "height_to_normal":
        number, type_id, name = NODE_TYPES[kind]
        return number, type_id, name, VECTOR3, default_value(VECTOR3), [pin("Height", SCALAR, INPUT, 0.5),
            pin("Strength", SCALAR, INPUT, 1.0), pin("Normal", VECTOR3, OUTPUT)]
    raise ValueError(f"Unsupported showcase node kind: {kind}")


def master_pins(output: int) -> list[tuple[str, int, int, Any]]:
    pins = [
        pin("BaseColor", COLOR, INPUT, [1.0, 1.0, 1.0, 1.0]), pin("Metallic", SCALAR, INPUT, 0.0),
        pin("Roughness", SCALAR, INPUT, 0.5), pin("Specular", SCALAR, INPUT, 0.5),
        pin("ClearCoat", SCALAR, INPUT, 0.0), pin("ClearCoatRoughness", SCALAR, INPUT, 0.25),
        pin("SheenColor", COLOR, INPUT, [0.0, 0.0, 0.0, 1.0]), pin("SheenRoughness", SCALAR, INPUT, 0.5),
        pin("Normal", VECTOR3, INPUT, [0.0, 0.0, 1.0]), pin("DetailNormal", VECTOR3, INPUT, [0.0, 0.0, 1.0]),
        pin("Parallax", SCALAR, INPUT, 0.0), pin("Emission", COLOR, INPUT, [0.0, 0.0, 0.0, 1.0]),
        pin("Occlusion", SCALAR, INPUT, 1.0), pin("Opacity", SCALAR, INPUT, 1.0),
        pin("SubsurfaceColor", COLOR, INPUT, [1.0, 0.35, 0.25, 1.0]), pin("Subsurface", SCALAR, INPUT, 0.0),
        pin("Anisotropy", SCALAR, INPUT, 0.0), pin("Tangent", VECTOR3, INPUT, [1.0, 0.0, 0.0]),
        pin("Transmission", SCALAR, INPUT, 0.0), pin("IndexOfRefraction", SCALAR, INPUT, 1.5),
        pin("Refraction", SCALAR, INPUT, 0.0), pin("Thickness", SCALAR, INPUT, 1.0),
        pin("MaterialAttributes", MATERIAL_ATTRIBUTES, INPUT), pin("WorldPositionOffset", VECTOR3, INPUT),
        pin("PixelDepthOffset", SCALAR, INPUT, 0.0),
    ]
    if output == UNLIT:
        allowed = {"BaseColor", "Emission", "Opacity", "WorldPositionOffset", "PixelDepthOffset"}
        pins = [entry for entry in pins if entry[0] in allowed]
        pins[0] = ("Color", pins[0][1], pins[0][2], pins[0][3])
    return pins


class GraphBuilder:
    def __init__(self, scope: str, output: int, material_output: bool = False) -> None:
        self.scope = scope
        self.output = output
        self.nodes: list[dict[str, Any]] = []
        self.connections: list[dict[str, Any]] = []
        output_name = "Material Output" if material_output else (
            "Unlit Shader Output" if output == UNLIT else "Transparent Shader Output" if output == TRANSPARENT
            else "Lit Shader Output")
        self.master = self._append_node(0, "keire.output.material", output_name, COLOR,
                                        [1.0, 1.0, 1.0, 1.0], master_pins(output), [1280.0, 160.0])

    def _append_node(self, kind: int, type_id: str, name: str, value_type: int, value: Any,
                     pins: list[tuple[str, int, int, Any]], position: list[float], *, symbol: str = "",
                     metadata: dict[str, Any] | None = None) -> dict[str, Any]:
        ordinal = len(self.nodes)
        node_id = stable_id(f"{self.scope}/node/{ordinal}/{type_id}/{name}")
        encoded_pins = []
        for pin_ordinal, (pin_name, pin_type, direction, pin_default) in enumerate(pins):
            encoded_pins.append({
                "id": stable_id(f"{node_id}/pin/{pin_ordinal}/{direction}/{pin_name}"),
                "name": pin_name,
                "type": pin_type,
                "direction": direction,
                "default": pin_default,
            })
        node = {
            "id": node_id,
            "typeId": type_id,
            "kind": kind,
            "name": name,
            "position": position,
            "valueType": value_type,
            "value": value,
            "textureSemantic": 0,
            "symbol": symbol,
            "include": "",
            "function": "",
            "referencedAsset": None,
            "parameterMetadata": metadata or {"description": "", "category": "", "sortPriority": 0},
            "pins": encoded_pins,
        }
        self.nodes.append(node)
        return node

    def add(self, kind: str, value_type: int, position: tuple[float, float], *, name: str | None = None,
            value: Any | None = None, symbol: str = "", defaults: dict[str, Any] | None = None,
            metadata: dict[str, Any] | None = None) -> dict[str, Any]:
        kind_number, type_id, default_name, canonical_type, canonical_value, pins = node_spec(kind, value_type)
        defaults = defaults or {}
        pins = [(pin_name, pin_type, direction, defaults.get(pin_name, pin_default))
                for pin_name, pin_type, direction, pin_default in pins]
        return self._append_node(kind_number, type_id, name or default_name, canonical_type,
                                 canonical_value if value is None else value, pins,
                                 [float(position[0]), float(position[1])], symbol=symbol, metadata=metadata)

    def parameter(self, prop: Property, position: tuple[float, float], prefix: str = "") -> dict[str, Any]:
        metadata: dict[str, Any] = {
            "description": f"{prop.display_name} for the {self.scope.split('/')[-1]} example.",
            "category": prop.category,
            "sortPriority": len([node for node in self.nodes if node["kind"] == 1]),
        }
        if prop.minimum is not None:
            metadata["minimum"] = prop.minimum
        if prop.maximum is not None:
            metadata["maximum"] = prop.maximum
        if prop.step is not None:
            metadata["step"] = prop.step
        symbol = f"{prefix}{prop.symbol}"
        return self.add("parameter", prop.value_type, position, name=prop.display_name, value=prop.default,
                        symbol=symbol, metadata=metadata)

    @staticmethod
    def _pin(node: dict[str, Any], name: str, direction: int) -> str:
        matches = [entry for entry in node["pins"] if entry["name"] == name and entry["direction"] == direction]
        if len(matches) != 1:
            raise ValueError(f"Expected one {name} pin on {node['name']}, found {len(matches)}")
        return str(matches[0]["id"])

    def connect(self, source: dict[str, Any], output_pin: str, target: dict[str, Any], input_pin: str) -> None:
        ordinal = len(self.connections)
        self.connections.append({
            "id": stable_id(f"{self.scope}/connection/{ordinal}/{source['id']}/{output_pin}/{target['id']}/{input_pin}"),
            "output": [source["id"], self._pin(source, output_pin, OUTPUT)],
            "input": [target["id"], self._pin(target, input_pin, INPUT)],
        })

    def graph(self) -> dict[str, Any]:
        return {
            "schemaVersion": 3,
            "purpose": 0,
            "output": self.output,
            "nodes": self.nodes,
            "connections": self.connections,
            "keywords": [],
            "includeRoots": ["Assets"],
        }


def add_parameter_connections(builder: GraphBuilder, example: Example, prefix: str = "") -> dict[str, dict[str, Any]]:
    parameters: dict[str, dict[str, Any]] = {}
    for index, prop in enumerate(example.properties):
        parameters[prop.symbol] = builder.parameter(prop, (40.0, 60.0 + index * 140.0), prefix)
    return parameters


def author_network(builder: GraphBuilder, example: Example, prefix: str = "") -> dict[str, dict[str, Any]]:
    params = add_parameter_connections(builder, example, prefix)
    master = builder.master
    x = 360.0
    style = example.style

    def direct(symbol: str, pin_name: str) -> None:
        if symbol in params and any(pin["name"] == pin_name for pin in master["pins"]):
            builder.connect(params[symbol], "Value", master, pin_name)

    if style == "paint":
        direct("BaseTint", "BaseColor")
        direct("Metallic", "Metallic")
        direct("Roughness", "Roughness")
    elif style == "ceramic":
        uv = builder.add("uv", VECTOR2, (x, 80.0))
        transform = builder.add("uv_transform", VECTOR2, (x + 240.0, 80.0))
        sample = builder.add("texture_sample", COLOR, (x + 500.0, 80.0))
        builder.connect(uv, "UV", transform, "UV")
        builder.connect(params["Tiling"], "Value", transform, "Tiling")
        builder.connect(params["BaseTexture"], "Value", sample, "Texture")
        builder.connect(transform, "UV", sample, "UV")
        builder.connect(sample, "RGBA", master, "BaseColor")
        direct("Roughness", "Roughness")
    elif style == "pulse":
        time = builder.add("time", SCALAR, (x, 280.0))
        speed = builder.add("multiply", SCALAR, (x + 220.0, 280.0))
        sine = builder.add("sine", SCALAR, (x + 440.0, 280.0))
        normalized = builder.add("multiply", SCALAR, (x + 660.0, 280.0), defaults={"B": 0.5})
        add_half = builder.add("add", SCALAR, (x + 860.0, 280.0), defaults={"B": 0.5})
        emission = builder.add("lerp", COLOR, (x + 860.0, 60.0), defaults={"A": [0.0, 0.0, 0.0, 1.0]})
        builder.connect(time, "Seconds", speed, "A")
        builder.connect(params["PulseSpeed"], "Value", speed, "B")
        builder.connect(speed, "Result", sine, "Value")
        builder.connect(sine, "Result", normalized, "A")
        builder.connect(normalized, "Result", add_half, "A")
        builder.connect(params["EmissionColor"], "Value", emission, "B")
        builder.connect(add_half, "Result", emission, "T")
        direct("BaseTint", "Color")
        builder.connect(emission, "Result", master, "Emission")
    elif style == "cutout":
        uv = builder.add("uv", VECTOR2, (x, 180.0))
        noise = builder.add("simple_noise", SCALAR, (x + 240.0, 180.0))
        mask = builder.add("smooth_step", SCALAR, (x + 500.0, 180.0), defaults={"Edge Max": 0.62})
        builder.connect(uv, "UV", noise, "UV")
        builder.connect(params["NoiseScale"], "Value", noise, "Scale")
        builder.connect(params["Cutoff"], "Value", mask, "Edge Min")
        builder.connect(noise, "Noise", mask, "Value")
        direct("LeafTint", "BaseColor")
        builder.connect(mask, "Result", master, "Opacity")
    elif style == "clearcoat":
        direct("BodyColor", "BaseColor")
        direct("BaseRoughness", "Roughness")
        direct("CoatStrength", "ClearCoat")
        direct("CoatRoughness", "ClearCoatRoughness")
    elif style == "brushed":
        tangent = builder.add("world_normal", VECTOR3, (x + 240.0, 520.0), name="Brushing Direction")
        direct("MetalTint", "BaseColor")
        direct("Roughness", "Roughness")
        direct("Anisotropy", "Anisotropy")
        builder.connect(tangent, "Vector", master, "Tangent")
        master["pins"][1]["default"] = 1.0
    elif style == "glass":
        direct("GlassTint", "BaseColor")
        direct("Frost", "Roughness")
        direct("Opacity", "Opacity")
        direct("Refraction", "Refraction")
        master["pins"][18]["default"] = 0.9
        master["pins"][19]["default"] = 1.45
    elif style == "stone":
        position = builder.add("world_position", VECTOR3, (x, 120.0))
        normal = builder.add("world_normal", VECTOR3, (x, 300.0))
        sample = builder.add("triplanar", COLOR, (x + 320.0, 120.0))
        uv = builder.add("uv", VECTOR2, (x, 520.0))
        noise = builder.add("gradient_noise", SCALAR, (x + 320.0, 520.0), defaults={"Scale": 12.0})
        detail = builder.add("height_to_normal", VECTOR3, (x + 600.0, 500.0), defaults={"Strength": 2.0})
        builder.connect(params["StoneTexture"], "Value", sample, "Texture")
        builder.connect(position, "Vector", sample, "Position")
        builder.connect(normal, "Vector", sample, "Normal")
        builder.connect(params["WorldScale"], "Value", sample, "Scale")
        builder.connect(sample, "RGBA", master, "BaseColor")
        builder.connect(uv, "UV", noise, "UV")
        builder.connect(noise, "Noise", detail, "Height")
        builder.connect(detail, "Normal", master, "Normal")
        direct("Roughness", "Roughness")
    elif style == "dissolve":
        uv = builder.add("uv", VECTOR2, (x, 260.0))
        noise = builder.add("gradient_noise", SCALAR, (x + 240.0, 260.0), defaults={"Scale": 8.0})
        mask = builder.add("smooth_step", SCALAR, (x + 500.0, 260.0), defaults={"Edge Max": 0.62})
        builder.connect(uv, "UV", noise, "UV")
        builder.connect(params["Threshold"], "Value", mask, "Edge Min")
        builder.connect(noise, "Noise", mask, "Value")
        direct("SurfaceTint", "BaseColor")
        direct("EdgeColor", "Emission")
        builder.connect(mask, "Result", master, "Opacity")
    elif style == "hologram":
        uv = builder.add("uv", VECTOR2, (x, 260.0))
        time = builder.add("time", SCALAR, (x, 440.0))
        animated_time = builder.add("multiply", SCALAR, (x + 220.0, 440.0))
        panner = builder.add("panner", VECTOR2, (x + 240.0, 260.0), defaults={"Speed": [0.0, 0.35]})
        gradient = builder.add("linear_gradient", SCALAR, (x + 500.0, 260.0), defaults={"Direction": [0.0, 1.0]})
        hue = builder.add("hue_shift", COLOR, (x + 500.0, 60.0))
        opacity = builder.add("multiply", SCALAR, (x + 760.0, 260.0))
        builder.connect(uv, "UV", panner, "UV")
        builder.connect(time, "Seconds", animated_time, "A")
        builder.connect(params["ScanSpeed"], "Value", animated_time, "B")
        builder.connect(animated_time, "Result", panner, "Time")
        builder.connect(panner, "UV", gradient, "UV")
        builder.connect(params["HologramColor"], "Value", hue, "Color")
        builder.connect(time, "Seconds", hue, "Shift")
        builder.connect(gradient, "Gradient", opacity, "A")
        builder.connect(params["Opacity"], "Value", opacity, "B")
        builder.connect(hue, "Color", master, "BaseColor")
        builder.connect(hue, "Color", master, "Emission")
        builder.connect(opacity, "Result", master, "Opacity")
    elif style == "vertex_wave":
        uv = builder.add("uv", VECTOR2, (x, 300.0))
        wave = builder.add("wave", SCALAR, (x + 240.0, 300.0))
        amplitude = builder.add("multiply", SCALAR, (x + 500.0, 300.0))
        displacement = builder.add("multiply", VECTOR3, (x + 760.0, 300.0),
                                   defaults={"A": [0.0, 1.0, 0.0]})
        builder.connect(uv, "UV", wave, "UV")
        builder.connect(params["WaveFrequency"], "Value", wave, "Frequency")
        builder.connect(wave, "Wave", amplitude, "A")
        builder.connect(params["WaveAmplitude"], "Value", amplitude, "B")
        builder.connect(amplitude, "Result", displacement, "B")
        direct("SurfaceTint", "BaseColor")
        builder.connect(displacement, "Result", master, "WorldPositionOffset")
    elif style == "shield":
        normal = builder.add("world_normal", VECTOR3, (x, 260.0))
        fresnel = builder.add("fresnel", SCALAR, (x + 260.0, 260.0))
        color = builder.add("lerp", COLOR, (x + 540.0, 80.0))
        opacity = builder.add("multiply", SCALAR, (x + 540.0, 360.0))
        builder.connect(normal, "Vector", fresnel, "Normal")
        builder.connect(params["FresnelPower"], "Value", fresnel, "Power")
        builder.connect(params["CoreColor"], "Value", color, "A")
        builder.connect(params["EdgeColor"], "Value", color, "B")
        builder.connect(fresnel, "Fresnel", color, "T")
        builder.connect(fresnel, "Fresnel", opacity, "A")
        builder.connect(params["Opacity"], "Value", opacity, "B")
        builder.connect(color, "Result", master, "BaseColor")
        builder.connect(color, "Result", master, "Emission")
        builder.connect(opacity, "Result", master, "Opacity")
    else:
        raise ValueError(f"Unsupported showcase style: {style}")
    return params


def shader_graph(example: Example) -> tuple[dict[str, Any], dict[str, dict[str, Any]]]:
    builder = GraphBuilder(f"sandbox/shader/{example.number:02d}/{example.slug}", example.output)
    parameters = author_network(builder, example)
    return builder.graph(), parameters


def material_surface_graph(example: Example) -> dict[str, Any]:
    material_properties = tuple(
        Property(prop.symbol, f"Material {prop.display_name}", prop.value_type, prop.material_value,
                 prop.material_value, prop.category, prop.minimum, prop.maximum, prop.step)
        for prop in example.properties
    )
    material_example = Example(example.number, example.level, example.slug, example.display_name, example.output,
                               example.style, material_properties, example.alpha_mode, example.double_sided)
    builder = GraphBuilder(f"sandbox/material/{example.number:02d}/{example.slug}/surface", example.output, True)
    author_network(builder, material_example, "MG_")
    return builder.graph()


def material_graph(example: Example, shader_asset: str, shader_parameters: dict[str, dict[str, Any]]) -> dict[str, Any]:
    scope = f"sandbox/material/{example.number:02d}/{example.slug}"
    output_node = stable_id(f"{scope}/template-output")
    properties = []
    nodes = []
    connections = []
    for index, prop in enumerate(example.properties):
        parameter = shader_parameters[prop.symbol]
        property_pin = stable_id(f"{scope}/template-pin/{prop.symbol}")
        value_node = stable_id(f"{scope}/value-node/{prop.symbol}")
        value_pin = stable_id(f"{scope}/value-pin/{prop.symbol}")
        properties.append({
            "id": parameter["id"],
            "name": prop.symbol,
            "type": prop.value_type,
            "pin": property_pin,
            "value": prop.material_value,
        })
        nodes.append({
            "id": value_node,
            "name": f"Shader Default - {prop.display_name}",
            "position": [80.0, 80.0 + index * 110.0],
            "type": prop.value_type,
            "outputPin": value_pin,
            "value": prop.material_value,
        })
        connections.append({
            "id": stable_id(f"{scope}/value-connection/{prop.symbol}"),
            "output": {"node": value_node, "pin": value_pin},
            "input": {"node": output_node, "pin": property_pin},
        })
    return {
        "schemaVersion": 3,
        "shader": {"kind": "graph", "asset": shader_asset, "target": "default", "keywords": {}},
        "surface": {
            "alphaMode": example.alpha_mode,
            "alphaCutoff": 0.5,
            "doubleSided": example.double_sided,
        },
        "bakedLighting": {"contributeEmission": True, "emissiveIntensity": 1.0},
        "output": {"node": output_node, "position": [620.0, 120.0]},
        "properties": properties,
        "nodes": nodes,
        "connections": connections,
        "surfaceGraph": material_surface_graph(example),
    }


def metadata(asset_id: str, asset_type: str, importer: str, importer_version: int,
             dependencies: list[str] | None = None, subassets: list[str] | None = None) -> dict[str, Any]:
    return {
        "dependencies": dependencies or [],
        "id": asset_id,
        "importer": importer,
        "importerVersion": importer_version,
        "schemaVersion": 1,
        "subAssets": subassets or [],
        "type": asset_type,
    }


def graph_paths(example: Example) -> tuple[Path, Path]:
    level = f"{1 if example.level == 'Foundations' else 2 if example.level == 'Production' else 3:02d}_{example.level}"
    shader = SHOWCASE_ROOT / "ShaderGraphs" / level / f"SG_{example.number:02d}_{example.slug}.keireshadergraph"
    material = SHOWCASE_ROOT / "MaterialGraphs" / level / f"MG_{example.number:02d}_{example.slug}.keirematerialgraph"
    return shader, material


def json_text(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2) + "\n"


def text_metadata(path: Path) -> dict[str, Any]:
    return metadata(stable_id(f"sandbox/text/{path.relative_to(SANDBOX_ROOT).as_posix()}"), TEXT_TYPE, "Keire.Text", 1)


def transform(position: list[float], scale: list[float] | None = None) -> dict[str, Any]:
    return {
        "type": TRANSFORM_TYPE,
        "version": 1,
        "enabled": True,
        "data": {
            "position": position,
            "rotation": [0.0, 0.0, 0.0, 1.0],
            "scale": scale or [1.0, 1.0, 1.0],
        },
    }


def scene_entity(scope: str, name: str, position: list[float], components: list[dict[str, Any]] | None = None,
                 parent: str | None = None, scale: list[float] | None = None) -> dict[str, Any]:
    return {
        "id": stable_id(f"sandbox/scene/{scope}"),
        "name": name,
        "active": True,
        "layer": 0,
        "parent": parent,
        "components": [transform(position, scale), *(components or [])],
    }


def mesh_renderer(material: str, mesh: str) -> dict[str, Any]:
    return {
        "type": MESH_RENDERER_TYPE,
        "version": 3,
        "enabled": True,
        "data": {
            "mesh": mesh,
            "material": material,
            "tint": [1.0, 1.0, 1.0, 1.0],
            "visible": True,
            "castShadows": True,
            "receiveShadows": True,
            "staticLighting": False,
            "giReceive": 0,
            "lightmapScale": 1.0,
            "preserveLightmapUvs": True,
        },
    }


def orbit_component() -> dict[str, Any]:
    return {
        "type": SHOWCASE_ORBIT_TYPE,
        "version": 1,
        "enabled": True,
        "data": {"managedState": '{"fields":[],"version":1}'},
    }


def vfx_component(effect: str, seed: int) -> dict[str, Any]:
    return {
        "type": VFX_EMITTER_TYPE,
        "version": 2,
        "enabled": True,
        "data": {
            "autoDestroy": False,
            "boundsCenter": [0.0, 0.0, 0.0],
            "boundsExtent": [5.0, 5.0, 5.0],
            "culling": 0,
            "editModePreview": True,
            "effect": effect,
            "parameterOverrides": "[]",
            "playOnAwake": True,
            "quality": 2,
            "seedOffset": seed,
            "simulationSpeed": 1.0,
        },
    }


def showcase_scene(material_ids: dict[int, str], plinth_material: str) -> dict[str, Any]:
    entities: list[dict[str, Any]] = []
    material_root = stable_id("sandbox/scene/material-lab")
    vfx_root = stable_id("sandbox/scene/vfx-lab")
    entities.append(scene_entity("material-lab", "Material Lab - 12 Shader and Material Graph Pairs", [0.0, 0.0, 0.0]))
    entities.append(scene_entity("vfx-lab", "VFX Lab - Production Effects", [0.0, 0.0, 0.0]))
    entities.append(scene_entity("camera", "Showcase Camera", [0.0, 7.25, -23.0], [{
        "type": CAMERA_TYPE,
        "version": 1,
        "enabled": True,
        "data": {
            "projection": 0,
            "clearMode": 0,
            "primary": True,
            "priority": 100,
            "fieldOfView": 56.0,
            "orthographicSize": 10.0,
            "nearPlane": 0.05,
            "farPlane": 1000.0,
            "clearColor": [0.008, 0.012, 0.024, 1.0],
        },
    }]))
    entities[-1]["components"][0]["data"]["rotation"] = [0.152123, 0.0, 0.0, 0.988362]
    entities.append(scene_entity("key-light", "Key Directional Light", [0.0, 10.0, -5.0], [{
        "type": DIRECTIONAL_LIGHT_TYPE,
        "version": 2,
        "enabled": True,
        "data": {
            "bakeMode": 0,
            "color": [1.0, 0.94, 0.86, 1.0],
            "contactShadows": True,
            "cookie": None,
            "cookieOffset": [0.0, 0.0],
            "cookieRotation": 0.0,
            "cookieScale": [1.0, 1.0],
            "indirectMultiplier": 1.0,
            "intensity": 4.0,
            "shadowBias": 0.035,
            "shadowResolution": 2,
            "shadowStrength": 1.0,
            "shadows": 2,
            "temperature": 6200.0,
            "useTemperature": False,
        },
    }]))
    entities[-1]["components"][0]["data"]["rotation"] = [0.880651, 0.113152, -0.376104, 0.264946]
    entities.append(scene_entity("floor", "Showcase Plinth", [0.0, -0.8, 2.0], [
        mesh_renderer(plinth_material, BUILTIN_MESHES[1])
    ], scale=[20.0, 0.35, 15.0]))

    x_positions = (-7.5, -2.5, 2.5, 7.5)
    z_positions = (-5.0, 0.0, 5.0)
    level_roots: dict[str, str] = {}
    for level_index, level in enumerate(("Foundations", "Production", "Advanced")):
        level_id = stable_id(f"sandbox/scene/level/{level}")
        level_roots[level] = level_id
        entities.append(scene_entity(f"level/{level}", f"{level_index + 1:02d} - {level}", [0.0, 0.0, 0.0],
                                     parent=material_root))
    for index, example in enumerate(EXAMPLES):
        row = index // 4
        column = index % 4
        entities.append(scene_entity(
            f"material/{example.number:02d}", f"{example.number:02d} - {example.display_name}",
            [x_positions[column], 1.15, z_positions[row]],
            [mesh_renderer(material_ids[example.number], BUILTIN_MESHES[index % len(BUILTIN_MESHES)]), orbit_component()],
            parent=level_roots[example.level], scale=[1.55, 1.55, 1.55]))
    for index, (name, effect) in enumerate(VFX_EXAMPLES):
        entities.append(scene_entity(
            f"vfx/{index}", f"VFX {index + 1:02d} - {name}",
            [x_positions[index], 1.2, 10.0], [vfx_component(effect, 1000 + index * 7919)], parent=vfx_root))
    return {
        "schemaVersion": 5,
        "name": "Kéire Sandbox - Materials, Shaders, Scripts, and VFX",
        "entities": entities,
        "prefabInstances": [],
        "prefabOverrides": [],
        "lighting": {
            "backend": 0,
            "bakeAmbientOcclusion": True,
            "denoise": True,
            "indirectBounceCount": 2,
            "lightmapResolution": 1024,
            "maximumLightmapResolution": 4096,
            "paddingTexels": 4,
            "quality": 1,
            "samplesPerTexel": 64,
            "texelsPerUnit": 32,
        },
        "bakedLighting": None,
    }


def showcase_readme() -> str:
    rows = "\n".join(
        f"| {example.number:02d} | {example.level} | {example.display_name} | {EXAMPLE_FOCUS[example.style]} |"
        for example in EXAMPLES
    )
    return f"""# Kéire Material Lab

This folder intentionally separates reusable custom shaders from authored materials:

- `ShaderGraphs/` contains twelve reusable Shader Graph contracts and their default implementations.
- `MaterialGraphs/` contains twelve Material Graphs that select those shaders, override exposed shader parameters,
  and add material-specific surface expression networks.
- `Assets/Scenes/SandboxShowcase.keirescene` stages every pair and four VFX effects in one production scene.

| # | Level | Example | Focus |
|---:|---|---|---|
{rows}

Start with 01 and progress in order. Shader Graphs define reusable rendering behavior. Material Graphs consume them and
remain the authoring surface for individual materials; they are separate asset types by design.
"""


def root_readme() -> str:
    return """# Kéire Sandbox

The Sandbox is Kéire's production learning project for rendering, Shader Graph, Material Graph, managed scripting,
VFX, physics, UI, audio, animation, and packaging.

## Start here

Open `Assets/Scenes/SandboxShowcase.keirescene`. It is the startup scene and contains:

- twelve paired, schema-3 Shader Graph and Material Graph examples organized as Foundations, Production, and Advanced;
- a managed `ShowcaseOrbit` behaviour attached to every material sculpture;
- four edit-mode VFX examples selected from the full effect library under `Assets/Vfx`;
- a broader gameplay scene at `Assets/Scenes/SampleScene.keirescene` for input, UI, physics, audio, and animation.

The paired assets live under `Assets/Examples/MaterialLab`. Shader Graphs define reusable custom shader behavior;
Material Graphs select those shaders, expose their parameters as inputs, and author material-specific surface logic.
They are deliberately separate workflows.

The audio files `InterfaceConfirm.wav` and `SpatialEmitter.wav` are deterministic synthetic tones generated by
`Scripts/Assets/generate-sample-audio.ps1`. They contain no third-party samples and are distributed under `LICENSE.txt`.
"""


def vfx_guide() -> str:
    return """# Sandbox VFX Examples

The startup showcase scene previews Arcane Nova, Arcane Sigil Orbit, Ember Shard Cyclone, and Forge Sparks. Additional
effects in this folder demonstrate GPU/CPU simulation, mesh particles, event-driven spawning, exposed parameters, and
deterministic seeds. `Assets/Scripts/Runtime/FpsVfxShowcase.cs` demonstrates managed event and Blackboard control.
"""


def showcase_script() -> str:
    return """using Keire;

namespace KeireSandbox;

/// <summary>Animates Material Lab sculptures without coupling the scene to a particular material or shader.</summary>
[StableComponentId("73616e64-626f-4078-8000-000000000060")]
[ExecutionOrder(40)]
public sealed class ShowcaseOrbit : Behaviour
{
    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000061")]
    [Range(-180.0, 180.0), Tooltip("Yaw rotation speed in degrees per second.")]
    private float _rotationSpeed = 18.0f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000062")]
    [Range(0.0, 0.5), Tooltip("Vertical presentation bob in metres.")]
    private float _bobHeight = 0.08f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000063")]
    [Range(0.0, 8.0), Tooltip("Presentation bob frequency.")]
    private float _bobFrequency = 1.25f;

    [HotReloadState]
    private float _elapsed;

    private Vector3 _origin;

    protected override void Awake() => _origin = Entity.Transform.LocalPosition;

    protected override void Update()
    {
        float deltaTime = Time.DeltaTime;
        if (deltaTime <= 0.0f)
            return;

        _elapsed += deltaTime;
        TransformHandle transform = Entity.Transform;
        transform.LocalRotation = Quaternion.Euler(0.0f, _elapsed * _rotationSpeed);
        transform.LocalPosition = _origin + (Vector3.Up * (MathF.Sin(_elapsed * _bobFrequency) * _bobHeight));
    }
}
"""


def expected_files() -> dict[Path, bytes]:
    result: dict[Path, bytes] = {}
    material_ids: dict[int, str] = {}
    plinth_material = stable_id("sandbox/material-asset/showcase-plinth")
    for example in EXAMPLES:
        shader_path, material_path = graph_paths(example)
        shader_asset = stable_id(f"sandbox/shader-asset/{example.number:02d}/{example.slug}")
        material_asset = stable_id(f"sandbox/material-asset/{example.number:02d}/{example.slug}")
        shader, parameters = shader_graph(example)
        material = material_graph(example, shader_asset, parameters)
        shader_subassets = [
            derive_subasset_id(shader_asset, "shader/14650fb0739d0383"),
            derive_subasset_id(shader_asset, "material/default"),
        ]
        material_subassets = [
            derive_subasset_id(material_asset, "material-graph/shader/14650fb0739d0383"),
            derive_subasset_id(material_asset, "material/default"),
        ]
        material_ids[example.number] = material_subassets[1]
        result[shader_path] = json_text(shader).encode("utf-8")
        result[Path(f"{shader_path}.keiremeta")] = json_text(metadata(
            shader_asset, SHADER_GRAPH_TYPE, "Keire.ShaderGraph", 16, subassets=shader_subassets)).encode("utf-8")
        result[material_path] = json_text(material).encode("utf-8")
        result[Path(f"{material_path}.keiremeta")] = json_text(metadata(
            material_asset, MATERIAL_GRAPH_TYPE, "Keire.MaterialGraph", 5,
            dependencies=[shader_asset], subassets=material_subassets)).encode("utf-8")

    material_readme = SHOWCASE_ROOT / "README.md"
    result[material_readme] = showcase_readme().encode("utf-8")
    result[Path(f"{material_readme}.keiremeta")] = json_text(text_metadata(material_readme)).encode("utf-8")
    result[PLINTH_MATERIAL_PATH] = json_text({
        "schemaVersion": 2,
        "shader": DEFAULT_SURFACE_SHADER,
        "surface": {"alphaMode": 0, "alphaCutoff": 0.5, "doubleSided": False},
        "properties": {
            "Tint": [0.025, 0.045, 0.085, 1.0],
            "MetallicFactor": 0.18,
            "RoughnessFactor": 0.72,
            "EmissiveFactor": [0.0, 0.008, 0.02, 1.0],
        },
    }).encode("utf-8")
    result[Path(f"{PLINTH_MATERIAL_PATH}.keiremeta")] = json_text(metadata(
        plinth_material, MATERIAL_TYPE, "Keire.Material", 5,
        dependencies=[DEFAULT_SURFACE_SHADER])).encode("utf-8")
    result[SCENE_PATH] = json_text(showcase_scene(material_ids, plinth_material)).encode("utf-8")
    result[Path(f"{SCENE_PATH}.keiremeta")] = json_text(metadata(
        stable_id("sandbox/scene-asset/showcase"), SCENE_TYPE, "Keire.Scene", 6)).encode("utf-8")
    result[SCRIPT_PATH] = showcase_script().encode("utf-8")
    result[Path(f"{SCRIPT_PATH}.keiremeta")] = json_text(metadata(
        stable_id("sandbox/script/showcase-orbit"), TEXT_TYPE, "Keire.Text", 1)).encode("utf-8")
    result[VFX_GUIDE_PATH] = vfx_guide().encode("utf-8")
    result[Path(f"{VFX_GUIDE_PATH}.keiremeta")] = json_text(text_metadata(VFX_GUIDE_PATH)).encode("utf-8")
    result[SANDBOX_ROOT / "README.md"] = root_readme().encode("utf-8")
    return result


def managed_files() -> set[Path]:
    roots = [SHOWCASE_ROOT]
    files: set[Path] = {SCENE_PATH, Path(f"{SCENE_PATH}.keiremeta"), SCRIPT_PATH,
                        Path(f"{SCRIPT_PATH}.keiremeta"), VFX_GUIDE_PATH, Path(f"{VFX_GUIDE_PATH}.keiremeta"),
                        SANDBOX_ROOT / "README.md"}
    for root in roots:
        if root.is_dir():
            files.update(path for path in root.rglob("*") if path.is_file())
    return files


def check() -> list[str]:
    expected = expected_files()
    errors: list[str] = []
    if LEGACY_GRAPH_ROOT.exists():
        errors.append(f"legacy graph root still exists: {LEGACY_GRAPH_ROOT.relative_to(REPOSITORY_ROOT)}")
    if LEGACY_GENERATED_GRAPH_ROOT.exists():
        errors.append(
            f"legacy generated graph root still exists: {LEGACY_GENERATED_GRAPH_ROOT.relative_to(REPOSITORY_ROOT)}"
        )
    if LEGACY_SCENE_PATH.exists() or Path(f"{LEGACY_SCENE_PATH}.keiremeta").exists():
        errors.append("legacy ShaderMaterialShowcase scene still exists")
    for path, content in expected.items():
        if not path.is_file():
            errors.append(f"missing generated file: {path.relative_to(REPOSITORY_ROOT)}")
        elif path.read_bytes() != content:
            errors.append(f"generated file differs: {path.relative_to(REPOSITORY_ROOT)}")
    unexpected = managed_files() - set(expected)
    for path in sorted(unexpected):
        errors.append(f"unexpected generated file: {path.relative_to(REPOSITORY_ROOT)}")
    return errors


def safe_remove_tree(path: Path) -> None:
    resolved = path.resolve()
    expected = {LEGACY_GRAPH_ROOT.resolve(), LEGACY_GENERATED_GRAPH_ROOT.resolve(), SHOWCASE_ROOT.resolve()}
    if resolved not in expected or SANDBOX_ROOT.resolve() not in resolved.parents:
        raise RuntimeError(f"Refusing to remove unmanaged Sandbox path: {resolved}")
    if path.exists():
        shutil.rmtree(path)


def generate() -> None:
    safe_remove_tree(LEGACY_GRAPH_ROOT)
    safe_remove_tree(LEGACY_GENERATED_GRAPH_ROOT)
    safe_remove_tree(SHOWCASE_ROOT)
    for legacy in (LEGACY_SCENE_PATH, Path(f"{LEGACY_SCENE_PATH}.keiremeta")):
        if legacy.exists():
            legacy.unlink()
    for path, content in expected_files().items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
    errors = check()
    if errors:
        raise RuntimeError("Sandbox showcase generation did not converge: " + "; ".join(errors))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Validate generated content without changing files.")
    arguments = parser.parse_args()
    if arguments.check:
        errors = check()
        if errors:
            print("Sandbox showcase drift detected:", file=sys.stderr)
            for error in errors:
                print(f"  - {error}", file=sys.stderr)
            return 1
        print(f"Sandbox showcase is current ({len(EXAMPLES)} Shader Graph and Material Graph pairs).")
        return 0
    generate()
    print(f"Generated Sandbox showcase ({len(EXAMPLES)} Shader Graph and Material Graph pairs).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

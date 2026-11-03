"""Audit generated SPIR-V uniform layouts against SDL Vulkan's binding range.

This checks declared compiled layouts, not whether every member is read on a
particular execution path. Oversized declarations require a binding audit.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path


def audit_module(data: bytes, limit: int) -> list[dict]:
    if len(data) % 4 or len(data) < 20:
        raise ValueError("Invalid SPIR-V module size")
    words = struct.unpack(f"<{len(data) // 4}I", data)
    if words[0] != 0x07230203:
        raise ValueError("Invalid SPIR-V magic")
    names = {}
    types = {}
    constants = {}
    decorations = {}
    member_decorations = {}
    variables = []
    index = 5
    while index < len(words):
        count = words[index] >> 16
        opcode = words[index] & 0xFFFF
        if count == 0 or index + count > len(words):
            raise ValueError("Malformed SPIR-V instruction")
        operands = words[index + 1:index + count]
        if opcode == 5:  # OpName
            raw = struct.pack(f"<{len(operands) - 1}I", *operands[1:])
            names[operands[0]] = raw.split(b"\0", 1)[0].decode("utf-8")
        elif 19 <= opcode <= 33:
            types[operands[0]] = (opcode, operands[1:])
        elif opcode == 43:  # OpConstant (array lengths here are 32-bit integers)
            constants[operands[1]] = operands[2]
        elif opcode == 59:
            variables.append(operands[:3])
        elif opcode == 71:
            decorations[(operands[0], operands[1])] = operands[2:]
        elif opcode == 72:
            member_decorations[(operands[0], operands[1], operands[2])] = operands[3:]
        index += count

    def extent(type_id: int, matrix_stride: int | None = None, row_major: bool = False) -> int:
        opcode, operands = types[type_id]
        if opcode in (21, 22):  # integer / float width
            return operands[0] // 8
        if opcode == 23:  # vector
            return extent(operands[0]) * operands[1]
        if opcode == 24:  # matrix, stored with explicit member stride
            if matrix_stride is None:
                raise ValueError("Matrix lacks a declared stride")
            _, vector = types[operands[0]]
            rows, columns = vector[1], operands[1]
            return matrix_stride * (rows if row_major else columns)
        if opcode == 28:  # array
            stride = decorations[(type_id, 6)][0]
            return stride * constants[operands[1]]
        if opcode == 30:  # struct
            maximum = 0
            for member, child in enumerate(operands):
                offset = member_decorations[(type_id, member, 35)][0]
                stride = member_decorations.get((type_id, member, 7), (None,))[0]
                row = (type_id, member, 4) in member_decorations
                maximum = max(maximum, offset + extent(child, stride, row))
            return maximum
        raise ValueError(f"Unsupported uniform type opcode {opcode}")

    findings = []
    for pointer, variable, storage in variables:
        if storage != 2:  # Uniform; storage buffers are deliberately excluded.
            continue
        _, pointer_operands = types[pointer]
        target = pointer_operands[1]
        if (target, 2) not in decorations:  # Block, not legacy BufferBlock
            continue
        size = extent(target)
        findings.append({
            "name": names.get(variable, str(variable)),
            "set": decorations[(variable, 34)][0],
            "binding": decorations[(variable, 33)][0],
            "declaredBytes": size,
            "exceedsLimit": size > limit,
        })
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("header", type=Path)
    parser.add_argument("--limit", type=int, default=4096)
    parser.add_argument("--prefix", default="BuiltinCloud")
    args = parser.parse_args()
    if args.limit <= 0:
        parser.error("--limit must be positive")
    source = args.header.read_text(encoding="utf-8-sig")
    modules = re.findall(r"unsigned char (\w*SpirV)\[\]\s*=\s*([^;]+);", source)
    result = []
    for name, encoded in modules:
        if not name.startswith(args.prefix):
            continue
        data = bytes(int(value, 16) for value in re.findall(r"\\x([0-9a-fA-F]{2})", encoded))
        result.append({"shader": name, "uniforms": audit_module(data, args.limit)})
    if not result:
        parser.error("No matching SPIR-V modules found")
    print(json.dumps({"limitBytes": args.limit, "modules": result}, indent=2))
    return int(any(binding["exceedsLimit"] for module in result for binding in module["uniforms"]))


if __name__ == "__main__":
    raise SystemExit(main())

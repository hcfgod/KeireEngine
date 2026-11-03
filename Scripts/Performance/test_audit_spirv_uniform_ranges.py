import struct
import unittest

from audit_spirv_uniform_ranges import audit_module


def instruction(opcode, *operands):
    return [((len(operands) + 1) << 16) | opcode, *operands]


def uniform_fixture(array_length):
    words = [0x07230203, 0x00010000, 0, 20, 0]
    instructions = [
        instruction(22, 1, 32),  # float
        instruction(23, 2, 1, 4),  # float4
        instruction(21, 3, 32, 0),
        instruction(43, 3, 4, array_length),
        instruction(28, 5, 2, 4),
        instruction(71, 5, 6, 16),  # ArrayStride
        instruction(30, 6, 5),
        instruction(72, 6, 0, 35, 0),  # member Offset
        instruction(71, 6, 2),  # Block
        instruction(32, 7, 2, 6),
        instruction(59, 7, 8, 2),
        instruction(71, 8, 34, 3),
        instruction(71, 8, 33, 0),
    ]
    for item in instructions:
        words.extend(item)
    return struct.pack(f"<{len(words)}I", *words)


class UniformRangeTests(unittest.TestCase):
    def test_exact_limit_is_accepted(self):
        finding = audit_module(uniform_fixture(256), 4096)[0]
        self.assertEqual(finding["declaredBytes"], 4096)
        self.assertFalse(finding["exceedsLimit"])

    def test_array_past_binding_range_is_reported(self):
        finding = audit_module(uniform_fixture(257), 4096)[0]
        self.assertEqual(finding["declaredBytes"], 4112)
        self.assertTrue(finding["exceedsLimit"])
        self.assertEqual((finding["set"], finding["binding"]), (3, 0))

    def test_truncated_instruction_is_rejected(self):
        with self.assertRaises(ValueError):
            audit_module(uniform_fixture(256)[:-4], 4096)

    def test_invalid_header_is_rejected(self):
        with self.assertRaises(ValueError):
            audit_module(bytes(20), 4096)


if __name__ == "__main__":
    unittest.main()

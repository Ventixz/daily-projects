import unittest
from asciiart import parse_pnm, resize, to_ascii, RAMP


def pgm(w, h, pixels, comment=b""):
    return b"P5\n" + comment + b"%d %d\n255\n" % (w, h) + bytes(pixels)


class Tests(unittest.TestCase):
    def test_parse_pgm(self):
        w, h, rows = parse_pnm(pgm(2, 2, [0, 10, 20, 255]))
        self.assertEqual((w, h), (2, 2))
        self.assertEqual(rows, [[0, 10], [20, 255]])

    def test_header_comment(self):
        _, _, rows = parse_pnm(pgm(1, 1, [7], comment=b"# made by test\n"))
        self.assertEqual(rows, [[7]])

    def test_ppm_luma(self):
        data = b"P6\n1 1\n255\n" + bytes([255, 0, 0])
        _, _, rows = parse_pnm(data)
        self.assertAlmostEqual(rows[0][0], 76.245)

    def test_maxval_scaling(self):
        data = b"P5\n1 1\n15\n" + bytes([15])
        self.assertAlmostEqual(parse_pnm(data)[2][0][0], 255)

    def test_rejects_bad_input(self):
        with self.assertRaises(ValueError):
            parse_pnm(b"P1\n1 1\n0")
        with self.assertRaises(ValueError):
            parse_pnm(pgm(2, 2, [1, 2, 3]))

    def test_resize_average(self):
        self.assertEqual(resize([[0, 100], [100, 200]], 1, 1), [[100]])

    def test_extremes_map_to_ramp_ends(self):
        out = to_ascii([[0, 255]], width=2)
        self.assertEqual(out, RAMP[0] + RAMP[-1])

    def test_invert(self):
        out = to_ascii([[0, 255]], width=2, invert=True)
        self.assertEqual(out, RAMP[-1] + RAMP[0])

    def test_aspect_correction(self):
        rows = [[0] * 8 for _ in range(8)]
        self.assertEqual(len(to_ascii(rows, width=8).splitlines()), 4)

    def test_gradient_monotonic(self):
        rows = [[x * 255 // 31 for x in range(32)]]
        line = to_ascii(rows, width=32)
        idx = [RAMP.index(c) for c in line]
        self.assertEqual(idx, sorted(idx))


if __name__ == "__main__":
    unittest.main()

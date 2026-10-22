import os
import tempfile
import unittest

import mosaic as m


class MosaicTests(unittest.TestCase):
    def test_ppm_roundtrip(self):
        img = m.demo_target(5, 3)
        back = m.parse_ppm(m.write_ppm(img))
        self.assertEqual((back.width, back.height, back.pixels), (5, 3, img.pixels))

    def test_ppm_comment_and_errors(self):
        img = m.parse_ppm(b"P6\n# hi\n1 1\n255\n\x01\x02\x03")
        self.assertEqual(img.get(0, 0), (1, 2, 3))
        with self.assertRaises(ValueError):
            m.parse_ppm(b"P3\n1 1\n255\n")
        with self.assertRaises(ValueError):
            m.parse_ppm(b"P6\n2 2\n255\n\x00")

    def test_maxval_scaling(self):
        img = m.parse_ppm(b"P6\n1 1\n15\n\x0f\x00\x07")
        self.assertEqual(img.get(0, 0), (255, 0, 119))

    def test_resize_average(self):
        img = m.Image(2, 1, bytes([0, 0, 0, 100, 200, 50]))
        self.assertEqual(m.resize(img, 1, 1).get(0, 0), (50, 100, 25))

    def test_average_region(self):
        img = m.Image(2, 1, bytes([0, 0, 0, 10, 20, 30]))
        self.assertEqual(m.average_colour(img, 1, 0, 2, 1), (10, 20, 30))

    def test_best_match_and_reuse_limit(self):
        avgs = [(0, 0, 0), (250, 250, 250)]
        self.assertEqual(m.best_match((240, 240, 240), avgs), 1)
        self.assertEqual(m.best_match((240, 240, 240), avgs, {1: 1}, 1), 0)
        with self.assertRaises(ValueError):
            m.best_match((0, 0, 0), avgs, {0: 1, 1: 1}, 1)

    def test_mosaic_picks_matching_tiles(self):
        target = m.Image(4, 2, bytes([255, 0, 0] * 2 + [0, 0, 255] * 2) * 2)
        lib = [m.solid(3, 3, (250, 5, 5)), m.solid(3, 3, (5, 5, 250))]
        out = m.build_mosaic(target, lib, 2)
        self.assertEqual((out.width, out.height), (4, 2))
        self.assertEqual(out.get(0, 0), (250, 5, 5))
        self.assertEqual(out.get(3, 1), (5, 5, 250))

    def test_crops_to_whole_tiles(self):
        out = m.build_mosaic(m.solid(7, 5, (9, 9, 9)), [m.solid(2, 2, (9, 9, 9))], 3)
        self.assertEqual((out.width, out.height), (6, 3))

    def test_bad_tile(self):
        with self.assertRaises(ValueError):
            m.build_mosaic(m.solid(2, 2, (0, 0, 0)), [m.solid(1, 1, (0, 0, 0))], 5)

    def test_reuse_limit_spreads_tiles(self):
        target = m.solid(4, 4, (10, 10, 10))
        lib = [m.solid(2, 2, (10, 10, 10)), m.solid(2, 2, (60, 60, 60))]
        out = m.build_mosaic(target, lib, 2, max_reuse=2)
        seen = {out.get(x, y) for x in (0, 2) for y in (0, 2)}
        self.assertEqual(seen, {(10, 10, 10), (60, 60, 60)})

    def test_cli_default_demo(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "o.ppm")
            self.assertEqual(m.main(["-o", p]), 0)
            self.assertEqual(m.parse_ppm(open(p, "rb").read()).width, 96)
            self.assertEqual(m.main(["/nonexistent.ppm", "-o", p]), 1)


if __name__ == "__main__":
    unittest.main()

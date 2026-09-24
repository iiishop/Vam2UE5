import tempfile
import unittest
from pathlib import Path
from vam_graphics_evidence import summarize


class GraphicsEvidenceTests(unittest.TestCase):
    def test_measures_actual_frames_and_preserves_startup_and_hitch(self):
        quality = dict(schema='vam-graphics-quality/1', capture_frames=8,
                       startup_frames_excluded_from_cap_check=2, median_fps_relative_tolerance=.1)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'profile.csv'
            path.write_text('FrameTime,GPUTime\n2000,8\n80,8\n16.6667,8\n16.6667,8\n120,8\n16.6667,8\n16.6667,8\n16.6667,8\n[Metadata],value\n')
            result = summarize(path, 60, quality)
            self.assertTrue(result['frame_pacing_passed'])
            self.assertEqual(result['maximum_frame_ms_after_startup'], 120)
            self.assertEqual(result['maximum_frame_ms_including_startup'], 2000)
            self.assertEqual(len(result['all_frame_ms']), 8)
            self.assertFalse(summarize(path, 120, quality)['frame_pacing_passed'])
            path.write_text('FrameTime,GPUTime\n16.6,8\n')
            with self.assertRaisesRegex(ValueError, 'Incomplete'):
                summarize(path, 60, quality)


if __name__ == '__main__':
    unittest.main()

import unittest
from prepare_hair_render_reference import source_half_width_to_groom_diameter_cm


class HairWidthUnits(unittest.TestCase):
    def test_source_ribbon_full_span_matches_ue_diameter(self):
        for radius_m in (.0001,.00015,.0008):
            diameter_cm=source_half_width_to_groom_diameter_cm(radius_m)
            self.assertAlmostEqual(diameter_cm*.5, radius_m*100)

    def test_invalid_width_is_rejected(self):
        for value in (0,-1,float('nan'),float('inf')):
            with self.assertRaises(ValueError):
                source_half_width_to_groom_diameter_cm(value)


if __name__=='__main__':unittest.main()

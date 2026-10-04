"""Raster payloads are consumed instead of silently producing black screenshots."""
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import preview
class GraphicsPreviewTests(unittest.TestCase):
    def frame(self):
        return {'frame':[dict(kind=9,x=0,y=0,w=0,h=0,rgb=0)]}
    def test_matching_runner_pixels_are_visible(self):
        frame=self.frame();frame['raster_pixels_hex']=(b'\x00\xf8'*(466*466)).hex()
        image=preview.raster(frame)
        self.assertEqual(image.getpixel((233,233)),(255,0,0,255))
    def test_old_runner_and_truncated_payload_are_rejected(self):
        with self.assertRaisesRegex(ValueError,'matching SDK'):preview.raster(self.frame())
        frame=self.frame();frame['raster_pixels_hex']='0000'
        with self.assertRaisesRegex(ValueError,'466x466'):preview.raster(frame)
if __name__=='__main__':unittest.main()

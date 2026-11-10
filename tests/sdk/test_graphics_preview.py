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
    def test_rgb565_commands_have_separate_snapshots(self):
        def image(x,offset):
            return dict(kind=7,x=x,y=233,w=1,h=1,rgb=0,pixel_offset=offset)
        frame={'pixels_hex':'00f81f00','frame':[image(232,0),image(233,2)]}
        result=preview.raster(frame)
        self.assertEqual(result.getpixel((232,233))[:3],(255,0,0))
        self.assertEqual(result.getpixel((233,233))[:3],(0,0,255))
        frame['frame'][1]['pixel_offset']=3
        with self.assertRaisesRegex(ValueError,'payload'):preview.raster(frame)

    def test_old_runner_and_truncated_payload_are_rejected(self):
        with self.assertRaisesRegex(ValueError,'matching SDK'):preview.raster(self.frame())
        frame=self.frame();frame['raster_pixels_hex']='0000'
        with self.assertRaisesRegex(ValueError,'466x466'):preview.raster(frame)
if __name__=='__main__':unittest.main()

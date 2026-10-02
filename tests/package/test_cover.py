import io
import struct
import sys
import unittest
import json
import tempfile
from pathlib import Path
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
import package
from test_package_sections import WASM,KEY,KW,signed,meta

class Cover(unittest.TestCase):
    def png(self,size=(210,210)):
        out=io.BytesIO();Image.new('RGBA',size,(255,0,0,255)).save(out,format='PNG');return out.getvalue()
    def test_png_and_both_backends_signed_cover(self):
        cover=package.encode_cover_png(self.png())
        self.assertEqual(len(cover),133232)
        self.assertEqual(cover[32:34],b'\0\xf8')
        for options in ({},{'native':b'\0aot\5\0\0\0native','native_metadata':meta(WASM)},
                        {'native':b'\0aot\5\0\0\0native','native_metadata':meta(WASM),'include_wasm':False}):
            raw=package._assemble(WASM,b'resources',cover=cover,**options,**KW)
            checked=package.validate_envelope(raw,KEY.public_key(),42)
            self.assertEqual(checked['cover'],package.decode_cover(cover))
            tampered=bytearray(raw);tampered[-1]^=1
            with self.assertRaises(ValueError):package.validate_envelope(tampered,KEY.public_key(),42)
    def test_signed_bad_cover_bounds_duplicate_and_unknown_codec(self):
        cover=package.encode_cover_png(self.png())
        good=package._assemble(WASM,b'',cover=cover,**KW)
        off=struct.unpack_from('<I',good,280)[0]
        for position,fmt,value in ((8,'H',2),(10,'H',2),(12,'I',31),(16,'H',65535),(24,'I',0xffffffff)):
            raw=bytearray(good);struct.pack_into('<'+fmt,raw,off+position,value)
            with self.subTest(position=position),self.assertRaises(ValueError):
                package.validate_envelope(signed(raw),KEY.public_key(),42)
        for position,value in ((272,1),(272,5),(280,0xffffffff),(284,0xffffffff),(284,133231)):
            raw=bytearray(good);struct.pack_into('<I',raw,position,value)
            with self.assertRaises(ValueError):package.validate_envelope(signed(raw),KEY.public_key(),42)
    def test_png_dimensions_size_and_animation_bounded(self):
        for raw in (self.png((211,210)),self.png()+bytes(65536),b'not png'):
            with self.assertRaises(ValueError):package.encode_cover_png(raw)
        bomb=bytearray(self.png());struct.pack_into('>II',bomb,16,0x7fffffff,0x7fffffff)
        with self.assertRaises(ValueError):package.encode_cover_png(bomb)
        out=io.BytesIO();a=Image.new('RGB',(210,210));b=Image.new('RGB',(210,210),'red')
        a.save(out,format='PNG',save_all=True,append_images=[b])
        with self.assertRaises(ValueError):package.encode_cover_png(out.getvalue())

    def test_manifest_cover_path_stays_inside_app(self):
        import tinyrt
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);app=root/'app';app.mkdir()
            (app/'main.c').write_text('/* fixture */',encoding='utf-8')
            (root/'outside.png').write_bytes(self.png())
            manifest=dict(app_id='demo.cover',title='Cover',version=1,abi_version=1,
                          permissions=1,memory_pages=2,budget=1000,sources=['main.c'],cover='../outside.png')
            path=app/'app.json';path.write_text(json.dumps(manifest),encoding='utf-8')
            with self.assertRaises(ValueError):tinyrt.load_manifest(path)
            manifest['cover']='cover.png';(app/'cover.png').write_bytes(self.png())
            path.write_text(json.dumps(manifest),encoding='utf-8')
            self.assertEqual(tinyrt.load_manifest(path)['cover'],'cover.png')
            (app/'cover.png').unlink()
            try:(app/'cover.png').symlink_to(root/'outside.png')
            except OSError:return
            with self.assertRaises(ValueError):tinyrt.load_manifest(path)

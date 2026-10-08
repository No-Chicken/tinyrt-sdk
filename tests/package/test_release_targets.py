"""Release manifests accept supported, correctly paired AOT architectures."""
import json
from pathlib import Path
import unittest
from jsonschema import Draft202012Validator

class ReleaseTargetTests(unittest.TestCase):
    def test_supported_target_pairs(self):
        root=Path(__file__).resolve().parents[2]
        schema=json.loads((root/'specs/release-v2.schema.json').read_text(encoding='utf8'))
        nodes=[schema['properties']['requirements']['properties']['aot'],
               schema['allOf'][0]['else']['properties']['requirements']['properties']['aot']]
        for node in nodes:
            validator=Draft202012Validator(node)
            for arch,cpu,valid in [('xtensa','esp32s3',True),('riscv32','generic-rv32',True),
                                   ('xtensa','generic-rv32',False),('riscv32','esp32s3',False),
                                   ('arm','generic-rv32',False)]:
                with self.subTest(arch=arch,cpu=cpu):
                    value=dict(target_arch=arch,target_cpu=cpu,compat_id='a'*64)
                    self.assertEqual(validator.is_valid(value),valid)

if __name__=='__main__':unittest.main()

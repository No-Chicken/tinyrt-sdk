import subprocess
import sys
import unittest
from pathlib import Path


class ApiReferenceTests(unittest.TestCase):
    def test_reference_matches_contract_and_guest_header(self):
        root = Path(__file__).resolve().parents[2]
        subprocess.run([sys.executable, root / "scripts/generate_api.py", "--check"], check=True)

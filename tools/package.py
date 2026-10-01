#!/usr/bin/env python3
"""TinyRT v1 packer. Requires cryptography >= 43.

The fixed development scalar 1 is PUBLIC and INSECURE. It is available only via
--development-key / development-public-key for tests and demos. Device trust
configuration must explicitly opt in. This tool never reads firmware keys by
default and never generates or replaces production keys.
"""
import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

HEADER_SIZE = 256
MAX_PACKAGE_SIZE = 0x4A000
P256_ORDER = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
DOMAIN = b"TinyRT-package-v1\0"

def development_key():
    """Public, test-only private key. Never trust in production."""
    return ec.derive_private_key(1, ec.SECP256R1())

def uint32(value):
    result = int(value, 0)
    if not 0 <= result <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("expected uint32")
    return result

def build_package(wasm, assets, *, app_id, title, version, abi_version,
                  permissions, memory_pages, budget, key_id, private_key):
    """Sign validated metadata and payload; runtime still validates Wasm/ABI."""
    if re.fullmatch(r"[a-z0-9._-]{1,31}", app_id) is None:
        raise ValueError("app_id must be 1..31 ASCII [a-z0-9._-] bytes")
    encoded_title = title.encode("utf-8", errors="strict")
    if not 1 <= len(encoded_title) <= 63 or b"\0" in encoded_title:
        raise ValueError("title must be 1..63 UTF-8 bytes without NUL")
    if not 1 <= version <= 0xFFFFFFFF or abi_version != 1:
        raise ValueError("version must be positive uint32 and ABI must be 1")
    if not 0 <= permissions <= 15:
        raise ValueError("unknown permission bits")
    if not 1 <= memory_pages <= 16 or not 1 <= budget <= 100000:
        raise ValueError("memory pages or instruction budget exceed v1 policy")
    if not 0 <= key_id <= 0xFFFFFFFF:
        raise ValueError("key ID must fit uint32")
    if not isinstance(private_key, ec.EllipticCurvePrivateKey) or not isinstance(private_key.curve, ec.SECP256R1):
        raise ValueError("signing key must be an ECDSA P-256 private key")
    if len(wasm) <= 8 or not wasm.startswith(b"\0asm\x01\0\0\0"):
        raise ValueError("input must begin with Wasm v1 magic and contain sections")
    total = HEADER_SIZE + len(wasm) + len(assets)
    if total > MAX_PACKAGE_SIZE:
        raise ValueError("complete package exceeds 296 KiB")
    payload = wasm + assets
    header = bytearray(HEADER_SIZE)
    header[:8] = b"TRPKG001"
    struct.pack_into("<HH11I", header, 8, 1, HEADER_SIZE, total, HEADER_SIZE,
                     len(wasm), HEADER_SIZE + len(wasm), len(assets), version,
                     abi_version, permissions, memory_pages, budget, key_id)
    header[56:56 + len(app_id)] = app_id.encode("ascii")
    header[88:88 + len(encoded_title)] = encoded_title
    header[152:184] = hashlib.sha256(payload).digest()
    # Library RFC6979 support makes identical inputs produce identical identities.
    signature = private_key.sign(DOMAIN + header[:192],
        ec.ECDSA(hashes.SHA256(), deterministic_signing=True))
    r, s = utils.decode_dss_signature(signature)
    header[192:256] = r.to_bytes(32, "big") + min(s, P256_ORDER - s).to_bytes(32, "big")
    return bytes(header) + payload

def read_bounded(path):
    with Path(path).open("rb") as source:
        data = source.read(MAX_PACKAGE_SIZE + 1)
    if len(data) > MAX_PACKAGE_SIZE:
        raise ValueError("input exceeds maximum package size")
    return data

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    pack = commands.add_parser("pack", help="build a signed TinyRT v1 package")
    pack.add_argument("--wasm", required=True, type=Path)
    pack.add_argument("--assets", type=Path)
    pack.add_argument("--output", required=True, type=Path)
    pack.add_argument("--app-id", required=True)
    pack.add_argument("--title", required=True)
    pack.add_argument("--version", required=True, type=uint32)
    pack.add_argument("--abi-version", type=uint32, default=1)
    pack.add_argument("--permissions", type=uint32, default=15)
    pack.add_argument("--memory-pages", type=uint32, default=4)
    pack.add_argument("--budget", type=uint32, default=10000)
    pack.add_argument("--key-id", required=True, type=uint32)
    keys = pack.add_mutually_exclusive_group(required=True)
    keys.add_argument("--key", type=Path, help="existing unencrypted P-256 PEM private key")
    keys.add_argument("--development-key", action="store_true", help="PUBLIC TEST KEY ONLY; scalar 1")
    public = commands.add_parser("development-public-key", help="export PUBLIC TEST key, SEC1 65 bytes")
    public.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "development-public-key":
            key = development_key()
            data = key.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
            print("WARNING: public development key for TESTS ONLY; never trust in production.", file=sys.stderr)
            args.output.write_bytes(data)
            return 0
        for source in (args.wasm, args.assets, args.key):
            if source is not None and (args.output.resolve() == source.resolve() or
                    (args.output.exists() and args.output.samefile(source))):
                raise ValueError("output must not overwrite an input or signing key")
        if args.development_key:
            key = development_key()
            print("WARNING: signing with public development key for TESTS ONLY.", file=sys.stderr)
        else:
            key = serialization.load_pem_private_key(args.key.read_bytes(), password=None)
        data = build_package(read_bounded(args.wasm), read_bounded(args.assets) if args.assets else b"",
            app_id=args.app_id, title=args.title, version=args.version, abi_version=args.abi_version,
            permissions=args.permissions, memory_pages=args.memory_pages, budget=args.budget,
            key_id=args.key_id, private_key=key)
        args.output.write_bytes(data)
        print(json.dumps({"app_id": args.app_id, "version": args.version,
            "package_size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "key_id": args.key_id, "development_key": args.development_key}, sort_keys=True))
        return 0
    except (OSError, ValueError, TypeError, UnicodeError) as error:
        parser.exit(2, f"package: {error}\n")

if __name__ == "__main__":
    raise SystemExit(main())

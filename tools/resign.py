"""Operator-only signing of reviewed bytes; never converts unreviewed AOT to trust.

Fresh uploaded AOT must use release_compile.py. A legacy AOT migration requires an
operator-owned approval binding the original whole-package hash and every AOT
metadata field. The uploader must never select this approval, key or profile.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from cryptography.hazmat.primitives import hashes,serialization
from cryptography.hazmat.primitives.asymmetric import ec,utils
import package
import release_validation as rv

def resign_package(raw,source_public_key,source_key_id,private_key,key_id,*,approval=None):
    info,parts,_,_=rv.inspect_package(raw,source_public_key,source_key_id)
    if not isinstance(private_key,ec.EllipticCurvePrivateKey) or not isinstance(private_key.curve,ec.SECP256R1):
        raise ValueError('release signer must be P-256')
    if private_key.public_key().public_numbers()==package.development_key().public_key().public_numbers():
        raise ValueError('public development key cannot be a release authority')
    if type(key_id) is not int or not 0<=key_id<=0xffffffff:raise ValueError('invalid release key ID')
    original_sha=hashlib.sha256(raw).hexdigest()
    if info['aot']:
        if not isinstance(approval,dict) or set(approval)!={'schema','package_sha256','aot','review_id','method'}:
            raise ValueError('AOT requires an operator-owned legacy migration approval')
        if (approval['schema']!=1 or approval['method']!='reviewed-legacy-migration'
            or approval['package_sha256']!=original_sha or approval['aot']!=info['aot']
            or not isinstance(approval['review_id'],str) or not approval['review_id'].strip()):
            raise ValueError('AOT approval does not match the reviewed package and provenance')
    value=bytearray(raw);struct.pack_into('<I',value,52,key_id)
    signature=private_key.sign(package.DOMAIN+value[:192],ec.ECDSA(hashes.SHA256(),deterministic_signing=True))
    r,s=utils.decode_dss_signature(signature);value[192:256]=r.to_bytes(32,'big')+min(s,package.P256_ORDER-s).to_bytes(32,'big')
    result=bytes(value);after,newparts,_,_=rv.inspect_package(result,private_key.public_key(),key_id)
    if parts!=newparts or raw[:52]!=result[:52] or raw[56:192]!=result[56:192] or raw[256:]!=result[256:]:
        raise ValueError('resigning changed reviewed application bytes')
    if (info['app_id'],info['version'],info['aot'])!=(after['app_id'],after['version'],after['aot']):
        raise ValueError('resigning changed application identity or AOT provenance')
    audit=dict(schema=1,source_sha256=original_sha,package_sha256=hashlib.sha256(result).hexdigest(),
               app_id=info['app_id'],version=info['version'],source_key_id=source_key_id,key_id=key_id,
               sections_unchanged=True,aot_approval=approval,validation='authenticated envelope and metadata; not native execution')
    return result,audit

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('input','source-public-key','key','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--source-key-id',type=int,required=True);p.add_argument('--key-id',type=int,required=True)
    p.add_argument('--approval',type=Path);args=p.parse_args(argv)
    if args.output.exists() or args.output.resolve() in {args.input.resolve(),args.key.resolve(),args.source_public_key.resolve()}:
        p.error('output must be a new file, separate from all inputs')
    try:
        public=ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(),args.source_public_key.read_bytes())
        private=serialization.load_pem_private_key(args.key.read_bytes(),None)
        approval=json.loads(args.approval.read_bytes()) if args.approval else None
        result,audit=resign_package(args.input.read_bytes(),public,args.source_key_id,private,args.key_id,approval=approval)
        with args.output.open('xb') as stream:stream.write(result)
        print(json.dumps(audit,sort_keys=True));return 0
    except (ValueError,OSError,TypeError,KeyError):p.exit(2,'resign: input, approval or signing validation failed\n')

if __name__=='__main__':raise SystemExit(main())

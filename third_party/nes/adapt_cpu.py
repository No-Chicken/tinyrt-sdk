"""Split upstream's 256-opcode dispatch to fit TinyRT's 8 KiB interpreter stack.

All 256 case bodies are copied verbatim. Upstream remains unchanged. The generated
file retains Apache-2.0 notices and adds an explicit modification notice.
"""
from pathlib import Path
import hashlib, json, re

def verify_source():
    base=Path(__file__).parent
    manifest=json.loads((base/'source.json').read_text(encoding='utf-8'))
    for entry in manifest['files']:
        data=(base/'upstream'/entry['path']).read_bytes()
        if hashlib.sha256(data).hexdigest()!=entry['sha256']:
            raise ValueError('pinned NES source hash mismatch: '+entry['path'])
    return manifest

def generate(out):
    verify_source()
    source=Path(__file__).parent/'upstream/src/nes_cpu.c'
    text=source.read_text(encoding='utf-8')
    begin=text.index('        switch (nes->nes_cpu.opcode){')
    brace=text.index('{',begin);end=brace+1;depth=1
    while depth:
        if text[end]=='{':depth+=1
        elif text[end]=='}':depth-=1
        end+=1
    body=text[brace+1:end-1]
    entries=re.findall(r'^[ \t]*case 0x([0-9A-F]{2}):([^\n]*)$',body,re.MULTILINE)
    assert len(entries)==256 and [int(n,16) for n,_ in entries]==list(range(256))
    prefix='/* Modified for TinyRT N1 (2026): dispatch is split into 16 non-inlined\n * groups. Opcode semantics and cycle accounting are copied unchanged from the\n * pinned Apache-2.0 upstream. See ../source.json and ../README.md. */\n#if defined(_MSC_VER)\n#define N1_NOINLINE __declspec(noinline)\n#else\n#define N1_NOINLINE __attribute__((noinline))\n#endif\n'
    functions=[]
    for group in range(16):
        cases='\n'.join('case 0x'+n+':'+code for n,code in entries[group*16:group*16+16])
        functions.append(f'static N1_NOINLINE void n1_dispatch_{group}(nes_t *nes) {{\nswitch(nes->nes_cpu.opcode) {{\n{cases}\n}}\n}}\n')
    replacement='        switch(nes->nes_cpu.opcode >> 4) {\n'+''.join(f'        case {g}: n1_dispatch_{g}(nes); break;\n' for g in range(16))+'        }'
    text=text[:begin]+replacement+text[end:]
    pos=text.index('void nes_opcode(nes_t* nes,uint16_t ticks){')
    text=text[:pos]+prefix+'\n'.join(functions)+text[pos:]
    target=out/'nes_cpu_bounded.c';target.write_text(text,encoding='utf-8');return target

"""Export actual round-screen previews and a self-contained web handoff."""
from pathlib import Path
import hashlib
import json
import shutil
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parent
media = root / 'media'
for label, ms in [('ready', 32), ('playing', 9900), ('game-over', 14000)]:
    frame = Image.open(root / f'build/classic-preview/frame-{ms:06d}.png').convert('RGBA')
    assert frame.size == (466, 466)
    mask = Image.new('L', frame.size, 0)
    ImageDraw.Draw(mask).ellipse((0, 0, 465, 465), fill=255)
    frame = Image.composite(frame, Image.new('RGBA', frame.size, '#15191e'), mask)
    frame.save(media / f'round-{label}.png')
package = root / 'build/demo.sky-hop.trpkg'
catalog = json.loads((root / 'website.json').read_text(encoding='utf-8'))
catalog['package'] = dict(file=package.name, size=package.stat().st_size,
    sha256=hashlib.sha256(package.read_bytes()).hexdigest())
dest = root / 'build/web-release'
dest.mkdir(parents=True, exist_ok=True)
shutil.copy2(package, dest / package.name)
shutil.copy2(root / 'cover.png', dest / 'cover.png')
shutil.copy2(root / 'README.md', dest / 'README.md')
shutil.copy2(root / 'assets/NOTICE.md', dest / 'ASSET-NOTICE.md')
shutil.copy2(root / 'assets/LICENSE-samuelcust.txt', dest / 'LICENSE-samuelcust.txt')
shutil.copytree(media, dest / 'media', dirs_exist_ok=True)
(dest / 'catalog.json').write_text(json.dumps(catalog, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
shutil.make_archive(str(root / 'build/sky-hop-web-release'), 'zip', dest)
print(dest)

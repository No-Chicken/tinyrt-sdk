"""Create a temporary metrics manifest preserving the production app identity."""
import json
from pathlib import Path
import shutil

APP = Path(__file__).resolve().parents[1]
SDK = APP.parents[1]
out = SDK / "build/diagnostic-wave"
out.mkdir(parents=True, exist_ok=True)
manifest = json.loads((APP / "app.json").read_bytes())
manifest.setdefault("defines", {})["WAVE_METRICS"] = 1
for name in (*manifest["sources"], "wave_physics.h", manifest["cover"]):
    shutil.copy2(APP / name, out / name)
(out / "app.json").write_bytes((json.dumps(manifest, indent=2) + "\n").encode("utf-8"))
print(out)

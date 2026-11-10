"""Generate the bilingual guest API reference from the pinned public ABI."""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs/api.md"
ANY = "init / event / render / stop"
OK = "0 成功 / success；非法调用触发 trap / invalid use traps"
GFX = "0 成功 / success；−1 拒绝 / rejected"
BUSY = "0 接受 / accepted；1 忙或不可用 / busy or unavailable；非法调用触发 trap / invalid use traps"


def policies(abi):
    g, raster = abi["graphics"], abi["raster"]
    rows = {}
    def add(names, stages, limits, result=OK):
        for name in names.split():
            rows[name] = stages, limits, result

    add("asset_read", "init / event", f"≤ {abi['resources']['max_read_bytes']} bytes", "读取字节数 / bytes read；越界触发 trap / invalid range traps")
    add("audio_play", "init / event", f"PCM16 LE mono, {abi['audio']['sample_rate']} Hz, 2–{abi['audio']['max_bytes']} bytes (偶数 / even), ≤ {abi['audio']['max_requests_per_callback']} 次/回调 / requests per callback", BUSY)
    add("clock_interval", "init / event", f"{abi['clock']['min_interval_ms']}–{abi['clock']['max_interval_ms']} ms")
    add("debug_log", ANY, "UTF-8 1–128 bytes, ≤ 50/s", "始终返回 0，超限丢弃 / always 0, drops excess")
    add("draw_arc", "render", "0 < thickness ≤ radius, 0 ≤ start ≤ end ≤ 360°")
    add("draw_clear", "render", f"RGB24, ≤ {g['max_commands']} 命令/帧 / commands per frame")
    add("draw_rect", "render", "视口内 / inside viewport")
    add("draw_round_rect", "render", "视口内 / inside viewport, 0 ≤ radius ≤ min(w,h)/2")
    image = g["rgb565"]
    add("draw_rgb565 draw_rgb565_scaled", "render", f"RGB565 LE, ≤ {image['max_width']} × {image['max_height']} 源像素 / source pixels, {image['max_images']} 图/帧 / image per frame")
    add("draw_skip", "render", "唯一操作 / sole operation")
    add("draw_text", "render", f"UTF-8 1–{g['text_max_bytes']} bytes")
    add("draw_text_box", "render", f"UTF-8 1–{g['text_max_bytes']} bytes, font_px ∈ {g['text_box_font_px']}, align 0–2")
    add("fb_config", "init", "一次 / once, RGB565 ≤ 128 KiB, scale 1/2", GFX)
    add("fb_present", "render", "单个连续矩形 / one packed patch, length = w × h × 2", GFX)
    add("gfx_begin", "render", "帧首 / first in frame, flags mask 0x301", GFX)
    add("gfx_damage", "render", "批内视口矩形 / viewport rectangle inside batch", GFX)
    add("gfx_end", "render", "关闭当前批 / close open batch", GFX)
    add("gfx_submit", "render", f"≤ {raster['max_submit_bytes']} bytes/次 / call, ≤ {raster['max_frame_bytes']} bytes, {raster['max_records']} 条/帧 / records per frame", GFX)
    add("gfx_pal_upload", "init / event", f"{raster['palettes']} 调色板 / palettes, first + count ≤ 256", GFX)
    add("gfx_tex_upload", "init / event", f"{raster['textures']} 槽 / slots, ≤ 512 × 512, 总计 / total ≤ {raster['texture_bytes']} bytes", GFX)
    add("gfx_tex_update_rows", "init / event", "整行，范围限于现有纹理 / packed complete rows within existing texture", GFX)
    add("gfx_tex_free", "init / event", "slot 0–31, framebuffer 保留 / reserves slot 31 when configured", GFX)
    add("haptic_pulse", "init / event", f"1–{abi['haptic']['max_strength']}%, 1–{abi['haptic']['max_duration_ms']} ms", BUSY)
    add("haptic_stop", "init / event", "无参数 / no arguments", BUSY)
    add("input_events", "init", "允许掩码 / allowed masks " + ", ".join(hex(i) for i in abi['optional_input']['allowed_masks']))
    add("kv_get", ANY, f"key 0–{abi['storage']['keys']-1}", "值或缺省值 / value or fallback；非法调用触发 trap / invalid use traps")
    add("kv_set", ANY, f"key 0–{abi['storage']['keys']-1}, i32")
    add("now_ms", ANY, "u32 毫秒 / milliseconds", "宿主时钟值 / host time；缺少时钟触发 trap / missing clock traps")
    add("random_u32", ANY, "u32", "随机数 / random value；无随机源触发 trap / missing source traps")
    add("runtime_backend", ANY, "无参数 / no arguments", "0 解释器 / interpreter, 1 AOT")
    add("gfx_caps", ANY, "无参数 / no arguments", "能力位掩码 / capability mask")
    add("sys_info", ANY, "9 × u32 LE (36 bytes)", "0 成功 / success；−2 缓冲区过小 / short buffer；越界触发 trap / invalid address traps")
    if "quotas" in abi:
        quotas = abi["quotas"]
        old, new = quotas["level_1_to_3"], quotas["level_4"]
        def levels(field):
            return f"Level 1–3: {old[field]}, Level 4: {new[field]}"
        add("draw_clear", "render", "RGB24, 命令数/帧 / commands per frame — " + levels("commands"))
        add("draw_text", "render", "UTF-8 字节上限 / max bytes — " + levels("text_bytes"))
        add("draw_text_box", "render", f"font_px ∈ {g['text_box_font_px']}, align 0–2, UTF-8 字节上限 / max bytes — " + levels("text_bytes"))
        add("draw_rgb565 draw_rgb565_scaled", "render", f"RGB565 LE, ≤ {image['max_width']} × {image['max_height']} 源像素 / source pixels, 图/帧 / images per frame — " + levels("images") + ", 总字节/帧 / bytes per frame — " + levels("pixel_bytes_total"))
        slots = "槽数 / slots (key starts at 0) — " + levels("kv_keys")
        add("kv_set", ANY, slots + ", i32")
        add("kv_get", ANY, slots, rows["kv_get"][2])
    return rows


def render():
    abi = json.loads((ROOT / "contracts/abi-v1.json").read_text(encoding="utf-8"))
    header = (ROOT / "include/tinyrt.h").read_text(encoding="utf-8")
    notes = {}
    signatures = {}
    comment = ""
    for line in header.splitlines():
        if line.startswith("/*"):
            comment = line[2:]
        elif line.lstrip().startswith("*"):
            comment += " " + line.strip().lstrip("*").strip()
        else:
            match = re.fullmatch(r"TINYRT_IMPORT\((\w+)\) (.+);", line)
            if match:
                name = match.group(1)
                signatures[name] = match.group(2)
                if comment:
                    notes[name] = comment.removesuffix("*/").strip()
            comment = ""

    rows = policies(abi)
    for item in abi["imports"]:
        if item["since_level"] >= 3 or "stages" in item:
            rows[item["name"]] = (" / ".join(item["stages"]), item["limits"], item["errors"])
    names = {item["name"] for item in abi["imports"]}
    if names != rows.keys() or names != signatures.keys():
        raise ValueError("ABI imports, guest declarations and API policies must match")
    permissions = {spec["value"]: name for name, spec in abi["permissions"].items()}
    permissions[0] = "无 / none"
    out = ["# TinyRT Guest API", "", "由合同生成 / Generated by `python scripts/generate_api.py`.",
           "", "权限缺失会触发 trap，终止回调。Missing required permissions traps the callback.",
           "传统绘图帧从 `draw_clear` 开始。Legacy drawing starts with `draw_clear`."]
    for item in abi["imports"]:
        name = item["name"]
        args = signatures[name].split("(", 1)[1].removesuffix(")")
        count = 0 if args == "void" else len(args.split(","))
        if item["signature"] != "(" + "i" * count + ")i":
            raise ValueError(f"{name}: Wasm signature differs from C declaration")
        stage, limit, result = rows[name]
        out.extend(["", f"## `{name}`", "", f"```c\n{signatures[name]};\n```", "",
                    "| Wasm | 权限 / Permission | 阶段 / Stage | since_level | 限额 / Limits | 返回与错误 / Results and errors |",
                    "|---|---|---|---|---|---|",
                    f"| `{item['signature']}` | {permissions[item['permission']]} ({item['permission']}) | {stage} | {item['since_level']} | {limit} | {result} |"])
        if name in notes:
            out.extend(["", notes[name]])
        if name == "sys_info" and "quotas" in abi:
            out.extend(["", "长度 36–51 字节时写入原有九个字段。至少 52 字节时另写本应用等级的文本字节、图片数、命令数与 KV 槽数上限。",
                        "Use `tinyrt_sys_info_t` for the original nine fields or `tinyrt_sys_info_v4_t` for all thirteen fields on a Level 4 host."])
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    content = render()
    if args.check:
        if content != OUTPUT.read_text(encoding="utf-8"):
            raise SystemExit("docs/api.md is stale; run scripts/generate_api.py")
    else:
        OUTPUT.parent.mkdir(exist_ok=True)
        OUTPUT.write_bytes(content.encode("utf-8"))

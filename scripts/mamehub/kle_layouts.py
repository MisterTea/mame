#!/usr/bin/env python3
"""Build Keyboard Layout Editor JSON and KLE-style PNG legends."""

from __future__ import annotations

import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# Match keyboard-layout-editor.com DCS profile (render.js unitSizes.px).
UNIT = 54
BEVEL = 6
BEVEL_TOP = 3
ROUND_OUTER = 5
ROUND_INNER = 3
PAD = 12
SCALE = 2

INACTIVE_C = "#2a3038"
INACTIVE_T = "#8a9099"
ACTIVE_C = "#3d9a6a"
ACTIVE_T = "#04140c"
BACKCOLOR = "#12141a"

# Physical 60% ANSI + inverted-T arrows (KeyboardEvent.code → key).
def _k(code: str, label: str, x: float, y: float, w: float = 1.0, h: float = 1.0) -> dict:
    return {"code": code, "label": label, "x": x, "y": y, "w": w, "h": h}


BOARD: list[dict] = [
    _k("Backquote", "`", 0, 0),
    *[_k(f"Digit{n}", str(n), float(n), 0) for n in range(1, 10)],
    _k("Digit0", "0", 10, 0),
    _k("Minus", "-", 11, 0),
    _k("Equal", "=", 12, 0),
    _k("Backspace", "Backspace", 13, 0, 2),
    _k("Tab", "Tab", 0, 1, 1.5),
    *[_k(f"Key{c}", c, 1.5 + i, 1) for i, c in enumerate("QWERTYUIOP")],
    _k("BracketLeft", "[", 11.5, 1),
    _k("BracketRight", "]", 12.5, 1),
    _k("Backslash", "\\", 13.5, 1, 1.5),
    _k("CapsLock", "Caps", 0, 2, 1.75),
    *[_k(f"Key{c}", c, 1.75 + i, 2) for i, c in enumerate("ASDFGHJKL")],
    _k("Semicolon", ";", 10.75, 2),
    _k("Quote", "'", 11.75, 2),
    _k("Enter", "Enter", 12.75, 2, 2.25),
    _k("ShiftLeft", "Shift", 0, 3, 2.25),
    *[_k(f"Key{c}", c, 2.25 + i, 3) for i, c in enumerate("ZXCVBNM")],
    _k("Comma", ",", 9.25, 3),
    _k("Period", ".", 10.25, 3),
    _k("Slash", "/", 11.25, 3),
    _k("ShiftRight", "Shift", 12.25, 3, 2.75),
    _k("ControlLeft", "Ctrl", 0, 4, 1.25),
    _k("MetaLeft", "Win", 1.25, 4, 1.25),
    _k("AltLeft", "Alt", 2.5, 4, 1.25),
    _k("Space", "Space", 3.75, 4, 6.25),
    _k("AltRight", "Alt", 10, 4, 1.25),
    _k("MetaRight", "Win", 11.25, 4, 1.25),
    _k("ContextMenu", "Menu", 12.5, 4, 1.25),
    _k("ControlRight", "Ctrl", 13.75, 4, 1.25),
    _k("ArrowUp", "↑", 16.5, 2),
    _k("ArrowLeft", "←", 15.5, 3),
    _k("ArrowDown", "↓", 16.5, 3),
    _k("ArrowRight", "→", 17.5, 3),
]


def _hex_rgb(color: str) -> tuple[int, int, int]:
    c = color.lstrip("#")
    return int(c[0:2], 16), int(c[2:4], 16), int(c[4:6], 16)


def _lighten(color: str, mix: float = 0.18) -> tuple[int, int, int]:
    r, g, b = _hex_rgb(color)
    return (
        min(255, int(r + (255 - r) * mix)),
        min(255, int(g + (255 - g) * mix)),
        min(255, int(b + (255 - b) * mix)),
    )


def _keycap_legend(kb: dict, action: str) -> str:
    caps = kb.get("keycaps") or {}
    if action in caps:
        return str(caps[action])
    labels = kb.get("labels") or {}
    return str(labels.get(action) or action)


def build_kle(profile: dict) -> list:
    kb = profile.get("keyboard") or {}
    bindings = kb.get("bindings") or {}
    title = profile.get("title") or profile["id"]
    meta = {
        "name": f"MAMEHub {title} keyboard",
        "author": "MAMEHub",
        "backcolor": BACKCOLOR,
        "radii": "8px",
        "notes": "Active keys (green) are the default Player 1 controls.",
    }
    by_y: dict[float, list[dict]] = {}
    for key in BOARD:
        by_y.setdefault(key["y"], []).append(key)

    rows: list = [meta]
    current_c = None
    current_t = None
    current_a = None
    for y in sorted(by_y):
        row_out: list = []
        x = 0.0
        for key in sorted(by_y[y], key=lambda k: k["x"]):
            props: dict = {}
            dx = key["x"] - x
            if dx:
                props["x"] = dx
            if key["w"] != 1:
                props["w"] = key["w"]
            if key["h"] != 1:
                props["h"] = key["h"]
            action = bindings.get(key["code"])
            if action:
                c, t = ACTIVE_C, ACTIVE_T
                action_legend = _keycap_legend(kb, action)
                letterish = key["code"].startswith("Key") or key["code"].startswith("Digit")
                if letterish and key["label"] and key["label"].upper() != action_legend.upper():
                    # KLE legend slots: 0 top-left (physical), 7 center (action)
                    legend = key["label"] + "\n\n\n\n\n\n\n" + action_legend
                    align = 4
                else:
                    legend = action_legend
                    align = 7
            else:
                c, t = INACTIVE_C, INACTIVE_T
                legend = key["label"]
                align = 7
            if c != current_c or t != current_t or align != current_a:
                props["c"] = c
                props["t"] = t
                props["a"] = align
                current_c, current_t, current_a = c, t, align
            if props:
                row_out.append(props)
            row_out.append(legend)
            x = key["x"] + key["w"]
        rows.append(row_out)
    return rows


def deserialize_kle(layout: list) -> tuple[dict, list[dict]]:
    """Minimal KLE deserialize (no rotation / stepped caps)."""
    meta: dict = {}
    keys: list[dict] = []
    cur = {
        "x": 0.0,
        "y": 0.0,
        "w": 1.0,
        "h": 1.0,
        "c": "#cccccc",
        "t": "#000000",
        "a": 4,
    }
    for row in layout:
        if isinstance(row, dict):
            meta = row
            continue
        cur["x"] = 0.0
        for item in row:
            if isinstance(item, dict):
                if "x" in item:
                    cur["x"] += float(item["x"])
                if "y" in item:
                    cur["y"] += float(item["y"])
                if "w" in item:
                    cur["w"] = float(item["w"])
                if "h" in item:
                    cur["h"] = float(item["h"])
                if "c" in item:
                    cur["c"] = item["c"]
                if "t" in item:
                    cur["t"] = item["t"]
                if "a" in item:
                    cur["a"] = item["a"]
                continue
            keys.append(
                {
                    "x": cur["x"],
                    "y": cur["y"],
                    "w": cur["w"],
                    "h": cur["h"],
                    "c": cur["c"],
                    "t": cur["t"],
                    "legend": str(item),
                }
            )
            cur["x"] += cur["w"]
            cur["w"] = 1.0
            cur["h"] = 1.0
        cur["y"] += 1.0
    return meta, keys


def _font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    size = max(10, int(size))
    for path in (
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ):
        p = Path(path)
        if p.is_file():
            try:
                return ImageFont.truetype(str(p), size)
            except OSError:
                continue
    return ImageFont.load_default()


def _fit_font(draw: ImageDraw.ImageDraw, text: str, max_w: float, max_h: float, start: int):
    size = start
    while size >= 9:
        font = _font(size)
        bbox = draw.textbbox((0, 0), text, font=font)
        tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
        if tw <= max_w and th <= max_h:
            return font
        size -= 1
    return _font(9)


def render_png(layout: list, dest: Path) -> None:
    meta, keys = deserialize_kle(layout)
    if not keys:
        raise SystemExit("empty KLE layout")
    max_x = max(k["x"] + k["w"] for k in keys)
    max_y = max(k["y"] + k["h"] for k in keys)
    s = SCALE
    unit = UNIT * s
    pad = PAD * s
    width = int(max_x * unit + pad * 2)
    height = int(max_y * unit + pad * 2)
    bg = _hex_rgb(meta.get("backcolor") or BACKCOLOR)
    img = Image.new("RGB", (width, height), bg)
    draw = ImageDraw.Draw(img)
    bevel = BEVEL * s
    round_o = ROUND_OUTER * s
    round_i = ROUND_INNER * s

    for key in keys:
        x = pad + key["x"] * unit
        y = pad + key["y"] * unit
        w = key["w"] * unit - s  # 1px visual gap at 1x
        h = key["h"] * unit - s
        outer = _hex_rgb(key["c"])
        inner = _lighten(key["c"])
        text_c = _hex_rgb(key["t"])
        draw.rounded_rectangle((x, y, x + w, y + h), radius=round_o, fill=outer)
        ix = x + bevel
        iy = y + bevel - BEVEL_TOP * s
        iw = w - bevel * 2
        ih = h - bevel * 2 - (BEVEL_TOP * s)
        draw.rounded_rectangle((ix, iy, ix + iw, iy + ih), radius=round_i, fill=inner)
        raw = key["legend"]
        lines = [ln for ln in raw.split("\n") if ln][:2]
        if not lines:
            continue
        if len(lines) == 1:
            font = _fit_font(draw, lines[0], iw - 4 * s, ih - 2 * s, 13 * s)
            bbox = draw.textbbox((0, 0), lines[0], font=font)
            tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
            tx = ix + (iw - tw) / 2 - bbox[0]
            ty = iy + (ih - th) / 2 - bbox[1]
            draw.text((tx, ty), lines[0], font=font, fill=text_c)
        else:
            key_font = _font(8 * s)
            kb_ = draw.textbbox((0, 0), lines[0], font=key_font)
            draw.text((ix + 2 * s - kb_[0], iy + 1 * s - kb_[1]), lines[0], font=key_font, fill=text_c)
            action_font = _fit_font(draw, lines[1], iw - 6 * s, ih - 12 * s, 12 * s)
            ab = draw.textbbox((0, 0), lines[1], font=action_font)
            aw, ah = ab[2] - ab[0], ab[3] - ab[1]
            draw.text(
                (ix + (iw - aw) / 2 - ab[0], iy + (ih - ah) / 2 + 3 * s - ab[1]),
                lines[1],
                font=action_font,
                fill=text_c,
            )

    dest.parent.mkdir(parents=True, exist_ok=True)
    img.save(dest, "PNG", optimize=True)


def write_layout_assets(profile: dict, dest_dir: Path) -> None:
    layout = build_kle(profile)
    dest_dir.mkdir(parents=True, exist_ok=True)
    pid = profile["id"]
    json_path = dest_dir / f"{pid}.json"
    png_path = dest_dir / f"{pid}.png"
    json_path.write_text(json.dumps(layout, indent=2) + "\n", encoding="utf-8")
    # Keep Keyboard Layout Editor captures; only synthesize a PNG when missing.
    if not png_path.is_file():
        render_png(layout, png_path)

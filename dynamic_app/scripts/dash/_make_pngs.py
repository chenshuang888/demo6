"""dashboard_pkg 天气图标素材：8 张 40×40 RGB565 .bin。
每张配色一目了然，无需依赖字体。
"""
from PIL import Image, ImageDraw
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "assets")
os.makedirs(ASSETS, exist_ok=True)

SIZE = 40
DARK = (0x14, 0x10, 0x1F)


def save(name, draw_fn, bg=DARK):
    img = Image.new("RGB", (SIZE, SIZE), bg)
    d = ImageDraw.Draw(img)
    draw_fn(d)
    p = os.path.join(ASSETS, name + ".png")
    img.save(p)
    return p


def clear(d):  # 太阳
    d.ellipse([12, 12, 28, 28], fill=(0xF5, 0x9E, 0x0B))
    for ang in range(0, 360, 45):
        import math
        x = 20 + 14 * math.cos(math.radians(ang))
        y = 20 + 14 * math.sin(math.radians(ang))
        d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=(0xF5, 0x9E, 0x0B))


def cloudy(d):  # 一朵云 + 半个太阳
    d.ellipse([22, 8, 36, 22], fill=(0xF5, 0x9E, 0x0B))
    # 云
    d.ellipse([4,  18, 18, 32], fill=(0xE5, 0xE7, 0xEB))
    d.ellipse([12, 14, 28, 30], fill=(0xE5, 0xE7, 0xEB))
    d.ellipse([20, 18, 36, 32], fill=(0xE5, 0xE7, 0xEB))
    d.rectangle([8, 26, 34, 32], fill=(0xE5, 0xE7, 0xEB))


def overcast(d):  # 满云
    for off in [(2, 14), (10, 10), (18, 14), (4, 22), (16, 22), (24, 22)]:
        d.ellipse([off[0], off[1], off[0]+14, off[1]+14], fill=(0x9C, 0xA3, 0xAF))


def rain(d):
    overcast(d)
    for x in [10, 18, 26]:
        d.line([(x, 30), (x - 2, 36)], fill=(0x60, 0xA5, 0xFA), width=2)


def snow(d):
    overcast(d)
    for x in [10, 18, 26]:
        d.ellipse([x - 2, 30, x + 2, 34], fill=(0xE5, 0xE7, 0xEB))
        d.ellipse([x - 2, 35, x + 2, 39], fill=(0xE5, 0xE7, 0xEB))


def fog(d):
    for y in [12, 18, 24, 30]:
        d.rectangle([4, y, 36, y + 3], fill=(0x9C, 0xA3, 0xAF))


def thunder(d):
    overcast(d)
    # 闪电
    d.polygon([(20, 24), (14, 36), (19, 36), (16, 40),
               (24, 30), (19, 30), (22, 24)],
              fill=(0xF5, 0x9E, 0x0B))


def unknown(d):
    d.ellipse([6, 6, 34, 34], outline=(0x9B, 0x94, 0xB5), width=2)
    # 大问号用三段
    d.arc([12, 8, 28, 22], 200, 340, fill=(0xF1, 0xEC, 0xFF), width=3)
    d.line([(20, 22), (20, 28)], fill=(0xF1, 0xEC, 0xFF), width=3)
    d.ellipse([18, 30, 22, 34], fill=(0xF1, 0xEC, 0xFF))


pngs = []
for name, fn in [
    ("ic_clear",    clear),
    ("ic_cloudy",   cloudy),
    ("ic_overcast", overcast),
    ("ic_rain",     rain),
    ("ic_snow",     snow),
    ("ic_fog",      fog),
    ("ic_thunder",  thunder),
    ("ic_unknown",  unknown),
]:
    pngs.append(save(name, fn))

script = os.path.normpath(os.path.join(
    HERE, "..", "..", "..", "managed_components",
    "lvgl__lvgl", "scripts", "LVGLImage.py"))
for p in pngs:
    rc = subprocess.run([sys.executable, script,
                         "--ofmt", "BIN", "--cf", "RGB565",
                         "-o", ASSETS, p]).returncode
    if rc != 0:
        sys.exit("LVGLImage.py failed: " + p)
print("ok ->", ASSETS)

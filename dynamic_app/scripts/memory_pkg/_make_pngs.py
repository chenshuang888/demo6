"""memory_pkg 卡牌素材：48×48 RGB565 .bin。
back.bin 卡背 + p1..p4.bin 4 张图案。
颜色用项目深紫青绿配色。
"""
from PIL import Image, ImageDraw
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "assets")
os.makedirs(ASSETS, exist_ok=True)

SIZE = 48
BG_BACK = (0x23, 0x1C, 0x3A)
FG_BACK = (0x06, 0xB6, 0xD4)


def save(name, draw_fn, bg):
    img = Image.new("RGB", (SIZE, SIZE), bg)
    d = ImageDraw.Draw(img)
    draw_fn(d)
    p = os.path.join(ASSETS, name + ".png")
    img.save(p)
    return p


def back(d):
    # 双层菱形花纹
    d.polygon([(24, 6), (42, 24), (24, 42), (6, 24)], outline=FG_BACK, width=2)
    d.polygon([(24, 14), (34, 24), (24, 34), (14, 24)], fill=FG_BACK)


def p1(d):  # 红圆
    d.ellipse([8, 8, 39, 39], fill=(0xEF, 0x44, 0x44))
    d.ellipse([18, 18, 29, 29], fill=(0xFE, 0xF3, 0xC7))


def p2(d):  # 绿方
    d.rectangle([8, 8, 39, 39], fill=(0x10, 0xB9, 0x81))
    d.rectangle([16, 16, 31, 31], fill=(0xF1, 0xEC, 0xFF))


def p3(d):  # 黄三角
    d.polygon([(24, 6), (42, 40), (6, 40)], fill=(0xF5, 0x9E, 0x0B))
    d.polygon([(24, 18), (35, 36), (13, 36)], fill=(0x14, 0x10, 0x1F))


def p4(d):  # 紫五角星
    d.rectangle([0, 0, SIZE, SIZE], fill=(0x14, 0x10, 0x1F))
    pts = [(24, 6), (29, 19), (43, 19), (32, 27),
           (36, 41), (24, 33), (12, 41), (16, 27),
           (5, 19), (19, 19)]
    d.polygon(pts, fill=(0xA7, 0x8B, 0xFA))


pngs = []
pngs.append(save("back", back, BG_BACK))
pngs.append(save("p1",   p1,   (0x14, 0x10, 0x1F)))
pngs.append(save("p2",   p2,   (0x14, 0x10, 0x1F)))
pngs.append(save("p3",   p3,   (0x14, 0x10, 0x1F)))
pngs.append(save("p4",   p4,   (0x14, 0x10, 0x1F)))

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

"""doodle_pkg 工具栏图标素材：3 张 40×40 RGB565 .bin。

参考 dash/_make_pngs.py 同款做法：用 PIL 画几何，再调 LVGLImage.py 转 .bin。
跑一次即可：
    python dynamic_app/scripts/doodle_pkg/_make_pngs.py
"""
from PIL import Image, ImageDraw
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "assets")
os.makedirs(ASSETS, exist_ok=True)

SIZE = 40
BG   = (0xFF, 0xFF, 0xFF)   # 白底，跟工具栏 C_PANEL 同色

# 三种图标：橡皮 / 保存 / 更多
COLOR_ERASE = (0xFF, 0x9F, 0x40)   # 暖橙
COLOR_SAVE  = (0x34, 0xC7, 0x59)   # iOS 绿
COLOR_MORE  = (0x6E, 0x6E, 0x73)   # 中灰


def save(name, draw_fn):
    img = Image.new("RGB", (SIZE, SIZE), BG)
    draw_fn(ImageDraw.Draw(img))
    p = os.path.join(ASSETS, name + ".png")
    img.save(p)
    return p


def eraser(d):
    """斜放的橡皮擦：底白色矩形 + 顶橙色矩形，旋转 30° 视觉。
       用两个填充矩形 + 边框近似，简化避免 PIL 旋转抗锯齿问题。"""
    # 主体：矩形从左下到右上倾斜
    pts_top    = [(8, 22), (22, 8),  (32, 18), (18, 32)]   # 上半（橙）
    pts_bottom = [(18, 32), (32, 18), (34, 20), (20, 34)]  # 下半小段（白带边）
    d.polygon(pts_top, fill=COLOR_ERASE, outline=(0x6B, 0x3C, 0x10))
    d.polygon(pts_bottom, fill=(0xFA, 0xFA, 0xFA),
              outline=(0x6B, 0x3C, 0x10))


def save_icon(d):
    """软盘：方形 + 顶部凹槽 + 底部标签。"""
    # 主体方框
    d.rectangle([6, 6, 34, 34], fill=COLOR_SAVE,
                outline=(0x14, 0x6E, 0x33), width=2)
    # 顶部金属片（凹槽）
    d.rectangle([12, 6, 28, 14], fill=(0x14, 0x6E, 0x33))
    d.rectangle([22, 8, 26, 13], fill=(0xE5, 0xE7, 0xEB))
    # 底部白色标签
    d.rectangle([10, 20, 30, 32], fill=(0xF5, 0xF5, 0xF7),
                outline=(0x14, 0x6E, 0x33))
    # 标签上两条线模拟文字
    d.line([(13, 24), (27, 24)], fill=(0x9C, 0xA3, 0xAF), width=1)
    d.line([(13, 28), (24, 28)], fill=(0x9C, 0xA3, 0xAF), width=1)


def more(d):
    """三个水平点。"""
    for cx in (12, 20, 28):
        d.ellipse([cx - 4, 16, cx + 4, 24], fill=COLOR_MORE)


def trash(d):
    """垃圾桶：保留备用，业务暂未用到。"""
    # 桶身
    d.rectangle([10, 14, 30, 34], fill=(0xFF, 0x3B, 0x30),
                outline=(0x80, 0x18, 0x10), width=2)
    # 桶盖
    d.rectangle([6,  10, 34, 14], fill=(0xFF, 0x3B, 0x30),
                outline=(0x80, 0x18, 0x10), width=2)
    # 把手
    d.rectangle([16, 6,  24, 10], outline=(0x80, 0x18, 0x10), width=2)
    # 桶身竖纹
    for x in (15, 20, 25):
        d.line([(x, 18), (x, 30)], fill=(0xFF, 0xFF, 0xFF), width=1)


pngs = []
for name, fn in [
    ("ic_erase", eraser),
    ("ic_save",  save_icon),
    ("ic_more",  more),
    ("ic_trash", trash),
]:
    pngs.append(save(name, fn))

# 用 LVGL 自带 LVGLImage.py 把 PNG 转 RGB565 .bin
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

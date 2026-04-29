"""memory_pkg 菜单图标：紫底脑袋图标。32×32 → icon.bin (RGB565)。"""
from PIL import Image, ImageDraw
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
BG = (0xA7, 0x8B, 0xFA)
FG = (0x14, 0x10, 0x1F)

img = Image.new("RGB", (32, 32), BG)
d = ImageDraw.Draw(img)
# 中心一组 4 张小卡片表示翻牌
d.rectangle([4, 4, 14, 14], fill=FG)
d.rectangle([18, 4, 28, 14], fill=FG)
d.rectangle([4, 18, 14, 28], fill=(0xFE, 0xF3, 0xC7))
d.rectangle([18, 18, 28, 28], fill=FG)
# 翻开那张画个圆
d.ellipse([6, 20, 12, 26], fill=BG)

png_path = os.path.join(HERE, "icon.png")
img.save(png_path)
print("png ok:", png_path)

script = os.path.normpath(os.path.join(
    HERE, "..", "..", "..", "managed_components",
    "lvgl__lvgl", "scripts", "LVGLImage.py"))
rc = subprocess.run([sys.executable, script,
                     "--ofmt", "BIN", "--cf", "RGB565",
                     "-o", HERE, png_path]).returncode
if rc != 0:
    sys.exit("LVGLImage.py failed")
print("bin ok:", os.path.join(HERE, "icon.bin"))

"""生成 32×32 菜单图标 icon.png（默认黑底白星），转 LVGL .bin。

每个 app pack 都可以拷一份这个脚本改改参数。
"""
from PIL import Image, ImageDraw
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- 在这里改图案 ----
BG = (0xDC, 0x26, 0x26)   # 红底
FG = (0xFE, 0xF3, 0xC7)   # 米白
SHAPE = "star"            # circle | square | triangle | star

img = Image.new("RGB", (32, 32), BG)
d = ImageDraw.Draw(img)
if SHAPE == "circle":
    d.ellipse([4, 4, 27, 27], fill=FG)
elif SHAPE == "square":
    d.rectangle([6, 6, 25, 25], fill=FG)
elif SHAPE == "triangle":
    d.polygon([(15, 3), (29, 28), (2, 28)], fill=FG)
elif SHAPE == "star":
    # 5 角星，简化点列
    pts = [(15,3),(18,12),(28,12),(20,18),(23,28),
           (15,22),(7,28),(10,18),(2,12),(12,12)]
    d.polygon(pts, fill=FG)

png_path = os.path.join(HERE, "icon.png")
img.save(png_path)
print("png ok:", png_path)

# 调 LVGLImage.py 转 RGB565 .bin（与 assets/*.bin 一致格式）
script = os.path.normpath(os.path.join(
    HERE, "..", "..", "..", "managed_components",
    "lvgl__lvgl", "scripts", "LVGLImage.py"))
rc = subprocess.run([sys.executable, script,
                     "--ofmt", "BIN", "--cf", "RGB565",
                     "-o", HERE, png_path]).returncode
if rc != 0:
    sys.exit("LVGLImage.py failed")
print("bin ok:", os.path.join(HERE, "icon.bin"))

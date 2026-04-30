"""dashboard_pkg 菜单图标：青底太阳。32×32 → icon.bin (RGB565)。"""
from PIL import Image, ImageDraw
import os, subprocess, sys, math

HERE = os.path.dirname(os.path.abspath(__file__))
BG = (0x06, 0xB6, 0xD4)
FG = (0xF5, 0x9E, 0x0B)

img = Image.new("RGB", (32, 32), BG)
d = ImageDraw.Draw(img)
d.ellipse([10, 10, 22, 22], fill=FG)
for ang in range(0, 360, 45):
    x = 16 + 11 * math.cos(math.radians(ang))
    y = 16 + 11 * math.sin(math.radians(ang))
    d.ellipse([x - 1.5, y - 1.5, x + 1.5, y + 1.5], fill=FG)

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

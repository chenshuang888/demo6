"""habit_pkg 菜单图标：青绿底白色对勾。32×32 → icon.bin (RGB565)。"""
from PIL import Image, ImageDraw
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
BG = (0x06, 0xB6, 0xD4)
FG = (0xF1, 0xEC, 0xFF)

img = Image.new("RGB", (32, 32), BG)
d = ImageDraw.Draw(img)
# 粗对勾
d.line([(7, 17), (14, 24)], fill=FG, width=4)
d.line([(14, 24), (26, 9)],  fill=FG, width=4)

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

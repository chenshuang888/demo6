"""一次性脚本：生成 3 张 32x32 演示用 PNG（红/绿/蓝主色 + 简单几何图案）。

跑完会输出 a.png b.png c.png 到本目录，再用 LVGLImage.py 转成 .bin。
"""
from PIL import Image, ImageDraw
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def make(name: str, bg: tuple, fg: tuple, shape: str) -> str:
    img = Image.new("RGB", (32, 32), bg)
    d = ImageDraw.Draw(img)
    if shape == "circle":
        d.ellipse([6, 6, 25, 25], fill=fg, outline=(255, 255, 255))
    elif shape == "square":
        d.rectangle([6, 6, 25, 25], fill=fg, outline=(255, 255, 255))
    elif shape == "triangle":
        d.polygon([(15, 4), (28, 27), (3, 27)], fill=fg, outline=(255, 255, 255))
    p = os.path.join(HERE, name)
    img.save(p)
    return p


make("a.png", (220, 50, 50),  (255, 240, 200), "circle")    # 红底白圆
make("b.png", (40, 160, 80),  (240, 240, 240), "square")    # 绿底白方
make("c.png", (40, 100, 200), (255, 220, 50),  "triangle")  # 蓝底黄三角
print("ok")

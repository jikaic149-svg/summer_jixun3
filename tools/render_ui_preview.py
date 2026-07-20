#!/usr/bin/env python3
"""Render documentation previews for the 320x240 LCD layout."""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
FONT = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 16)
SMALL = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 14)

BG = "#eef0ec"
HEADER = "#082038"
PANEL = "#ffffff"
BORDER = "#bdbece"
TEXT = "#183038"
MUTED = "#687068"
PRIMARY = "#1890e8"
SUCCESS = "#28c030"
DANGER = "#e81820"


def centered(draw: ImageDraw.ImageDraw, rect, text, font=FONT, fill=PANEL):
    x, y, w, h = rect
    box = draw.textbbox((0, 0), text, font=font)
    tw, th = box[2] - box[0], box[3] - box[1]
    draw.text((x + (w - tw) // 2 - box[0], y + (h - th) // 2 - box[1]),
              text, font=font, fill=fill)


def box(draw, rect, fill=PANEL, outline=BORDER, text=None, text_color=TEXT):
    x, y, w, h = rect
    draw.rectangle((x, y, x + w - 1, y + h - 1), fill=fill, outline=outline)
    if text:
        centered(draw, rect, text, fill=text_color)


def header(draw, title):
    draw.rectangle((0, 0, 319, 27), fill=HEADER)
    centered(draw, (0, 0, 320, 28), title)


def main_preview() -> Image.Image:
    image = Image.new("RGB", (320, 240), BG)
    draw = ImageDraw.Draw(image)
    header(draw, "电磁炮击靶系统")

    box(draw, (8, 34, 160, 42))
    draw.text((16, 37), "距离", font=SMALL, fill=MUTED)
    draw.text((16, 54), "75 cm", font=FONT, fill=TEXT)
    box(draw, (176, 34, 136, 42), PRIMARY, PRIMARY, "距离发射", PANEL)

    cards = [
        ((8, 82, 96, 38), "状态", "待机", SUCCESS),
        ((112, 82, 96, 38), "方位", "0°", TEXT),
        ((216, 82, 96, 38), "仰角", "0°", TEXT),
    ]
    for (x, y, w, h), label, value, color in cards:
        box(draw, (x, y, w, h))
        draw.text((x + 6, y + 2), label, font=SMALL, fill=MUTED)
        draw.text((x + 6, y + 18), value, font=SMALL, fill=color)

    box(draw, (8, 126, 96, 48), PRIMARY, PRIMARY, "固定靶", PANEL)
    box(draw, (112, 126, 96, 48), PRIMARY, PRIMARY, "自动搜索", PANEL)
    box(draw, (216, 126, 96, 48), PRIMARY, PRIMARY, "往复扫描", PANEL)
    box(draw, (8, 182, 208, 50), text="触摸屏已就绪")
    box(draw, (224, 182, 88, 50), DANGER, DANGER, "停止", PANEL)
    return image


def keypad_preview() -> Image.Image:
    image = Image.new("RGB", (320, 240), BG)
    draw = ImageDraw.Draw(image)
    header(draw, "设置距离")
    labels = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "清", "0", "退"]
    for index, label in enumerate(labels):
        row, column = divmod(index, 3)
        box(draw, (8 + column * 52, 36 + row * 45, 44, 42), text=label)

    box(draw, (176, 36, 136, 48), text="75")
    centered(draw, (176, 92, 136, 28), "范围 50-100 cm", SMALL, MUTED)
    box(draw, (176, 128, 136, 42), SUCCESS, SUCCESS, "确认", PANEL)
    box(draw, (176, 176, 136, 42), DANGER, DANGER, "取消", PANEL)
    return image


def main() -> None:
    docs = ROOT / "docs"
    docs.mkdir(exist_ok=True)
    main_preview().save(docs / "ui_main_preview.png")
    keypad_preview().save(docs / "ui_distance_preview.png")


if __name__ == "__main__":
    main()

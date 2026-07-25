#!/usr/bin/env python3
"""Render documentation previews for the 320x240 LCD layout."""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
FONT = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 16)
SMALL = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 14)

BG = "#08131f"
HEADER = "#050b12"
PANEL = "#122536"
PANEL_ALT = "#183248"
BORDER = "#2a465f"
TEXT = "#eaf3ff"
MUTED = "#89a2b5"
PRIMARY = "#2f80ed"
THEME = "#8b5cf6"
SUCCESS = "#2bcb75"
WARNING = "#f4a62a"
DANGER = "#ed4b5f"
DISABLED = "#263746"
DISABLED_TEXT = "#667783"


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
    draw.rectangle((0, 0, 319, 29), fill=HEADER)
    draw.rectangle((0, 28, 319, 29), fill=PRIMARY)
    centered(draw, (0, 0, 320, 30), title, fill=TEXT)


def main_preview() -> Image.Image:
    image = Image.new("RGB", (320, 240), BG)
    draw = ImageDraw.Draw(image)
    header(draw, "电磁炮击靶系统")

    box(draw, (8, 36, 96, 42))
    draw.text((16, 39), "设定", font=SMALL, fill=MUTED)
    draw.text((16, 56), "75 cm", font=FONT, fill=TEXT)
    box(draw, (112, 36, 96, 42), PANEL, SUCCESS)
    draw.text((118, 39), "DIST", font=SMALL, fill=MUTED)
    draw.text((118, 56), "74.2cm", font=FONT, fill=TEXT)
    box(draw, (216, 36, 96, 42), PRIMARY, PRIMARY, "距离定位", TEXT)

    cards = [
        ((8, 84, 96, 36), "状态", "ENABLED", SUCCESS),
        ((112, 84, 96, 36), "YAW", "A24 T10", TEXT),
        ((216, 84, 96, 36), "PITCH", "A0 T0", TEXT),
    ]
    for (x, y, w, h), label, value, color in cards:
        box(draw, (x, y, w, h))
        draw.text((x + 6, y + 2), label, font=SMALL, fill=MUTED)
        draw.text((x + 6, y + 17), value, font=SMALL, fill=color)
    draw.rectangle((8, 84, 11, 119), fill=SUCCESS)

    box(draw, (8, 126, 96, 44), PRIMARY, PRIMARY, "固定靶", TEXT)
    box(draw, (112, 126, 96, 44), PRIMARY, PRIMARY, "自动搜索", TEXT)
    box(draw, (216, 126, 96, 44), DISABLED, BORDER, "SCAN OFF", DISABLED_TEXT)
    box(draw, (8, 176, 96, 40), PRIMARY, PRIMARY, "MOTOR", TEXT)
    box(draw, (112, 176, 96, 40), WARNING, WARNING, "充电", TEXT)
    box(draw, (216, 176, 96, 40), THEME, THEME, "THEME", TEXT)
    box(draw, (8, 220, 304, 18), text="YAW/PITCH OK", text_color=SUCCESS)
    return image


def cannon_preview() -> Image.Image:
    image = Image.new("RGB", (320, 240), BG)
    draw = ImageDraw.Draw(image)
    header(draw, "发射控制")
    box(draw, (4, 2, 80, 26), HEADER, HEADER, "< BACK", TEXT)

    box(draw, (8, 36, 304, 58))
    draw.rectangle((8, 36, 12, 93), fill=WARNING)
    draw.text((20, 39), "CHG 00:42/01:30", font=SMALL, fill=TEXT)
    draw.text((20, 61), "FIRE 0.0s", font=FONT, fill=MUTED)
    centered(draw, (174, 48, 130, 24), "CHARGING", SMALL, WARNING)
    centered(draw, (8, 100, 304, 24), "确认炮口方向与人员安全", SMALL, WARNING)

    box(draw, (8, 130, 304, 42), DANGER, DANGER, "发射", TEXT)
    box(draw, (8, 176, 304, 42), WARNING, WARNING, "立即断开发射", TEXT)
    box(draw, (8, 220, 304, 18), text="发射继电器已断开",
        text_color=SUCCESS)
    return image


def charge_preview() -> Image.Image:
    image = Image.new("RGB", (320, 240), BG)
    draw = ImageDraw.Draw(image)
    header(draw, "充电控制")
    box(draw, (4, 2, 80, 26), HEADER, HEADER, "< BACK", TEXT)

    box(draw, (8, 36, 304, 58))
    draw.rectangle((8, 36, 12, 93), fill=SUCCESS)
    draw.text((20, 39), "充电继电器", font=SMALL, fill=MUTED)
    draw.text((20, 61), "已断开", font=FONT, fill=SUCCESS)
    centered(draw, (174, 48, 130, 24), "PB1 / LOW", SMALL, MUTED)
    centered(draw, (8, 100, 304, 24), "充电期间禁止发射", SMALL, WARNING)

    box(draw, (8, 130, 304, 42), WARNING, WARNING, "开始充电", TEXT)
    box(draw, (8, 176, 304, 42), DANGER, DANGER, "立即断开充电", TEXT)
    box(draw, (8, 220, 304, 18), text="充电继电器已断开",
        text_color=SUCCESS)
    return image


def motor_preview() -> Image.Image:
    image = Image.new("RGB", (320, 240), BG)
    draw = ImageDraw.Draw(image)
    header(draw, "GIMBAL MOTOR")
    box(draw, (4, 2, 80, 26), HEADER, HEADER, "< BACK", TEXT)

    box(draw, (8, 36, 304, 46))
    draw.rectangle((8, 36, 12, 81), fill=SUCCESS)
    draw.text((20, 38), "MOTOR STATE", font=SMALL, fill=MUTED)
    draw.text((20, 57), "ENABLED", font=FONT, fill=SUCCESS)
    centered(draw, (152, 47, 152, 24), "AUTO HOLD @ POWER", SMALL, MUTED)

    box(draw, (8, 88, 148, 36))
    draw.rectangle((8, 88, 11, 123), fill=PRIMARY)
    draw.text((18, 90), "YAW", font=SMALL, fill=MUTED)
    draw.text((82, 97), "24°", font=FONT, fill=TEXT)
    box(draw, (164, 88, 148, 36))
    draw.rectangle((164, 88, 167, 123), fill=SUCCESS)
    draw.text((174, 90), "PITCH", font=SMALL, fill=MUTED)
    draw.text((250, 97), "0°", font=FONT, fill=TEXT)

    box(draw, (8, 130, 148, 42), SUCCESS, SUCCESS, "ENABLE / HOME", TEXT)
    box(draw, (164, 130, 148, 42), DISABLED, BORDER, "SCAN OFF", DISABLED_TEXT)
    box(draw, (8, 176, 148, 42), WARNING, WARNING, "ZERO CURRENT", TEXT)
    box(draw, (164, 176, 148, 42), DANGER, DANGER, "DISABLE", TEXT)
    box(draw, (8, 220, 304, 18), text="YAW/PITCH OK", text_color=SUCCESS)
    return image


def theme_preview() -> Image.Image:
    source = ROOT / "tft_theme_1.jpg"
    image = Image.open(source).convert("RGB").resize((320, 240))
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, 319, 39), fill=HEADER)
    draw.rectangle((0, 38, 319, 39), fill=THEME)
    box(draw, (0, 0, 96, 40), HEADER, HEADER, "< BACK", TEXT)
    centered(draw, (96, 0, 128, 40), "THEME 1 / 2", fill=TEXT)
    box(draw, (224, 0, 96, 40), THEME, THEME, "NEXT >", TEXT)
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
    box(draw, (176, 128, 136, 42), SUCCESS, SUCCESS, "确认", TEXT)
    box(draw, (176, 176, 136, 42), DANGER, DANGER, "取消", TEXT)
    return image


def angle_preview() -> Image.Image:
    image = Image.new("RGB", (320, 240), BG)
    draw = ImageDraw.Draw(image)
    header(draw, "SET YAW TARGET")
    labels = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "+/-", "0", "退"]
    for index, label in enumerate(labels):
        row, column = divmod(index, 3)
        box(draw, (8 + column * 52, 36 + row * 45, 44, 42), text=label)

    box(draw, (176, 36, 136, 48), text="-10")
    centered(draw, (176, 92, 136, 28), "YAW -50..50 DEG", SMALL, MUTED)
    box(draw, (176, 128, 136, 42), SUCCESS, SUCCESS, "确认", TEXT)
    box(draw, (176, 176, 136, 42), DANGER, DANGER, "取消", TEXT)
    return image


def main() -> None:
    docs = ROOT / "docs"
    docs.mkdir(exist_ok=True)
    main_preview().save(docs / "ui_main_preview.png")
    cannon_preview().save(docs / "ui_cannon_preview.png")
    charge_preview().save(docs / "ui_charge_preview.png")
    motor_preview().save(docs / "ui_motor_preview.png")
    theme_preview().save(docs / "ui_theme_preview.png")
    keypad_preview().save(docs / "ui_distance_preview.png")
    angle_preview().save(docs / "ui_angle_preview.png")


if __name__ == "__main__":
    main()

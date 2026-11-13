#!/usr/bin/env python3
"""Render the pinned original credits in readable PSP pages without removing any names."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
from html.parser import HTMLParser
import json
from pathlib import Path
import re

from PIL import Image, ImageDraw, ImageFont

from pack_versus_ui import SCALE, pack_images, render_label


class Text(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.parts = []

    def handle_data(self, data):
        self.parts.append(data)

    def handle_starttag(self, tag, attrs):
        if tag.lower() == "br":
            self.parts.append("\n")


def plain(html):
    text = Text()
    text.feed(html)
    return "\n".join(line.strip() for line in "".join(text.parts).splitlines() if line.strip())


def wrap(text, font, width):
    lines = []
    for paragraph in text.splitlines():
        line = ""
        for token in re.split(r"(?<=[、\s])", paragraph):
            if line and font.getlength(line + token) > width:
                lines.append(line.rstrip())
                line = ""
                token = token.lstrip()
            for char in token:
                if font.getlength(line + char) > width:
                    if not line:
                        raise ValueError("A single credits glyph exceeds the page")
                    lines.append(line)
                    line = ""
                line += char
        if line:
            lines.append(line)
    if re.sub(r"\s", "", "".join(lines)) != re.sub(r"\s", "", text):
        raise ValueError("Wrapping changed the original credits text")
    return lines


def build(reference, output, font):
    language_path = reference / "upstream/shared/assets/assets/config/language/zh-CN.json"
    language = json.loads(language_path.read_text())["txt"]
    original = language["credits_stage"]["body"]
    manifest = json.loads((reference / "manifest.json").read_text())
    commit = manifest["main"]["commit"]
    footer = language["game_interface_manager"]["credits_footer"]
    footer = footer.replace("{url}", manifest["main"]["url"] + "/commit/" + commit)
    footer = footer.replace("{label}", "master@" + commit[:7])
    text = plain(original + footer)

    size = round(30 * SCALE)
    face = ImageFont.truetype(str(font), size)
    notdef = face.getmask(chr(0x10ffff))
    unsupported = []
    for char in sorted(set(text)):
        if char.isspace():
            continue
        mask = face.getmask(char)
        if (mask.size, bytes(mask)) == (notdef.size, bytes(notdef)):
            unsupported.append(char)
    if unsupported:
        raise ValueError(f"Credits font does not contain the original characters: {unsupported}")
    width, height = round(740 * SCALE), round(416 * SCALE)
    line_height = round(37 * SCALE)
    lines = wrap(text, face, width - 4)
    per_page = height // line_height
    pages = [lines[index:index + per_page] for index in range(0, len(lines), per_page)]
    if not 1 <= len(pages) <= 31:
        raise ValueError("Credits exceed the page limit")
    images = {
        "title": render_label("制作人员", font, 42),
        "controls": render_label("← / →  翻页    × / ○  返回", font, 28),
    }
    background_path = reference / "flash-source/SRC_XFL/ui/loading/LIBRARY/#链接/BM_coverBackGround.png"
    with Image.open(background_path) as source:
        images["background"] = source.convert("RGBA").resize((round(800 * SCALE), round(600 * SCALE)),
                                                               Image.Resampling.LANCZOS)
    output.mkdir(parents=True, exist_ok=True)
    for index, page in enumerate(pages):
        canvas = Image.new("RGBA", (width, height))
        draw = ImageDraw.Draw(canvas)
        for row, line in enumerate(page):
            box = draw.textbbox((2, row * line_height), line, font=face)
            if box[0] < 0 or box[2] > width or box[3] > height:
                raise ValueError(f"Credits page {index + 1} clips {line}")
            draw.text((2, row * line_height), line, font=face, fill="#ffff00")
        images[f"page-{index}"] = canvas
        images[f"number-{index}"] = render_label(f"{index + 1} / {len(pages)}", font, 26)
        canvas.save(output / f"credits-page-{index + 1}.png")
    pack_images(images, output / "CREDITS.BVU")
    metadata = dict(
        body_html=original, footer_html=footer, plain_text=text, pages=pages,
        source_language_sha256=hashlib.sha256(language_path.read_bytes()).hexdigest(),
        background_sha256=hashlib.sha256(background_path.read_bytes()).hexdigest(),
        original_commit=commit, font_size_px=size, page_pixels=[width, height],
        font_sha256=hashlib.sha256(font.read_bytes()).hexdigest(), unsupported_characters=unsupported,
        platform_adaptation="Original attribution and contact text retained; desktop web links are offline text. Platform donation widgets are not PSP controls.",
    )
    (output / "CREDITS.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n")
    print(f"{len(pages)} credits pages, {len(lines)} lines, original names and contacts preserved")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--reference", type=Path, default=Path("reference"))
    parser.add_argument("--font", type=Path, required=True)
    args = parser.parse_args()
    build(args.reference, args.output, args.font)

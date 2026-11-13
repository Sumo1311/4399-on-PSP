#!/usr/bin/env python3
"""Pack the pinned Flash interface art and PSP labels for one-on-one versus."""

import argparse
import json
import re
import struct
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont
from versus_roster import MISSING, roster, write_grid

SCALE = 272 / 600
RECORD = struct.Struct("<32sI4H")
HEADER = struct.Struct("<8sII")
SPRITE_LIMIT = 1024
LABELS = {
    "title-choose": "↑ / ↓  选择    ×  确认",
    "player-pick": "选择玩家角色",
    "cpu-pick": "选择电脑角色",
    "dummy-pick": "选择陪练角色",
    "player-assist": "选择玩家援助",
    "cpu-assist": "选择电脑援助",
    "dummy-assist": "选择陪练援助",
    "team-cpu": "对电脑",
    "credits": "制作人员",
    "watch-single": "单人观战",
    "watch-team": "小队观战",
    "watch-p1": "P1",
    "watch-p2": "P2",
    "arcade": "闯关",
    "score": "分数",
    "arcade-missing": "本关素材尚未移植",
    "arcade-no-route": "该阵营没有闯关路线",
    "arcade-yes": "继续",
    "arcade-no": "结束",
    "arcade-next": "×  下一关    ○  返回标题",
    "arcade-title": "×  返回标题",
    "player-team": "选择三名玩家队员",
    "cpu-team": "选择三名电脑队员",
    "order-player": "安排玩家出场顺序",
    "order-cpu": "安排电脑出场顺序",
    "order-choose": "↑ / ↓  选择    ×  确认    ○  返回",
    "map-pick": "选择地图",
    "pause": "暂停",
    "resume": "START  继续",
    "quit": "○  返回选人",
    "confirm": "×  确认",
    "choose": "← / →  选择    ×  确认    ○  返回",
    "grid-choose": "方向键  选择    △  形态    ×  确认    ○  返回",
    "assist-choose": "方向键  选择    ×  确认    ○  返回",
    "unavailable": "尚未移植",
    "already-picked": "队员已选",
    "load-error": "无法读取游戏素材",
    "wlan": "双人 WLAN",
    "wlan-single": "创建房间  1 对 1",
    "wlan-team": "创建房间  3 对 3",
    "wlan-join": "加入房间",
    "wlan-search": "正在搜索房间",
    "wlan-wait": "等待另一台 PSP 加入",
    "wlan-connect": "正在连接",
    "wlan-off": "请打开 PSP 的 WLAN 开关",
    "wlan-timeout": "连接超时",
    "wlan-disconnect": "对方已断开",
    "wlan-version": "请在两台 PSP 安装同一版本",
    "wlan-failed": "无法启动 WLAN 房间",
    "wlan-cancel": "○  取消",
    "wlan-back": "× / ○  返回",
    "wlan-leave": "SELECT  断开",
    "wlan-p1": "本机 P1",
    "wlan-p2": "本机 P2",
    "wlan-ready": "已确认",
    "wlan-fighters": "双方选择角色",
    "wlan-assists": "双方选择援助",
    "wlan-teams": "双方各选三名队员",
    "wlan-order": "双方安排出场顺序",
    "wlan-map": "P1 选择地图",
    "wlan-pick-keys": "方向键 选人   △ 形态   × 确认   ○ 撤回   SELECT 断开",
    "wlan-assist-keys": "方向键  选择    ×  确认    ○  返回    SELECT  断开",
    "wlan-order-keys": "↑ / ↓  选择    ×  确认    ○  撤回    SELECT  断开",
    "wlan-map-keys": "← / →  选择    ×  确认    ○  返回    SELECT  断开",
}


def pack_arcade_routes(source, fighters, assists, maps, target):
    fighter_ids = {entry["name"]: i for i, entry in enumerate(fighters)}
    assist_ids = {entry["name"]: i for i, entry in enumerate(assists)}
    map_ids = {name: i for i, name in enumerate(maps)}
    routes = {}
    for route in read_json(source)["level"]:
        key = route["comic_type"], route["game_mode"]
        if key in routes or len(route["stage"]) != 8:
            raise ValueError(f"Invalid arcade route {key}")
        rows = bytearray()
        for stage in route["stage"]:
            foes = [fighter_ids[name] for name in stage["fighter"]]
            if not 1 <= len(foes) <= 3 or len(foes) != len(set(foes)) or stage["map"] == "random":
                raise ValueError(f"Invalid arcade stage {stage}")
            rates = stage.get("properties", {})
            hp, attack = (rates.get(name, 1) for name in ("hp_rate", "attack_rate"))
            if not 0 < hp <= 10 or not 0 < attack <= 10:
                raise ValueError(f"Invalid arcade modifiers {rates}")
            health_percent, attack_percent = round(100 * hp), round(100 * attack)
            if (health_percent / 100, attack_percent / 100) != (hp, attack):
                raise ValueError(f"Arcade modifiers exceed hundredths precision {rates}")
            rows.extend(struct.pack("<8H", *(foes + [MISSING] * (3 - len(foes))),
                                    assist_ids[stage["assistant"]], map_ids[stage["map"]], len(foes),
                                    health_percent, attack_percent))
        routes[key] = rows
    expected = [(faction, mode) for faction in range(2) for mode in range(2)]
    if set(routes) != set(expected):
        raise ValueError("Arcade source must contain one route for each faction and mode")
    target.write_bytes(b"BVNARC01" + b"".join(routes[key] for key in expected))


def pack_arcade_results(xfl, target):
    images = {}
    for key, area, file in (
        ("continue-bg", "game_over", "bg.jpg.png"),
        ("over-bg", "game_over", "bg_gameover.jpg.png"),
        ("over", "game_over", "gameover.png"),
        ("continue", "game_over", "txt1.png"),
        ("winner", "loading", "winer.png"),
        ("clear-bg", "loading", "通关.png"),
    ):
        with Image.open(xfl / area / "LIBRARY" / file) as source:
            images[key] = source.convert("RGBA").resize(
                (round(source.width * SCALE), round(source.height * SCALE)), Image.Resampling.LANCZOS)
    pack_images(images, target)


def alignment(size, quantum):
    return (size + quantum - 1) // quantum * quantum


def pack_images(images, target):
    if not 0 < len(images) <= SPRITE_LIMIT:
        raise ValueError("UI sprite count exceeds its PSP table capacity")
    pixels = bytearray()
    records = bytearray()
    placements = {}
    for key, source in sorted(images.items()):
        name = key.encode("ascii")
        if not name or len(name) > 31:
            raise ValueError(f"Invalid PSP art key {key}")
        identity = id(source)
        if identity in placements:
            records.extend(RECORD.pack(name, *placements[identity]))
            continue
        width, height = source.size
        stride = max(8, 1 << (width - 1).bit_length())
        rows = 1 << (height - 1).bit_length()
        if stride > 512 or rows > 512:
            raise ValueError(f"Oversized PSP art {key}")
        tile = bytearray(alignment(stride * rows * 2, 16))
        source = source.convert("RGBA")
        for y in range(height):
            for x in range(width):
                r, g, b, a = source.getpixel((x, y))
                value = (r >> 4) | ((g >> 4) << 4) | ((b >> 4) << 8) | ((a >> 4) << 12)
                struct.pack_into("<H", tile, (y * stride + x) * 2, value)
        placements[identity] = (len(pixels), width, height, stride, rows)
        records.extend(RECORD.pack(name, *placements[identity]))
        pixels.extend(tile)
    start = alignment(HEADER.size + len(records), 16)
    if len(pixels) > 6 * 1024 * 1024:
        raise ValueError("UI exceeds its PSP memory budget")
    target.write_bytes(HEADER.pack(b"BVNUI001", len(images), start) + records
                       + bytes(start - HEADER.size - len(records)) + pixels)


def render_label(text, font, size):
    face = ImageFont.truetype(font, round(size * SCALE))
    left, top, right, bottom = face.getbbox(text, stroke_width=1)
    image = Image.new("RGBA", (right - left + 2, bottom - top + 2))
    ImageDraw.Draw(image).text((1 - left, 1 - top), text, font=face, fill="#f5f8ff",
                               stroke_width=1, stroke_fill="#19243a")
    return image


def training_highlight(image):
    # TrainingInputHistoryUI.HIGHLIGHT_CT: RGB offsets (50, -30, -30), unchanged alpha.
    r, g, b, a = image.convert("RGBA").split()
    return Image.merge("RGBA", (r.point(lambda x: min(255, x + 50)),
                                g.point(lambda x: max(0, x - 30)),
                                b.point(lambda x: max(0, x - 30)), a))


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def health_face(image, frame, shape):
    edge = ET.parse(shape).find(".//{*}DOMLayer[@layerType='mask']//{*}Edge")
    points = [(float(x) / 20, float(y) / 20) for x, y in re.findall(r"[!|](-?[\d.]+) (-?[\d.]+)", edge.get("edges"))]
    with Image.open(image) as art, Image.open(frame) as border:
        portrait = art.convert("RGBA")
        mask = Image.new("L", portrait.size)
        ImageDraw.Draw(mask).polygon(points, fill=255)
        portrait.putalpha(ImageChops.multiply(portrait.getchannel("A"), mask))
        return Image.alpha_composite(portrait, border.convert("RGBA"))


def ground_cues(library, images):
    cues = (("特效-跳", 8, 128, 40, 24), ("特效-跳-空中", 10, 128, 40, 24), ("特效-落地", 7, 128, 40, 24),
            ("XG_fz_bleach_mc", 11, 128, 160, 128), ("fzccxg", 10, 128, 160, 128),
            ("xg_dash", 20, 224, 96, 64), ("xg_dash_air", 22, 224, 96, 64))
    for cue, (symbol, expected, width, height, origin_y) in enumerate(cues):
        source = ET.parse(library / "_AS链接/元件" / f"{symbol}.xml")
        frames = {}
        for layer in source.findall(".//{*}DOMLayer"):
            if layer.get("visible") == "false" or layer.get("layerType") == "guide":
                continue
            for frame in layer.findall("./{*}frames/{*}DOMFrame"):
                elements = frame.find("./{*}elements")
                if not len(elements):
                    continue
                bitmap, = elements
                matrix = bitmap.find(".//{*}Matrix")
                if not bitmap.tag.endswith("DOMBitmapInstance") or set(matrix.attrib) - {"tx", "ty"}:
                    raise ValueError(f"Unexpected authored geometry in {symbol}")
                with Image.open(library / (bitmap.get("libraryItemName") + ".png")) as image:
                    pos = width // 2 + int(matrix.get("tx", "0")), origin_y + int(matrix.get("ty", "0"))
                    canvas = Image.new("RGBA", (width, height))
                    if min(pos) < 0 or pos[0] + image.width > canvas.width or pos[1] + image.height > canvas.height:
                        raise ValueError(f"The effect canvas clips {symbol}")
                    canvas.paste(image.convert("RGBA"), pos)
                texture = canvas.resize((round(width * SCALE), round(height * SCALE)), Image.Resampling.NEAREST)
                first = int(frame.get("index"))
                for index in range(first, first + int(frame.get("duration", "1"))):
                    if index in frames:
                        raise ValueError(f"Multiple layers on {symbol} frame {index}")
                    frames[index] = texture
        if set(frames) != set(range(expected)):
            raise ValueError(f"Unexpected timeline length for {symbol}")
        images.update({f"ground-{cue}-{index}": texture for index, texture in frames.items()})


def build(reference, target, font):
    images = {}
    fighters, selection = roster(reference)
    assists, _ = roster(reference, assists=True)
    xfl = reference / "flash-source/SRC_XFL/ui"
    assets = reference / "tag-assets/shared/assets/assets/packs"

    def add(key, file, size=None):
        with Image.open(file) as source:
            width, height = size or source.size
            images[key] = source.convert("RGBA").resize((round(width * SCALE), round(height * SCALE)),
                                                          Image.Resampling.LANCZOS)

    title = xfl / "title/LIBRARY"
    add("title", title / "封面.jpg.png")
    add("single-play", title / "single_play.png")
    add("training", title / "training.png")
    add("team-play", title / "team_play.png")
    add("studio", title / "logo_5dplay.png", (82, 51))
    select = xfl / "select/LIBRARY"
    add("select", select / "sbg.jpg.png")
    add("select-item", select / "spitem.png")
    add("select-map", select / "selectbg.png")
    add("select-frame", select / "selectk.png")
    add("select-arrow", select / "sarrow.png")
    loading = xfl / "loading/LIBRARY"
    add("order-bg", loading / "bg.jpg.png", (800, 600))
    add("order-vs", loading / "vs.png")
    for i in range(1, 4):
        add(f"order-number-{i}", loading / f"s{i}.png")
    fight = xfl / "fight/LIBRARY"
    portrait_shape = fight / "#链接/MC_hpBarFace.xml"
    for i, fighter in enumerate(fighters):
        faces, folder = fighter["faces"], fighter["folder"]
        add(f"fighter-icon-{i}", folder / faces["face"], (50, 50))
        images[f"fighter-name-{i}"] = render_label(fighter["title"], font, 20)
        if fighter["file"]:
            art = health_face(folder / faces["face_bar"], fight / "bar_lr.png", portrait_shape)
            images[f"fighter-hud-{i}"] = art.resize((round(102 * SCALE), round(64 * SCALE)), Image.Resampling.LANCZOS)
            add(f"fighter-card-{i}", folder / faces["face_big"], (102, 68))
    for i, assist in enumerate(assists):
        faces, folder = assist["faces"], assist["folder"]
        add(f"assist-icon-{i}", folder / faces["face"], (50, 50))
        images[f"assist-name-{i}"] = render_label(assist["title"], font, 20)
        if assist["file"]:
            add(f"assist-card-{i}", folder / faces["face_big"], (102, 68))

    originals = (
        "bar", "bar_back", "bar_center", "bar_front",
        "sbar", "sbar_back", "sbar_front", "sbar_txt1", "sbar_txt2",
        "qbar_back2", "qbar_border2", "qbar_bar1", "qbar_bar2", "qbar_bar3", "qbar_ready",
        "qbar_outback", "qbar_outborder", "qbar_bar",
        "win_bleach", "win_naruto",
        "txt_round", "txt_fight", "txt_k", "txt_o", "txt_kod", "txt_timeover", "txt_drawgame",
        "txt_winner", "txt_perfect", "timetxt_wx",
    )
    for name in originals:
        add(name, fight / f"{name}.png")
    for index in range(1, 6):
        add(f"r{index}", fight / f"r{index}.png")
    for index in range(10):
        add(f"time-{index}", fight / f"timetxt_{index}.png")

    for key, value in LABELS.items():
        size = 28 if key.startswith("wlan-") and key.endswith("-keys") else 31
        images["label-" + key] = render_label(value, font, size)
    commands = ("□", "↓＋□", "↑＋□", "□", "↓", None, "L", "○", "↓＋○", "↑＋○",
                None, None, None, "○", "R", "↑＋R", "↓＋R", "×", "□＋×", "↑＋□＋×", "↓＋□＋×", "R")
    for index, text in enumerate(commands):
        if text:
            images[f"log-{index}"] = render_label(text, font, 29)
            if index == 6:
                images[f"log-highlight-{index}"] = training_highlight(images[f"log-{index}"])
    for key, text in (("left", "←"), ("right", "→"), ("drop", "↓＋×"), ("assist", "△"), ("sep", "–"),
                      ("ghost-step", "↓＋L"), ("ghost-jump", "↑＋L")):
        images[f"log-{key}"] = render_label(text, font, 29)
        if key == "assist":
            images["log-highlight-assist"] = training_highlight(images["log-assist"])
    images["label-final"] = render_label("FINAL", font, 70)
    for i in range(4):
        add(f"qi-{i}", fight / f"qbar_txt_{i}.png")
    config = read_json(reference / "upstream/shared/assets/assets/config/packs.json")
    for stage in config["maps"]:
        path = assets / "maps" / stage
        add("map-" + stage, path / f"{stage}.png")
        text = read_json(path / "meta.json")["language"]["zh-CN"]["name"]
        images["name-" + stage] = render_label(text, font, 32)
    target.mkdir(parents=True, exist_ok=True)
    ground_cues(xfl.parent / "effect/LIBRARY", images)
    pack_images(images, target / "UI.BVU")
    write_grid(fighters, selection, target / "ROSTER.BVR")
    write_grid(assists, selection, target / "ASSISTS.BVR", assists=True)
    pack_arcade_routes(reference / "upstream/shared/assets/assets/config/mission.json",
                       fighters, assists, config["maps"], target / "ARCADE.BVM")
    pack_arcade_results(xfl, target / "ARCADE.BVU")
    (target / "STAGES.TXT").write_text("".join(stage.upper() + "\n" for stage in config["maps"]), encoding="ascii")
    print(f"{len(images)} interface sprites, {len(config['maps'])} map choices")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--reference", type=Path, default=Path("reference"))
    parser.add_argument("--font", type=Path, required=True)
    args = parser.parse_args()
    build(args.reference, args.output, args.font)

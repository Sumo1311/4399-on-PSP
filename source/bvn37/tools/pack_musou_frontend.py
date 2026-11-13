#!/usr/bin/env python3
"""Pack the original Musou world map and its PSP control labels."""
import argparse
import json
from pathlib import Path
import re
import struct
from xml.etree import ElementTree as ET

from PIL import Image

from pack_musou import build as pack_campaign
from pack_versus_ui import SCALE, pack_images, render_label
from versus_roster import roster

LABELS = {
    "musou": "无双模式",
    "world-control": "方向键  区域    ×  出战    △  队伍    ○  返回",
    "battle-control": "△  换人    START  暂停",
    "result-control": "×  返回世界地图",
    "save-failed": "无法保存进度",
    "save-control": "×  重试    ○  返回",
    "wave": "波次",
    "kills": "击倒",
    "level": "等级",
    "mission": "任务",
    "money": "金币",
    "victory": "任务完成",
    "defeat": "任务失败",
    "timeout": "时间到",
    "locked": "区域未解锁",
    "missing": "该任务素材尚未齐备",
    "error": "无法读取无双素材",
    "team-title": "无双队伍",
    "team-control": "← / →  选队员    ×  更换    △  设为队长    ○  返回",
    "team-hp": "体力",
    "team-qi": "灵力",
    "team-energy": "精力",
    "team-attack": "攻击",
    "experience": "经验",
    "experience-slash": "/",
    "shop-title": "角色列表",
    "shop-coins": "金币不足",
    "shop-missing": "该角色素材尚未齐备",
    "shop-confirm-title": "解锁角色",
    "shop-confirm-control": "×  确认购买    ○  取消",
    "shop-dismiss-control": "× / ○  返回",
    "shop-buy-control": "方向键  选择    L / R  翻页    ×  购买    ○  返回",
    "shop-choose-control": "方向键  选择    L / R  翻页    ×  编入队伍    ○  返回",
    "shop-missing-control": "方向键  选择    L / R  翻页    素材未齐备    ○  返回",
    "shop-back-control": "○  返回",
}


def pack_shop(reference, target):
    model = (reference / "upstream/CORE_KernelLogic/src/net/play5d/game/bvn/data/musou/MusouFighterModel.as")
    offers = re.findall(r"fighters\.push\(new MusouFighterSellVO\('([A-Za-z0-9_]+)',\s*(\d+)\)\);",
                        model.read_text(encoding="utf-8"))
    fighters, _ = roster(reference)
    slots = {entry["name"]: index for index, entry in enumerate(fighters) if not entry["internal"]}
    names = {name for name, _ in offers}
    if len(names) != len(offers) or not {"ichigo", "naruto", "sakura"} <= names <= slots.keys():
        raise ValueError("The Flash Musou shop does not match the fighter catalog")
    data = bytearray(struct.pack("<8s2H", b"BVNSHP01", len(offers), 0))
    for name, price in offers:
        data.extend(struct.pack("<32sI2H", name.encode("ascii"), int(price), slots[name], 0))
    target.write_bytes(data)


def pack_frontend(reference, output, font):
    xfl = reference / "flash-source/SRC_XFL/ui/big_map/LIBRARY"
    config = reference / "upstream/shared/assets/assets/config"
    world = json.loads((config/"musou/map1/map1.json").read_text())
    markers = {}
    for node in ET.parse(xfl/"pointMc.xml").findall(".//{*}DOMSymbolInstance[@name]"):
        matrix = node.find("./{*}matrix/{*}Matrix")
        markers[node.get("name")] = tuple(float(matrix.get(field)) for field in ("tx","ty"))
    order = [edge["P"] for edge in world["way"]]
    if set(markers) != set(order):
        raise ValueError("The Flash world map and mission markers disagree")
    (output/"MUSOU.PTS").write_bytes(b"BVNMAP01"+b"".join(struct.pack("<2f",*markers[name]) for name in order))
    images = {key:render_label(value,font,22 if key.endswith("control") else 28) for key,value in LABELS.items()}
    for name, key in (("bvnmap.png","world"),("p1.png","point"),("p2.png","point-done")):
        with Image.open(xfl/name) as source:
            images[key] = source.convert("RGBA").resize(tuple(round(n*SCALE) for n in source.size), Image.Resampling.LANCZOS)
    boss = reference / "flash-source/SRC_XFL/ui/musou/LIBRARY"
    for name, key in (
        ("BOSS_HP.png", "boss-hp"), ("BOSS_HP_bar.png", "boss-hp-bar"), ("BOSS_HP_shadow.png", "boss-hp-shadow"),
        ("warnbg.png", "boss-warn-bg"), ("warning.png", "boss-warn-text"), ("warn1.png", "boss-warn-strip"),
        ("boss_ko.png", "boss-ko"), ("ko_xian.png", "boss-ko-line"),
    ):
        with Image.open(boss/name) as source:
            images[key] = source.convert("RGBA").resize(tuple(round(n*SCALE) for n in source.size), Image.Resampling.LANCZOS)
    dialog = reference / "flash-source/SRC_XFL/ui/dialog/LIBRARY"
    for name, key in (
        ("dialogbg1.png", "team-panel"), ("formbg.png", "shop-panel"), ("fighterbox.png", "shop-feature"),
        ("facek.png", "shop-face-frame"), ("bigface_star.png", "leader-star"), ("coin_ico.png", "coin-icon"),
        ("confrim-dialog1.png", "shop-confirm"),
    ):
        with Image.open(dialog / name) as source:
            images[key] = source.convert("RGBA").resize(tuple(round(n * SCALE) for n in source.size),
                                                         Image.Resampling.LANCZOS)
    pack_images(images, output/"MUSOUUI.BVU")
    pack_campaign(reference, output/"MUSOU.BVM")
    pack_shop(reference, output/"MUSOUSHOP.BVM")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output",type=Path)
    parser.add_argument("--font",required=True)
    parser.add_argument("--reference",type=Path,default=Path("reference"))
    args = parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    pack_frontend(args.reference,args.output,args.font)

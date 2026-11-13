#!/usr/bin/env python3
"""Compile fighter timelines, detached objects, and lossless textures into BVN packs."""
import argparse
import json
import os
import re
import struct
import zlib
from ctypes import CDLL, c_char_p, c_int, create_string_buffer
from ctypes.util import find_library
from dataclasses import dataclass
from pathlib import Path

from fighter_actions import frame_scripts
from pack_fighter import CLIPS, EMPTY_BOX, DetachedTimeline, FighterTextures, authored_stats, read_timeline
from pack_stage import aligned

HEADER = struct.Struct("<8s4I2H3f")
CLIP = struct.Struct("<4H")
FRAME = struct.Struct("<ffI4H4f2I2H")
HIT = struct.Struct("<4f2H")
STRIKE = struct.Struct("<2f6HI")
EVENT = struct.Struct("<2BH2f")
PROJECTILE = struct.Struct("<16f10H2hf")
ATTACKER = struct.Struct("<3f4HI")
TEXTURE = struct.Struct("<3I")
POSE = struct.Struct("<ffI4H")
RESIDENT_TEXTURE_BUDGET = 8 * 1024 * 1024
STREAMED_TEXTURE_LIMIT = 32 * 1024 * 1024
FINISH = 0xFFFF
CORE_CLIPS = (*CLIPS, "击飞", "击飞_落", "击飞_弹", "击飞_倒", "击飞_起")
BINDINGS = (("setAttack", "砍1"), ("setSkill1", "砍技1"), ("setSkill2", "砍技2"), ("setAttackAIR", "跳砍"),
            ("setDefense", "防御"), (None, "防御恢复"), ("setDash", "瞬步"),
            ("setZhao1", "招1"), ("setZhao2", "招2"), ("setZhao3", "招3"),
            (None, "开场"), (None, "胜利"), (None, "失败"),
            ("setSkillAIR", "跳招"), ("setBisha", "必杀"), ("setBishaUP", "上必杀"),
            ("setBishaSUPER", "超必杀"), ("setJump", "起跳"),
            (None, "万解"), (None, "万解W"), (None, "万解S"), ("setBishaAIR", "空中必杀"))
THROW_BINDINGS = (("setCatch1", "摔1"), ("setCatch2", "摔2"))
AI_RANGES = ("kanmian", "kj1mian", "kj2mian", "tkanmian", None, None, None,
             "zh1mian", "zh2mian", "zh3mian", None, None, None, "tzmian", "bsmian", "sbsmian", "cbsmian", None,
             None, None, None, "kbsmian")
ALL_BINDINGS = BINDINGS + THROW_BINDINGS
COMMANDS = {name: (i, label) for i, (name, label) in enumerate(ALL_BINDINGS) if name}
WALK_FIELDS = {"moveLeft": 1, "moveRight": 2}
WALK_SETTERS = {"setMoveLeft": 1, "setMoveRight": 2, "setMove": 3}
GROUND_ACTION_SETTERS = (
    "setDefense", "setJump", "setDash", "setAttack", "setSkill1", "setSkill2",
    "setZhao1", "setZhao2", "setZhao3", "setCatch1", "setCatch2",
    "setBisha", "setBishaUP", "setBishaSUPER",
)
OPS = ("velocity", "damping", "allow", "end", "gravity", "steel", "qi", "airMove", "shake", "shine",
       "dash", "dashStop", "fire", "summon", "cutin", "endCutin", "landing", "goto",
       "branchHit", "qiOnHit", "probe", "moveOnce", "moveToTarget", "selfLaunch", "remove",
       "switchForm", "ifHit", "ifRange", "ifHurt", "skip", "cross", "faceTarget", "slow", "shakeLoop",
       "hurtAction", "recover", "ifAttacker", "stageCue", "jumpCount", "attackerLanding", "attackerPause",
       "ifHealth", "loseHealth", "loseEnergy", "targetAnchor", "moveTarget", "ifTarget", "targetVisible",
       "attackerProbe", "attackerGoto", "attackerRange", "attackerCutin", "ownerBuff", "ownerHeal", "targetHealthPower",
       "attackerCapture", "attackerMiss", "attackerRelease", "healPercent", "controlAttacker", "attackerQiCap",
       "ifFlight", "flightMode", "flightPose", "targetVelocity", "rememberAttacker", "controlRemembered", "attackerWall",
       "attackerAlignHitY", "attackerAnchor", "attackerFollow", "attackerShowTarget", "attackerBehind",
       "targetDamping", "timelineValue", "ifTimelineValue", "timelineQi", "ifEnergy", "strikeReaction", "airAttackLock",
       "groundCancel", "counterPosition", "walkCancel", "screenPose", "bankaiPose", "comboLimit", "hittable",
       "attackerStopTracking", "swapForm", "mapBackdrop", "attackerPushOut", "ulquiorraDashSound", "gainEnergy", "fighterBuff", "ifDisplayOverlap", "targetOffset", "attackerStopTargetAction", "attackerAllowTargetGuard",
       "timelineMove", "kamui", "ifWaterInput", "waterWalk", "hinataState",
       "followAttackerY", "attackerRecoverOwner", "attackerWaterEnergy", "waterPose", "twinLionPose", "ifShunko",
       "facingPose", "rememberFacing", "transferQi", "assistWind", "assistSwap", "attackerHurtBind",
       "attackerHoming", "attackerRemoveAtSide", "attackerRandomGoto", "attackerRandomMove", "shadow", "endShadow", "walk")
STAGE_CUES = ("jump", "jumpAir", "touchFloor", "dash")
AIZEN_WALL_CLASS = "aizen_fla.dankon_gif_22"
CALL = re.compile(r'parent\.(\$(?:mc|effect|fighter)_ctrler)\.(\w+)\((.*)\)', re.S)
ATTACKER_CALL = re.compile(r'(?:this\.)?\$attacker_ctrler\.(\w+(?:\.\w+)?)\((.*)\)', re.S)
ATTACKER_JUST_HIT = re.compile(r'this\.\$attacker_ctrler\.justHit\(("[^"]+")\)')
ALIGN_HIT_Y = re.compile(
    r'if\(this\.\$attacker_ctrler\.justHit\(("[^"]+")\)\)\{'
    r'(this\.\$attacker_ctrler\.owner_fighter_ctrler\.target\.visible=false;)?'
    r'this\.\$attacker_ctrler\.owner_fighter_ctrler\.getTargetSP\(\)\.y=this\.y;\}')
ASSIST_CALL = re.compile(r'(?:this\.)?\$assisterCtrler\.(\w+(?:\.\w+)?)\((.*)\)', re.S)
ASSIST_BUFF = re.compile(r'(?:this\.)?\$assisterCtrler\.getOwner\(\)\.getBuffCtrl\(\)\.(speedUp|attackUp|defenseUp)\((.*)\)', re.S)
ASSIST_HEAL = re.compile(r'(?:this\.)?\$assisterCtrler\.getOwner\(\)\.addHp\((this\.\w+|\d+(?:\.\d+)?)\)')
ASSIST_OWNER_DAMAGE = re.compile(r'(?:this\.)?\$assisterCtrler\.getOwner\(\)\.loseHp\((this\.\w+|\d+(?:\.\d+)?)\)')
ASSIST_OWNER_MOVE = re.compile(r'(?:this\.)?\$assisterCtrler\.getOwner\(\)\.getCtrler\(\)\.moveOnce\((.*)\)', re.S)
ASSIST_OWNER_PROBE = re.compile(r'(?:this\.)?\$assisterCtrler\.checkHitOwner\(("[^"]+")\)')
ASSIST_HEALTH_POWER = re.compile(
    r'this\.\$assisterCtrler\.hitModel\.getHitVO\(("[^"]+")\)\.power\s*=\s*'
    r'this\.\$assisterCtrler\.getOwner\(\)\.getCtrler\(\)\.getTargetSP\(\)\.hpMax\s*\*\s*this\.(\w+)')
CHILD_REQUEST = re.compile(r'this\.\$attacker_ctrler\.(fire|setHitTarget)\(\s*("[^"]+")')
ACTION_ASSIGN = re.compile(r'parent\.\$mc_ctrler\.getAction\(\)\.(\w+)\s*=\s*(null|"[^"]+")')
JUMP_COUNT = re.compile(r'parent\.\$mc_ctrler\.getAction\(\)\.jumpTimes\s*=\s*([0-9]+)')
TIMELINE_GOTO = re.compile(r'gotoAndPlay\(("[^"]+")\)')
FRAME_SWAP = re.compile(r'parent\.gotoAndStop\(([2-9])\)')
ROOT_NEXT_FRAME = "parent.$fighter_ctrler.self.nextFrame()"
ALIAS = re.compile(r'var\s+(_temp_\d+)\s*:\s*\*\s*=\s*(parent(?:\.\$(?:mc|effect|fighter)_ctrler)?|'
                   r'this\.\$(?:attacker_ctrler|assisterCtrler)(?:\.effect)?)\s*')
STATEMENT_TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/|[{};]', re.S)
EMPTY_SWITCH = re.compile(r"switch\s*\(\s*\d+\s*\)\s*\{\s*\}")
TEMP_DECL = re.compile(r"var\s+(_temp_\d+)\s*:\s*\*\s*=\s*(.+)", re.S)
STRING = r'"(?:\\.|[^"\\])*"'
TEMP_TOKEN = re.compile(STRING + r"|\b_temp_\d+\b")
OPERAND = rf"(?:{STRING}|\b_temp_\d+\b|[+-]?(?:\d+(?:\.\d*)?|\.\d+))"
LITERAL_EXPR = re.compile(rf"{OPERAND}(?:\s*(?:>>>|>>|<<|[+\-*/%&|^])\s*{OPERAND})?")
EMPTY_CLOSURE = re.compile(r"function\s*\(\s*\)\s*:\s*\*\s*\{\s*\}")
JUST_HIT = re.compile(
    r'if\s*\(\s*parent\.\$fighter_ctrler\.justHit\(("[^"]+")\)\s*\)\s*\{([^{}]*)\}'
    r'(?:\s*else\s*\{([^{}]*)\})?', re.S)
CONDITIONAL = re.compile(r'if\s*\((.*?)\)\s*\{', re.S)
TARGET = 'parent.$fighter_ctrler.target'
TARGET_SPRITE = 'parent.$fighter_ctrler.getTargetSP()'
TARGET_GROUNDED = TARGET + " && !" + TARGET + ".isInAir"
TARGET_IN_AIR = 'parent.$fighter_ctrler.getTargetSP().isInAir'
TARGET_STEEL = 'parent.$fighter_ctrler.getTargetSP().isSteelBody'
TARGET_VISIBLE = re.compile(re.escape(TARGET) + r'\.visible\s*=\s*(true|false)')
TARGET_LOCAL = re.compile(r'this\.(\w+)\s*=\s*parent\.\$fighter_ctrler\.getTargetSP\(\)')
DISPLAY_OVERLAP = re.compile(
    r'this\.(\w+)\.hitTestObject\(\s*'
    rf'(?:{re.escape(TARGET)}|{re.escape(TARGET_SPRITE)}\.getDisplay\(\))\s*\)')
TARGET_OFFSET = re.compile(
    rf'{re.escape(TARGET_SPRITE)}\.(x|y)\s*=\s*{re.escape(TARGET_SPRITE)}\.\1\s*([+-])\s*(\d+(?:\.\d+)?)')
TARGET_GRAVITY = re.compile(
    rf'{re.escape(TARGET_SPRITE)}\.getCtrler\(\)\.getMcCtrl\(\)\.isApplyG\((true|false)\)')
OFFSET_LOCAL = re.compile(r'this\.(\w+)\s*=\s*(-?\d+(?:\.\d+)?)')
HIT_TEST = re.compile(r'parent\.\$fighter_ctrler\.justHit\((.*)\)', re.S)
CACHED_HIT = re.compile(
    r'\b(this\.\w+)\s*=\s*(parent\.\$fighter_ctrler\.justHit\("[^"]+"\))\s*;\s*if\s*\(\s*\1\s*\)')
RANGE_TEST = re.compile(r'parent\.\$fighter_ctrler\.targetInRange\((.*)\)', re.S)
HURT_TEST = re.compile(r'parent\.\$fighter_ctrler\.(?:getTargetState\(\)|getTargetSP\(\)\.actionState)\s*(==|!=)\s*(0|20|21|22)')
HEALTH_TEST = re.compile(r'parent\.\$fighter_ctrler\.hp\s*>\s*(\d+(?:\.\d+)?)')
RETURN_TARGET = re.escape("parent.$fighter_ctrler.getTargetSP().getCurrentTarget()")
FIGHTER_BUFF = re.compile(
    rf'({re.escape(TARGET_SPRITE)}|{RETURN_TARGET})\.getBuffCtrl\(\)\.(speedDown|attackUp|speedUp)'
    r'\(\s*(\d+(?:\.\d+)?),\s*(\d+(?:\.\d+)?)\s*\)')
RETURN_TARGET_ENERGY_TEST = re.compile(rf'{RETURN_TARGET}\.energy\s*(<=?|>=)\s*(\d+(?:\.\d+)?)')
RETURN_TARGET_ENERGY_GAIN = re.compile(
    rf'{RETURN_TARGET}\.energy\s*=\s*{RETURN_TARGET}\.energy\s*\+\s*(\d+(?:\.\d+)?)')
SAVE_COUNTER_POSITION = re.compile(
    rf'this\.(kj2|zh2)x\s*=\s*{RETURN_TARGET}\.x;\s*this\.\1y\s*=\s*{RETURN_TARGET}\.y;')
RESTORE_COUNTER_POSITION = re.compile(
    rf'{RETURN_TARGET}\.x\s*=\s*this\.(kj2|zh2)x;\s*{RETURN_TARGET}\.y\s*=\s*this\.\1y;')
COUNTER_POSITION = re.compile(r'@(save|restore)CounterPosition (kj2|zh2)')
STRIKE_REACTION = re.compile(
    r'parent\.\$fighter_ctrler\.hitModel\.getHitVO\(("[^"]+")\)\.(hitx|hurtType|powerRate|isBreakDef|power|hitType|hurtTime)\s*=\s*(-?\d+(?:\.\d+)?|true|false)')
ATTACKER_TEST = re.compile(r'(!?)parent\.\$mc_ctrler\.getAttacker\(("[^"]+")\)')
ATTACKER_GOTO = re.compile(r'parent\.\$mc_ctrler\.getAttacker\(("[^"]+")\)\.gotoAndPlay\(("[^"]+")\)')
ATTACKER_REMOVE = re.compile(r'parent\.\$mc_ctrler\.getAttacker\(("[^"]+")\)\.removeSelf\(\)')
ATTACKER_LOCAL = re.compile(r'this\.(\w+)\s*=\s*parent\.\$mc_ctrler\.getAttacker\(("[^"]+")\)')
REMEMBERED_TEST = re.compile(r'this\.(\w+)')
REMEMBERED_GOTO = re.compile(r'this\.(\w+)\.gotoAndPlay\(("[^"]+")\)')
FLIGHT_MODE = re.compile(r'this\.flyMode\s*=\s*(true|false)')
TIMELINE_COUNTERS = {"kCount": 4, "skillCount": 5, "shihouCount": 6, "dashCount": 7, "skipcbs": 8,
                     "yh": 9, "soushikenn": 10, "soushikennCD": 11, "cloneDistance": 12, "sbsCount": 13,
                     "sandHit": 14}
TIMELINE_FIELDS = {"Loop_num": 0, "loops": 0, "isBeHitByWu": 1, "u_add_qi": 2, **TIMELINE_COUNTERS}
TIMELINE_SET = re.compile(r'this\.(' + "|".join(TIMELINE_FIELDS) + r')\s*=\s*(-?\d+(?:\.\d+)?|true|false)')
FORCE_HURT_ACTION = re.compile(
    r'parent\.\$mc_ctrler\.getAction\(\)\.hurtAction\s*=\s*("[^"]+")\s*;'
    r'\s*parent\.\$mc_ctrler\.beHit\(null\)\s*;')
MAP_BACKDROP = re.compile(r'@mapBackdrop (true|false)(?: (\d+))?')
BANKAI_FLAG_SET = re.compile(r'parent\.wanKai\s*=\s*(true|false)')
COMBO_LIMITS = (("K2", "砍4"), ("Kj1", "招22"), ("Kj2", "招22"))
COMBO_TIMER = re.compile(
    r'\+\+this\.mugenCombo(K2|Kj1|Kj2)\s*;\s*this\.\$self_main\.delayCall\(this\.antiMugenCombo,\s*1\)\s*;')
COMBO_EVENT = re.compile(r'@comboLimit (K2|Kj1|Kj2)')
HITTABLE = re.compile(rf'(this\.\$self_main|{re.escape(TARGET_SPRITE)})\.isAllowBeHit\s*=\s*(true|false)')
HAS_ENERGY = re.compile(r'this\.\$self_main\.hasEnergy\(\s*(\d+(?:\.\d+)?)(?:,\s*(true|false))?\s*\)')
ENERGY_AND_CALL = re.compile(r'(' + HAS_ENERGY.pattern + r')\s*&&\s*(parent\.\$mc_ctrler\.\w+\(.*\))')
USE_ENERGY = re.compile(r'this\.\$self_main\.useEnergy\(\s*(\d+(?:\.\d+)?)\s*\)')
TIMELINE_NUMERIC = r'(Loop_num|loops|' + "|".join(TIMELINE_COUNTERS) + r')'
TIMELINE_STEP = re.compile(r'(\+\+|--)this\.' + TIMELINE_NUMERIC)
TIMELINE_COMPARE = re.compile(r'(\+\+)?(?:this|parent)\.' + TIMELINE_NUMERIC + r'\s*(==|!=|<=|>=|<|>)\s*(\d+)')
TIMELINE_SWITCH = re.compile(r"switch\s*\(\s*(this\.kCount|parent\.yh)\s*\)\s*\{(.*)\}", re.S)
TIMELINE_CASE = re.compile(r"\b(?:case\s+(\d+)|(default))\s*:")
CASE_BREAK = re.compile(r"\bbreak\s*;\s*$")
KAKASHI_ELEMENT_SET = re.compile(r"parent\.yh\s*=\s*([0-3])")
KAKASHI_CLONE_MOVE = re.compile(r"parent\.\$fighter_ctrler\.moveOnce\(parent\.dist\s*\*\s*20\s*/\s*3(\s*\*\s*-1)?,\s*0\)")
WATER_INPUT = "Boolean(this.$key.skill2())||Boolean(this.$key.moveLEFT())||Boolean(this.$key.moveRIGHT())"
HINATA_LOOP = 'this.sbsCount<7&&parent.$fighter_ctrler.getTargetState()==21'
FOLLOW_ATTACKER_Y = re.compile(r'@followAttackerY ("[^"]+") (-?\d+)')
SAND_FOLLOW_HANDLER = (
    "functionfollow(param1:Event):*{if(!this.move_mc){return;}"
    "var_loc2_:*=" + "this.$attacker_ctrler.owner_fighter_ctrler.getTargetSP();"
    "_loc2_.x=this.x;_loc2_.y=this.y+this.move_mc.y;}"
)
RATE = re.compile(r'(-?(?:\d+(?:\.\d*)?|\.\d+))\s*/\s*parent\.\$fighter_ctrler\.speed')
VELOCITY_WITH_OFFSET = re.compile(rf'(\d+(?:\.\d+)?)\s*\+\s*{RATE.pattern}\s*,\s*0')
FACE = re.compile(r'\.defineBishaFace\(\s*("[^"]+")\s*,\s*(\w+)\s*\)')


@dataclass(frozen=True)
class ClipPlan:
    label: str
    start: int
    end: int
    repeat_from: int = 0
    held: bool = False
    exit_to_idle: bool = False
    hold_last: bool = False


def split_statements(body):
    depth, start = 0, 0
    for token in STATEMENT_TOKEN.finditer(body):
        if token[0] == "{":
            depth += 1
        elif token[0] == "}":
            depth -= 1
            if depth == 0 and re.match(r"\s*(?:if|switch)\b", body[start:token.start()]) and not re.match(
                    r"\s*else\b", body[token.end():]):
                yield body[start:token.end()].strip()
                start = token.end()
        elif token[0] == ";" and depth == 0:
            if statement := body[start:token.start()].strip():
                yield statement
            start = token.end()
    if statement := body[start:].strip():
        yield statement


def without_dead_temporaries(body):
    # FFDec exposes the SWF's obfuscation arithmetic as local temporary assignments.
    # An unused primitive expression or empty closure has no effect on the frame.
    primitives = set()
    locals_without_effects = set()
    lines = []
    for statement in split_statements(body):
        names = {token[0] for token in TEMP_TOKEN.finditer(statement) if token[0][0] != '"'}
        if statement in locals_without_effects:
            continue
        if decl := TEMP_DECL.fullmatch(statement):
            name, expression = decl.groups()
            dependencies = {token[0] for token in TEMP_TOKEN.finditer(expression) if token[0][0] != '"'}
            primitive = LITERAL_EXPR.fullmatch(expression) and dependencies <= primitives
            if name not in locals_without_effects and (primitive or EMPTY_CLOSURE.fullmatch(expression)):
                locals_without_effects.add(name)
                if primitive:
                    primitives.add(name)
                lines.append((statement, name, dependencies))
                continue
        primitives.difference_update(names)
        lines.append((statement, None, names))

    live = set()
    kept = []
    for statement, name, dependencies in reversed(lines):
        if name is not None and name not in live:
            continue
        kept.append(statement)
        live.update(dependencies)
    return reversed(kept)


def statements(body):
    aliases = {}
    literals = {}
    # Some Flash timelines store justHit immediately before branching on it.
    body = CACHED_HIT.sub(r"if(\2)", body)
    for statement in without_dead_temporaries(body):
        if EMPTY_SWITCH.fullmatch(statement):
            continue
        if choice := TIMELINE_SWITCH.fullmatch(statement):
            selector, cases_body = choice[1], choice[2].strip()
            cases = list(TIMELINE_CASE.finditer(cases_body))
            if not cases or cases[0].start() or len({case[1] for case in cases}) != len(cases):
                raise ValueError(f"Uncompiled timeline switch {statement!r}")
            branches = []
            default = ""
            for i, case in enumerate(cases):
                next_case = cases[i + 1].start() if i + 1 < len(cases) else len(cases_body)
                branch = cases_body[case.end():next_case].strip()
                if i + 1 < len(cases) and not CASE_BREAK.search(branch):
                    raise ValueError(f"Uncompiled timeline switch fallthrough {statement!r}")
                branch = CASE_BREAK.sub("", branch)
                if case[2]:
                    if i + 1 != len(cases):
                        raise ValueError(f"Unsupported default branch position {statement!r}")
                    default = branch
                else:
                    branches.append((case[1], branch))
            if not default:
                for value, branch in branches:
                    yield f"if({selector} == {value}){{{branch}}}"
                continue
            for value, branch in reversed(branches):
                otherwise = f"else{{{default}}}" if default else ""
                default = f"if({selector} == {value}){{{branch}}}{otherwise}"
            yield default
            continue
        if gated := ENERGY_AND_CALL.fullmatch(statement):
            yield f"if({gated[1]}){{{gated[4]};}}"
            continue
        if (branch := conditional_parts(statement)) and branch[0].strip() == "!Boolean(this)":
            # Frame methods run on a MovieClip instance. The ABC's false path is unreachable.
            if branch[2]:
                yield from statements(branch[2])
            continue
        alias = ALIAS.fullmatch(statement)
        if alias:
            aliases[alias[1]] = alias[2]
        else:
            if decl := TEMP_DECL.fullmatch(statement):
                name, value = decl.groups()
                # Inline only a single literal use in an argument list.
                if (re.fullmatch(r'-?\d+(?:\.\d+)?|true|false|null', value)
                        and len(re.findall(rf"\b{name}\b", body)) == 2
                        and re.search(rf"(?<=[,(])\s*{name}\s*(?=[,)])", body)):
                    literals[name] = value
                    continue
            if aliases or literals:
                statement = TEMP_TOKEN.sub(lambda token: aliases.get(token[0], literals.get(token[0], token[0])),
                                           statement)
            yield statement


def selected_frames(clips):
    return sorted({frame for clip in clips for frame in range(clip.start, clip.end)})


def block_end(statement, start):
    depth = 1
    for token in STATEMENT_TOKEN.finditer(statement, start):
        if token[0] == "{":
            depth += 1
        elif token[0] == "}":
            depth -= 1
            if depth == 0:
                return token.start()
    raise ValueError(f"Unclosed conditional block: {statement!r}")


def conditional_parts(statement):
    if not (condition := CONDITIONAL.match(statement)):
        return None
    start = condition.end()
    end = block_end(statement, start)
    yes, tail = statement[start:end], statement[end + 1:].strip()
    if not tail:
        return condition[1], yes, None
    if otherwise := re.match(r"else\s*\{", tail):
        end = block_end(tail, otherwise.end())
        if not tail[end + 1:].strip():
            return condition[1], yes, tail[otherwise.end():end]
    if re.match(r"else\s+if\s*\(", tail):
        nested = tail[4:].lstrip()
        if conditional_parts(nested):
            return condition[1], yes, nested
    return None


def changes_timeline(statement):
    if test := JUST_HIT.fullmatch(statement):
        return test[3] is not None and all(
            any(changes_timeline(inner) for inner in statements(branch)) for branch in test.group(2, 3))
    call = CALL.fullmatch(statement)
    return statement == ROOT_NEXT_FRAME or TIMELINE_GOTO.fullmatch(statement) is not None or FRAME_SWAP.fullmatch(statement) is not None or (
        call is not None and call[2] in ("loop", "gotoAndPlay", "hurtFly", "doWanKai", "idle"))


def nested_statements(body):
    for statement in statements(body):
        if condition := conditional_parts(statement):
            _, yes, no = condition
            yield from nested_statements(yes)
            if no:
                yield from nested_statements(no)
        else:
            yield statement


def clip_references(body):
    for statement in nested_statements(body):
        if goto := TIMELINE_GOTO.fullmatch(statement):
            yield json.loads(goto[1])
            continue
        if (assign := ACTION_ASSIGN.fullmatch(statement)) and assign[2] != "null":
            yield json.loads(assign[2])
            continue
        call = CALL.fullmatch(statement)
        if not call or call[1] != "$mc_ctrler":
            continue
        name, args = call[2], call[3]
        if name in COMMANDS:
            values = json.loads(f"[{args}]")
            if values and isinstance(values[0], str):
                yield values[0]
        elif name in ("gotoAndPlay", "loop", "idle", "setTouchFloor", "setHitTarget", "justHitToPlay", "setHurtAction"):
            values = json.loads(f"[{args}]")
            index = 1 if name in ("setHitTarget", "justHitToPlay") else 0
            if len(values) > index and isinstance(values[index], str):
                yield values[index]


def clip_plan(timeline, scripts, extra, full=False, first_attack="砍1", only=None):
    labels = timeline.labels
    normal = sorted((name for name in labels if re.fullmatch(r"砍[2-9]\d*", name)), key=labels.__getitem__)
    defensive = ("防御", "防御恢复") if "防御" in labels and "防御恢复" in labels else ()
    dash = ("瞬步",) if "瞬步" in labels else ()
    defaults = [label for _, label in ALL_BINDINGS if label in labels and label not in ("防御", "防御恢复")]
    core = tuple(first_attack if name == "砍1" else name for name in CORE_CLIPS)
    escapes = ("起身",) if full and "起身" in labels else ()
    names = list(dict.fromkeys((*core, *normal, *defensive, *dash, *(defaults if full else ()), *extra, *escapes)))
    if only is not None:
        names = list(only)
    held = {"站立", "起跳", "跳", "跳中", "落", "击飞", "击飞_弹", "击飞_倒", "防御恢复"}
    clips = []
    for name in names:
        start = labels.get(name, 0)
        if not start:
            if name not in CORE_CLIPS[9:]:
                raise ValueError(f"Missing timeline label {name}")
            clips.append(ClipPlan(name, 0, 0))
            continue
        boundary = min((frame for frame in labels.values() if frame > start), default=timeline.frame_count + 1)
        if name in held:
            clips.append(ClipPlan(name, start, start + 1, held=True))
        elif name == "被打":
            clips.append(ClipPlan(name, start, min(start + 8, boundary)))
        elif name == "击飞_落":
            clips.append(ClipPlan(name, start, min(start + 3, boundary)))
        elif name == "防御":
            clips.append(ClipPlan(name, start, min(start + 4, boundary)))
        elif name in ("胜利", "失败"):
            stop = next((frame for frame in range(start, boundary)
                         if list(statements(scripts.get(frame, ""))) == ["parent.$mc_ctrler.stop()"]), None)
            if stop is not None:
                clips.append(ClipPlan(name, start, stop + 1))
            else:
                loop_label = name + "_loop"
                repeat = labels.get(loop_label)
                if repeat != boundary:
                    raise ValueError(f"Missing stop or loop for {name}")
                limit = min((frame for frame in labels.values() if frame > repeat), default=timeline.frame_count + 1)
                loop = next((frame for frame in range(repeat, limit) if
                             list(statements(scripts.get(frame, ""))) == [f'parent.$mc_ctrler.loop("{loop_label}")']), None)
                if loop is None:
                    raise ValueError(f"Missing loop frame for {name}")
                clips.append(ClipPlan(name, start, loop, repeat_from=repeat))
        else:
            for frame in range(start, timeline.frame_count + 1):
                body = list(statements(scripts.get(frame, "")))
                if name == "走":
                    if body == ['parent.$mc_ctrler.loop("走")']:
                        clips.append(ClipPlan(name, start, frame))
                        break
                    if 'parent.$mc_ctrler.loop("走")' in body:
                        clips.append(ClipPlan(name, start, frame + 1))
                        break
                    if "parent.$mc_ctrler.stop()" in body:
                        clips.append(ClipPlan(name, start, frame + 1, hold_last=True))
                        break
                    if frame >= boundary:
                        raise ValueError("The walk timeline has no authored loop")
                    continue
                if body == ["parent.$mc_ctrler.idle()"]:
                    clips.append(ClipPlan(name, start, frame, exit_to_idle=True))
                    break
                if "parent.$mc_ctrler.stop()" in body:
                    clips.append(ClipPlan(name, start, frame + 1, hold_last=True))
                    break
                # A label by itself does not stop playback. Branch and landing loops are frame scripts.
                if any(changes_timeline(stmt) for stmt in body):
                    clips.append(ClipPlan(name, start, frame + 1))
                    break
            else:
                raise ValueError(f"Cannot resolve the end of {name} starting at frame {start}")
        if full:
            for frame in range(clips[-1].start, clips[-1].end):
                for target in clip_references(scripts.get(frame, "")):
                    if target not in names and target in labels:
                        names.append(target)
    return clips


def detached_key(kind, frame, values):
    if not 1 <= len(values) <= 2 or not isinstance(values[0], str) or values[0].strip() != values[0]:
        raise ValueError(f"Invalid detached object call on frame {frame}: {values}")
    params = values[1] if len(values) == 2 and values[1] is not None else {}
    return (kind, frame, values[0], json.dumps(params, sort_keys=True)), params


def detached_calls(clips, scripts, assist=False):
    launches = {}
    for frame in selected_frames(clips):
        for statement in nested_statements(scripts.get(frame, "")):
            call = ASSIST_CALL.fullmatch(statement) if assist else CALL.fullmatch(statement)
            if not call or (not assist and call[1] != "$mc_ctrler"):
                continue
            action, args = call.groups()[-2:]
            if action not in ("fire", "addAttacker"):
                continue
            values = json.loads(f"[{args}]")
            kind = "projectile" if action == "fire" else "attacker"
            key, params = detached_key(kind, frame, values)
            launches[key] = params
    return launches


def child_requests(scripts):
    for path in sorted(scripts.rglob("*.as")):
        source = path.read_text(encoding="utf-8")
        follow = "this.move_mc.y" in source and SAND_FOLLOW_HANDLER in re.sub(r"\s+", "", source)
        symbol = ".".join(path.relative_to(scripts).with_suffix("").parts)
        if symbol == AIZEN_WALL_CLASS:
            yield "childProbe", 5, symbol, "wallBody"
        if not follow and not CHILD_REQUEST.search(source):
            continue
        if follow:
            yield "childAnchor", 0, symbol, "move_mc"
        for frame, body in frame_scripts(path):
            for statement in nested_statements(body):
                if match := CHILD_REQUEST.match(statement):
                    kind = "childProjectile" if match[1] == "fire" else "childProbe"
                    yield kind, frame, symbol, json.loads(match[2])


def child_projectiles(attacker_scripts):
    launches = {}
    for owner, commands in (attacker_scripts or {}).items():
        for phase, body in commands.items():
            for statement in statements(body):
                if not (call := ATTACKER_CALL.fullmatch(statement)) or call[1] != "fire":
                    continue
                values = json.loads(f"[{call[2]}]")
                if not 1 <= len(values) <= 2 or not isinstance(values[0], str):
                    raise ValueError(f"Invalid child projectile call in {owner}, frame {phase}: {statement}")
                key = (*owner, phase, values[0])
                if key in launches:
                    raise ValueError(f"Repeated child projectile {key}")
                launches[key] = (values[1] if len(values) == 2 else None) or {}
    return launches


def registered_faces(initializer):
    return {json.loads(label): symbol for label, symbol in FACE.findall(initializer)}


def move_target(values):
    if not values or values == [None]:
        return None, False, (0, 0)
    if len(values) != 1 or not isinstance(values[0], dict):
        raise ValueError(f"Invalid target movement: {values}")
    params = values[0]
    name = params.get("followmc")
    if not isinstance(name, str) or not name or set(params) - {"followmc", "speed"}:
        raise ValueError(f"Uncompiled target movement: {values}")
    speed = params.get("speed")
    if isinstance(speed, dict):
        if set(speed) - {"x", "y"}:
            raise ValueError(f"Invalid target movement speed: {speed}")
        axes = tuple(0 if speed.get(axis) is None else speed[axis] for axis in ("x", "y"))
        limited = True
    else:
        axes = (speed or 0, speed or 0)
        limited = bool(speed)
    if any(type(value) not in (int, float) or value < 0 for value in axes):
        raise ValueError(f"Invalid target movement speed: {speed}")
    return name, limited, axes


def target_anchors(clips, scripts):
    names = set()
    for frame in selected_frames(clips):
        for statement in nested_statements(scripts.get(frame, "")):
            if (call := CALL.fullmatch(statement)) and call.group(1, 2) == ("$mc_ctrler", "moveTarget"):
                name, _, _ = move_target(json.loads(f"[{call[3]}]"))
                if name:
                    names.add(name)
    return sorted(names)


def special_art(clips, scripts, initializer):
    faces = registered_faces(initializer)
    used_faces, probes = {}, set()
    for frame in selected_frames(clips):
        body = scripts.get(frame, "")
        probes.update(match[1] for match in DISPLAY_OVERLAP.finditer(body))
        for statement in nested_statements(body):
            if not (call := CALL.fullmatch(statement)) or call[2] not in ("bisha", "startWanKai", "setHitTarget"):
                continue
            values = json.loads(f"[{call[3]}]")
            if call[2] == "setHitTarget":
                probes.add(values[0])
            elif name := (values[1] if call[2] == "bisha" and len(values) > 1 else
                          values[0] if call[2] == "startWanKai" and values else None):
                # FighterEffectCtrl.getFace returns null for an unregistered label.
                if name in faces:
                    used_faces[name] = faces[name]
    return used_faces, sorted(probes)


def projectile_axis(params, default):
    if isinstance(params, dict):
        if set(params) - {"start", "add", "min", "max", "hit"}:
            raise ValueError(f"Unknown projectile axis parameters: {params}")
        return (params.get("start", default), params.get("add", 0), params.get("min", -999),
                params.get("max", 999), params.get("hit", float("nan")))
    return (params or default, 0, -999, 999, float("nan"))


def inline_target_offset(body):
    """The target and height locals are both assigned before the offset is consumed."""
    lines = list(statements(body))
    if len(lines) < 4 or not (target := TARGET_LOCAL.fullmatch(lines[0])) or not (air := OFFSET_LOCAL.fullmatch(lines[1])):
        return lines
    condition = conditional_parts(lines[2])
    test = f"Boolean(this.{target[1]}) && !this.{target[1]}.isInAir"
    if not condition or condition[0].strip() != test or condition[2] is not None:
        return lines
    yes = list(statements(condition[1]))
    if len(yes) != 1 or not (ground := OFFSET_LOCAL.fullmatch(yes[0])) or ground[1] != air[1]:
        return lines
    call = CALL.fullmatch(lines[3])
    if not call or call.group(1, 2) != ("$fighter_ctrler", "moveToTarget"):
        return lines
    args = call[3].split(",")
    if len(args) != 3 or args[1].strip() != f"this.{air[1]}":
        return lines
    prefix = "parent.$fighter_ctrler.moveToTarget"
    stand = f"{prefix}({args[0]},{ground[2]},{args[2]});"
    airborne = f"{prefix}({args[0]},{air[2]},{args[2]});"
    return [f"if({TARGET_GROUNDED}){{{stand}}}else{{{airborne}}}", *lines[4:]]


def events_for_frame(body, stats, clip_ids, deferred, deferred_effects, frame, object_ids,
                     face_clips, probe_ids, anchor_ids, strike_id, attacker_labels, source_labels, flight, references):
    def event(op, x=0, y=0, command=0, target=FINISH):
        return OPS.index(op), command, target, x, y

    body = SAVE_COUNTER_POSITION.sub(r"@saveCounterPosition \1;", body)
    body = RESTORE_COUNTER_POSITION.sub(r"@restoreCounterPosition \1;", body)
    body = COMBO_TIMER.sub(r"@comboLimit \1;", body)
    body = FORCE_HURT_ACTION.sub(r"@startHurtAction \1;", body)
    for statement in inline_target_offset(body):
        if transfer := re.fullmatch(r"@transferQi (\d+)", statement):
            yield event("transferQi", int(transfer[1]))
            continue
        if statement.startswith("@startHurtAction "):
            yield event("hurtAction", command=1, target=clip_ids[json.loads(statement.removeprefix("@startHurtAction "))])
            continue
        if backdrop := MAP_BACKDROP.fullmatch(statement):
            yield event("mapBackdrop", backdrop[1] == "true", int(backdrop[2] or 0))
            continue
        if combo := COMBO_EVENT.fullmatch(statement):
            index, label = next((i, label) for i, (field, label) in enumerate(COMBO_LIMITS) if field == combo[1])
            yield event("comboLimit", command=index, target=clip_ids[label])
            continue
        if hittable := HITTABLE.fullmatch(statement):
            yield event("hittable", hittable[2] == "true", command=int(hittable[1] == TARGET_SPRITE))
            continue
        if buff := FIGHTER_BUFF.fullmatch(statement):
            recipient, kind, value, duration = buff.groups()
            if (recipient, kind) == (TARGET_SPRITE, "speedDown"):
                option, value = 0, -float(value)
            elif recipient != TARGET_SPRITE and kind == "attackUp":
                option, value = 1, float(value)
            elif recipient != TARGET_SPRITE and kind == "speedUp":
                option, value = 2, float(value)
            else:
                raise ValueError(f"Unsupported fighter buff {statement!r}")
            frames, hundredths = round(float(duration) * 60), round(value * 100)
            if not 0 < frames <= 3600 or frames / 60 != float(duration) or hundredths / 100 != value:
                raise ValueError(f"Unsupported fighter buff timing or amount {statement!r}")
            yield event("fighterBuff", hundredths, frames, command=option)
            continue
        if gain := RETURN_TARGET_ENERGY_GAIN.fullmatch(statement):
            yield event("gainEnergy", float(gain[1]))
            continue
        if spend := USE_ENERGY.fullmatch(statement):
            yield event("loseEnergy", float(spend[1]))
            continue
        if position := COUNTER_POSITION.fullmatch(statement):
            yield event("counterPosition", position[1] == "restore", command=int(position[2] == "zh2"))
            continue
        if test := JUST_HIT.fullmatch(statement):
            hit = strike_id(json.loads(test[1]))
            passed = list(statements(test[2]))
            failed = list(statements(test[3])) if test[3] is not None else []
            calls = [CALL.fullmatch(s) for s in (*passed, *failed)]
            controller = all(c and c[1] == "$mc_ctrler" for c in calls)
            if controller and len(passed) == len(failed) == 1 and all(c[2] == "gotoAndPlay" for c in calls):
                yes, no = (clip_ids[json.loads(c[3])] for c in calls)
                yield event("branchHit", hit, no, target=yes)
                continue
            if controller and len(passed) == 1 and not failed and calls[0][2] == "addQi":
                yield event("qiOnHit", json.loads(calls[0][3]), target=hit)
                continue
        if condition := conditional_parts(statement):
            test, yes, no = condition
            test = test.strip()
            if (local := REMEMBERED_TEST.fullmatch(test)) and local[1] in references:
                action = list(statements(yes))
                goto = REMEMBERED_GOTO.fullmatch(action[0]) if len(action) == 1 else None
                if not goto or goto[1] != local[1] or no:
                    raise ValueError(f"Uncompiled use of remembered attacker {statement}")
                slot, name = references[local[1]]
                for target, phase in attacker_labels[name, json.loads(goto[2])]:
                    yield event("controlRemembered", phase, command=slot, target=target)
                continue
            if test.startswith("!"):
                test, yes, no = test[1:].strip(), no or "", yes
            if hit := HIT_TEST.fullmatch(test):
                name, include_guard = (json.loads(f"[{hit[1]}]") + [False])[:2]
                tests = [("ifHit", strike_id(name), 0, int(include_guard))]
            elif area := RANGE_TEST.fullmatch(test):
                axes = json.loads(f"[{area[1]}]")
                tests = [("ifRange", *bounds, axis) for axis, bounds in enumerate(axes) if bounds is not None]
            elif attacker := ATTACKER_TEST.fullmatch(test):
                if attacker[1]:
                    yes, no = no or "", yes
                key = strike_key(json.loads(attacker[2]))
                tests = [("ifAttacker", key & 0xffff, key >> 16, 0)]
            elif health := HEALTH_TEST.fullmatch(test):
                tests = [("ifHealth", float(health[1]), 0, 0)]
            elif energy := RETURN_TARGET_ENERGY_TEST.fullmatch(test):
                tests = [("ifEnergy", float(energy[2]), 0, {"<": 0, "<=": 2, ">=": 3}[energy[1]])]
            elif energy := HAS_ENERGY.fullmatch(test):
                tests = [("ifEnergy", float(energy[1]), int(energy[2] == "true"), 1)]
            elif test == TARGET:
                tests = [("ifTarget", 0, 0, 0)]
            elif test == TARGET_GROUNDED:
                tests = [("ifTarget", 0, 0, 1)]
            elif test == TARGET_IN_AIR:
                tests = [("ifTarget", 0, 0, 2)]
            elif test == TARGET_STEEL:
                tests = [("ifTarget", 0, 0, 3)]
            elif overlap := DISPLAY_OVERLAP.fullmatch(test):
                tests = [("ifDisplayOverlap", probe_ids[overlap[1]], 0, 0)]
            elif flight and test == "this.flyMode":
                tests = [("ifFlight", 0, 0, 0)]
            elif test in ("parent.speedbuff", "!parent.speedbuff"):
                if test.startswith("!"):
                    yes, no = no or "", yes
                tests = [("ifShunko", 0, 0, 0)]
            elif test == "this.isBeHitByWu":
                tests = [("ifTimelineValue", 1, 0, 2)]
            elif test in ("this.skipcbs", "this.soushikenn", "this.soushikennCD"):
                tests = [("ifTimelineValue", 1, TIMELINE_COUNTERS[test.removeprefix("this.")], 0)]
            elif re.sub(r"\s+", "", test) == WATER_INPUT:
                tests = [("ifWaterInput", 0, 0, 0)]
            elif re.sub(r"\s+", "", test) == HINATA_LOOP:
                tests = [("ifTimelineValue", 7, TIMELINE_COUNTERS["sbsCount"], 1), ("ifHurt", 0, 0, 2)]
            elif all(compares := [TIMELINE_COMPARE.fullmatch(part.strip()) for part in test.split("||")]):
                if len(compares) > 1 and any(match[1] or match[3] != ">=" for match in compares):
                    raise ValueError(f"Unsupported disjunction of timeline comparisons: {test}")
                if compares[0][3] in ("!=", ">=", ">"):
                    yes, no = no or "", yes
                tests = []
                for increment, name, operator, value in (match.groups() for match in compares):
                    if increment:
                        yield event("timelineValue", 1, 1, command=TIMELINE_FIELDS[name])
                    tests.append(("ifTimelineValue", int(value) + int(operator in ("<=", ">")),
                                  TIMELINE_COUNTERS.get(name, 0), int(operator not in ("==", "!="))))
            else:
                states = [HURT_TEST.fullmatch(part.strip()) for part in test.split("||")]
                if not all(states):
                    raise ValueError(f"Uncompiled condition {test!r}")
                if len(states) == 1 and states[0][1] == "!=":
                    yes, no = no or "", yes
                elif any(state[1] != "==" for state in states):
                    raise ValueError(f"Uncompiled hurt-state disjunction {test!r}")
                mask = 0
                for state in states:
                    mask |= {"20": 1, "21": 2, "0": 4, "22": 8}[state[2]]
                tests = [("ifHurt", 0, 0, mask)]
            args = (stats, clip_ids, deferred, deferred_effects, frame, object_ids,
                    face_clips, probe_ids, anchor_ids, strike_id, attacker_labels, source_labels, flight, references)
            passed = list(events_for_frame(yes, *args))
            failed = list(events_for_frame(no, *args)) if no else []
            for i, (op, x, y, option) in enumerate(tests):
                yield event(op, x, y, option, len(tests) - i - 1 + len(passed) + bool(failed))
            yield from passed
            if failed and tests:
                yield event("skip", target=len(failed))
                yield from failed
            continue
        if statement in ("@ulquiorraDashSound start", "@ulquiorraDashSound stop"):
            yield event("ulquiorraDashSound", statement.endswith("start"))
            continue
        if flight and (mode := FLIGHT_MODE.fullmatch(statement)):
            yield event("flightMode", mode[1] == "true")
            continue
        if assignment := TIMELINE_SET.fullmatch(statement):
            yield event("timelineValue", json.loads(assignment[2]), command=TIMELINE_FIELDS[assignment[1]])
            continue
        if element := KAKASHI_ELEMENT_SET.fullmatch(statement):
            yield event("timelineValue", int(element[1]), command=TIMELINE_FIELDS["yh"])
            continue
        if statement == "parent.dist = -1" or statement == "--parent.dist":
            yield event("timelineValue", 1, int(statement == "--parent.dist"), command=TIMELINE_FIELDS["cloneDistance"])
            continue
        if move := KAKASHI_CLONE_MOVE.fullmatch(statement):
            yield event("timelineMove", (20 if move[1] else -20) / 3, command=TIMELINE_FIELDS["cloneDistance"])
            continue
        if statement == "@kamui":
            yield event("kamui")
            continue
        if statement in ("@waterWalk start", "@waterWalk stop"):
            yield event("waterWalk", int(statement.endswith("start")))
            continue
        if statement in ("@hinataLions", "@hinataChakra", "@hinataFollowHit"):
            mode = ("@hinataLions", "@hinataChakra", "@hinataFollowHit").index(statement)
            yield event("hinataState", command=mode, target=strike_id("sbs1") if mode == 2 else FINISH)
            continue
        if statement == "@rememberFacing":
            yield event("rememberFacing")
            continue
        if follow := FOLLOW_ATTACKER_Y.fullmatch(statement):
            bank, = attacker_labels[json.loads(follow[1]), None]
            yield event("followAttackerY", int(follow[2]), target=bank[0])
            continue
        if reaction := STRIKE_REACTION.fullmatch(statement):
            strike, field, value = reaction.groups()
            if (field == "isBreakDef") != (value in ("true", "false")):
                raise ValueError(f"Unexpected value for strike field {field}: {value}")
            yield event("strikeReaction", float(json.loads(value)),
                        command=("hitx", "hurtType", "powerRate", "isBreakDef", "power", "hitType", "hurtTime").index(field),
                        target=strike_id(json.loads(strike)))
            continue
        if statement == "parent.$mc_ctrler.getAction().airHitTimes = 0":
            yield event("airAttackLock")
            continue
        if increment := TIMELINE_STEP.fullmatch(statement):
            yield event("timelineValue", 1 if increment[1] == "++" else -1, 1, command=TIMELINE_FIELDS[increment[2]])
            continue
        if bankai := BANKAI_FLAG_SET.fullmatch(statement):
            yield event("timelineValue", bankai[1] == "true", command=3)
            continue
        if statement == "parent.$mc_ctrler.addQi(this.u_add_qi)":
            yield event("timelineQi")
            continue
        if local := ATTACKER_LOCAL.fullmatch(statement):
            slot, name = references[local[1]]
            yield event("rememberAttacker", command=slot, target=attacker_labels[name, None][0][0])
            continue
        if assign := ACTION_ASSIGN.fullmatch(statement):
            if assign[1] in WALK_FIELDS:
                label = json.loads(assign[2])
                if label not in (None, "走"):
                    raise ValueError(f"Unsupported movement label: {statement!r}")
                yield event("walkCancel", bool(label), command=WALK_FIELDS[assign[1]])
                continue
            if assign[1] == "hurtAction":
                label = json.loads(assign[2])
                yield event("hurtAction", target=clip_ids[label] if label else FINISH)
                continue
            command = COMMANDS.get("set" + assign[1][:1].upper() + assign[1][1:])
            if command is None:
                raise ValueError(f"Uncompiled action assignment {statement!r}")
            label = json.loads(assign[2])
            yield event("allow", command=command[0], target=clip_ids[label] if label else FINISH)
            continue
        if jump_count := JUMP_COUNT.fullmatch(statement):
            yield event("jumpCount", int(jump_count[1]))
            continue
        if gravity := TARGET_GRAVITY.fullmatch(statement):
            yield event("gravity", gravity[1] == "true", command=1)
            continue
        if offset := TARGET_OFFSET.fullmatch(statement):
            axis, sign, amount = offset.groups()
            delta = float(sign + amount)
            yield event("targetOffset", delta if axis == "x" else 0, delta if axis == "y" else 0)
            continue
        if visible := TARGET_VISIBLE.fullmatch(statement):
            yield event("targetVisible", visible[1] == "true")
            continue
        if goto := TIMELINE_GOTO.fullmatch(statement):
            yield event("goto", target=clip_ids[json.loads(goto[1])])
            continue
        if swap := FRAME_SWAP.fullmatch(statement):
            yield event("swapForm", target=int(swap[1]) - 2)
            continue
        if statement == ROOT_NEXT_FRAME:
            yield event("swapForm")
            continue
        if goto := ATTACKER_GOTO.fullmatch(statement):
            name, label = map(json.loads, goto.groups())
            if (name, label) not in attacker_labels:
                raise ValueError(f"Missing attacker timeline label {name}.{label}")
            for target, phase in attacker_labels[name, label]:
                yield event("controlAttacker", phase, target=target)
            continue
        if remove := ATTACKER_REMOVE.fullmatch(statement):
            name = json.loads(remove[1])
            for target, _ in attacker_labels[name, None]:
                yield event("controlAttacker", command=1, target=target)
            continue
        if statement == "this.recoveryBisha = 3":
            continue  # Orihime's timeline declares and writes this field without reading it.
        match = CALL.fullmatch(statement)
        if not match:
            raise ValueError(f"Uncompiled frame statement {statement!r}")
        owner, name, args = match.groups()
        if owner == "$mc_ctrler" and name == "movePercent" and (offset := VELOCITY_WITH_OFFSET.fullmatch(args)):
            yield event("velocity", float(offset[1]), float(offset[2]), command=2)
            continue
        values = json.loads("[" + RATE.sub(lambda value: str(float(value[1]) / stats[0]), args) + "]")
        if owner == "$effect_ctrler":
            if name == "shake" and len(values) <= 3:
                x, y, time = values + [0, 3, 0][len(values):]
                yield event("shake", max(x, y) * 2, time * 1000)
            elif name == "shine" and len(values) <= 1 and 0 <= (values[0] if values else 0xFFFFFF) <= 0xFFFFFF:
                yield event("shine", values[0] if values else 0xFFFFFF)
            elif (name == "bisha" and len(values) <= 2) or (name == "startWanKai" and len(values) <= 1):
                if name == "startWanKai":
                    kind, label = 3, values[0] if values else None
                else:
                    kind, label = (2 if values and values[0] else 1), values[1] if len(values) == 2 else None
                target, base_only = face_clips.get(label, (FINISH, False))
                yield event("cutin", base_only, command=kind, target=target)
            elif name == "ghostStep" and not values:
                yield event("cutin", command=4)
            elif name in ("endBisha", "endWanKai", "endGhostStep") and not values:
                yield event("endCutin", command={"endBisha": 1, "endWanKai": 2, "endGhostStep": 3}[name])
            elif name == "startShake" and len(values) <= 2:
                x, y = (values + [0, 3][len(values):])
                # Keep the existing Y-first event layout for previously packed banks.
                yield event("shakeLoop", y, x, command=1)
            elif name == "endShake" and not values:
                yield event("shakeLoop")
            elif name == "slowDown" and len(values) == 1:
                yield event("slow", round(values[0] * 1000))
            elif name == "dash" and values in ([], [True], [False]):
                yield event("stageCue", int(values != [False]), command=STAGE_CUES.index(name))
            elif name in STAGE_CUES and not values:
                yield event("stageCue", command=STAGE_CUES.index(name))
            elif name == "shadow" and len(values) <= 3:
                r, g, b = [max(-255, min(255, int(v))) for v in (values + [0, 0, 0])[:3]]
                yield event("shadow", r, g, target=b & 0xffff)
            elif name in ("endShadow", "walk") and not values:
                yield event(name)
            else:
                raise ValueError(f"Uncompiled effect call {statement!r}")
        elif owner == "$fighter_ctrler":
            if name == "moveOnce" and len(values) <= 2:
                x, y = (values + [0, 0])[:2]
                yield event("moveOnce", x or 0, y or 0)
            elif name == "setTargetVelocity" and len(values) == 2:
                yield event("targetVelocity", *values)
            elif name == "moveToTarget" and len(values) <= 3:
                x, y = (values + [None, None])[:2]
                face = bool(values[2]) if len(values) == 3 else True
                flags = int(x is not None) | (int(y is not None) << 1) | (int(bool(face)) << 2)
                yield event("moveToTarget", x or 0, y or 0, command=flags)
            elif name == "doWanKai" and len(values) <= 1:
                yield event("switchForm", target=values[0] - 2 if values and values[0] else FINISH)
            elif name == "setCross" and len(values) == 1:
                yield event("cross", values[0])
            elif name == "setDirectToTarget" and not values:
                yield event("faceTarget")
            elif name in ("loseHp", "loseEnergy") and len(values) == 1 and values[0] >= 0:
                yield event("loseHealth" if name == "loseHp" else "loseEnergy", values[0])
            elif name == "addHpPercent" and len(values) == 1 and 0 < values[0] <= 1:
                percent = round(values[0] * 10000)
                if percent / 10000 != values[0]:
                    raise ValueError(f"Unsupported healing precision: {statement}")
                yield event("healPercent", percent)
            else:
                raise ValueError(f"Uncompiled fighter call {statement!r}")
        elif name in ("move", "movePercent", "damping", "dampingPercent", "stopMove") and len(values) <= 2:
            x, y = (values + [0, 0])[:2]
            percent = name.endswith("Percent")
            rate = stats[0] if percent else 1
            yield event("damping" if name.startswith("damping") else "velocity", x * rate, y * rate,
                        command=int(percent))
        elif name in COMMANDS and len(values) <= (2 if name.startswith("setBisha") else 1):
            slot, default = COMMANDS[name]
            label = (values[0] if values else None) or default
            cost = values[1] if len(values) == 2 else None
            if len(values) == 2 and (type(cost) is not int or not 0 <= cost < FINISH):
                raise ValueError(f"Invalid authored Qi cost: {statement}")
            if label in clip_ids:
                yield event("allow", cost or 0, int(cost is not None), command=slot, target=clip_ids[label])
            else:
                deferred.add((name, label))
        elif name in WALK_SETTERS and not values:
            yield event("walkCancel", 1, command=WALK_SETTERS[name])
        elif name == "setAllAct" and not values:
            yield event("groundCancel")
            for setter in GROUND_ACTION_SETTERS:
                slot, label = COMMANDS[setter]
                if label in clip_ids:
                    yield event("allow", command=slot, target=clip_ids[label])
            for slot, (_, label) in enumerate(BINDINGS):
                if label in ("万解", "万解W", "万解S") and label in clip_ids:
                    yield event("allow", command=slot, target=clip_ids[label])
        elif name == "endAct" and not values:
            yield event("end")
        elif name == "idle" and len(values) <= 1:
            label = (values[0] if values else None) or "站立"
            yield event("recover", target=clip_ids[label] if label != "站立" else FINISH)
        elif name == "stop" and not values:
            pass  # The clip planner holds this frame until the fighter changes action.
        elif name == "setHurtAction" and len(values) == 1:
            yield event("hurtAction", target=clip_ids[values[0]])
        elif name == "justHitToPlay" and 2 <= len(values) <= 4:
            hit, label, idle_on_miss, include_guard = (values + [False, False])[:4]
            yield event("ifHit", strike_id(hit), command=int(include_guard), target=1)
            yield event("goto", target=clip_ids[label])
            if idle_on_miss:
                yield event("recover")
        elif name in ("gotoAndPlay", "loop") and len(values) == 1:
            if name == "gotoAndPlay" and values[0] not in source_labels:
                continue  # FighterMC catches the missing-label error and keeps playing.
            yield event("goto", target=clip_ids[values[0]])
        elif name == "setTouchFloor" and len(values) <= 2:
            label = (values[0] if values else None) or "落地"
            yield event("landing", command=int(bool(values[1])) if len(values) == 2 else 1, target=clip_ids[label])
        elif name == "setHitTarget" and len(values) == 2:
            yield event("probe", probe_ids[values[0]], target=clip_ids[values[1]])
        elif name == "moveTarget":
            anchor, limited, speed = move_target(values)
            yield event("moveTarget", *speed, command=int(limited), target=anchor_ids[anchor] if anchor else FINISH)
        elif name == "hurtFly" and len(values) == 2:
            yield event("selfLaunch", *values)
        elif name == "dash" and len(values) <= 1:
            yield event("dash", stats[0] * (values[0] if values else 3))
        elif name == "dashStop" and len(values) <= 1:
            yield event("dashStop", values[0] if values else 0.5)
        elif name in ("fire", "addAttacker"):
            kind, op = ("projectile", "fire") if name == "fire" else ("attacker", "summon")
            yield event(op, target=object_ids[detached_key(kind, frame, values)[0]])
        elif name in ("isApplyG", "setSteelBody", "addQi", "setAirMove"):
            op = {"isApplyG": "gravity", "setSteelBody": "steel", "addQi": "qi", "setAirMove": "airMove"}[name]
            if not (len(values) == 1 or name == "setSteelBody" and len(values) == 2):
                raise ValueError(f"Invalid arguments to {name}")
            yield event(op, *values)
        else:
            raise ValueError(f"Uncompiled controller call {statement!r}")


def strike_key(name):
    value = 2166136261
    for byte in name.encode():
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def attacker_axis(value, original):
    if isinstance(value, (int, float)):
        return original + value, 0, None
    if not isinstance(value, dict) or set(value) - {"moveToTarget", "followTarget", "offset", "range"}:
        raise ValueError(f"Uncompiled attacker axis {value!r}")
    flags = int(bool(value.get("moveToTarget"))) | (int(bool(value.get("followTarget"))) << 2)
    bounds = value.get("range")
    if bounds is not None and (not isinstance(bounds, list) or len(bounds) != 2 or
                               not all(isinstance(v, (int, float)) for v in bounds) or bounds[0] > bounds[1]):
        raise ValueError(f"Invalid attacker axis range {bounds!r}")
    return value.get("offset", original), flags, bounds if flags & 1 else None


def lz4_compressor():
    library = CDLL(os.environ.get("LZ4_LIBRARY") or find_library("lz4") or "/opt/homebrew/opt/lz4/lib/liblz4.dylib")
    bound = library.LZ4_compressBound
    bound.argtypes = [c_int]
    bound.restype = c_int
    compress = library.LZ4_compress_HC
    compress.argtypes = [c_char_p, c_char_p, c_int, c_int, c_int]
    compress.restype = c_int

    def encode(pixels):
        buffer = create_string_buffer(bound(len(pixels)))
        size = compress(pixels, buffer, len(pixels), len(buffer), 10)
        if size <= 0:
            raise ValueError("LZ4 could not encode a fighter texture")
        return buffer.raw[:size]

    return encode


def compress_textures(textures, frames, encode):
    payload = bytearray()
    entries, indices = [], {}
    for (_, _, pixels), offset in sorted(textures.offsets.items(), key=lambda item: item[1]):
        compressed = encode(pixels)
        indices[offset] = len(entries)
        entries.append((len(payload), len(compressed), len(pixels)))
        payload.extend(compressed)
    def remap(pose):
        return (pose[0], pose[1], indices[pose[2]], *pose[3:]) if pose[5] else pose

    return [remap(frame) for frame in frames], [remap(tile) for tile in textures.tiles], entries, payload


def assist_owner_event(statement, probes, constants, strike_id):
    if move := ASSIST_OWNER_MOVE.fullmatch(statement):
        x, y = json.loads(f"[{move[1]}]")
        return OPS.index("moveOnce"), 0, FINISH, x, y
    if power := ASSIST_HEALTH_POWER.fullmatch(statement):
        value = constants[power[2]]
        basis_points = round(10000 * value)
        if basis_points / 10000 != value:
            raise ValueError(f"Unsupported percentage damage precision: {statement}")
        return OPS.index("targetHealthPower"), 0, strike_id(json.loads(power[1])), basis_points, 0
    if buff := ASSIST_BUFF.fullmatch(statement):
        value, seconds = json.loads(f"[{buff[2]}]")
        hundredths, frames = round(value * 100), round(seconds * 60)
        if (hundredths / 100, frames / 60) != (value, seconds):
            raise ValueError(f"Unsupported buff precision: {statement}")
        kind = ("speedUp", "attackUp", "defenseUp").index(buff[1])
        return OPS.index("ownerBuff"), kind, FINISH, hundredths, frames
    probe = FINISH
    if condition := conditional_parts(statement):
        test, yes, no = condition
        match = ASSIST_OWNER_PROBE.fullmatch(test.strip())
        actions = list(statements(yes))
        if not match or no is not None or len(actions) != 1:
            return None
        probe = probes[json.loads(match[1])]
        statement = actions[0]
    if heal := ASSIST_HEAL.fullmatch(statement):
        token = heal[1]
        amount = constants[token.removeprefix("this.")] if token.startswith("this.") else float(token)
        return OPS.index("ownerHeal"), 0, probe, amount, 0
    if damage := ASSIST_OWNER_DAMAGE.fullmatch(statement):
        token = damage[1]
        amount = constants[token.removeprefix("this.")] if token.startswith("this.") else float(token)
        return OPS.index("ownerHeal"), 1, probe, amount, 0
    return None


def attacker_capture_event(statement, strike_id):
    condition = conditional_parts(statement)
    if not condition or condition[2] is not None:
        return None
    test, yes, _ = condition
    actions = list(statements(yes))
    if test == "!this.isHit" and actions == ["this.$attacker_ctrler.removeSelf()"]:
        return OPS.index("attackerMiss"), 0, FINISH, 0, 0
    if test == "this.hitTarget" and actions == [
            "this.target.getDisplay().visible = true", "this.target.isApplyG = true"]:
        return OPS.index("attackerRelease"), 0, FINISH, 0, 0
    hit = ATTACKER_JUST_HIT.fullmatch(test)
    if not hit or len(actions) != 2 or actions[0] != "this.target = this.$attacker_ctrler.owner_fighter_ctrler.getTargetSP()":
        return None
    nested = conditional_parts(actions[1])
    if nested and nested[0] == "this.target" and nested[2] is None and list(statements(nested[1])) == [
            "this.hitTarget = this.target", "this.target.isApplyG = false",
            "this.target.getDisplay().visible = false", "this.isHit = true"]:
        return OPS.index("attackerCapture"), 0, strike_id(json.loads(hit[1])), 0, 0
    return None


def compile_pack(source, timeline, scripts, clips, stats, destination, attacker_scripts=None, faces=None, probes=(),
                 inherited_faces=(), assist=False, assist_constants=None, flight=False, compact_textures=False):
    hit_defs = json.loads((source / "hits.json").read_text(encoding="utf-8"))
    clip_ids = {clip.label: i for i, clip in enumerate(clips) if clip.start}
    defaults = [clip_ids.get(label, FINISH) for _, label in ALL_BINDINGS]
    has_throws = not assist and any(clip != FINISH for clip in defaults[len(BINDINGS):])
    has_escape = not assist and "起身" in clip_ids
    if not has_throws and not has_escape:
        defaults = defaults[:len(BINDINGS)]
    textures = FighterTextures(source, compact_textures)
    disk_clips, frames, events, hits, strikes, projectiles, attackers = [], [], [], [], [], [], []
    strike_ids, deferred, deferred_effects = {}, set(), set()
    launch_clips = [ClipPlan("assist", 2, timeline.frame_count + 1)] if assist else clips
    launches = detached_calls(launch_clips, scripts, assist)
    launches = {key: value for key, value in launches.items() if key[:3] not in timeline.missing_objects}
    deferred_effects.update("source.retryMissing." + ".".join(map(str, key)) for key in timeline.missing_objects)
    children = child_projectiles(attacker_scripts)
    probe_ids = {name: i for i, name in enumerate(probes)}
    anchor_ids = {name: i for i, name in enumerate(target_anchors(clips, scripts))}
    missing_anchors = set(anchor_ids) - {name for points in timeline.anchors.values() for name, _ in points}
    if missing_anchors:
        raise ValueError(f"Missing authored target anchors: {', '.join(sorted(missing_anchors))}")
    face_symbols = sorted(set((faces or {}).values()))
    face_indices = {symbol: len(clips) + i for i, symbol in enumerate(face_symbols)}
    face_clips = {name: (face_indices[symbol], name in inherited_faces) for name, symbol in (faces or {}).items()}
    extra_poses = [(kind, prefix, poses) for kind, prefix, poses in (
        ("flightPose", "flight", timeline.flight_poses),
        ("screenPose", "screen", timeline.screen_poses),
        ("bankaiPose", "bankai", timeline.bankai_poses),
        ("waterPose", "waterPose", timeline.water_poses),
        ("twinLionPose", "twinLionPose", timeline.twin_lion_poses),
        ("facingPose", "facing", timeline.facing_poses),
    ) if poses]
    pose_events = {}
    for index, (kind, _, poses) in enumerate(extra_poses, start=len(clips) + len(face_symbols)):
        for offset, frame in enumerate(poses):
            key = frame if isinstance(frame, tuple) else (frame, -1)
            pose_events.setdefault(key, []).append((OPS.index(kind), 0, index, offset, 0))
    object_ids = {key: i for kind in ("projectile", "attacker")
                  for i, key in enumerate((key for key in launches if key[0] == kind),
                                           start=int(assist and kind == "attacker"))}
    attacker_labels = {}
    for (kind, frame, name, _), target in object_ids.items():
        if kind != "attacker":
            continue
        sprite = timeline.attackers[frame, name]
        attacker_labels.setdefault((name, None), []).append((target, 0))
        for label, phase in sprite.labels.items():
            attacker_labels.setdefault((name, label), []).append((target, phase))
    first_child = sum(key[0] == "projectile" for key in launches)
    child_ids = {key: first_child + i for i, key in enumerate(children)}
    references = {}
    for frame in selected_frames(clips):
        for statement in nested_statements(scripts.get(frame, "")):
            if local := ATTACKER_LOCAL.fullmatch(statement):
                name = json.loads(local[2])
                slot, previous = references.setdefault(local[1], (len(references), name))
                if name != previous or slot >= 8:
                    raise ValueError(f"Unsupported attacker reference {local[1]} on frame {frame}")

    def strike_id(name):
        if name not in strike_ids:
            hit = hit_defs[name]
            # HitVO accepts slowDown. The source's object copier ignores the unknown field slow.
            if hit["hurtType"] not in (0, 1) or not -1 <= hit["hitType"] <= 11:
                raise ValueError(f"Uncompiled strike attributes for {name}")
            strike_ids[name] = len(strikes)
            hurt_frames = max(4, int(hit["hurtTime"] * .03 + .5) + 3)
            flags = hit["hurtType"] | (int(hit["isBreakDef"]) << 1) | (int(hit.get("isWeak", False)) << 2)
            flags |= int(hit.get("checkDirect", True)) << 3
            flags |= int(not hit.get("targetApplyG", True)) << 6
            flags |= int("bs" in name) << 4
            flags |= int(any(key in name for key in ("tz", "kj", "zh", "sh"))) << 5
            key = strike_key(name)
            if not key or any(row[-1] == key for row in strikes):
                raise ValueError(f"Strike ID collision for {name}")
            strikes.append((hit["hitx"], hit["hity"], hit["power"], hurt_frames, hit["hitType"] & 0xffff, flags,
                            hit["hurtTime"], round(hit.get("slowDown", 0) * 1000), key))
        return strike_ids[name]

    def bake_hits(hit_regions, probe_regions=(), probe_indices=probe_ids):
        first_hit = len(hits)
        for name, box in hit_regions:
            if box[0] >= box[2] or box[1] >= box[3]:
                continue
            hits.append((*box, strike_id(name), 0))
        for name, box in probe_regions:
            if box[0] < box[2] and box[1] < box[3]:
                hits.append((*box, probe_indices[name], 1))
        return first_hit, len(hits) - first_hit

    def bake(clip, frame, key, visual, run_script=True, phase=-1):
        first_hit, hit_count = bake_hits(timeline.hits.get(frame, []), timeline.probes.get(frame, []))
        if frame in timeline.displays:
            hits.append((*timeline.displays[frame], FINISH, 2))
            hit_count += 1
        first_event = len(events)
        authored = scripts.get(frame, "")
        events.extend(pose_events.get((frame, phase), ()))
        events.extend((OPS.index("targetAnchor"), 0, anchor_ids[name], x, y)
                      for name, (x, y) in timeline.anchors.get(frame, []) if name in anchor_ids)
        if clip.label in ("胜利", "失败") and not clip.repeat_from and frame == clip.end - 1:
            run_script = False  # The clip repeats the last pose in place of the terminal stop().
        if run_script and (clip.label in ("走", "落地", "砍1", "击飞_起") or clip.label not in CORE_CLIPS or
                           flight and clip.label in ("站立", "落") or
                           clip.label == "站立" and FORCE_HURT_ACTION.search(authored)):
            try:
                events.extend(events_for_frame(authored, stats, clip_ids, deferred, deferred_effects,
                                               frame, object_ids, face_clips, probe_ids, anchor_ids,
                                               strike_id, attacker_labels, timeline.labels, flight, references))
            except (ValueError, TypeError, KeyError) as error:
                raise ValueError(f"{clip.label}, frame {frame}: {error}") from error
        frames.append((*textures.bake(key, visual, allow_tiles=True), *timeline.bodies.get(frame, EMPTY_BOX),
                       first_hit, first_event, hit_count, len(events) - first_event))

    for clip in clips:
        first = len(frames)
        repeat = 0
        if clip.start == clip.end:
            disk_clips.append((first, 0, 0, clip.start))
            continue
        if clip.held and clip.start in timeline.cycles:
            count, repeat = timeline.cycles[clip.start]
            for i in range(count):
                # Only entering the parent frame runs its script. Its children then animate on their own.
                bake(clip, clip.start, f"{clip.start}-{i}", timeline.children[clip.start, i], run_script=i == 0, phase=i)
        else:
            for frame in range(clip.start, clip.end):
                bake(clip, frame, frame, timeline.poses[frame])
            if clip.repeat_from:
                repeat = clip.repeat_from - clip.start
            elif clip.hold_last or clip.label in ("被打", "击飞_落", "防御", "胜利", "失败"):
                repeat = len(frames) - first - 1
        if clip.exit_to_idle:
            repeat = FINISH
        disk_clips.append((first, len(frames) - first, repeat, clip.start))

    for symbol in face_symbols:
        disk_clips.append((len(frames), 1, 0, 1))
        frames.append((*textures.bake("face-" + symbol, timeline.faces[symbol]), *EMPTY_BOX,
                       len(hits), len(events), 0, 0))
    for kind, prefix, poses in extra_poses:
        disk_clips.append((len(frames), len(poses), 0, 1))
        for frame, pose in poses.items():
            suffix = "-".join(map(str, frame)) if isinstance(frame, tuple) else str(frame)
            visual = textures.bake(f"{prefix}-{suffix}", pose, allow_tiles=True, premultiply=kind == "screenPose")
            frames.append((*visual, *EMPTY_BOX, len(hits), len(events), 0, 0))

    objects = [(kind, (frame, name), params) for (kind, frame, name, _), params in launches.items()]
    objects.extend(("projectile", key, params) for key, params in children.items())
    if assist:
        objects.insert(0, ("assister", (0, "assister"), {}))
    for kind, key, params in objects:
        frame, name = key[0], key[-1]
        is_assister = kind == "assister"
        if is_assister:
            sprite = DetachedTimeline(timeline.frame_count, 1, (-30, 0, 0), (),
                {name: frame - 1 for name, frame in timeline.labels.items()},
                {frame - 1: pose for frame, pose in timeline.poses.items()},
                {frame - 1: hits for frame, hits in timeline.hits.items()},
                {frame - 1: probes for frame, probes in timeline.probes.items()})
        else:
            sprite = (timeline.projectiles if kind == "projectile" else timeline.attackers)[key]
        is_attacker = kind != "projectile"
        object_probes = {name: i for i, name in enumerate(sorted({name for boxes in sprite.probes.values() for name, _ in boxes}))}
        if not isinstance(params, dict):
            raise ValueError(f"Invalid detached object parameters for {name} on frame {frame}")
        x, y, owner_x = sprite.position
        if is_attacker:
            if set(params) - {"x", "y", "applyG"}:
                raise ValueError(f"Uncompiled attacker parameters for {name} on frame {frame}: {params}")
            horizontal = params.get("x", 0)
            if isinstance(horizontal, dict) and "y" in horizontal:
                # FighterAttacker reads the vertical axis only at the top level.
                horizontal = {key: value for key, value in horizontal.items() if key != "y"}
            spawn_x, target_x, range_x = attacker_axis(horizontal, x)
            spawn_y, target_y, range_y = attacker_axis(params.get("y", 0), y)
        clip = len(disk_clips)
        disk_clips.append((len(frames), sprite.count, sprite.count - 1, 1))
        script = scripts if is_assister else (attacker_scripts or {}).get((frame, name), {})
        capture = any(ATTACKER_JUST_HIT.search(body) for body in script.values())
        for phase in range(sprite.count):
            pose = sprite.poses[phase]
            if is_assister:
                baked = textures.bake(phase + 1, pose, allow_tiles=True)
            else:
                baked = textures.bake(pose[0], pose[1:], allow_tiles=True) if pose else textures.blank()
            first_hit, hit_count = bake_hits(sprite.hits.get(phase, []), sprite.probes.get(phase, []), object_probes)
            first_event = len(events)
            if is_attacker and phase == sprite.initial:
                for axis, bounds in enumerate((range_x, range_y)):
                    if bounds is not None:
                        events.append((OPS.index("attackerRange"), axis, FINISH, *bounds))
            if "move_mc" in sprite.anchors.get(phase, {}):
                events.append((OPS.index("attackerAnchor"), 0, FINISH, *sprite.anchors[phase]["move_mc"]))
            if is_attacker and (is_assister or attacker_scripts is not None):
                body = script.get(phase + 1, "") if not is_assister or phase else ""
                for statement in statements(body):
                    if statement in ("stop", "stop()"):
                        continue  # FighterAttacker advances the MovieClip manually with nextFrame().
                    if statement == "@assistWind":
                        events.append((OPS.index("assistWind"), 0, FINISH, 0, 0))
                        continue
                    if statement in ("@assistSwapRemember", "@assistSwap"):
                        events.append((OPS.index("assistSwap"), int(statement == "@assistSwap"), FINISH, 0, 0))
                        continue
                    if statement in ("@assistJudge false", "@assistJudge true"):
                        events.append((OPS.index("timelineValue"), 0, FINISH, int(statement.endswith("true")), 0))
                        continue
                    if bind := re.fullmatch(r'@assistHurtBind ("[^"]+") ("[^"]+")', statement):
                        labels = tuple(json.loads(value) for value in bind.groups())
                        target, phase = attacker_labels[labels][0]
                        events.append((OPS.index("attackerHurtBind"), 0, target, phase, 0))
                        continue
                    if homing := re.fullmatch(r'@attackerHoming ("[^"]+") (\d+) (\d+)', statement):
                        events.append((OPS.index("attackerHoming"), 0, sprite.labels[json.loads(homing[1])],
                                       int(homing[2]), int(homing[3])))
                        continue
                    if statement == "@attackerRemoveAtSide":
                        events.append((OPS.index("attackerRemoveAtSide"), 0, FINISH, 0, 0))
                        continue
                    if random_jump := re.fullmatch(r'@attackerRandomGoto ("[^"]+") ("[^"]+") ("[^"]+")', statement):
                        labels = [sprite.labels[json.loads(label)] for label in random_jump.groups()]
                        events.append((OPS.index("attackerRandomGoto"), 0, labels[0], labels[1], labels[2]))
                        continue
                    if random_move := re.fullmatch(r'@attackerRandomMove (\d+) (\d+) (-?\d+)', statement):
                        first, second, vertical = map(int, random_move.groups())
                        events.append((OPS.index("attackerRandomMove"), 0, first, second, vertical))
                        continue
                    if sibling := re.fullmatch(r'@playSibling ("[^"]+") ("[^"]+")', statement):
                        labels = tuple(json.loads(part) for part in sibling.groups())
                        for target, start in attacker_labels[labels]:
                            events.append((OPS.index("controlAttacker"), 2, target, start, 0))
                        continue
                    if statement.startswith("@gotoWhenOwnerBusy "):
                        label = json.loads(statement.removeprefix("@gotoWhenOwnerBusy "))
                        events.append((OPS.index("attackerGoto"), 1, sprite.labels[label], 0, 0))
                        continue
                    if statement in ("@startOwnerQiCap", "@endOwnerQiCap"):
                        events.append((OPS.index("attackerQiCap"), int(statement == "@startOwnerQiCap"), FINISH, 0, 0))
                        continue
                    if statement == "@aizenWall":
                        if not any(key == "wallBody" for key, _ in sprite.probes.get(phase, ())):
                            raise ValueError("Aizen's wall body is missing on the authored push frame")
                        events.append((OPS.index("attackerPushOut"), 0, object_probes["wallBody"], 0, 0))
                        continue
                    if statement == "@sandWall":
                        events.append((OPS.index("attackerWall"), 5, 107, -19, 27))
                        continue
                    if statement == "@clearSandWall":
                        events.append((OPS.index("attackerWall"), 0, FINISH, 0, 0))
                        continue
                    if statement == "@sandCageAlign":
                        events.append((OPS.index("attackerAlignHitY"), 0, strike_id("zh3"), strike_id("zh31"), 0))
                        continue
                    if statement in ("@startSandFollow", "@endSandFollow"):
                        events.append((OPS.index("attackerFollow"), 0, FINISH, int(statement == "@startSandFollow"), 0))
                        continue
                    if statement == "@sandShowTarget":
                        events.append((OPS.index("attackerShowTarget"), 0, FINISH, 0, 0))
                        continue
                    if statement in ("parent.setChildIndex(this,0)", "@swapTargetLayer"):
                        events.append((OPS.index("attackerBehind"), int(statement == "@swapTargetLayer"), FINISH, 0, 0))
                        continue
                    if statement in ("@stopTargetAction true", "@stopTargetAction false"):
                        events.append((OPS.index("attackerStopTargetAction"), 0, FINISH, int(statement.endswith("true")), 0))
                        continue
                    if statement == "@allowTargetHit":
                        events.append((OPS.index("hittable"), 0, FINISH, 1, 0))
                        continue
                    if statement == "@allowTargetGuard":
                        events.append((OPS.index("attackerAllowTargetGuard"), 0, FINISH, 0, 0))
                        continue
                    if statement == "@recoverAirSuperOwner":
                        events.append((OPS.index("attackerRecoverOwner"), 0, FINISH, 0, 0))
                        continue
                    if statement == "@waterEnergy":
                        events.append((OPS.index("attackerWaterEnergy"), 0, clip_ids["砍技3"], 1.5, clip_ids["水走"]))
                        continue
                    if statement == "@attackerShadow":
                        events.append((OPS.index("shadow"), 0, 0, 0, 0))
                        continue
                    if align := ALIGN_HIT_Y.fullmatch(re.sub(r"\s+", "", statement)):
                        strike = strike_id(json.loads(align[1]))
                        events.append((OPS.index("attackerAlignHitY"), int(bool(align[2])), strike, strike, 0))
                        continue
                    if branch := conditional_parts(statement):
                        test, yes, no = branch
                        actions = list(statements(yes))
                        owner_target = "this.$attacker_ctrler.owner_fighter_ctrler.target"
                        if test.strip() == owner_target and actions == [owner_target + ".visible = true"] and no is None:
                            events.append((OPS.index("attackerShowTarget"), 0, FINISH, 0, 0))
                            continue
                        hit = ATTACKER_JUST_HIT.fullmatch(test.strip())
                        jump = ATTACKER_CALL.fullmatch(actions[0]) if len(actions) == 1 else None
                        if hit and jump and jump[1] == "gotoAndPlay" and no is None:
                            target = json.loads(jump[2])
                            if target not in sprite.labels:
                                raise ValueError(f"Unknown attacker follow-up label {target!r} in {name}")
                            events.extend(((OPS.index("ifHit"), 0, 1, strike_id(json.loads(hit[1])), 0),
                                           (OPS.index("attackerGoto"), 0, sprite.labels[target], 0, 0)))
                            continue
                        hit = ATTACKER_JUST_HIT.fullmatch(test.strip().removeprefix("!"))
                        if test.strip().startswith("!") and hit and jump and jump[1] == "gotoAndPlay" and no is None:
                            target = json.loads(jump[2])
                            events.append((OPS.index("attackerGoto"), 2, sprite.labels[target], strike_id(json.loads(hit[1])), 0))
                            continue
                    if is_assister and (event := assist_owner_event(statement, object_probes, assist_constants or {}, strike_id)):
                        events.append(event)
                        continue
                    if capture:
                        if statement == "this.isHit = false":
                            events.append((OPS.index("attackerRelease"), 0, FINISH, 0, 0))
                            continue
                        if event := attacker_capture_event(statement, strike_id):
                            events.append(event)
                            continue
                    statement = statement.replace("this.$attacker_ctrler.owner_fighter_ctrler.getEffectCtrl().",
                                                  "this.$attacker_ctrler.effect.", 1)
                    call = (ASSIST_CALL if is_assister else ATTACKER_CALL).fullmatch(statement)
                    if not call:
                        raise ValueError(f"Uncompiled attacker {name}, frame {phase + 1}: {statement}")
                    action, args = call.groups()
                    values = json.loads(f"[{args}]")
                    if action in ("move", "damping") and len(values) <= 2:
                        dx, dy = (values + [0, 0])[:2]
                        events.append((OPS.index("velocity" if action == "move" else "damping"), 0, FINISH, dx, dy))
                    elif action == "removeSelf" and not values:
                        events.append((OPS.index("remove"), 0, FINISH, 0, 0))
                    elif action == "stopFollowTarget" and not values:
                        events.append((OPS.index("attackerStopTracking"), 0, FINISH, 0, 0))
                    elif action == "setTouchFloor" and len(values) == 1 and (values[0] is None or values[0] in sprite.labels):
                        events.append((OPS.index("attackerLanding"), 0, sprite.labels.get(values[0], FINISH), 0, 0))
                    elif action == "stop" and not values:
                        events.append((OPS.index("attackerPause"), 0, FINISH, 0, 0))
                    elif is_assister and action in ("fire", "addAttacker"):
                        entry, op = ("projectile", "fire") if action == "fire" else ("attacker", "summon")
                        key = detached_key(entry, phase + 1, values)[0]
                        if key[:3] not in timeline.missing_objects:
                            events.append((OPS.index(op), 0, object_ids[key], 0, 0))
                    elif is_assister and action == "justHit" and len(values) == 3 and values[2] is False:
                        events.extend(((OPS.index("ifHit"), 0, 1, strike_id(values[0]), 0),
                                       (OPS.index("attackerGoto"), 0, sprite.labels[values[1]], 0, 0)))
                    elif action == "fire":
                        events.append((OPS.index("fire"), 0, child_ids[frame, name, phase + 1, values[0]], 0, 0))
                    elif action == "setHitTarget" and len(values) == 2 and values[1] in sprite.labels:
                        if values == ["bs1", "hit"] and sprite.symbols == ["gaara_fla.沙缚枢_1_93"] and values[0] not in object_probes:
                            continue  # The source checks a child named bs1. Its attack area is named bs1atm.
                        events.append((OPS.index("attackerProbe"), 0, sprite.labels[values[1]], object_probes[values[0]], 0))
                    elif action == "gotoAndPlay" and len(values) == 1:
                        jump_target = values[0]
                        if type(jump_target) is int or (isinstance(jump_target, str) and re.fullmatch(r"[0-9]+", jump_target)):
                            target_phase = int(jump_target) - 1
                        else:
                            target_phase = sprite.labels.get(jump_target, -1)
                        if not 0 <= target_phase < sprite.count:
                            raise ValueError(f"Invalid attacker frame {jump_target!r} in {name}")
                        events.append((OPS.index("attackerGoto"), 0, target_phase, 0, 0))
                    elif action == "moveToTarget" and len(values) <= (3 if is_assister else 2):
                        dx, dy = (values + [None, None])[:2]
                        axes = int(dx is not None) | (int(dy is not None) << 1)
                        if is_assister and (len(values) < 3 or values[2]):
                            axes |= 4
                        events.append((OPS.index("moveToTarget"), axes, FINISH, dx or 0, dy or 0))
                    elif is_assister and action == "setDirectToTarget" and not values:
                        events.append((OPS.index("moveToTarget"), 4, FINISH, 0, 0))
                    elif is_assister and action == "setApplyG" and len(values) == 1 and isinstance(values[0], bool):
                        events.append((OPS.index("gravity"), 0, FINISH, values[0], 0))
                    elif is_assister and action == "finish" and len(values) <= 1:
                        events.append((OPS.index("remove"), 0, FINISH, int(not values or bool(values[0])), 0))
                    elif is_assister and action == "endAct" and not values:
                        events.append((OPS.index("end"), 0, FINISH, 0, 0))
                    elif is_assister and action == "effect.bisha" and (not values or values in ([False], [True])):
                        events.append((OPS.index("attackerCutin"), 0, FINISH, 2 if values == [True] else 1, 0))
                    elif is_assister and action == "effect.endBisha" and not values:
                        events.append((OPS.index("attackerCutin"), 0, FINISH, 0, 0))
                    elif action == "effect.dash" and values in ([], [True], [False]):
                        events.append((OPS.index("stageCue"), STAGE_CUES.index("dash"), FINISH, int(values != [False]), 0))
                    elif action == "effect.shadow" and len(values) <= 3:
                        r, g, b = [max(-255, min(255, int(v))) for v in (values + [0, 0, 0])[:3]]
                        events.append((OPS.index("shadow"), 0, b & 0xffff, r, g))
                    elif action in ("effect.endShadow", "effect.walk") and not values:
                        events.append((OPS.index(action.removeprefix("effect.")), 0, FINISH, 0, 0))
                    elif action == "effect.slowDown" and len(values) == 1:
                        events.append((OPS.index("slow"), 0, FINISH, round(values[0] * 1000), 0))
                    elif action == "setCrossMap" and len(values) == 1 and isinstance(values[0], bool):
                        events.append((OPS.index("cross"), 0, FINISH, values[0], 0))
                    elif is_assister and action == "setCross" and values == [False]:
                        # AssisterCtrler declares no setCross method. This final call changes no state in Flash.
                        deferred_effects.add("source.missing.AssisterCtrler.setCross")
                    elif action in ("owner_fighter_ctrler.setTargetVelocity", "owner_fighter_ctrler.setTargetDamping") and len(values) == 2:
                        op = "targetVelocity" if action.endswith("Velocity") else "targetDamping"
                        events.append((OPS.index(op), 0, FINISH, *values))
                    elif action == "effect.shine" and len(values) <= 1:
                        events.append((OPS.index("shine"), 0, FINISH, values[0] if values else 0xffffff, 0))
                    elif action == "effect.shake" and 2 <= len(values) <= 3:
                        time = values[2] * 1000 if len(values) == 3 else 0
                        events.append((OPS.index("shake"), 0, FINISH, max(values[:2]) * 2, time))
                    elif action != "endAct" or values:
                        raise ValueError(f"Uncompiled attacker {name}, frame {phase + 1}: {statement}")
            frames.append((*baked, *EMPTY_BOX, first_hit, first_event, hit_count, len(events) - first_event))

        if is_attacker:
            attackers.append((spawn_x, spawn_y, owner_x, clip, sprite.initial, bool(params.get("applyG", True)),
                              target_x | (target_y << 1), strike_key(name)))
            continue
        if set(params) - {"x", "y", "hold", "hits", "hp"}:
            raise ValueError(f"Uncompiled projectile parameters for {name} on frame {frame}: {params}")
        strike = strike_id(name if name in hit_defs else name.replace("atm", "", 1))
        label = lambda key: sprite.labels.get(key, FINISH)
        hold = params.get("hold") or -1
        projectiles.append((x, y, *sprite.bounds, *projectile_axis(params.get("x"), 5),
                            *projectile_axis(params.get("y"), 0), clip, strike, sprite.initial,
                            label("loop"), sprite.labels.get("loop_start", 0), label("hit"), label("timeout"),
                            label("remove"), label("hit_over"), params.get("hp") or 100,
                            -1 if hold == -1 else int(hold * 60), params.get("hits") or -1, owner_x))

    packed_frames, tiles, texture_entries, payload = compress_textures(
        textures, frames, lambda pixels: zlib.compress(pixels, level=1)
    )
    lz4 = not assist and len(payload) > RESIDENT_TEXTURE_BUDGET
    if lz4:
        packed_frames, tiles, texture_entries, payload = compress_textures(textures, frames, lz4_compressor())
    tables = ((CLIP, disk_clips), (FRAME, packed_frames), (HIT, hits), (STRIKE, strikes), (EVENT, events),
              (PROJECTILE, projectiles), (ATTACKER, attackers), (TEXTURE, texture_entries), (POSE, tiles))
    if len(payload) > STREAMED_TEXTURE_LIMIT:
        raise ValueError("Compressed fighter textures exceed the loader's stream limit")
    if lz4 and not has_throws and not has_escape:
        defaults.extend([FINISH] * len(THROW_BINDINGS))
    bindings = struct.pack(f"<{len(defaults)}H", *defaults)
    source_ranges = {}
    for line in (source / "fighter.tsv").read_text(encoding="utf-8").splitlines():
        if line.startswith("range\t"):
            _, _, name, *bounds = line.split("\t")
            source_ranges[name] = tuple(map(float, bounds))
    ranges = b"".join(struct.pack("<4f", *source_ranges.get(name, EMPTY_BOX)) for name in AI_RANGES)
    if has_throws or lz4 or has_escape:
        ranges += struct.pack("<8f", *EMPTY_BOX, *EMPTY_BOX)
    start = aligned(HEADER.size + 12 + 2 * has_escape + len(bindings) + len(ranges) + sum(fmt.size * len(rows) for fmt, rows in tables))
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("wb") as out:
        magic = b"BVNASST1" if assist else b"BVNFGT12" if has_throws else b"BVNFGT11"
        if lz4:
            magic = b"BVNFGT13"
        if has_escape:
            magic = b"BVNFGT15" if lz4 else b"BVNFGT14"
        out.write(HEADER.pack(magic, len(frames), len(hits), len(events), start, len(disk_clips), len(strikes), *stats))
        out.write(struct.pack("<2H2I", len(projectiles), len(attackers), len(texture_entries), len(tiles)))
        if has_escape:
            out.write(struct.pack("<H", clip_ids["起身"]))
        out.write(bindings)
        out.write(ranges)
        for fmt, rows in tables:
            for row in rows:
                try:
                    out.write(fmt.pack(*row))
                except struct.error as error:
                    raise ValueError(f"Invalid {fmt.format} record: {row}") from error
        out.write(bytes(start - out.tell()))
        out.write(payload)
    destination.with_suffix(".json").write_text(json.dumps({"clips": [clip.__dict__ for clip in clips],
        "deferred_bindings": sorted(deferred, key=str), "deferred_effects": sorted(deferred_effects),
        "cutin_faces": faces or {}, "collision_probes": list(probes),
        "projectiles": [{"frame": frame, "instance": name, "params": params,
                         "origin": timeline.projectiles[frame, name].position,
                         "hit_box": timeline.projectiles[frame, name].bounds,
                         "labels": timeline.projectiles[frame, name].labels}
                        for (kind, frame, name, _), params in launches.items() if kind == "projectile"],
        "attackers": [{"frame": frame, "instance": name, "params": params,
                       "origin": timeline.attackers[frame, name].position,
                       "symbols": timeline.attackers[frame, name].symbols}
                      for (kind, frame, name, _), params in launches.items() if kind == "attacker"],
        "child_projectiles": [{"owner_frame": key[0], "owner_instance": key[1], "frame": key[2],
                               "instance": key[3], "params": params,
                               "origin": timeline.projectiles[key].position, "hit_box": timeline.projectiles[key].bounds}
                              for key, params in children.items()],
        "strikes": strike_ids, "ai_ranges": source_ranges, "texture_bytes": len(textures.pixels),
        "packed_texture_bytes": len(payload), "pose_tiles": len(tiles), "blank_poses": textures.blank_poses},
        ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"{destination}: {len(clips)} moves, {len(projectiles)} projectiles, {len(attackers)} attackers, {len(frames)} poses, "
          f"{len(hits)} hit regions, {len(events)} events")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("main_script", type=Path)
    parser.add_argument("timeline_script", type=Path)
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--moves", nargs="*", default=[])
    args = parser.parse_args()
    timeline = read_timeline(args.source)
    scripts = dict(frame_scripts(args.timeline_script))
    clips = clip_plan(timeline, scripts, args.moves)
    if args.output is None:
        print(" ".join(f"{clip.start} {clip.end - 1}" for clip in clips if clip.start))
        return
    if timeline.attackers:
        raise ValueError("Use build_actions.py to bind the detached actors' ActionScript classes")
    initializer = dict(frame_scripts(args.main_script))[timeline.variant + 1]
    faces, probes = special_art(clips, scripts, initializer)
    compile_pack(args.source, timeline, scripts, clips, authored_stats(args.main_script, timeline.variant), args.output,
                 faces=faces, probes=probes)


if __name__ == "__main__":
    main()

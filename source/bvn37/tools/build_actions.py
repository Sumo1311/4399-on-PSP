#!/usr/bin/env python3
"""Build a native action pack for a named fighter variant from the pinned SWF."""
import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path

from fighter_actions import extract, frame_scripts, read_constants
from pack_actions import (AIZEN_WALL_CLASS, COMBO_LIMITS, SAND_FOLLOW_HANDLER, child_requests, clip_plan,
                          compile_pack, detached_calls,
                          registered_faces, special_art, statements, target_anchors)
from pack_fighter import authored_stats, read_timeline


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def verified_source(reference, group, name):
    variant = read_json(reference / "source_catalog.json")[group][name]
    if variant["source"]["kind"] != "swf":
        raise ValueError(f"{name} does not have a compiled SWF source")
    relative = "shared/assets/assets/packs/" + variant["source"]["path"]
    source = reference / "tag-assets" / relative
    manifest = read_json(reference / "manifest.json")["tag_assets"]
    tree = reference / manifest["tree_file"]
    tree_bytes = tree.read_bytes()
    if hashlib.sha256(tree_bytes).hexdigest() != manifest["tree_sha256"]:
        raise ValueError("The TagAssets file index differs from the verified snapshot")
    expected = next(item for item in json.loads(tree_bytes)["tree"] if item["path"] == relative)
    data = source.read_bytes()
    if hashlib.sha1(f"blob {len(data)}\0".encode() + data).hexdigest() != expected["sha"]:
        raise ValueError(f"The SWF does not match the pinned source for {name}")
    return variant, source


def build(args):
    reference = args.reference.resolve()
    variant, source = verified_source(reference, "fighters", args.variant)

    variant_frame = args.internal_frame if args.internal_frame is not None else variant["start_frame"]
    scratch = args.scratch / (f"{args.variant}-frame{variant_frame}" if args.internal_frame is not None else args.variant)
    scratch.mkdir(parents=True, exist_ok=True)
    jpexs = args.jpexs.resolve()
    scripts = args.decompiled
    if scripts is None:
        subprocess.run([args.java, "-Xmx1g", "-jar", jpexs / "ffdec.jar", "-config", "autoDeobfuscate=1",
                        "-export", "script", scratch, source], check=True, timeout=180)
        scripts = scratch / "scripts"
    main_script = scripts / "main_mc.as"
    if args.variant == "byakuya_bankai_old":
        if variant_frame != 2 or set(dict(frame_scripts(main_script))) != {1, 2}:
            raise ValueError("Unexpected old Byakuya main timeline")
        # Flash seeks to the last available main_mc frame when the requested index is beyond it.
        variant_frame = 1
    constants_root = reference / "upstream/CORE_Shared/src/net/play5d/game/bvn/data"
    constants = read_constants([constants_root / "HitType.as", constants_root / "fighter/FighterHurtType.as"])
    hits = extract(main_script, variant_frame, constants)
    (scratch / "hits.json").write_text(json.dumps(hits, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    ids = scratch / "hit-ids.txt"
    ids.write_text("".join(key + "\n" for key in hits), encoding="utf-8")

    classpath = os.pathsep.join(map(str, (args.classes, jpexs / "ffdec.jar", jpexs / "lib/*")))
    extractor = [args.java, "-Xmx1g", "-cp", classpath, "ExtractFighter", source, str(variant_frame), ids, scratch]
    if args.variant == "mayuri":
        extractor += ["--actor-class", "mayuri_fla.剑八全动作_2"]
    subprocess.run([*extractor, "--metadata"], check=True, timeout=120)
    tsv = (scratch / "fighter.tsv").read_text(encoding="utf-8")
    symbols = [row.split("\t", 2)[2] for row in tsv.splitlines() if row.startswith("symbol\t")]
    authored_scripts = [scripts / Path(*symbol.split(".")).with_suffix(".as") for symbol in symbols]
    timeline_script = next((path for path in authored_scripts if path.is_file()), None)
    if timeline_script is None:
        raise ValueError(f"The fighter's SWF symbol has no decompiled class: {symbols}")
    commands = dict(frame_scripts(timeline_script))
    if args.variant in ("pain_old", "sasuke_susanoo_old"):
        recover_legacy_frame(args, source, scripts, timeline_script, scratch, commands)
    if args.variant == "uryuu_ginreigojiaku":
        change = 'parent.$fighter_ctrler.defineAction("zh2",{"power":30,"hitType":1,"hitx":0,"hity":0,"hurtTime":400,"hurtType":0,"isBreakDef":false});'
        base = hits["zh2"]
        if base["hity"] or base["hurtType"] or base["isBreakDef"]:
            raise ValueError("Unexpected starting strike for Uryuu's launch follow-up")
        replace_frame_script(commands, 461, change, "".join(
            f'parent.$fighter_ctrler.hitModel.getHitVO("zh2").{field}={value};'
            for field, value in (("power", 30), ("hitType", 1), ("hitx", 0), ("hurtTime", 400))))
        compile_uryuu_qi(commands)
    if args.variant == "gaara_old":
        replace_frame_script(commands, 693,
            'this.justHit=parent.$fighter_ctrler.justHit("sbs3");if(this.justHit)',
            'this.sandHit=0;if(parent.$fighter_ctrler.justHit("sbs3")){this.sandHit=1;}if(this.sandHit==1)')
        replace_frame_script(commands, 709, "if(!this.justHit)", "if(this.sandHit!=1)")
    if args.variant == "soifon_old":
        reference = 'parent.$mc_ctrler.getAttacker("flymc")'
        replace_frame_script(commands, 891, f'this.fmc={reference};if(this.fmc){{this.fmc.gotoAndPlay',
                             f'if({reference}){{{reference}.gotoAndPlay')
    if args.variant == "suigetsu":
        compile_water_walk(timeline_script, commands)
    if args.variant == "hinata":
        compile_hinata(timeline_script, commands)
    if args.variant == "naruto_kurama":
        compile_kurama_air_super(timeline_script, commands)
    if args.variant == "obito":
        compile_obito_kamui(timeline_script, commands)
    if args.variant == "toushirou_bankai":
        declaration = "public var cbsmc:MovieClip;"
        if declaration not in timeline_script.read_text() or not commands.get(758, "").lstrip().startswith("this.cbsmc;"):
            raise ValueError("Unexpected Hitsugaya super attacker declaration")
        commands[758] = commands[758].replace("this.cbsmc;", "", 1)
    if args.variant == "byakuya_bankai":
        compile_byakuya_backdrop(timeline_script, commands)
    backdrops = {"ulquiorra": ((885, 912), (1309, 1340)), "ulquiorra_resurreccion": ((1139, 1162),)}
    if args.variant in backdrops:
        layer = "parent.parent.parent.parent.getChildAt(0)"
        show = ("this.darkBmp=newBitmap(newBitmapData(800,600,false,3355443));"
                f'this.darkBmp.name="darkBmp";{layer}.addChildAt(this.darkBmp,1);')
        hide = f"{layer}.removeChild(this.darkBmp);"
        for start, end in backdrops[args.variant]:
            for frame, prefix, event in ((start, show, "@mapBackdrop true 3355443;"),
                                         (end, hide, "@mapBackdrop false;")):
                body = re.sub(r"\s+", "", commands.get(frame, ""))
                if not body.startswith(prefix):
                    raise ValueError(f"Unexpected Ulquiorra backdrop in frame {frame}")
                commands[frame] = event + body[len(prefix):]
    if args.variant == "ulquiorra_resurreccion":
        for frame, statement, cue in ((33, "this.channel = new dashSnd().play()", "start"),
                                      (51, "this.channel.stop()", "stop")):
            lines = list(statements(commands.get(frame, "")))
            if lines.count(statement) != 1:
                raise ValueError(f"Unexpected Ulquiorra dash sound script in frame {frame}")
            commands[frame] = commands[frame].replace(statement, "@ulquiorraDashSound " + cue, 1)
    if args.variant == "konan":
        mentions = sum(len(re.findall(r"\bCbshit\b", path.read_text(encoding="utf-8"))) for path in scripts.rglob("*.as"))
        initializer = "this.Cbshit = false;"
        if mentions != 2 or initializer not in commands.get(786, ""):
            raise ValueError("The SWF's Cbshit flag is no longer write-only")
        commands[786] = commands[786].replace(initializer, "", 1)
    if args.variant == "yoruichi":
        callback = ('functionEndBuff_yoruichi(param1:Event):*{if(parent.$fighter_ctrler.speed==13)'
                    '{parent.speedbuff=false;stage.removeEventListener(Event.ENTER_FRAME,this.EndBuff_yoruichi);}}')
        if callback not in re.sub(r"\s+", "", timeline_script.read_text(encoding="utf-8")):
            raise ValueError("Yoruichi's speed buff cleanup changed")
        # The speedUp event also sets the script's flag and resets it when speed returns to normal.
        replace_frame_script(commands, 109,
            "stage.addEventListener(Event.ENTER_FRAME,this.EndBuff_yoruichi);parent.speedbuff=true;", "")
        if any("startGlow(" in script.read_text(encoding="utf-8") for script in scripts.rglob("*.as")):
            raise ValueError("Yoruichi's timeline has a glow to stop")
        replace_frame_script(commands, 100, "parent.$effect_ctrler.endGlow();", "")
    bankai_intro = args.variant == "ogihci"
    if bankai_intro:
        # FFDec's automatic pass chooses the false branch of Boolean(this) in frame 246.
        # The raw ABC jumps over that branch on an instantiated fighter MovieClip.
        raw = scratch / "raw"
        relative_script = timeline_script.relative_to(scripts)
        symbol = ".".join(relative_script.with_suffix("").parts)
        subprocess.run([args.java, "-Xmx1g", "-jar", jpexs / "ffdec.jar", "-config", "autoDeobfuscate=0",
                        "-selectclass", symbol, "-export", "script", raw, source], check=True, timeout=180)
        idle = dict(frame_scripts(raw / "scripts" / relative_script))[246]
        if "!Boolean(this)" not in idle or list(statements(idle)) != ["parent.$mc_ctrler.idle()"]:
            raise ValueError("The raw SWF does not contain the expected frame-246 idle branch")
        commands[246] = idle
        visibility = "this.air.visible = Boolean(parent.wanKai);"
        if visibility not in commands.get(1132, ""):
            raise ValueError("Missing the authored White Ichigo intro layer rule")
        commands[1132] = commands[1132].replace(visibility, "")
    flight = args.variant == "gaara"
    water = args.variant == "suigetsu"
    twin_lion = args.variant == "hinata"
    facing_layers = ((524, "gtls"), (690, "txls")) if args.variant == "kenshin" else ()
    for frame, layer in facing_layers:
        replace_frame_script(commands, frame, f"if(parent.scaleX==-1){{this.{layer}.scaleX=-1;}}",
                             "@rememberFacing;")
    if flight:
        source_script = timeline_script.read_text(encoding="utf-8")
        for fragment in ("this.sandMc.visible = this.flyMode;", "getAction().defense = null;",
                         "this.addEventListener(Event.ENTER_FRAME,this.changeMode);"):
            if fragment not in source_script:
                raise ValueError(f"Missing authored Gaara flight rule: {fragment}")
        if any(re.search(r"\bthis\.defense\b(?!\s*=)", path.read_text(encoding="utf-8"))
               for path in scripts.rglob("*.as")):
            raise ValueError("The authored guard flag has readers")
        for frame, body in commands.items():
            body = re.sub(r"this\.defense\s*=\s*(?:true|false);", "", body)
            commands[frame] = re.sub(
                r"this\.(?:add|remove)EventListener\(Event\.ENTER_FRAME,this\.changeMode\);", "", body)
    timeline = read_timeline(scratch)
    if args.variant == "yoruichi":
        if "跳砍下" in timeline.labels:
            raise ValueError("Yoruichi's missing jump-attack label now has a timeline")
        # The one-frame binding references a missing label in the SWF.
        replace_frame_script(commands, 310, 'parent.$mc_ctrler.getAction().skill1="跳砍下";',
                             "parent.$mc_ctrler.getAction().skill1=null;")
    moves = args.moves
    if args.variant == "soifon":
        reset = "".join(f"this.mugenCombo{field}=0;" for field, _ in COMBO_LIMITS)
        branches = "".join(f'elseif(this.mugenCombo{field}>=2){{parent.$mc_ctrler.gotoAndPlay("{label}");'
                           f'this.mugenCombo{field}=0;}}' for field, label in COMBO_LIMITS)
        callback = ("functionantiMugenCombo():void{if(parent.$fighter_ctrler.getTargetState()!=21){" + reset + "}"
                    + branches + "else{this.$self_main.delayCall(this.antiMugenCombo,1);}}")
        if callback not in re.sub(r"\s+", "", timeline_script.read_text(encoding="utf-8")):
            raise ValueError("The SWF's anti-infinite-combo timer does not match the supported rule")
        moves = [*moves, *(label for _, label in COMBO_LIMITS)]
    first_attack = "招1" if args.variant.startswith("xb_") and "砍1" not in timeline.labels else "砍1"
    clips = clip_plan(timeline, commands, moves, full=args.all_actions, first_attack=first_attack)
    ranges = [str(frame) for clip in clips if clip.start for frame in (clip.start, clip.end - 1)]
    launches = detached_calls(clips, commands)
    initializers = dict(frame_scripts(main_script))
    # Transforming keeps the fighter's effect controller, including its base portrait registrations.
    faces, probes = special_art(clips, commands, initializers.get(2, "") + "\n" + initializers[variant_frame + 1])
    anchors = target_anchors(clips, commands)
    inherited_faces = faces.keys() - registered_faces(initializers[variant_frame + 1]).keys()
    screen_character = 460 if args.variant == "karin" else None  # Authored healing-flame sprite.
    objects = []
    if launches or faces or probes or anchors or flight or water or twin_lion or bankai_intro or facing_layers or screen_character is not None:
        requests = scratch / "objects.tsv"
        rows = [f"{kind}\t{frame}\t{name}\n" for kind, frame, name in dict.fromkeys(key[:3] for key in launches)]
        rows.extend(f"face\t0\t{symbol}\n" for symbol in sorted(set(faces.values())))
        rows.extend(f"probe\t0\t{name}\n" for name in probes)
        rows.extend(f"anchor\t0\t{name}\n" for name in anchors)
        if flight:
            rows.append("flightLayer\t0\tsandMc\n")
        if water:
            rows.append("waterLayer\t1586:1605\tmoveWater|idleWater\n")
        if twin_lion:
            rows.append("twinLionLayer\t1:32768\tsoushikennLeft,soushikennRight,soushikennLeft2,soushikennRight2|\n")
        if bankai_intro:
            rows.append("bankaiLayer\t1132\tair\n")
        rows.extend(f"facingLayer\t{frame}\t{layer}\n" for frame, layer in facing_layers)
        if screen_character is not None:
            rows.append(f"screenLayer\t0\t{screen_character}\n")
        if any(key[0] == "attacker" for key in launches):
            rows.extend(f"{kind}\t{frame}\t{symbol}\t{name}\n"
                        for kind, frame, symbol, name in child_requests(scripts))
        requests.write_text("".join(rows), encoding="utf-8")
        objects = ["--objects", requests]
    subprocess.run([*extractor, *objects, *ranges], check=True, timeout=180)
    baked = read_timeline(scratch)
    if (water and not baked.water_poses) or (twin_lion and not baked.twin_lion_poses):
        raise ValueError("The fighter's alternate state layers were not extracted")
    if screen_character is not None and any(clip.start <= 730 < clip.end for clip in clips) and not baked.screen_poses:
        raise ValueError("The fighter's screen-blended layer was not extracted")
    if bankai_intro and any(clip.start <= 1132 < clip.end for clip in clips) and not baked.bankai_poses:
        raise ValueError("The fighter's alternate intro layer was not extracted")
    if any(any(clip.start <= frame < clip.end for clip in clips) and frame not in baked.facing_poses
           for frame, _ in facing_layers):
        raise ValueError("Kenshin's facing layer was not extracted on its script frame")
    compile_pack(scratch, baked, commands, clips, authored_stats(main_script, variant_frame), args.output,
                 detached_scripts(baked, scripts), faces, probes, inherited_faces, flight=flight,
                 compact_textures=args.variant == "aizen_gz")


def replace_frame_script(commands, frame, old, new):
    compact = re.sub(r"\s+", "", commands[frame])
    if compact.count(old) != 1:
        raise ValueError(f"Unexpected source script at frame {frame}: {old}")
    commands[frame] = compact.replace(old, new, 1)


def recover_legacy_frame(args, source, scripts, timeline_script, scratch, commands):
    # FFDec's deobfuscation pass sometimes follows a dead Boolean(this) branch.
    relative = timeline_script.relative_to(scripts)
    symbol = ".".join(relative.with_suffix("").parts)
    raw = scratch / "raw"
    subprocess.run([args.java, "-Xmx1g", "-jar", args.jpexs.resolve() / "ffdec.jar", "-config",
                    "autoDeobfuscate=0", "-selectclass", symbol, "-export", "script", raw, source],
                   check=True, timeout=180)
    original = dict(frame_scripts(raw / "scripts" / relative))
    if args.variant == "pain_old":
        body = re.sub(r"\s+", "", original[19])
        if not body.startswith("if(!Boolean(this)){") or not body.endswith('}§§pop();parent.$mc_ctrler.loop("走");'):
            raise ValueError("Unexpected bytecode for old Pain's walk loop")
        commands[19] = 'parent.$mc_ctrler.loop("走");'
    else:
        body = re.sub(r"\s+", "", original[1206])
        if body != "§§push(parent);if(!Boolean(this)){switch(109){}}§§pop().$effect_ctrler.endBisha();":
            raise ValueError("Unexpected bytecode for old Sasuke's cut-in cleanup")
        commands[1206] = "parent.$effect_ctrler.endBisha();"


def compile_uryuu_qi(commands):
    target = re.escape("parent.$fighter_ctrler.getTargetSP().qi")
    add = re.escape("parent.$mc_ctrler.addQi")
    transfer = re.compile(rf"if\({target}>=(\d+)\)\{{{add}\(\1\);{target}={target}-\1;\}}"
                          rf"else\{{{add}\({target}\);{target}=0;\}}")
    for frame, body in commands.items():
        if "getTargetSP().qi" in body:
            compact = re.sub(r"\s+", "", body)
            commands[frame], count = transfer.subn(r"@transferQi \1;", compact)
            if not count or "getTargetSP().qi" in commands[frame]:
                raise ValueError(f"Unexpected old Uryuu Qi transfer on frame {frame}")


def compile_water_walk(path, commands):
    source = re.sub(r"\s+", "", path.read_text())
    script = (
        'functionwaterHandler(param1:Event):void{if(this.currentLabel!="水走")'
        '{this.removeEventListener(Event.ENTER_FRAME,this.waterHandler);this.$self_main.setVecX(0);return;}'
        'if(Boolean(this.$key.moveLEFT())&&!this.$key.moveRIGHT())'
        '{this.$self_main.setVecX(-8);this.$self_main.direct=-1;'
        'this.idleWater&&(this.idleWater.visible=false);this.moveWater&&(this.moveWater.visible=true);}'
        'elseif(Boolean(this.$key.moveRIGHT())&&!this.$key.moveLEFT())'
        '{this.$self_main.setVecX(8);this.$self_main.direct=1;'
        'this.idleWater&&(this.idleWater.visible=false);this.moveWater&&(this.moveWater.visible=true);}'
        'else{this.$self_main.setVecX(0);parent.$fighter_ctrler.setDirectToTarget();'
        'this.idleWater&&(this.idleWater.visible=true);this.moveWater&&(this.moveWater.visible=false);}}'
    )
    if script not in source:
        raise ValueError("Unexpected Suigetsu water-walk listener")
    replace_frame_script(commands, 1586, "this.addEventListener(Event.ENTER_FRAME,this.waterHandler);", "@waterWalk start;")
    commands[1606] = "@waterWalk stop;" + commands[1606]


def compile_hinata(path, commands):
    source = re.sub(r"\s+", "", path.read_text())
    callbacks = (
        'functionendSoushikenn():void{this.soushikenn=false;}',
        'functionendSoushikennCD():void{this.soushikennCD=false;if(this.$self_main.actionState==0||this.$self_main.actionState==20)'
        '{parent.$mc_ctrler.setZhao3();parent.$mc_ctrler.setBishaSUPER();}}',
        'functionsbsHitsUp():void{if(parent.$fighter_ctrler.justHit("sbs1")){'
        'parent.$fighter_ctrler.getTargetSP().isAllowBeHit=true;this.$self_main.delayCall(this.sbsHitsUp,1);}}',
        'functiondoKuramaChakra():void{if(this.kuramaChakra){parent.$mc_ctrler.getAction().bishaSUPER=null;'
        'parent.$mc_ctrler.getAction().zhao3=null;parent.$effect_ctrler.startGlow(16711680);'
        'this.$self_main.isSteelBody=true;this.$self_main.isSuperSteelBody=true;this.$self_main.delayCall(this.doKuramaChakra,1);}}',
        'functionendKuramaChakra():void{parent.$effect_ctrler.endGlow();if(this.$self_main.actionState==0||this.$self_main.actionState==20)'
        '{parent.$mc_ctrler.setBishaSUPER();parent.$mc_ctrler.setZhao3();this.$self_main.isSteelBody=false;'
        'this.$self_main.isSuperSteelBody=false;}this.kuramaChakra=false;}',
    )
    if not all(callback in source for callback in callbacks):
        raise ValueError("Unexpected Hinata mode callbacks")
    fps = 'getDefinitionByName("net.play5d.game.bvn::MainGame").I.getFPS()'
    replace_frame_script(commands, 547, 'this.$self_main.getBuffCtrl().speedUp(2.5,15);'
                         f'this.$self_main.delayCall(this.endSoushikenn,15*{fps});'
                         f'this.$self_main.delayCall(this.endSoushikennCD,25*{fps});', '@hinataLions;')
    replace_frame_script(commands, 858, 'this.$self_main.delayCall(this.sbsHitsUp,1);', '@hinataFollowHit;')
    # FighterAction.clearAction resets bishaSUPERQi to 300 before the script reads it.
    replace_frame_script(commands, 920, 'parent.$mc_ctrler.idle();parent.$mc_ctrler.addQi(parent.$mc_ctrler.getAction().bishaSUPERQi);',
                         'parent.$mc_ctrler.addQi(300);parent.$mc_ctrler.idle();')
    replace_frame_script(commands, 996, 'this.$self_main.getBuffCtrl().attackUp(0.2,5);this.kuramaChakra=true;'
                         'this.$self_main.delayCall(this.doKuramaChakra,1);'
                         f'this.$self_main.delayCall(this.endKuramaChakra,5*{fps});', '@hinataChakra;')


def compile_kurama_air_super(path, commands):
    source = re.sub(r"\s+", "", path.read_text())
    handler = (
        'functionposition(param1:Event):*{this.kiY=this.ki.y;this.kiYY=this.kiY=this.kiY-125;'
        'if(parent.$fighter_ctrler.self.mc.currentLabel=="空中必杀")'
        '{parent.$fighter_ctrler.getTargetSP().getCurrentTarget().y=this.kiYY;}'
        'else{stage.removeEventListener(Event.ENTER_FRAME,this.position);}}'
    )
    if handler not in source:
        raise ValueError("Unexpected Kurama air-super position listener")
    replace_frame_script(commands, 1088, 'stage.addEventListener(Event.ENTER_FRAME,this.position);',
                         '@followAttackerY "ki" -125;')


def compile_obito_kamui(path, commands):
    source = re.sub(r"\s+", "", path.read_text())
    callback = (
        'functionKamuiMode():void{if(this.isOnKamuiMode){parent.$mc_ctrler.getAction().zhao3=null;'
        'if(!(this.currentLabel=="站立"||this.currentLabel=="走"||this.currentLabel=="招3")||this.$self_main.energy<=20)'
        '{this.isOnKamuiMode=false;if(this.isEndKamuiMode){this.isEndKamuiMode=false;this.recoveryAlpha();'
        'parent.$fighter_ctrler.setCross(false);this.$self_main.isAllowBeHit=true;parent.$effect_ctrler.endShadow();}return;}'
        'if(this.currentLabel=="走"){parent.$effect_ctrler.shadow(0);}this.$self_main.useEnergy(0.7);'
        'if(this.alpha>0.5){this.alpha-=0.05;}this.$self_main.isAllowBeHit=false;parent.$fighter_ctrler.setCross(true);'
        'this.$self_main.delayCall(this.KamuiMode,1);}}'
    )
    recover = ('functionrecoveryAlpha():void{if(this.alpha<1){this.alpha+=0.05;'
               'this.$self_main.delayCall(this.recoveryAlpha,1);}}')
    entry = ("this.$self_main.delayCall(this.KamuiMode,1)", "this.isOnKamuiMode = true", "this.isEndKamuiMode = true")
    if callback not in source or recover not in source or tuple(statements(commands.get(325, ""))) != entry:
        raise ValueError("Unexpected Obito Kamui callback")
    commands[325] = "@kamui;"


def compile_byakuya_backdrop(path, commands):
    stage = "parent.parent.parent.parent"
    create = (
        "this.byakuyaSuperBlackBack=newthis.BlackBack();"
        'this.byakuyaSuperBlackBack.name="byakuyaSuperBlackBack";'
        f"{stage}.addChildAt(this.byakuyaSuperBlackBack,0);"
        f"{stage}.getMap().setVisible(false);"
        "this.addEventListener(Event.ENTER_FRAME,this.delBlackHandler);"
    )
    restore = (
        f"this.byakuyaSuperBlackBack.fadOut();{stage}.getMap().setVisible(true);"
        "this.removeEventListener(Event.ENTER_FRAME,this.delBlackHandler);"
    )
    handler = ('functiondelBlackHandler(param1:Event):void{if(this.currentLabel!="超必杀"&&'
               f'this.currentLabel!="超必杀1"&&Boolean({stage}.getChildByName("byakuyaSuperBlackBack")))'
               f'{{{restore}}}}}')
    source = re.sub(r"\s+", "", path.read_text(encoding="utf-8"))
    compact = {frame: re.sub(r"\s+", "", commands.get(frame, "")) for frame in (857, 906, 1028)}
    if (handler not in source or compact[857] != f"this.BlackBack={stage}.getChildAt(0).constructorasClass;" or
            not compact[906].startswith(create) or compact[1028] != restore):
        raise ValueError("The SWF's backdrop setup or interruption cleanup differs from the supported scene rule")
    commands[857] = ""
    commands[906] = "@mapBackdrop true; " + compact[906][len(create):]
    commands[1028] = "@mapBackdrop false;"


def detached_scripts(timeline, scripts):
    result = {}
    for key, sprite in timeline.attackers.items():
        authored = [scripts / Path(*symbol.split(".")).with_suffix(".as") for symbol in sprite.symbols]
        path = next((path for path in authored if path.is_file()), None)
        if sprite.symbols and path is None:
            raise ValueError(f"Missing ActionScript class for attacker {key}: {sprite.symbols}")
        commands = dict(frame_scripts(path)) if path else {}
        if sprite.symbols == ["itachi_old_fla.卡卡西_上必杀_狗S_13"]:
            # The attacker is indexed by MovieClip.name. This dynamic _name field has no reader.
            replace_frame_script(commands, 43, '_name="bsatm";', "")
        if sprite.symbols == ["gaara_old_fla.元件23_10"]:
            replace_frame_script(commands, 126,
                "this.fighterCtrler=this.$attacker_ctrler.owner_fighter_ctrler;"
                "if(this.fighterCtrler){if(this.fighterCtrler.target){this.fighterCtrler.target.visible=true;}}",
                "@sandShowTarget;")
        if sprite.symbols == ["juggo_old_fla.招1刀光副本3_12"]:
            replace_frame_script(commands, 6,
                'if(this.$attacker_ctrler.owner_mc_ctrler){this.tu=this.$attacker_ctrler.owner_mc_ctrler.getAttacker("tu");'
                'if(this.tu){this.tu.gotoAndPlay("bow");}}',
                '@playSibling "tu" "bow";')
        if sprite.symbols == ["tensa_fla.Ueff_39"]:
            replace_frame_script(commands, 1,
                                 "this.selfEffect=new(this.$self_main.getCtrler().effect.constructorasClass)(this.$self_main);"
                                 "this.selfEffect.shadow(60,-180,-150);", "@attackerShadow;")
            replace_frame_script(commands, 10, "this.selfEffect.endShadow();", "")
        if sprite.symbols == ["naruto_hermit_fla.ki_65"]:
            replace_frame_script(commands, 9,
                                 'if(this.$attacker_ctrler.owner_fighter_ctrler.self.mc.currentLabel=="空中必杀")'
                                 '{this.$attacker_ctrler.owner_mc_ctrler.idle();}', '@recoverAirSuperOwner;')
        if sprite.symbols == ["suigetsu_fla.mc_021_16"]:
            owner = 'this.$attacker_ctrler.owner_fighter_ctrler'
            replace_frame_script(commands, 3,
                                 f'if({owner}.self.mc.currentLabel=="砍技3"||{owner}.self.mc.currentLabel=="水走")'
                                 f'{{{owner}.getTargetSP().getCurrentTarget().useEnergy(1.5);}}'
                                 'else{this.$attacker_ctrler.removeSelf();}', '@waterEnergy;')
        if sprite.symbols == ["test2_fla.SBS_39"]:
            source = re.sub(r"\s+", "", path.read_text(encoding="utf-8"))
            reset = "this.$attacker_ctrler.owner_fighter_ctrler.self.justHit=false;"
            end = ("this.removeEventListener(Event.ENTER_FRAME,follow);" + reset +
                   "this.fighterCtrler=this.$attacker_ctrler.owner_fighter_ctrler;"
                   "if(this.fighterCtrler){if(this.fighterCtrler.target){this.fighterCtrler.target.visible=true;}}")
            if (re.sub(r"\s+", "", commands[76]) != reset or
                    re.sub(r"\s+", "", commands[79]) != end or "functionfollow(" in source):
                raise ValueError(f"Unexpected sand burial end script in {path}")
            commands[76] = ""  # No source action reads the display object's justHit property.
            commands[79] = ""  # The authored getlex of the absent follow callback aborts this frame script.
        if sprite.symbols == ["byakuya_fla.sakura1_39"]:
            if list(statements(commands.get(11, ""))) != ["parent.$mc_ctrler.endAct()"]:
                raise ValueError("Unexpected Byakuya attacker frame 11")
            # GameStage reparents the attack to its player layer, which has no fighter controller.
            commands[11] = ""
        if sprite.symbols == ["starrk_fla.砍技1效果_9"]:
            source_branch = ('this.self=this.$attacker_ctrler.owner_fighter_ctrler.getTargetSP().getCurrentTarget();'
                             'this.selfState=this.self.actionState;'
                             'if(this.selfState!=0&&this.selfState!=0&&this.selfState!=20&&this.selfState!=21&&'
                             'this.selfState!=22&&this.selfState!=23&&this.selfState!=24)'
                             '{this.$attacker_ctrler.gotoAndPlay("no_hit");}')
            if re.sub(r"\s+", "", commands.get(42, "")) != source_branch:
                raise ValueError(f"Unexpected Starrk follow-up gate in {path}")
            commands[42] = '@gotoWhenOwnerBusy "no_hit"'
        if sprite.symbols == ["aizen_fla.blackBox_39"]:
            compile_aizen_box(path, commands, timeline.labels)
        if sprite.symbols == [AIZEN_WALL_CLASS]:
            compile_aizen_wall(path, commands)
        elif any("this.wallHandle" in body for body in commands.values()):
            compile_sand_wall(path, commands)
        if any(body == "this.follow();" for body in commands.values()):
            source = re.sub(r"\s+", "", path.read_text(encoding="utf-8"))
            controller = "this.$attacker_ctrler.owner_fighter_ctrler"
            handler = (f'functionfollow():*{{if(Boolean({controller}.justHit("zh3",false))||'
                       f'Boolean({controller}.justHit("zh31",false))){{{controller}.getTargetSP().y=this.y;}}}}')
            if handler not in source or "addEventListener(Event.ENTER_FRAME,this.follow)" in source:
                raise ValueError(f"Uncompiled sand cage contact callback in {path}")
            for frame, body in commands.items():
                if body == "this.follow();":
                    commands[frame] = "@sandCageAlign"
                elif body == "stage.removeEventListener(Event.ENTER_FRAME,this.follow);":
                    commands[frame] = ""
        if any("this.addEventListener(Event.ENTER_FRAME,this.follow)" in body for body in commands.values()):
            source = re.sub(r"\s+", "", path.read_text(encoding="utf-8"))
            if SAND_FOLLOW_HANDLER not in source:
                raise ValueError(f"Uncompiled sand capture listener in {path}")
            end = "this.removeEventListener(Event.ENTER_FRAME,this.follow);"
            begin = end + "this.addEventListener(Event.ENTER_FRAME,this.follow);"
            show = ("this.fighterCtrler=this.$attacker_ctrler.owner_fighter_ctrler;"
                    "if(this.fighterCtrler){if(this.fighterCtrler.target){this.fighterCtrler.target.visible=true;}}")
            for frame, body in commands.items():
                compact = re.sub(r"\s+", "", body)
                marker = {begin: "@startSandFollow", end: "@endSandFollow", show: "@sandShowTarget"}.get(compact)
                if marker:
                    commands[frame] = marker
        if any("this.qi_lock" in body for body in commands.values()):
            controller = "this.$attacker_ctrler.owner_fighter_ctrler"
            qi = controller + ".getTargetSP().getCurrentTarget().qi"
            handler = (f"functionqi_lock(param1:Event):*{{if({controller}.getTargetState()!=20)"
                       f"{{if({qi}>this.qi_now){{{qi}=this.qi_now;}}}}}}")
            if handler not in re.sub(r"\s+", "", path.read_text(encoding="utf-8")):
                raise ValueError(f"Uncompiled Qi listener in {path}")
            for frame, body in commands.items():
                compact = re.sub(r"\s+", "", body)
                if compact == f"this.qi_now={qi};this.addEventListener(Event.ENTER_FRAME,this.qi_lock);":
                    commands[frame] = "@startOwnerQiCap"
                elif compact == "this.removeEventListener(Event.ENTER_FRAME,this.qi_lock);":
                    commands[frame] = "@endOwnerQiCap"
        result[key] = commands
    return result


def compile_aizen_box(path, commands, fighter_labels):
    source = re.sub(r"\s+", "", path.read_text(encoding="utf-8"))
    listener = ('functionblackBox(param1:Event):*{if(this.$tc.self.mc.currentLabel!="被打")'
                '{this.$tc.getMcCtrl().endAct();}}')
    if listener not in source or "bs" in fighter_labels:
        raise ValueError(f"Unexpected Black Coffin listener or follow-up clip in {path}")
    translations = {
        1: ("this.$ec=$ac.owner_fighter_ctrler.getEffectCtrl();this.$fc=$ac.owner_fighter_ctrler;"
            "this.$mc=$ac.owner_mc_ctrler;this.$tc=this.$fc.target.$fighter_ctrler;", ""),
        3: ("parent.setChildIndex(this,this.$tc.self);", "parent.setChildIndex(this,0);"),
        11: ("parent.swapChildren(this,this.$tc.self);", "@swapTargetLayer"),
        16: ("this.$ec.slowDown(0.3);", "this.$attacker_ctrler.effect.slowDown(0.3);"),
        59: ("this.addEventListener(Event.ENTER_FRAME,this.blackBox);", "@stopTargetAction true"),
        # The pinned MovieClip has no "bs" label. FighterMC catches the failed jump in Flash release builds.
        72: ('if(this.$fc.self.mc.currentLabel=="超必杀"||this.$fc.self.mc.currentLabel=="bs")'
             '{this.$mc.gotoAndPlay("bs");}', ""),
        85: ("this.removeEventListener(Event.ENTER_FRAME,this.blackBox);", "@stopTargetAction false"),
        86: ('if(this.$tc.self.mc.currentLabel=="站立"||this.$tc.self.mc.currentLabel=="走")'
             '{this.$tc.getMcCtrl().setDefense();}', "@allowTargetGuard"),
    }
    for frame in range(26, 59):
        authored = "this.$fc.getTargetSP().isAllowBeHit=true;"
        translations[frame] = (authored, "@allowTargetHit")
    translations[29] = (
        "this.$ec.shake(7,7);this.$fc.getTargetSP().isAllowBeHit=true;",
        "this.$attacker_ctrler.effect.shake(7,7); @allowTargetHit",
    )
    if set(commands) != set(translations):
        raise ValueError(f"Unexpected Black Coffin script frames in {path}")
    for frame, (authored, compiled) in translations.items():
        if re.sub(r"\s+", "", commands[frame]) != authored:
            raise ValueError(f"Unexpected Black Coffin script at frame {frame} in {path}")
        commands[frame] = compiled


def compile_aizen_wall(path, commands):
    source = re.sub(r"\s+", "", path.read_text(encoding="utf-8"))
    target = "this.$attacker_ctrler.owner_fighter_ctrler.getTargetSP()"
    left, top = "this.wallBody.x+this.x", "this.wallBody.y+this.y"
    width, height = "this.wallBody.width", "this.wallBody.height"
    expected = (
        "functionwallHandler(param1:Event=null):void{if(this.wallBody){"
        f"if({target}.y>{top}&&{target}.y<{top}+{height}){{"
        f"if({target}.x>{left}&&{target}.x<{left}+{width}){{"
        f"if({target}.x>{left}+0.5*{width}){{{target}.x={left}+{width};}}"
        f"else{{{target}.x={left};}}"
        "}}}}"
    )
    if expected not in source or list(statements(commands.get(5, ""))) != ["this.wallHandler()"]:
        raise ValueError(f"Uncompiled one-frame wall push in {path}")
    commands[5] = "@aizenWall"


def compile_sand_wall(path, commands):
    source = re.sub(r"\s+", "", path.read_text(encoding="utf-8"))
    controller = "this.$attacker_ctrler.owner_fighter_ctrler"
    declarations = []
    for suffix in ("", "_mc"):
        for axis, edge in (("X", "left"), ("X", "right"), ("Y", "up"), ("Y", "down")):
            declarations.append(f"this.target_{axis}{suffix}_{edge}=this.{axis.lower()}+this.LINE_{edge.upper()};")
    checks = []
    for edge, inside in (("left", ">"), ("right", "<")):
        for suffix, target in (("", controller + ".getTargetSP()"), ("_mc", controller + ".target")):
            x, y = f"{target}.x", f"{target}.y"
            edge_x = f"this.target_X{suffix}_{edge}"
            upper, lower = f"this.target_Y{suffix}_up", f"this.target_Y{suffix}_down"
            center_test = f"this.x>{x}" if edge == "left" else f"{x}>this.x"
            bound_test = f"{x}>{edge_x}" if inside == ">" else f"{edge_x}>{x}"
            checks.append(f"if({center_test}){{if({bound_test}){{if({lower}>{y}&&{y}>{upper}){{{x}={edge_x};}}}}}}")
    expected = "functionwallHandle(param1:Event):*{" + "".join(declarations + checks) + "}"
    if expected not in source:
        raise ValueError(f"Uncompiled sand wall listener in {path}")
    enable = ("this.LINE_LEFT=-19;this.LINE_RIGHT=27;this.LINE_UP=-107;this.LINE_DOWN=5;"
              "this.$attacker_ctrler.setCrossMap(true);"
              f"if({controller}.getTargetSP().getCurrentTarget().direct==1)"
              "{this.LINE_LEFT=-19;this.LINE_RIGHT=27;}else{this.LINE_LEFT=-27;this.LINE_RIGHT=19;}"
              "stage.addEventListener(Event.ENTER_FRAME,this.wallHandle);")
    for frame, body in commands.items():
        compact = re.sub(r"\s+", "", body)
        if compact == enable:
            commands[frame] = "this.$attacker_ctrler.setCrossMap(true); @sandWall"
        elif compact == "stage.removeEventListener(Event.ENTER_FRAME,this.wallHandle);":
            commands[frame] = "@clearSandWall"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("variant")
    parser.add_argument("output", type=Path)
    parser.add_argument("--reference", type=Path, default=Path("reference"))
    parser.add_argument("--jpexs", type=Path, required=True)
    parser.add_argument("--classes", type=Path, required=True)
    parser.add_argument("--java", default="java")
    parser.add_argument("--scratch", type=Path, required=True)
    parser.add_argument("--decompiled", type=Path)
    parser.add_argument("--moves", nargs="*", default=[])
    parser.add_argument("--internal-frame", type=int, choices=range(2, 9))
    parser.add_argument("--all-actions", action="store_true")
    build(parser.parse_args())

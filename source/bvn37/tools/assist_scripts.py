"""Translate the remaining assist callbacks after checking their original scripts."""
import re

from fighter_actions import block_end
from build_actions import replace_frame_script


def compact(text):
    return re.sub(r"\s+", "", text)


def method(text, name):
    found = re.search(r"\bfunction\s+" + re.escape(name) + r"\([^)]*\)\s*:\s*\w+\s*\{", text)
    if not found:
        raise ValueError(f"Missing assist callback {name}")
    start = found.end() - 1
    return compact(text[start + 1:block_end(text, start) - 1])


def require_method(text, name, expected):
    if method(text, name) != compact(expected):
        raise ValueError(f"Unexpected assist callback {name}")


def prepare_assist_scripts(name, text, frames):
    if name == "temari":
        require_method(text, "Fuuton", """
            var _loc3_:Number = NaN;
            var _loc4_:Number = NaN;
            var _loc1_:int = int(this.$assisterCtrler.getTarget().heavy);
            var _loc2_:int = int(this.$assisterCtrler.getTarget().speed);
            switch(int(_loc1_)) {
                case 1: _loc3_ = 0.8; break;
                case 2: _loc3_ = 0.65; break;
                case 3: _loc3_ = 0.5; break;
                case 4: _loc3_ = 0.35; break;
                case 5: _loc3_ = 0.2; break;
                default: _loc3_ = 0.15;
            }
            _loc4_ = _loc2_ * _loc3_;
            if(this.scaleX > 0) {
                this.$assisterCtrler.owner_fighter_ctrler.getTargetSP().x =
                    this.$assisterCtrler.owner_fighter_ctrler.getTargetSP().x + _loc4_;
                this.$assisterCtrler.owner_fighter_ctrler.target.x += _loc4_;
            } else {
                this.$assisterCtrler.owner_fighter_ctrler.getTargetSP().x =
                    this.$assisterCtrler.owner_fighter_ctrler.getTargetSP().x - _loc4_;
                this.$assisterCtrler.owner_fighter_ctrler.target.x -= _loc4_;
            }
        """)
        for frame in range(35, 146):
            replace_frame_script(frames, frame, "this.Fuuton();", "@assistWind;")
    elif name == "gaara":
        if len(re.findall(r"\bjudge\b", text)) != 1:
            raise ValueError("Gaara's assist judge field has acquired a reader")
        replace_frame_script(frames, 44, "judge=true;", "")
    elif name == "byakuya":
        require_method(text, "HurtStateEvent", """
            if(this.$assisterCtrler.getOwner().getCtrler().getTargetState() != 21) {
                this.$assisterCtrler.getOwner().getCtrler().getMcCtrl().getAttacker("lzgl").gotoAndPlay("end");
                this.removeEventListener(Event.ENTER_FRAME,this.HurtStateEvent);
                this.$assisterCtrler.getTarget().getCtrler().getMcCtrl().isApplyG(true);
            }
        """)
        replace_frame_script(frames, 50, "this.judge=false;", "@assistJudge false;")
        replace_frame_script(frames, 52, "this.judge=true;", "@assistJudge true;")
        replace_frame_script(frames, 60, compact("""
            if(this.$assisterCtrler.getOwner().getCtrler().getTargetState() == 21 && this.judge) {
                this.$assisterCtrler.getTarget().getCtrler().getMcCtrl().isApplyG(false);
            }
            if(this.judge) { this.addEventListener(Event.ENTER_FRAME,this.HurtStateEvent); }
        """), '@assistHurtBind "lzgl" "end";')
    elif name == "yls":
        owner = "this.$assisterCtrler.getOwner()"
        target = "this.$assisterCtrler.getTarget()"
        own = "this.$assisterCtrler.getSelf()"
        before = (f"{own}.x={owner}.x;{own}.y={owner}.y;"
                  f"this.ax={target}.x;this.ay={target}.y;this.bx={owner}.x;this.by={owner}.y;")
        after = (f"{target}.x=this.bx;{target}.y=this.by;{owner}.x=this.ax;{owner}.y=this.ay;"
                 f"{own}.x={owner}.x;{own}.y={owner}.y;")
        replace_frame_script(frames, 9, before, "@assistSwapRemember;")
        replace_frame_script(frames, 10, after, "@assistSwap;")
    return frames


def prepare_assist_attackers(name, timeline, scripts, commands):
    if name not in ("renji", "naruto"):
        return
    for key, frames in commands.items():
        sprite = timeline.attackers[key]
        path = scripts.joinpath(*sprite.symbols[0].split(".")).with_suffix(".as")
        text = path.read_text(encoding="utf-8")
        if name == "renji":
            require_method(text, "moveDirect", """
                if(this.currentLabel != "攻击") {
                    if(Math.abs(this.$self_main.x - this.$self_main.getOwner().getCtrler().getTarget().x) < 10) {
                        this.$attacker_ctrler.gotoAndPlay("攻击");
                    } else if(this.$self_main.x < this.$self_main.getOwner().getCtrler().getTarget().x) {
                        this.$attacker_ctrler.move(5 * this.$self_main.direct,0);
                        this.$self_main.delayCall(this.moveDirect,1);
                    } else {
                        this.$attacker_ctrler.move(-5 * this.$self_main.direct,0);
                        this.$self_main.delayCall(this.moveDirect,1);
                    }
                }
            """)
            replace_frame_script(frames, 2, "this.$self_main.delayCall(this.moveDirect,1);",
                                 '@attackerHoming "攻击" 5 10;')
        else:
            require_method(text, "checkDelSelf", """
                if(this.$self_main.getIsTouchSide()) { this.$attacker_ctrler.removeSelf(); }
                else { this.$self_main.delayCall(this.checkDelSelf,1); }
            """)
            replace_frame_script(frames, 2, "this.$self_main.delayCall(this.checkDelSelf,1);",
                                 "@attackerRemoveAtSide;")
            replace_frame_script(frames, 3, compact("""
                switch(Math.floor(Math.random() * 3) + 1) {
                    case 1: this.$attacker_ctrler.gotoAndPlay("水平突进"); break;
                    case 2: this.$attacker_ctrler.gotoAndPlay("向下突进"); break;
                    case 3: this.$attacker_ctrler.gotoAndPlay("向上突进");
                }
            """), '@attackerRandomGoto "水平突进" "向下突进" "向上突进";')
            for frame, first, second, vertical in ((4, 12, 16, 0), (18, 10, 8, 4), (32, 10, 8, -4)):
                replace_frame_script(frames, frame,
                    f"this.$attacker_ctrler.move(Math.random()>0.5?{first}:{second},{vertical});",
                    f"@attackerRandomMove {first} {second} {vertical};")

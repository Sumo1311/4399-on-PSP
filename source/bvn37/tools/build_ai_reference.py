#!/usr/bin/env python3
"""Execute the original AS3 decision methods as JavaScript with typed game objects replaced by observations."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import re


def body_end(text, start):
    depth, index = 1, start
    quote = None
    while index < len(text):
        char = text[index]
        if quote:
            if char == "\\":
                index += 2
                continue
            if char == quote:
                quote = None
        elif text.startswith("//", index):
            end = text.find("\n", index)
            index = len(text) if end < 0 else end
            continue
        elif text.startswith("/*", index):
            index = text.index("*/", index + 2) + 2
            continue
        elif char in "\"'":
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index
        index += 1
    raise ValueError("Unterminated AS3 method")


def strip_comments(text):
    output, index, quote = [], 0, None
    while index < len(text):
        char = text[index]
        if quote:
            output.append(char)
            if char == "\\" and index + 1 < len(text):
                output.append(text[index + 1])
                index += 2
                continue
            if char == quote:
                quote = None
        elif text.startswith("//", index):
            end = text.find("\n", index)
            index = len(text) if end < 0 else end
            continue
        elif text.startswith("/*", index):
            end = text.index("*/", index + 2) + 2
            output.append(" " + "\n" * text[index:end].count("\n"))
            index = end
            continue
        else:
            output.append(char)
            if char in "\"'":
                quote = char
        index += 1
    return "".join(output)


def methods(text, constructor):
    text = strip_comments(text)
    pattern = re.compile(r"(?:public|private|protected)\s+(?:(?:override|final|static)\s+)*function\s+(\w+)\s*\(([^)]*)\)\s*(?::\s*(?:Vector\.<[^>]+>|[\w.]+))?\s*\{")
    output = {}
    for match in pattern.finditer(text):
        name, args = match.group(1, 2)
        if name == constructor:
            continue
        body = text[match.end():body_end(text, match.end())]
        body = body.replace("super.render()", "__baseRender()")
        body = re.sub(r"for\s+each\s*\(\s*var\s+(\w+)\s*:\s*[\w.]+\s+in\s+([^)]*)\)", r"for (var \1 of \2)", body)
        body = re.sub(r"\bvar\s+(\w+)\s*:\s*(?:Vector\.<[^>]+>|[\w.]+)", r"var \1", body)
        body = body.replace("_target as FighterMain", "(_target instanceof FighterMain ? _target : null)")
        body = re.sub(r"\b(\w+)\s+is\s+(\w+)", r"\1 instanceof \2", body)
        body = re.sub(r"\b(\w+)\s+as\s+(\w+)", r"\1", body)
        params = [arg.strip().split(":")[0] for arg in args.split(",") if arg.strip()]
        output[name] = dict(params=params, body=body)
    return output


BOOTSTRAP = r'''
export const intents = ['moveLeft','moveRight','jump','jumpDown','dash','downJump','defense','attack',
 'attackAIR','skillAIR','bishaAIR','skill1','skill2','zhao1','zhao2','zhao3','catch1','catch2',
 'bisha','bishaUP','bishaSUPER','assist','specialSkill','ghostStep','ghostJump','ghostJumpDowm'];
function avmSort(values, compare) {
 const swap=(a,b)=>{const tmp=values[a];values[a]=values[b];values[b]=tmp;};
 function sort(start, length) {
  if(length<2)return;
  if(length===2){if(compare(values[start],values[start+1])>0)swap(start,start+1);return;}
  if(length===3){
   if(compare(values[start],values[start+1])>0)swap(start,start+1);
   if(compare(values[start+1],values[start+2])>0){swap(start+1,start+2);if(compare(values[start],values[start+1])>0)swap(start,start+1);}
   return;
  }
  swap(start,start+Math.floor(length/2));
  let left=0,right=length;
  for(;;){
   do{++left;}while(left<length&&compare(values[start+left],values[start])<=0);
   do{--right;}while(right!==0&&compare(values[start+right],values[start])>=0);
   if(right<left)break;
   swap(start+left,start+right);
  }
  swap(start,start+right);
  let first=[start,right],second=[start+left,length-left];
  if(first[1]>=second[1])[first,second]=[second,first];
  sort(...first);sort(...second);
 }
 sort(0,values.length);
}
Object.defineProperty(Array.prototype,'sortOn',{value:function(field,flags){
 if(field!=='order'||flags!==2)throw Error('Unexpected AS3 sort');
 avmSort(this,(a,b)=>b[field]-a[field]);return this;
}});
Array.DESCENDING=2;
export function sortOrderReference(priorities){
 const values=priorities.map((order,index)=>({order,index}));
 values.sortOn('order',Array.DESCENDING);return values.map(value=>value.index);
}
class FighterMain {}
class Bullet {}
class FighterAttacker {}
class Assister {}
const commandFlags={attack:0,skill1:1,skill2:2,attackAIR:3,zhao1:7,zhao2:8,zhao3:9,
 skillAIR:13,bisha:14,bishaUP:15,bishaSUPER:16,bishaAIR:21};
const hitCommands={kj1:1,kj2:2,zh1:7,zh2:8,zh3:9,tz:13,bs:14,sbs:15,cbs:16,kbs:21};
const rangeCommands={ATTACK:0,SKILL_1:1,SKILL_2:2,ATTACK_AIR:3,ZHAO_1:7,ZHAO_2:8,
 ZHAO_3:9,SKILL_AIR:13,BISHA:14,BISHA_UP:15,BISHA_SUPER:16,BISHA_AIR:21};
const rangeIndices=Object.fromEntries(Object.entries(rangeCommands).map(([key,value])=>[source.ranges[key],value]));
export const sourceHashes=source.hashes;
export function createPolicy(level,seed){
 let random=seed>>>0||1,current,target,threats;
 const action={};
 const self=new FighterMain();
 const ctx={AILevel:level,_fighter:self,_fighterAction:null,_target:null,_targetFighter:null,
  _isConting:false,_breakActCache:{},_hitDownActCache:{},_contOrder:[],_distancePoint:{x:0,y:0},
  _moveKeep:40,_catchMoveKeep:15,_dashKeep:200,_jumpKeep:50,_moveFrame:0,_defenseFrame:0,_rateObj:{},
  GameConfig:{...source.clocks},FighterActionState:source.states,FighterHitRange:source.ranges,
  FighterSpecialFrame:{ATTACK:'basic-attack'},FighterMain,Bullet,FighterAttacker,Assister};
 for(const name of intents)ctx[name]=false;
 for(const [name,values] of Object.entries(source.rates))ctx[name]=Object.freeze(values);
 ctx.Math=Object.create(Math);
 ctx.Math.random=()=>{random^=random<<13;random^=random>>>17;random^=random<<5;random>>>=0;return random/4294967296;};
 for(const [name,method] of Object.entries(source.base))ctx[name]=new Function(...method.params,'with(this){'+method.body+'}').bind(ctx);
 ctx.__baseRender=ctx.render;
 for(const [name,method] of Object.entries(source.logic))ctx[name]=new Function(...method.params,'with(this){'+method.body+'}').bind(ctx);
 const mc={getAction:()=>action,getCurAction:()=>current.basicAttack?'basic-attack':'other'};
 const controller={getMcCtrl:()=>mc,hitModel:{getHitVOLike:id=>{
  const index=hitCommands[id];if(index===undefined)return [];
  const bits=current.actions[index];return [{isBreakDef:!!(bits&4),hurtType:bits&8?1:0}];
 }}};
 self.getCtrler=()=>controller;
 self.getCurrentTarget=()=>target;
 self.getTargets=()=>[target,...threats];
 self.getHitRange=id=>rangeIndices[id]===undefined?null:{matches:!!(current.actions[rangeIndices[id]]&2)};
 return {step(observation){
  current=observation;
  ctx.GameConfig.FPS_GAME=current.gameHz;
  Object.assign(self,current.self,{hp:current.self.health,hpMax:current.self.maxHealth,
    direct:current.self.facing,actionState:current.self.state});
  target=current.target.fighter?new FighterMain():{};
  Object.assign(target,current.target,{direct:current.target.facing,actionState:current.target.state,
    isAllowBeHit:current.targetHittable,getBodyArea:()=>current.targetBody?{intersects:range=>range.matches}:null});
  for(const [name,index] of Object.entries(commandFlags))action[name]=!!(current.actions[index]&1);
  threats=current.threats.map(view=>{
   const value=view.kind===1?new Bullet():view.kind===2?new FighterAttacker():view.kind===3?new Assister():{};
   Object.assign(value,view);value.isAttacking=view.kind===1?()=>view.attacking:view.attacking;return value;
  });
  ctx.render();
  let mask=0;for(let index=0;index<intents.length;++index)if(ctx[intents[index]])mask|=1<<index;
  return {mask:mask>>>0,random:random>>>0,movementFrames:ctx._moveFrame};
 }};
}
'''


def build(reference, output):
    kernel = reference / "upstream/CORE_KernelLogic/src/net/play5d/game/bvn/fighter/ctrler/ai"
    shared = reference / "upstream/CORE_Shared/src/net/play5d/game/bvn/data/fighter"
    paths = {"logic":kernel / "FighterAILogic.as", "base":kernel / "FighterAILogicBase.as",
             "states":shared / "FighterActionState.as", "ranges":shared / "FighterHitRange.as",
             "clocks":reference / "upstream/CORE_KernelLogic/src/net/play5d/game/bvn/GameConfig.as"}
    texts = {key:path.read_text() for key,path in paths.items()}
    data = dict(
        logic=methods(texts["logic"], "FighterAILogic"), base=methods(texts["base"], "FighterAILogicBase"),
        rates={name:[float(number) for number in values.split(",")]
               for name,values in re.findall(r"private static const (R\w+):Array\s*=\s*\[([^]]+)\];",texts["logic"])},
        states={name:int(value) for name,value in re.findall(r"public static const (\w+):int\s*=\s*(-?\d+);",texts["states"])},
        ranges=dict(re.findall(r"public static const (\w+):String\s*=\s*'([^']+)';",texts["ranges"])),
        clocks={name:int(value) for name,value in re.findall(r"public static (?:var|const) (FPS_GAME|FPS_ANIMATE):int\s*=\s*(\d+);",texts["clocks"])},
        hashes={key:hashlib.sha256(path.read_bytes()).hexdigest() for key,path in paths.items()},
    )
    assert len(data["rates"]) == 63 and "updateGhostStep" in data["logic"] and "updateContOrder" in data["base"]
    output.write_text("// Mechanically translated original AS3 methods; this is a decision-policy reference, not a Flash VM.\nconst source=" +
                      json.dumps(data,ensure_ascii=False,separators=(",",":")) + ";\n" + BOOTSTRAP)
    print(f"Translated {len(data['base']) + len(data['logic'])} original AS3 methods and 63 rate rows")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output",type=Path)
    parser.add_argument("--reference",type=Path,default=Path("reference"))
    args = parser.parse_args()
    build(args.reference,args.output)

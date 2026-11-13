// SPDX-License-Identifier: GPL-3.0-or-later
// Mechanical AS3 syntax adapter for pinned, explicitly selected method bodies.
export function methodAdapter(sources,globals){
 return function method(key,name){
  const text=sources[key].replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
  const match=new RegExp('(?:public|private|protected|internal)(?: static| override)? function '+name+'\\(([^)]*)\\)(?:\\s*:\\s*(?:[\\w.]+(?:<[^>]+>)?|\\*))?\\s*\\{').exec(text);
  if(!match)throw Error(key+' '+name);
  let begin=match.index+match[0].length,end=begin,depth=1;
  for(;depth;++end){if(end>=text.length)throw Error('Unclosed '+name);if(text[end]==='{')++depth;if(text[end]==='}')--depth;}
  const raw=text.slice(begin,end-1);
  const locals=[...raw.matchAll(/(?:var|,)\s*(\w+)\s*:/g)].map(x=>x[1]);
  const args=match[1].split(',').filter(Boolean).map(x=>x.trim().replace(/:(?:[\w.]+|\*)/,''));
  const hidden=new Set([...locals,...args.map(x=>x.replace(/^\.\.\./,'').split('=')[0].trim())]);
  let body=raw.replace(/for each\s*\(var (\w+):(?:Vector\.<Function>|Object|Function|DisplayObject|TeamVO|IGameSprite|FighterAttacker) in (\w+)\)/g,'for(var $1 of Object.values($2))')
   .replace(/var\s+(\w+):Vector\.<\w+>/g,'var $1')
   .replace(/,\s*(\w+):Vector\.<\w+>/g,',$1')
   .replace(/var\s+(\w+)\s*:\s*(?:[A-Za-z_$][\w.$]*|\*)/g,'var $1')
   .replace(/,\s*(\w+)\s*:\s*(?:Number|int|uint|Boolean|String|Function|Object|[A-Z]\w*)(?=\s*[=,;)])/g,',$1')
   .replace(/function\s*(\w*)\s*\(([^)]*)\)\s*:\s*[\w.]+/g,(_,n,a)=>'function '+n+'('+a.replace(/:\s*[\w.]+/g,'')+')')
   .replace(/new Vector\.<\w+>\(\)/g,'[]').replace(/\s+as Function/g,'');
  body=body.replace(/\b(\w+(?:\.\w+)*)\s+is\s+(BaseGameSprite|FighterMain|Bullet|FighterAttacker|MovieClip|DisplayObject|TeamVO|IGameSprite)\b/g,'$1 instanceof $2').replace(/\s+as\s+(?:BaseGameSprite|FighterMain|Bullet|FighterAttacker|MovieClip|DisplayObject|TeamVO|IGameSprite)\b/g,'');
  // AS3 coerces assignments to typed integer locals; removing the annotation
  // alone would retain fractional defense costs in the JS reference.
  for(const [,local,type] of raw.matchAll(/\bvar\s+(\w+)\s*:\s*(int|uint)\b/g)){
   const assign=new RegExp('(^\\s*|[;{}]\\s*)(var\\s+)?'+local+'\\s*=\\s*([^;{}\\n]+);','gm');
   body=body.replace(assign,(_,prefix,decl,value)=>prefix+(decl||'')+local+' = (('+value+')'+(type==='int'?'|0':'>>>0')+');');
   // AS3 typed integer locals start at zero, including `for (var i:int;...)`.
   // JS hoists an uninitialized var as undefined and would skip that loop.
   body='var '+local+'=0;'+body;
  }
  body=body.replace(/catch\s*\((\w+)\s*:\s*[\w.]+\)/g,'catch($1)');
  return Function(...Object.keys(globals),'hidden','return function('+args.join(',')+'){const self=this;const scope=new Proxy({},{has:(t,k)=>!hidden.has(k)&&Reflect.has(self,k),get:(t,k)=>{const v=Reflect.get(self,k,self);return typeof v===\'function\'?v.bind(self):v},set:(t,k,v)=>Reflect.set(self,k,v,self)});with(scope){'+body+'}}')(...Object.values(globals),hidden);
 };
}

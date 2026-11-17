/* Flash display/API compatibility work in progress. Original game classes are
 * separately compiled; unsupported paths throw rather than changing game logic.
 * No browser or online-service dependency is required for local game resources.
 */
var FlashRuntime = (function(global) {
    var R={movie:null,stage:null,root:null,classes:{},byId:{},movies:[],pending:[],
        broadcasts:{},deferred:[],constructQueue:[],scriptCleanup:[],scriptErrors:[],scriptErrorCount:0,timers:[],sounds:[],audioEvents:[],audioSequence:0,frame:0,uid:0,seed:42,rng:0,rngCalls:0,phase:'idle'};
    function scriptError(error,scope){var record={tick:R.frame,phase:R.phase,scope:scope,name:error.name||'Error',message:error.message||String(error),stack:error.stack||''};++R.scriptErrorCount;if(R.scriptErrors.length<32)R.scriptErrors.push(record);if(typeof nativeScriptError==='function')nativeScriptError(JSON.stringify(record));}
    R.worldEpoch=1;R.matrixSerial=1;
    var nativeMatrixMath=typeof nativeMatrixMultiply==='function'?nativeMatrixMultiply:null,nativeBoundsMath=typeof nativeBoundsTransform==='function'?nativeBoundsTransform:null;
    var nativeArrayMove=typeof nativeMoveArrayElement==='function'?nativeMoveArrayElement:null;
    var nativeDepthSort=typeof nativeSortDepth==='function'?nativeSortDepth:null,sourceNumber=global.Number,sourceArrayPush=Array.prototype.push,sourceMathFloor=Math.floor,nativeRandomStep=typeof nativeRandomStepFactory==='function'?nativeRandomStepFactory(R):null,nativeObjectHit=typeof nativeObjectHitFactory==='function'?nativeObjectHitFactory(function(a,b){return !!(a&&b&&a[0]<=b[1]&&a[1]>=b[0]&&a[2]<=b[3]&&a[3]>=b[2]);}):null;
    R.mathBackend=nativeMatrixMath&&nativeBoundsMath?'native-f32-twips':'javascript-f32-twips';
    // Every bounds reader marks its node current. A node already invalidated
    // has propagated the same invalidation to its dependent ancestors.
    function invalidateBounds(o){while(o&&o._boundsValid!==false){o._boundsValid=false;o=o.parent;}}
    // Render-only dirty state lives outside game objects. No new enumerable
    // fields, gameplay transforms, event order or collision data are changed.
    var renderNative=typeof nativeRenderUpdate==='function'?nativeRenderUpdate:null;
    var renderDirty=[],renderFlagsKey=Symbol("PSP primitive mirror flags"),attributeShareKey=Symbol("PSP shared authored attributes");
    function renderChanged(o,flags){if(!renderNative)return;var old=o[renderFlagsKey]||0;if((old&flags)===flags)return;if(!old)renderDirty.push(o);o[renderFlagsKey]=old|flags;}
    if(typeof nativeRenderChangedFactory==='function'&&renderNative)renderChanged=nativeRenderChangedFactory(renderChanged,renderNative,renderDirty,renderFlagsKey);
    function renderTreeChanged(o){renderChanged(o,63);if(o._children)for(var i=0;i<o._children.length;i++)renderTreeChanged(o._children[i]);}
    R.flushRenderMirror=function(){for(var i=0;i<renderDirty.length;i++){var o=renderDirty[i];renderNative(o,o[renderFlagsKey]);o[renderFlagsKey]=0;}renderDirty.length=0;return R.root._uid;};
    if(typeof nativeRenderBatchFactory==='function'&&renderNative){var sourceMirrorFlush=R.flushRenderMirror;R.flushRenderMirror=nativeRenderBatchFactory(sourceMirrorFlush,R,renderDirty,renderFlagsKey,renderNative);R.renderMirrorBackend=R.flushRenderMirror===sourceMirrorFlush?'source':'native-batch';}
    // A used descendant marks every ancestor. Clearing a marked subtree is
    // therefore conservative even when an ancestor has no cached matrix itself.
    // Weak numeric marks retain no removed display object or subtree.
    var worldCacheMarks=new WeakMap();
    function invalidateWorldCache(o){
        var flags=worldCacheMarks.get(o)||0;if(!flags)return;
        worldCacheMarks.set(o,0);
        if(o._children)for(var i=0;i<o._children.length;i++)invalidateWorldCache(o._children[i]);
    }
    if(typeof nativeWorldInvalidationFactory==='function')invalidateWorldCache=nativeWorldInvalidationFactory(invalidateWorldCache,worldCacheMarks);
    function markWorldCache(o){
        var own=true;
        while(o){var old=worldCacheMarks.get(o)||0,next=old|(own?3:2);if(old===next)return;worldCacheMarks.set(o,next);own=false;o=o.parent;}
    }
    function matrixChanged(o){renderChanged(o,1);o._matrixVersion=(o._matrixVersion||0)+1;++R.worldEpoch;invalidateWorldCache(o);invalidateBounds(o.parent);}
    function invalidateMotion(o){while(o&&o._motionValid!==false){o._motionValid=false;o=o.parent;}}
    function motionTree(o){if(o._motionValid)return o._hasMotion;var active=!!(o._skipAdvance||o._playing&&o instanceof MovieClip&&o._definition&&o._definition.frames&&o._definition.frames.length>1);if(o._children)for(var i=0;!active&&i<o._children.length;i++)active=motionTree(o._children[i]);o._hasMotion=active;o._motionValid=true;return active;}
    function markScriptPath(o){while(o&&!o._hasScriptSubtree){o._hasScriptSubtree=true;o=o.parent;}}
    // Track ready frame scripts outside display objects. Weak references keep
    // detached clips subject to the same collection as the ordinary orphan list.
    var pendingScriptBranches=new WeakSet(),pendingScriptQueued=new WeakSet(),pendingScriptRefs=new WeakMap(),pendingScriptQueue=[];
    function markPendingScriptPath(o){while(o){pendingScriptBranches.add(o);o=o.parent;}}
    function queuePendingScript(o){
        if(!o._pendingScript||o._lastScript===o._frame||!o._scripts||!o._scripts[o._frame])return;
        if(!pendingScriptQueued.has(o)){
            var ref=pendingScriptRefs.get(o);if(!ref){ref=weak(o);pendingScriptRefs.set(o,ref);}
            pendingScriptQueue.push(ref);pendingScriptQueued.add(o);
        }
        markPendingScriptPath(o);
    }
    function refreshPendingScripts(){
        var old=pendingScriptQueue;pendingScriptQueue=[];pendingScriptQueued=new WeakSet();pendingScriptBranches=new WeakSet();
        for(var i=0;i<old.length;i++){var o=old[i].deref();if(o)queuePendingScript(o);}
    }
    function weak(o){
        if(typeof nativeWeakRegister==='function'){var id=nativeWeakRegister(o);o=null;return {deref:function(){return nativeWeakGet(id);}};}
        if(typeof WeakRef==='function')return new WeakRef(o);
        throw new Error('This runtime requires native weak-reference support');
    }
    function alive(list){var values=[],kept=[];for(var i=0;i<list.length;i++){var o=list[i].deref();if(o){values.push(o);kept.push(list[i]);}}list.splice.apply(list,[0,list.length].concat(kept));return values;}
    function watchOrphan(o){if(o._orphanRef||o instanceof Stage)return;o._orphanRef=weak(o);R.movies.push(o._orphanRef);}
    function orphans(){var list=alive(R.movies),result=[],kept=[];for(var i=0;i<list.length;i++){var o=list[i];if(!o.parent){result.push(o);kept.push(o._orphanRef);}else o._orphanRef=null;}R.movies.splice.apply(R.movies,[0,R.movies.length].concat(kept));return result;}
    function cleanupOrphans(){var kept=[];for(var i=0;i<R.movies.length;i++){var o=R.movies[i].deref();if(o&&!o.parent&&o._placedByScript)kept.push(R.movies[i]);else if(o)o._orphanRef=null;}R.movies=kept;}
    function namespace(name){var p=name.split('.'),o=global;for(var i=0;i<p.length;i++)o=o[p[i]]||(o[p[i]]={});return o;}
    function inherit(c,p){c.prototype=Object.create(p.prototype);c.prototype.constructor=c;c.superClass_=p.prototype;
        c.base=function(me,name){var args=Array.prototype.slice.call(arguments,2);return p.prototype[name].apply(me,args);};}
    global.goog={DEBUG:false,global:global,provide:namespace,require:function(){},inherits:inherit,
        bind:function(f,o){return function(){return f.apply(o,arguments);};},
        exportSymbol:function(n,v){var p=n.split('.'),last=p.pop(),o=p.length?namespace(p.join('.')):global;o[last]=v;},
        exportProperty:function(o,n,v){o[n]=v;}};
    var flash=global.flash={display:{},events:{},geom:{},media:{},net:{},utils:{},system:{},ui:{},text:{},filters:{}};
    function property(proto,name,get,set){Object.defineProperty(proto,name,{get:get,set:set,configurable:true});}
    function integer(n){n=Number(n);if(!isFinite(n))return 0;return n<0?Math.ceil(n):Math.floor(n);}
    function twip(n){return integer(Number(n)*20)/20;}
    if(typeof nativeSourceRoundingFactory==='function'){integer=nativeSourceRoundingFactory(integer,global,null,false);twip=nativeSourceRoundingFactory(twip,global,integer,true);}
    function f32(n){return typeof nativeFloat32==='function'?nativeFloat32(n):(Math.fround?Math.fround(n):n);}
    function mul32(a,b){var ah=(a>>>16)&65535,al=a&65535,bh=(b>>>16)&65535,bl=b&65535;return (al*bl+((ah*bl+al*bh)<<16))|0;}
    R.random=function(){if(nativeRandomStep){var nativeRandomValue=nativeRandomStep();if(nativeRandomValue!==false)return nativeRandomValue;}
        if(!R.rng)R.rng=R.seed>>>0;
        R.rng=(R.rng&1)?((R.rng>>>1)^0x48000000):(R.rng>>>1);
        var s=mul32(R.rng,71);s=(((s<<13)^s)-(s>>21))|0;
        var v=mul32(s,s);v=(mul32(v,15731)+789221)|0;v=(mul32(v,s)+1376312589)|0;
        v=((v&0x7fffffff)+s)|0;v=(((v<<13)^v)-(v>>21))|0;++R.rngCalls;
        return (v&0x7fffffff)/2147483648;
    };
    Math.random=R.random;
    function Point(x,y){this.x=x===undefined?0:Number(x);this.y=y===undefined?0:Number(y);}
    Point.prototype.clone=function(){return new Point(this.x,this.y);};
    property(Point.prototype,'length',function(){return Math.sqrt(this.x*this.x+this.y*this.y);});
    Point.prototype.add=function(p){return new Point(this.x+p.x,this.y+p.y);};Point.prototype.subtract=function(p){return new Point(this.x-p.x,this.y-p.y);};
    Point.prototype.normalize=function(length){var d=this.length;if(d){this.x=this.x/d*length;this.y=this.y/d*length;}};
    Point.distance=function(a,b){return a.subtract(b).length;};
    function Rectangle(x,y,w,h){this.x=+x||0;this.y=+y||0;this.width=+w||0;this.height=+h||0;}
    Rectangle.prototype.contains=function(x,y){return x>=this.x&&y>=this.y&&x<this.x+this.width&&y<this.y+this.height;};
    function ColorTransform(r,g,b,a,ro,go,bo,ao){this.redMultiplier=r===undefined?1:+r;this.greenMultiplier=g===undefined?1:+g;
        this.blueMultiplier=b===undefined?1:+b;this.alphaMultiplier=a===undefined?1:+a;this.redOffset=+ro||0;this.greenOffset=+go||0;this.blueOffset=+bo||0;this.alphaOffset=+ao||0;}
    property(ColorTransform.prototype,'color',function(){return (this.redOffset<<16)|(this.greenOffset<<8)|this.blueOffset;},function(v){this.redMultiplier=this.greenMultiplier=this.blueMultiplier=0;this.redOffset=(v>>>16)&255;this.greenOffset=(v>>>8)&255;this.blueOffset=v&255;});
    flash.geom.Point=Point;flash.geom.Rectangle=Rectangle;flash.geom.ColorTransform=ColorTransform;
    function Event(type,bubbles,cancelable){this.type=String(type);this.bubbles=!!bubbles;this.cancelable=!!cancelable;this.target=null;this.currentTarget=null;this._stop=false;}
    Event.prototype.stopPropagation=function(){this._stop=true;};Event.prototype.preventDefault=function(){this.defaultPrevented=true;};
    var eventNames={ENTER_FRAME:'enterFrame',EXIT_FRAME:'exitFrame',FRAME_CONSTRUCTED:'frameConstructed',ADDED:'added',ADDED_TO_STAGE:'addedToStage',REMOVED:'removed',REMOVED_FROM_STAGE:'removedFromStage',COMPLETE:'complete',INIT:'init',CHANGE:'change',SOUND_COMPLETE:'soundComplete',OPEN:'open',CLOSE:'close'};
    for(var en in eventNames)Event[en]=eventNames[en];
    function EventDispatcher(){this._listeners={};}
    // Stage notifications do not bubble. Remember branches that have ever
    // contained a stage listener, without retaining removed display objects.
    // Marks are conservative after removal; reparenting propagates them to
    // the new ancestors. Snapshot traversal and callback order stay intact.
    var stageListenerBranches=new WeakSet(),unindexedEventNames=false,structuralEventListeners=0;
    function markStageListenerPath(o){while(o&&!stageListenerBranches.has(o)){stageListenerBranches.add(o);o=o.parent;}}
    function listenerFunction(record){return record.weak?record.fn.deref():record.fn;}
    EventDispatcher.prototype.addEventListener=function(type,fn,capture,priority,useWeak){
        // Only objects/functions can coerce to a stage-event property key.
        // Primitive undefined/null/numeric/symbol keys cannot match those
        // names and must not disable every indexed event branch permanently.
        if(type!==null&&(typeof type==='object'||typeof type==='function'))unindexedEventNames=true;
        if(!this._listeners)this._listeners={};var list=this._listeners[type]||(this._listeners[type]=[]);
        for(var i=0;i<list.length;i++)if(listenerFunction(list[i])===fn&&list[i].capture===!!capture)return;
        list.push({fn:useWeak?weak(fn):fn,weak:!!useWeak,capture:!!capture,priority:priority||0,serial:++R.uid});
        if(type==='addedToStage'||type==='removedFromStage')markStageListenerPath(this);
        else if(type==='added')structuralEventListeners|=1;
        else if(type==='removed')structuralEventListeners|=2;
        list.sort(function(a,b){return b.priority-a.priority||a.serial-b.serial;});
        if(type==='enterFrame'||type==='exitFrame'||type==='frameConstructed'){
            var b=R.broadcasts[type]||(R.broadcasts[type]=[]);if(alive(b).indexOf(this)<0)b.push(weak(this));
        }
    };
    EventDispatcher.prototype.removeEventListener=function(type,fn,capture){var list=this._listeners&&this._listeners[type];if(!list)return;
        for(var i=list.length-1;i>=0;i--)if(listenerFunction(list[i])===fn&&list[i].capture===!!capture)list.splice(i,1);
        if(!list.length&&R.broadcasts[type]){var b=R.broadcasts[type];for(var j=b.length-1;j>=0;j--)if(b[j].deref()===this)b.splice(j,1);}
    };
    EventDispatcher.prototype.hasEventListener=function(type){var list=this._listeners&&this._listeners[type];if(!list)return false;for(var i=list.length-1;i>=0;i--)if(!listenerFunction(list[i]))list.splice(i,1);return !!list.length;};
    EventDispatcher.prototype.dispatchEvent=function(event){
        if(!event.target)event.target=this;event.currentTarget=this;
        var list=(this._listeners&&this._listeners[event.type]||[]).slice();
        for(var i=0;i<list.length;i++){var fn=listenerFunction(list[i]);if(fn&&(this._listeners[event.type]||[]).indexOf(list[i])>=0){try{fn(event);}catch(error){scriptError(error,'event '+event.type);}}if(event._stop)break;}
        if(event.bubbles&&!event._stop&&this.parent)this.parent.dispatchEvent(event);return !event.defaultPrevented;
    };
    function emit(target,type,bubbles){
        // Avoid repeated ancestry queries for structural event names that
        // have never been registered. Marks stay conservative after removal.
        // Non-string names retain the original coercion/traversal behavior.
        if(!unindexedEventNames&&((type==='added'&&!(structuralEventListeners&1))||(type==='removed'&&!(structuralEventListeners&2))))return;
        var p=target;while(p){if(p.hasEventListener(type)){target.dispatchEvent(new Event(type,bubbles));return;}if(!bubbles)return;p=p.parent;}
    }
    function MouseEvent(type,bubbles,cancelable){Event.call(this,type,bubbles,cancelable);this.stageX=R.mouseX||0;this.stageY=R.mouseY||0;this.localX=this.stageX;this.localY=this.stageY;}
    inherit(MouseEvent,Event);var mouseNames={MOUSE_OVER:'mouseOver',MOUSE_OUT:'mouseOut',MOUSE_DOWN:'mouseDown',MOUSE_UP:'mouseUp',CLICK:'click',MOUSE_MOVE:'mouseMove',ROLL_OVER:'rollOver',ROLL_OUT:'rollOut'};
    for(var mn in mouseNames)MouseEvent[mn]=mouseNames[mn];
    function KeyboardEvent(type,bubbles,cancelable,charCode,keyCode){Event.call(this,type,bubbles,cancelable);this.charCode=charCode||0;this.keyCode=keyCode||0;this.shiftKey=false;this.ctrlKey=false;this.altKey=false;}
    inherit(KeyboardEvent,Event);KeyboardEvent.KEY_DOWN='keyDown';KeyboardEvent.KEY_UP='keyUp';
    function IOErrorEvent(type,bubbles,cancelable,text){Event.call(this,type,bubbles,cancelable);this.text=text||'';}inherit(IOErrorEvent,Event);IOErrorEvent.IO_ERROR='ioError';
    function SecurityErrorEvent(type){Event.call(this,type);}inherit(SecurityErrorEvent,Event);SecurityErrorEvent.SECURITY_ERROR='securityError';
    function TimerEvent(type){Event.call(this,type);}inherit(TimerEvent,Event);TimerEvent.TIMER='timer';TimerEvent.TIMER_COMPLETE='timerComplete';
    flash.events.Event=Event;flash.events.EventDispatcher=EventDispatcher;flash.events.MouseEvent=MouseEvent;flash.events.KeyboardEvent=KeyboardEvent;
    flash.events.IOErrorEvent=IOErrorEvent;flash.events.SecurityErrorEvent=SecurityErrorEvent;flash.events.TimerEvent=TimerEvent;
    function matrixTerm(a,b,c,d){return f32(f32(a*b)+f32(c*d));}
    function matrixMultiply(a,b){if(nativeMatrixMath)return nativeMatrixMath(a,b);var x=f32(b[4]*20),y=f32(b[5]*20);return [matrixTerm(a[0],b[0],a[2],b[1]),matrixTerm(a[1],b[0],a[3],b[1]),matrixTerm(a[0],b[2],a[2],b[3]),matrixTerm(a[1],b[2],a[3],b[3]),((roundTwips(matrixTerm(a[0],x,a[2],y))+Math.round(a[4]*20))|0)/20,((roundTwips(matrixTerm(a[1],x,a[3],y))+Math.round(a[5]*20))|0)/20];}
    function transformPoint(m,p){return new Point(m[0]*p.x+m[2]*p.y+m[4],m[1]*p.x+m[3]*p.y+m[5]);}
    function inverse(m){var d=m[0]*m[3]-m[1]*m[2];if(!d)return [0,0,0,0,0,0];return [m[3]/d,-m[1]/d,-m[2]/d,m[0]/d,(m[2]*m[5]-m[3]*m[4])/d,(m[1]*m[4]-m[0]*m[5])/d];}
    // Display bounds use Flash's f32 transform, rounded to integral twips with
    // ties to even. A sub-twip gap must not turn touching hit areas into a miss.
    function roundTwips(n){if(!isFinite(n))return 0;if(n>=2147483648||n< -2147483648)return -2147483648;var lo=Math.floor(n),fraction=n-lo;return fraction<.5?lo:fraction>.5?lo+1:lo%2===0?lo:lo+1;}
    function boundsPoint(m,x,y){x=f32(x*20);y=f32(y*20);return [((roundTwips(f32(f32(m[0]*x)+f32(m[2]*y)))+Math.round(m[4]*20))|0)/20,((roundTwips(f32(f32(m[1]*x)+f32(m[3]*y)))+Math.round(m[5]*20))|0)/20];}
    function boundsTransform(b,m){if(!b)return null;
        if(nativeBoundsMath)return nativeBoundsMath(b,m);
        if(m[1]===0&&m[2]===0){
            var tx=Math.round(m[4]*20),ty=Math.round(m[5]*20),x0=((roundTwips(f32(m[0]*f32(b[0]*20)))+tx)|0)/20,x1=((roundTwips(f32(m[0]*f32(b[1]*20)))+tx)|0)/20,y0=((roundTwips(f32(m[3]*f32(b[2]*20)))+ty)|0)/20,y1=((roundTwips(f32(m[3]*f32(b[3]*20)))+ty)|0)/20;
            return [Math.min(x0,x1),Math.max(x0,x1),Math.min(y0,y1),Math.max(y0,y1)];
        }
        var a=boundsPoint(m,b[0],b[2]),c=boundsPoint(m,b[1],b[2]),d=boundsPoint(m,b[0],b[3]),e=boundsPoint(m,b[1],b[3]);return [Math.min(a[0],c[0],d[0],e[0]),Math.max(a[0],c[0],d[0],e[0]),Math.min(a[1],c[1],d[1],e[1]),Math.max(a[1],c[1],d[1],e[1])];}
    function union(a,b){if(!a)return b&&b.slice();if(!b)return a;return [Math.min(a[0],b[0]),Math.max(a[1],b[1]),Math.min(a[2],b[2]),Math.max(a[3],b[3])];}
    function Graphics(owner){this.owner=owner;this.records=[];this.bounds=null;}
    Graphics.prototype.clear=function(){this.records=[];this.bounds=null;invalidateBounds(this.owner);};Graphics.prototype.beginFill=function(c,a){this.records.push(['fill',c,a===undefined?1:a]);};
    Graphics.prototype.endFill=function(){this.records.push(['end']);};Graphics.prototype.lineStyle=function(w,c,a){this.records.push(['line',w,c,a===undefined?1:a]);};
    Graphics.prototype.drawRect=function(x,y,w,h){this.records.push(['rect',x,y,w,h]);this.bounds=union(this.bounds,[x,x+w,y,y+h]);invalidateBounds(this.owner);};
    Graphics.prototype.drawRoundRect=function(x,y,w,h,ew,eh){this.drawRect(x,y,w,h);};
    Graphics.prototype.moveTo=function(x,y){this._last=[x,y];this.records.push(['move',x,y]);};
    Graphics.prototype.lineTo=function(x,y){this.records.push(['to',x,y]);this.bounds=union(this.bounds,[Math.min(x,this._last?this._last[0]:x),Math.max(x,this._last?this._last[0]:x),Math.min(y,this._last?this._last[1]:y),Math.max(y,this._last?this._last[1]:y)]);this._last=[x,y];invalidateBounds(this.owner);};
    // Allocate the Flash Transform wrapper on first access. Timeline artwork
    // that never reads transform needs no pair of owner-capturing closures.
    // The own property remains enumerable/configurable and assigned values
    // retain the previous plain-property behavior.
    var transformWrappers=new WeakMap();
    function displayTransform(){
        var transform=transformWrappers.get(this);if(transform)return transform;
        var self=this;transform={};
        property(transform,'colorTransform',function(){return new ColorTransform(self._color[0]/256,self._color[1]/256,self._color[2]/256,self._color[3]/256,self._color[4],self._color[5],self._color[6],self._color[7]);},function(c){if(self._color[3]<=0&&c.alphaMultiplier>0)renderTreeChanged(self);self._color=[c.redMultiplier*256,c.greenMultiplier*256,c.blueMultiplier*256,c.alphaMultiplier*256,c.redOffset,c.greenOffset,c.blueOffset,c.alphaOffset];self[attributeShareKey]&=~2;++self._colorVersion;renderChanged(self,2);});
        transformWrappers.set(this,transform);return transform;
    }
    function replaceTransform(value){Object.defineProperty(this,'transform',{value:value,writable:true,enumerable:true,configurable:true});}
    var transformDescriptor={get:displayTransform,set:replaceTransform,enumerable:true,configurable:true};
    var displayPrefix=typeof nativeDisplayPrefixFactory==='function'?nativeDisplayPrefixFactory(R):null;var displaySuffix=typeof nativeDisplaySuffixFactory==='function'?nativeDisplaySuffixFactory(R):null;
    function DisplayObject(){
        if(this._displayInitialized)return;this._displayInitialized=true;if(renderNative)Object.defineProperty(this,renderFlagsKey,{value:0,writable:true});EventDispatcher.call(this);
        var pending=R.pending.length?R.pending[R.pending.length-1]:null;
        if(pending&&pending.claimed)pending=null;if(pending)pending.claimed=true;
        if(!displayPrefix||!displayPrefix(this,pending)){
        this._uid=++R.uid;this.name=pending?pending.name:'instance'+this._uid;this.parent=pending?pending.parent:null;this._placedByScript=!pending;
        this._symbol=pending?pending.symbol:(this.constructor.$symbol===undefined?-1:this.constructor.$symbol);
        this._definition=R.movie&&R.movie.definitions[this._symbol]||null;
        this._matrix=pending?pending.matrix:[1,0,0,1,0,0];if(!pending)this[attributeShareKey]=0;
        this._matrixVersion=1;
        this._color=pending?pending.color:[256,256,256,256,0,0,0,0];
        this._colorVersion=1;
        }
        this._ratio=0;this._alpha=1;this.visible=true;this.mouseEnabled=true;this.mouseChildren=true;this.buttonMode=false;
        var suffixResult=displaySuffix?displaySuffix(this):false;if(suffixResult!==true){this._scriptFilters=suffixResult===false?[]:suffixResult;suffixResult=undefined;this._filtersOverride=false;this._filtersVersion=1;this.cacheAsBitmap=false;this.blendMode='normal';this.mask=null;}renderChanged(this,63);
        Object.defineProperty(this,'transform',transformDescriptor);
    }
    inherit(DisplayObject,EventDispatcher);
    // Authored placement arrays are read-only source assets. Private pending
    // placeholders have no public child handle; completed display objects share
    // those arrays until a public transform/alpha setter first needs to write.
    // Every in-place matrix/color writer below first takes its own copy.
    Object.defineProperty(DisplayObject.prototype,attributeShareKey,{value:3,writable:true});
    property(DisplayObject.prototype,'visible',function(){return this._visible!==false;},function(value){var was=this._visible;this._visible=!!value;renderChanged(this,32);if(this._visible&&was===false)renderTreeChanged(this);if(!this._visible&&R.stage&&R.stage.focus===this)R.stage.focus=null;});
    DisplayObject.prototype.toString=function(){return '[display '+this._uid+']';};
    function cloneFilters(list){var output=[];for(var i=0;i<list.length;i++){var filter=list[i];if(filter.matrix)output.push(new flash.filters.ColorMatrixFilter(filter.matrix.slice()));else throw new Error('Unsupported script filter');}return output;}
    property(DisplayObject.prototype,'filters',function(){return cloneFilters(this._scriptFilters);},function(value){this._scriptFilters=cloneFilters(value||[]);this._filtersOverride=true;++this._filtersVersion;});
    property(DisplayObject.prototype,'x',function(){return this._matrix[4];},function(v){v=twip(v);if(v!==this._matrix[4]){if(this[attributeShareKey]&1){this._matrix=this._matrix.slice();this[attributeShareKey]&=~1;}this._matrix[4]=v;matrixChanged(this);}});
    property(DisplayObject.prototype,'y',function(){return this._matrix[5];},function(v){v=twip(v);if(v!==this._matrix[5]){if(this[attributeShareKey]&1){this._matrix=this._matrix.slice();this[attributeShareKey]&=~1;}this._matrix[5]=v;matrixChanged(this);}});
    property(DisplayObject.prototype,'alpha',function(){return this._color[3]/256;},function(v){v=integer(Number(v)*256);if(v!==this._color[3]){if(this._color[3]<=0&&v>0)renderTreeChanged(this);if(this[attributeShareKey]&2){this._color=this._color.slice();this[attributeShareKey]&=~2;}this._color[3]=v;++this._colorVersion;renderChanged(this,2);}});
    property(DisplayObject.prototype,'scaleX',function(){return this._scaleX===undefined?Math.sqrt(this._matrix[0]*this._matrix[0]+this._matrix[1]*this._matrix[1]):this._scaleX;},function(v){this._cacheRotation();v=+v;if(v===this._scaleX)return;this._scaleX=v;this._rebuildMatrix();});
    property(DisplayObject.prototype,'scaleY',function(){return this._scaleY===undefined?Math.sqrt(this._matrix[2]*this._matrix[2]+this._matrix[3]*this._matrix[3]):this._scaleY;},function(v){this._cacheRotation();v=+v;if(v===this._scaleY)return;this._scaleY=v;this._rebuildMatrix();});
    property(DisplayObject.prototype,'rotation',function(){return this._rotation===undefined?Math.atan2(this._matrix[1],this._matrix[0])*180/Math.PI:this._rotation;},function(v){this._cacheRotation();v=+v;if(v===this._rotation)return;this._rotation=v;this._rebuildMatrix();});
    DisplayObject.prototype._cacheRotation=function(){if(this._rotation!==undefined)return;this._rotation=Math.atan2(this._matrix[1],this._matrix[0])*180/Math.PI;this._scaleX=Math.sqrt(this._matrix[0]*this._matrix[0]+this._matrix[1]*this._matrix[1]);this._scaleY=Math.sqrt(this._matrix[2]*this._matrix[2]+this._matrix[3]*this._matrix[3]);this._skew=Math.atan2(-this._matrix[2],this._matrix[3])-this._rotation*Math.PI/180;};
    DisplayObject.prototype._rebuildMatrix=function(){var r=this._rotation*Math.PI/180,s=this._skew||0;if(this[attributeShareKey]&1){this._matrix=this._matrix.slice();this[attributeShareKey]&=~1;}this._matrix[0]=f32(this._scaleX*Math.cos(r));this._matrix[1]=f32(this._scaleX*Math.sin(r));this._matrix[2]=f32(-this._scaleY*Math.sin(r+s));this._matrix[3]=f32(this._scaleY*Math.cos(r+s));matrixChanged(this);};
    var stageContinue=function(p){while(p&&!(p instanceof Stage))p=p.parent;return p||null;},nativeStageGetter=typeof nativeStageGetterFactory==='function'?nativeStageGetterFactory(stageContinue,Stage):null;property(DisplayObject.prototype,'stage',function(){if(nativeStageGetter)return nativeStageGetter(this);var p=this;while(p&&!(p instanceof Stage))p=p.parent;return p||null;});
    property(DisplayObject.prototype,'root',function(){var p=this;while(p.parent&&!(p.parent instanceof Stage))p=p.parent;return p;});
    property(DisplayObject.prototype,'loaderInfo',function(){return R.loaderInfo;});
    DisplayObject.prototype._bounds=function(){this._boundsValid=true;return this._definition&&this._definition.bounds||null;};
    // Numeric ancestry stamps avoid computing every ancestor's world matrix
    // before folding the same chain again for this object. The weak key and
    // numeric values do not keep removed display subtrees alive.
    var worldPaths=new WeakMap();
    var nativeWorldBuilder=typeof nativeWorldPathBuilderFactory==='function'&&typeof nativeMatrixChain==='function'?nativeWorldPathBuilderFactory(Stage,nativeMatrixChain):null;
    function sourceGlobalMatrix(){
        if(this._worldEpoch===R.worldEpoch)return this._worldMatrix;
        var path=worldPaths.get(this),changed=!this._worldMatrix,node=this,n=0;
        if(path&&!changed&&typeof nativeWorldPathCurrent==='function'&&nativeWorldPathCurrent(this,path,path.length,R.stage)){
            this._worldEpoch=R.worldEpoch;return this._worldMatrix;
        }
        if(!path){path=[];worldPaths.set(this,path);}
        if(nativeWorldBuilder){
            var built=nativeWorldBuilder(this,path,!!changed);
            if(built!==false){
                if(built!==null){this._worldMatrix=built;this._worldLocalVersion=this._matrixVersion;this._worldVersion=++R.matrixSerial;}
                this._worldEpoch=R.worldEpoch;return this._worldMatrix;
            }
        }
        do{
            if(path[n]!==node._uid||path[n+1]!==node._matrixVersion)changed=true;
            path[n++]=node._uid;path[n++]=node._matrixVersion;
            // Retain the existing direct-parent cache ownership while leaving
            // ancestor numeric matrices lazy until they are actually queried.
            node._worldParent=node.parent;node=node.parent;
        }while(node&&!(node instanceof Stage));
        if(path.length!==n){path.length=n;changed=true;}
        if(changed){
            // Flash folds local transforms from the leaf toward Stage. Twip
            // rounding makes parentWorld * local a different operation.
            var chain=[this._matrix];node=this.parent;while(node&&!(node instanceof Stage)){chain.push(node._matrix);node=node.parent;}
            if(typeof nativeMatrixChain==='function')this._worldMatrix=nativeMatrixChain(chain);else{var world=chain[0].slice();for(var i=1;i<chain.length;i++)world=matrixMultiply(chain[i],world);this._worldMatrix=world;}
            this._worldLocalVersion=this._matrixVersion;this._worldVersion=++R.matrixSerial;
        }
        this._worldEpoch=R.worldEpoch;return this._worldMatrix;
    }
    DisplayObject.prototype._globalMatrix=function(){
        // A constructor may query a child whose parent link already exists but
        // whose insertion into _children has not happened yet. Retain the
        // original ancestry validation for that entire construction window.
        if(R.pending.length)return sourceGlobalMatrix.call(this);
        if((worldCacheMarks.get(this)||0)&1)return this._worldMatrix;
        var matrix=sourceGlobalMatrix.call(this);markWorldCache(this);return matrix;
    };
    DisplayObject.prototype.localToGlobal=function(p){return transformPoint(this._globalMatrix(),p);};
    DisplayObject.prototype.globalToLocal=function(p){return transformPoint(inverse(this._globalMatrix()),p);};
    DisplayObject.prototype.getBounds=function(target){var b=boundsTransform(this._bounds(),matrixMultiply(inverse(target._globalMatrix()),this._globalMatrix()));return b?new Rectangle(b[0],b[2],b[1]-b[0],b[3]-b[2]):new Rectangle();};
    DisplayObject.prototype.getRect=DisplayObject.prototype.getBounds;
    property(DisplayObject.prototype,'width',function(){var b=boundsTransform(this._bounds(),this._matrix);return b?b[1]-b[0]:0;},function(v){var w=this.width;if(w)this.scaleX*=+v/w;});
    property(DisplayObject.prototype,'height',function(){var b=boundsTransform(this._bounds(),this._matrix);return b?b[3]-b[2]:0;},function(v){var h=this.height;if(h)this.scaleY*=+v/h;});
    DisplayObject.prototype._worldBounds=function(){var m=this._globalMatrix(),b=this._bounds();if(this._hitBoundsMatrix!==m||this._hitBoundsLocal!==b){this._hitBoundsMatrix=m;this._hitBoundsLocal=b;this._hitBounds=boundsTransform(b,m);}return this._hitBounds;};
    DisplayObject.prototype.hitTestObject=function(other){if(nativeObjectHit)return nativeObjectHit(this,other);var a=this._worldBounds(),b=other._worldBounds();return !!(a&&b&&a[0]<=b[1]&&a[1]>=b[0]&&a[2]<=b[3]&&a[3]>=b[2]);};
    DisplayObject.prototype.hitTestPoint=function(x,y,shape){var b=this._worldBounds();if(!(b&&x>=b[0]&&x<=b[1]&&y>=b[2]&&y<=b[3]))return false;return shape?shapePoint(this,x,y,false):true;};
    function edgeHit(d,x,y){
        var fills={},edges=d.edges||[];
        function cross(edge,t,sign){if(t<0||t>=1||!sign)return;var a=1-t,px=edge[2]===null?edge[0]+t*(edge[4]-edge[0]):a*a*edge[0]+2*a*t*edge[2]+t*t*edge[4];
            if(px<=x)return;var left=edge[6],right=edge[7];if(left)fills[left]=(fills[left]||0)+sign;if(right)fills[right]=(fills[right]||0)-sign;
        }
        for(var i=0;i<edges.length;i++){
            var e=edges[i];
            if(e[6]!==e[7]){
                if(e[2]===null){if((e[1]>y)!==(e[5]>y))cross(e,(y-e[1])/(e[5]-e[1]),e[5]>e[1]?1:-1);}
                else{
                    var a=e[1]-2*e[3]+e[5],b=2*(e[3]-e[1]),c=e[1]-y;
                    if(Math.abs(a)<1e-12){if(b)cross(e,-c/b,b>0?1:-1);}
                    else{var disc=b*b-4*a*c;if(disc>0){var root=Math.sqrt(disc),t0=(-b-root)/(2*a),t1=(-b+root)/(2*a);cross(e,t0,2*a*t0+b>0?1:-1);cross(e,t1,2*a*t1+b>0?1:-1);}}
                }
            }
            if(e[8]>0){
                var lx=e[0],ly=e[1],steps=e[2]===null?1:16;
                for(var j=1;j<=steps;j++){var t=j/steps,s=1-t,nx=e[2]===null?e[4]:s*s*e[0]+2*s*t*e[2]+t*t*e[4],ny=e[2]===null?e[5]:s*s*e[1]+2*s*t*e[3]+t*t*e[5],dx=nx-lx,dy=ny-ly,length=dx*dx+dy*dy,u=length?Math.max(0,Math.min(1,((x-lx)*dx+(y-ly)*dy)/length)):0;
                    var ex=x-lx-u*dx,ey=y-ly-u*dy;if(ex*ex+ey*ey<=e[8]*e[8]/4)return true;lx=nx;ly=ny;
                }
            }
        }
        for(var k in fills)if(d.winding?fills[k]!==0:Math.abs(fills[k])%2===1)return true;return false;
    }
    function shapePoint(o,x,y,skipInvisible){
        if(skipInvisible&&!o.visible)return false;
        var p=o.globalToLocal(new Point(x,y)),d=o._definition;
        if(d&&d.edges&&edgeHit(d,p.x,p.y))return true;
        if(d&&(d.kind===11||d.kind===33||d.kind===37)){var b=o._bounds();if(b&&p.x>=b[0]&&p.x<=b[1]&&p.y>=b[2]&&p.y<=b[3])return true;}
        if(o.graphics&&o.graphics.bounds){var b=o.graphics.bounds;if(p.x>=b[0]&&p.x<=b[1]&&p.y>=b[2]&&p.y<=b[3])return true;}
        if(o._children)for(var i=o._children.length-1;i>=0;i--)if(shapePoint(o._children[i],x,y,skipInvisible))return true;
        return false;
    }
    function DisplayObjectContainer(){DisplayObject.call(this);this._children=[];this._depths={};if(!this.parent)watchOrphan(this);}
    inherit(DisplayObjectContainer,DisplayObject);
    property(DisplayObjectContainer.prototype,'numChildren',function(){return this._children.length;});
    function walk(o,f){f(o);if(o._children){var a=o._children.slice();for(var i=0;i<a.length;i++)walk(a[i],f);}}
    function emitStageTree(o,type){
        // Non-string event keys keep the original coercion path and traversal.
        if(unindexedEventNames){walk(o,function(c){emit(c,type,false);});return;}
        if(!stageListenerBranches.has(o))return;
        emit(o,type,false);if(o._children){var a=o._children.slice();for(var i=0;i<a.length;i++)emitStageTree(a[i],type);}
    }
    DisplayObjectContainer.prototype._bounds=function(){if(this._boundsValid)return this._boundsCache;var b=this.graphics&&this.graphics.bounds||null;for(var i=0;i<this._children.length;i++)b=union(b,boundsTransform(this._children[i]._bounds(),this._children[i]._matrix));this._boundsValid=true;this._boundsCache=b;return b;};
    // A native cache hit uses the same pure methods,local bounds and world
    // matrix,plus the dependency marks maintained by matrixChanged(). Unknown
    // descriptors,construction windows and stale caches call the original body.
    if(typeof nativeCachedWorldBoundsFactory==='function'){
        DisplayObject.prototype._worldBounds=nativeCachedWorldBoundsFactory(R,worldCacheMarks,DisplayObject.prototype._worldBounds,DisplayObject.prototype._globalMatrix,DisplayObjectContainer.prototype._bounds);
    }

    DisplayObjectContainer.prototype.addChild=function(child){return this.addChildAt(child,this._children.length);};
    DisplayObjectContainer.prototype.addChildAt=function(child,index){
        if(child===this)throw new Error('Cannot add display object to itself');index=integer(index);
        if(index<0||index>this._children.length)throw new RangeError('addChildAt index');
        var oldStage=child.stage;if(child.parent)child.parent.removeChild(child);child._placedByScript=true;
        child.parent=this;this._children.splice(index,0,child);if(stageListenerBranches.has(child))markStageListenerPath(this);renderChanged(this,4);renderTreeChanged(child);matrixChanged(child);invalidateMotion(this);if(child._hasScriptSubtree)markScriptPath(this);if(pendingScriptBranches.has(child))markPendingScriptPath(this);emit(child,'added',true);
        if(this.stage&&!oldStage)emitStageTree(child,'addedToStage');return child;
    };
    DisplayObjectContainer.prototype.removeChild=function(child){var n=this._children.indexOf(child);if(n<0)throw new Error('removeChild: missing child');return this.removeChildAt(n);};
    DisplayObjectContainer.prototype.removeChildAt=function(index,fromTimeline){var c=this._children[index];if(!c)throw new RangeError('removeChildAt index');var stage=c.stage;if(!fromTimeline)c._placedByScript=true;
        emit(c,'removed',true);if(stage)emitStageTree(c,'removedFromStage');
        this._children.splice(index,1);renderChanged(this,4);invalidateBounds(this);invalidateMotion(this);c.parent=null;if(R.stage&&R.stage.focus===c)R.stage.focus=null;matrixChanged(c);if(c._children)watchOrphan(c);if(c._timelineDepth!==undefined&&this._depths[c._timelineDepth]===c)delete this._depths[c._timelineDepth];return c;};
    DisplayObjectContainer.prototype.getChildAt=function(i){if(!this._children[i])throw new RangeError('getChildAt index '+i);return this._children[i]._timelinePending?null:this._children[i];};
    DisplayObjectContainer.prototype.getChildByName=function(name){for(var i=0;i<this._children.length;i++)if(this._children[i].name===name)return this._children[i]._timelinePending?null:this._children[i];return null;};
    DisplayObjectContainer.prototype.getChildIndex=function(c){var i=this._children.indexOf(c);if(i<0)throw new Error('getChildIndex missing child');return i;};
    DisplayObjectContainer.prototype.setChildIndex=function(c,index){var old=this.getChildIndex(c);if(index<0||index>=this._children.length)throw new RangeError('setChildIndex');if(old===index)return;if(!nativeArrayMove||!nativeArrayMove(this._children,old,index)){this._children.splice(old,1);this._children.splice(index,0,c);}renderChanged(this,4);};
    DisplayObjectContainer.prototype.contains=function(c){while(c){if(c===this)return true;c=c.parent;}return false;};
    function Sprite(){DisplayObjectContainer.call(this);this.graphics=new Graphics(this);initializeTimeline(this);}
    inherit(Sprite,DisplayObjectContainer);
    function MovieClip(){Sprite.call(this);}
    inherit(MovieClip,Sprite);
    function initializeTimeline(o){var d=o._definition;if(!d||!d.frames)return;o._frame=1;o._playing=o instanceof MovieClip;o._scripts={};o._pendingScript=true;o._lastScript=0;o._newAt=R.frame;o._skipAdvance=o._placedByScript||o._symbol===0;o._needsConstruct=true;o._constructFull=true;construct(o,true);}
    function resolveFrame(o,frame){if(typeof frame==='string'&&o._definition&&o._definition.labels[frame]!==undefined)return o._definition.labels[frame];var n=integer(frame);return Math.max(1,Math.min(n,o.totalFrames));}
    function goto(o,frame,play){var target=resolveFrame(o,frame);
        if(o._runningScript&&target!==o._frame){o._queuedGoto=[target,play];if(o._scripts[target])R.scriptCleanup.push(o);return;}
        if(o._playing!==play){o._playing=play;invalidateMotion(o);}
        if(target===o._frame){o._queuedGoto=null;o._skipAdvance=true;invalidateMotion(o);return;}
        if(target!==o._frame){o._frame=target;o._pendingScript=true;o._lastScript=0;o._skipAdvance=true;invalidateMotion(o);o._needsConstruct=true;o._constructFull=true;construct(o,false);queuePendingScript(o);}
    }
    MovieClip.prototype.addFrameScript=function(){if(!this._scripts)this._scripts={};for(var i=0;i<arguments.length;i+=2){var frame=integer(arguments[i])+1;this._scripts[frame]=arguments[i+1];if(frame===this.currentFrame&&arguments[i+1]){if(R.phase==='scripts')R.scriptCleanup.push(this);else{this._lastScript=0;this._pendingScript=true;}}}markScriptPath(this);queuePendingScript(this);};
    MovieClip.prototype.gotoAndStop=function(frame,scene){goto(this,frame,false);};MovieClip.prototype.gotoAndPlay=function(frame,scene){goto(this,frame,true);};
    MovieClip.prototype.stop=function(){if(this._playing){this._playing=false;invalidateMotion(this);}};MovieClip.prototype.play=function(){if(!this._playing){this._playing=true;invalidateMotion(this);}};
    MovieClip.prototype.nextFrame=function(){if(this.currentFrame<this.totalFrames)goto(this,this.currentFrame+1,false);};MovieClip.prototype.prevFrame=function(){if(this.currentFrame>1)goto(this,this.currentFrame-1,false);};
    property(MovieClip.prototype,'currentFrame',function(){return this._frame||1;});
    property(MovieClip.prototype,'totalFrames',function(){return this._definition&&this._definition.frames?this._definition.frames.length:1;});if(typeof nativeGotoNoopFactory==='function')goto=nativeGotoNoopFactory(goto,resolveFrame,integer,invalidateMotion,global,MovieClip.prototype);
    property(MovieClip.prototype,'framesLoaded',function(){return this.totalFrames;});
    property(MovieClip.prototype,'currentLabel',function(){var labels=this._definition&&this._definition.labels||{},last=0,name=null;for(var k in labels)if(labels[k]<=this.currentFrame&&labels[k]>=last){last=labels[k];name=k;}return name;});
    function make(symbol,parent,name,matrix,color){
        var d=R.movie.definitions[symbol];if(!d)throw new Error('Unknown SWF symbol '+symbol);
        var Class=R.byId[symbol]||(d.kind===39?MovieClip:d.kind===37?TextField:DisplayObject);
        var p={symbol:symbol,parent:parent,name:name||'instance'+(++R.uid),matrix:matrix||[1,0,0,1,0,0],color:color||[256,256,256,256,0,0,0,0],claimed:false};
        R.pending.push(p);var obj;try{obj=new Class();}catch(e){e.message='Construct symbol '+symbol+' '+(d.class_name||'')+': '+e.message;throw e;}finally{R.pending.pop();}
        return obj;
    }
    var definitionBoundsCache={};
    function definitionBounds(symbol){
        if(Object.prototype.hasOwnProperty.call(definitionBoundsCache,symbol))return definitionBoundsCache[symbol];
        var d=R.movie.definitions[symbol],b=d.bounds||null;definitionBoundsCache[symbol]=null;
        if(d.frames)for(var i=0;i<d.frames[0].length;i++){var p=R.movie.placements[d.frames[0][i]];b=union(b,boundsTransform(definitionBounds(p[1]),p[2]));}
        definitionBoundsCache[symbol]=b;return b;
    }
    function makePending(symbol,parent,name,matrix,color){
        var p={symbol:symbol,parent:parent,name:name||'instance'+(++R.uid),matrix:matrix,color:color,claimed:false};
        R.pending.push(p);var child;try{child=new DisplayObject();}finally{R.pending.pop();}
        child._timelinePending=true;child._bounds=function(){this._boundsValid=true;return definitionBounds(this._symbol);};
        if(!parent._constructionQueued){parent._constructionQueued=true;R.constructQueue.push(parent);}
        return child;
    }
    function constructChildren(o){
        if(!o._children)return;
        // Root menu/stage changes defer authored child constructors to this
        // queue. Pause before those constructors; ordinary actor timelines and
        // battle updates keep their existing path. The native hook coalesces
        // multiple requests made within one source update.
        if(o===R.root&&o._constructionQueued&&o.currentFrame>=30&&typeof nativeBeginStaticLoading==='function')nativeBeginStaticLoading(o.currentFrame);
        o._constructionQueued=false;
        for(var i=0;i<o._children.length;i++){
            var pending=o._children[i];
            if(pending._timelinePending){
                var child=make(pending._symbol,o,pending.name,pending._matrix,pending._color);
                child._timelineDepth=pending._timelineDepth;child._born=pending._born;child._placement=pending._placement;child._ratio=pending._ratio;child._clipDepth=pending._clipDepth;child._filters=pending._filters;child._blend=pending._blend;child._timelineName=pending._timelineName;
                o._children[i]=child;if(stageListenerBranches.has(child))markStageListenerPath(o);renderChanged(o,4);renderChanged(child,63);o._depths[child._timelineDepth]=child;pending.parent=null;
                matrixChanged(child);invalidateMotion(o);if(child._hasScriptSubtree)markScriptPath(o);if(child._timelineName)o[child._timelineName]=child;
                emit(child,'added',true);if(o.stage)emitStageTree(child,'addedToStage');
            }else constructChildren(pending);
        }
    }
    function construct(o,initial){
        var d=o._definition;if(!d||!d.frames||!o._needsConstruct)return;
        var full=o._constructFull,frame=d.frames[o._frame-1],ops=d.operations[o._frame-1],expected,i,commands;
        o._needsConstruct=false;o._constructFull=false;
        if(full){expected={};commands=[];
            for(i=0;i<frame.length;i++){var p=R.movie.placements[frame[i]];expected[p[0]]=p;commands.push([0,frame[i],255,255]);}
            var depths=Object.keys(o._depths);
            for(i=0;i<depths.length;i++){var depth=depths[i],old=o._depths[depth],want=expected[depth];if(!want||old._symbol!==want[1]||old._born!==want[5])removeTimelineChild(o,old);}
        }else commands=ops;
        for(i=0;i<commands.length;i++){
            var op=commands[i];if(op[0]===1){var removed=o._depths[op[1]];if(removed)removeTimelineChild(o,removed);continue;}
            var id=op[1],flags=op[2],p=R.movie.placements[id],child=o._depths[p[0]];
            if(child&&(flags&2)&&(child._symbol!==p[1]||child._born!==p[5])){removeTimelineChild(o,child);child=null;}
            if(!child){
                if(!(flags&2))continue;
                child=initial||R.phase==='construct'?make(p[1],o,p[4],p[2],p[3]):makePending(p[1],o,p[4],p[2],p[3]);child._timelineDepth=p[0];child._born=p[5];child._timelineName=p[4];o._depths[p[0]]=child;
                var insertion=o._children.length;for(var j=0;j<o._children.length;j++)if(o._children[j]._timelineDepth>p[0]){insertion=j;break;}
                o._children.splice(insertion,0,child);if(stageListenerBranches.has(child))markStageListenerPath(o);renderChanged(o,4);matrixChanged(child);invalidateMotion(o);if(child._hasScriptSubtree)markScriptPath(o);
                if(!child._timelinePending){if(p[4])o[p[4]]=child;emit(child,'added',true);if(o.stage)emitStageTree(child,'addedToStage');}
            }else if(child._placement!==id||!full){
                if(flags&4){child._matrix=p[2];child[attributeShareKey]|=1;child._rotation=child._scaleX=child._scaleY=undefined;matrixChanged(child);}
                if(flags&8){if(child._color[3]<=0&&p[3][3]>0)renderTreeChanged(child);child._color=p[3];child[attributeShareKey]|=2;++child._colorVersion;renderChanged(child,2);}
            }
            if((op[3]&1)||(full&&p[8])){child._filtersOverride=false;child._scriptFilters=[];++child._filtersVersion;}
            renderChanged(child,8);child._placement=id;child._ratio=p[7];child._clipDepth=p[6];child._filters=p[8];child._blend=p[9];
        }
        var sounds=d.frame_sounds&&d.frame_sounds[o._frame];
        if(sounds)for(i=0;i<sounds.length;i++)timelineSound(sounds[i]);
    }
    function removeTimelineChild(parent,child){if(child.name&&parent[child.name]===child)parent[child.name]=null;parent.removeChildAt(parent._children.indexOf(child),true);}
    function Stage(){DisplayObjectContainer.call(this);this.stageWidth=550;this.stageHeight=400;this.scaleMode='noScale';this.quality='high';this.frameRate=30;this.focus=null;}
    inherit(Stage,DisplayObjectContainer);
    function plainHTML(value){return decodeXML(String(value).replace(/<br\s*\/?\s*>/gi,'\n').replace(/<\/p>\s*<p[^>]*>/gi,'\n').replace(/<[^>]*>/g,'')).replace(/&nbsp;/g,' ');}
    function TextField(){DisplayObject.call(this);var initial=this._definition&&this._definition.text||'';this._html=this._definition&&(this._definition.flags&0x200)?initial:null;this._text=this._html===null?initial:plainHTML(initial);this.selectable=false;this.type='dynamic';}
    inherit(TextField,DisplayObject);property(TextField.prototype,'text',function(){return this._text;},function(v){this._text=String(v);this._html=null;renderChanged(this,16);});
    property(TextField.prototype,'htmlText',function(){return this._html===null?this._text:this._html;},function(v){this._html=String(v);this._text=plainHTML(this._html);renderChanged(this,16);});
    property(TextField.prototype,'length',function(){return this._text.length;});
    function NativeButton(){DisplayObjectContainer.call(this);}inherit(NativeButton,DisplayObjectContainer);
    flash.display.DisplayObject=DisplayObject;flash.display.DisplayObjectContainer=DisplayObjectContainer;flash.display.Sprite=Sprite;
    flash.display.MovieClip=MovieClip;flash.display.Stage=Stage;flash.display.SimpleButton=NativeButton;flash.display.StageScaleMode={NO_SCALE:'noScale',SHOW_ALL:'showAll'};flash.text.TextField=TextField;
    function Loader(){DisplayObjectContainer.call(this);this.contentLoaderInfo=new EventDispatcher();}inherit(Loader,DisplayObjectContainer);
    Loader.prototype.load=function(){var self=this;R.deferred.push(function(){self.contentLoaderInfo.dispatchEvent(new IOErrorEvent('ioError',false,false,'Offline PSP runtime'));});};Loader.prototype.unload=function(){};Loader.prototype.unloadAndStop=function(){};flash.display.Loader=Loader;
    function SoundTransform(volume,pan){this.volume=volume===undefined?1:+volume;this.pan=+pan||0;}
    function SoundChannel(){EventDispatcher.call(this);this._soundTransform=new SoundTransform();this._stopped=false;this._audioId=0;this._nativeAudio=0;}
    inherit(SoundChannel,EventDispatcher);
    property(SoundChannel.prototype,'soundTransform',function(){return new SoundTransform(this._soundTransform.volume,this._soundTransform.pan);},function(t){this._soundTransform=new SoundTransform(t.volume,t.pan);if(this._audioId&&!this._stopped){R.audioEvents.push(['transform',this._audioId,f32(t.volume*Math.max(0,1-t.pan)),0,0,f32(t.volume*Math.max(0,1+t.pan))]);if(typeof nativeAudioTransform==='function')nativeAudioTransform(this._nativeAudio,t.volume,t.pan);}});
    property(SoundChannel.prototype,'position',function(){if(this._nativeAudio&&typeof nativeAudioPosition==='function')return nativeAudioPosition(this._nativeAudio);return Math.max(0,(R.frame-this._started)*1000/30+this._startMs);});
    SoundChannel.prototype.stop=function(){if(!this._stopped&&this._audioId){R.audioEvents.push(['stop',this._audioId]);if(typeof nativeAudioStop==='function')nativeAudioStop(this._nativeAudio);}this._stopped=true;};
    function beginSound(id,info,startMs,transform){
        var d=R.movie.definitions[id];if(!d||d.kind!==14)throw new Error('Unknown sound '+id);
        if(info.event===2){for(var i=0;i<R.sounds.length;i++)if(R.sounds[i]._symbol===id)R.sounds[i].stop();return null;}
        if(info.event===1)for(var i=0;i<R.sounds.length;i++)if(R.sounds[i]._symbol===id&&!R.sounds[i]._stopped)return null;
        var c=new SoundChannel();c._audioId=++R.audioSequence;c._symbol=id;c._started=R.frame;c._startMs=startMs||0;
        var begin=info.in_sample===undefined?Math.floor(c._startMs*44.1):info.in_sample,end=info.out_sample===undefined?Math.floor(d.samples*44100/d.sample_rate):info.out_sample;
        c._durationFrames=Math.max(0,(end-begin)/44100*30)*info.loops;
        R.audioEvents.push(['start',c._audioId,'event-'+id,info.loops,false]);
        if(typeof nativeAudioStart==='function')c._nativeAudio=nativeAudioStart(id,begin,end,info.loops,info.event===1,info.envelope||[]);
        R.sounds.push(c);if(transform)c.soundTransform=transform;return c;
    }
    function timelineSound(info){return beginSound(info.id,info,0,null);}
    function Sound(){EventDispatcher.call(this);this._symbol=this.constructor.$symbol===undefined?-1:this.constructor.$symbol;this.length=0;}
    inherit(Sound,EventDispatcher);Sound.prototype.play=function(start,loops,transform){return beginSound(this._symbol,{event:0,loops:Math.min(65535,Math.max(0,integer(loops||0))+1)},+start||0,transform);};
    Sound.prototype.load=function(){throw new Error('External streaming sound is not implemented');};flash.media.Sound=Sound;flash.media.SoundChannel=SoundChannel;flash.media.SoundTransform=SoundTransform;
    flash.utils.Dictionary=function(){};flash.utils.Proxy=function(){};
    function Timer(delay,count){EventDispatcher.call(this);this.delay=+delay;this.repeatCount=count||0;this.currentCount=0;this.running=false;R.timers.push(this);}
    function currentTime(){return typeof nativeClockMS==='function'?nativeClockMS():R.frame*1000/30;}
    inherit(Timer,EventDispatcher);Timer.prototype.start=function(){this.running=true;this._next=currentTime()+this.delay;};Timer.prototype.stop=function(){this.running=false;};Timer.prototype.reset=function(){this.running=false;this.currentCount=0;};flash.utils.Timer=Timer;
    flash.utils.getTimer=function(){return integer(currentTime());};
    flash.utils.setTimeout=function(fn,delay){var t=new Timer(delay,1),args=Array.prototype.slice.call(arguments,2);t.addEventListener('timer',function(){fn.apply(null,args);});t.start();return R.timers.length;};
    flash.utils.getDefinitionByName=function(name){name=String(name).replace('::','.');if(!R.classes[name])throw new ReferenceError('Unknown source class '+name);return R.classes[name];};
    flash.utils.getQualifiedClassName=function(obj){if(obj==null)return 'null';return obj.constructor.$qname||typeof obj;};
    flash.utils.Endian={LITTLE_ENDIAN:'littleEndian',BIG_ENDIAN:'bigEndian'};
    flash.utils.ByteArray=function(){this.position=0;this.length=0;this._bytes=[];};
    flash.utils.ByteArray.prototype.writeUTFBytes=function(s){for(var i=0;i<s.length;i++)this._bytes.push(s.charCodeAt(i)&255);this.length=this._bytes.length;};
    flash.utils.ByteArray.prototype.readUTFBytes=function(n){var s='';while(n--&&this.position<this._bytes.length)s+=String.fromCharCode(this._bytes[this.position++]);return s;};
    flash.net.URLRequest=function(url){this.url=url||'';this.method='GET';this.requestHeaders=[];this.data=null;};
    flash.net.URLRequestHeader=function(name,value){this.name=name;this.value=value;};flash.net.URLRequestMethod={GET:'GET',POST:'POST'};
    flash.net.URLVariables=function(){};flash.net.URLLoaderDataFormat={TEXT:'text',BINARY:'binary',VARIABLES:'variables'};
    function URLLoader(){EventDispatcher.call(this);this.data=null;this.dataFormat='text';}inherit(URLLoader,EventDispatcher);
    URLLoader.prototype.load=function(request){var self=this;R.deferred.push(function(){var match=String(request.url).match(/\/levels\/([0-9a-f]{32}\.xml)$/);
        if(match){self.data=nativeReadText('levels/'+match[1]);self.dispatchEvent(new Event('complete'));}
        else self.dispatchEvent(new IOErrorEvent('ioError',false,false,'Offline PSP runtime'));
    });};URLLoader.prototype.close=function(){};flash.net.URLLoader=URLLoader;
    var saves={};flash.net.SharedObject={getLocal:function(name){if(!saves[name]){var data={};if(typeof nativeLoadSave==='function'){var s=nativeLoadSave(name);if(s)data=JSON.parse(s);}saves[name]={data:data,flush:function(){if(typeof nativeStoreSave==='function')nativeStoreSave(name,JSON.stringify(this.data));return 'flushed';},close:function(){},clear:function(){this.data={};}};}return saves[name];}};
    R.peekSave=function(name){return saves[name]?saves[name].data:null;};
    function LocalConnection(){EventDispatcher.call(this);}inherit(LocalConnection,EventDispatcher);
    LocalConnection.prototype.allowDomain=function(){};LocalConnection.prototype.allowInsecureDomain=function(){};
    LocalConnection.prototype.connect=function(name){this.connectionName=String(name);};LocalConnection.prototype.close=function(){this.connectionName=null;};
    LocalConnection.prototype.send=function(){var self=this;R.deferred.push(function(){var e=new Event('status');e.level='error';e.code='';self.dispatchEvent(e);});};flash.net.LocalConnection=LocalConnection;
    flash.net.ObjectEncoding={AMF0:0,AMF3:3};flash.net.navigateToURL=function(request,target){R.lastExternalURL=String(request.url);};
    flash.system.Capabilities={playerType:'StandAlone',version:'PSP 0,1,0,0',language:'en',isDebugger:false,os:'PSP-2000'};
    flash.system.Security={allowDomain:function(){},allowInsecureDomain:function(){},sandboxType:'localTrusted'};
    flash.filters.ColorMatrixFilter=function(matrix){this.matrix=matrix;};
    var keys={UP:38,DOWN:40,LEFT:37,RIGHT:39,SPACE:32,ENTER:13,ESCAPE:27,SHIFT:16,CONTROL:17,DELETE:46,BACKSPACE:8,TAB:9};
    for(var k=65;k<=90;k++)keys[String.fromCharCode(k)]=k;flash.ui.Keyboard=keys;
    function XMLValue(value){this.value=value===undefined?'':String(value);}
    XMLValue.prototype.toString=function(){return this.value;};XMLValue.prototype.valueOf=XMLValue.prototype.toString;
    XMLValue.prototype.split=function(s){return this.value.split(s);};
    function decodeXML(s){return s.replace(/&quot;/g,'"').replace(/&apos;/g,"'").replace(/&lt;/g,'<').replace(/&gt;/g,'>').replace(/&amp;/g,'&');}
    function XML(source){this.source=source===undefined?'':String(source);}
    XML.prototype.child=function(name){var escaped=String(name).replace(/[.*+?^${}()|[\]\\]/g,'\\$&'),m=this.source.match(new RegExp('<'+escaped+'(?:\\s[^>]*)?\\s*/?>'));return {attribute:function(key){if(!m)return new XMLValue();var a=m[0].match(new RegExp('\\s'+key+'=(?:"([^"]*)"|\'([^\']*)\')'));return new XMLValue(a?decodeXML(a[1]===undefined?a[2]:a[1]):'');}};};global.XML=XML;
    // Level XML asks for hundreds of distinct flat tags. Index their first
    // matching fragments once, then retain the existing child/attribute parser
    // on that fragment. Overlapping '<' positions preserve the legacy match
    // even for tag-like text inside an attribute. Public source replacement
    // invalidates the index; uncommon names keep the original parser.
    var xmlChildFallback=XML.prototype.child,xmlTagIndexes=new WeakMap();
    XML.prototype.child=function(name){
        name=String(name);
        if(typeof this.source!=='string'||!/^[A-Za-z_][A-Za-z0-9_.:-]*$/.test(name))return xmlChildFallback.call(this,name);
        var cached=xmlTagIndexes.get(this);
        if(!cached||cached.source!==this.source){
            var tags=Object.create(null),pattern=/<([A-Za-z_][A-Za-z0-9_.:-]*)(?:\s[^>]*)?\s*\/?>/g,match;
            while((match=pattern.exec(this.source))){if(tags[match[1]]===undefined)tags[match[1]]=match[0];pattern.lastIndex=match.index+1;}
            cached={source:this.source,tags:tags};xmlTagIndexes.set(this,cached);
        }
        return xmlChildFallback.call({source:cached.tags[name]||''},name);
    };
    global.SecurityError=function(message){this.message=message||'';};global.IOError=function(message){this.message=message||'';};
    // AVM2 uses the avmplus quicksort, including its observable tie permutation.
    // Native JavaScript's stable sort changes equal-trigger enemy spawn order.
    function flashQuickSort(a,lo,hi,compare){
        var size=hi-lo,temp;
        function swap(i,j){temp=a[i];a[i]=a[j];a[j]=temp;}
        if(size<2)return;
        if(size===2){if(compare(a[lo],a[lo+1])>0)swap(lo,lo+1);return;}
        if(size===3){if(compare(a[lo],a[lo+1])>0)swap(lo,lo+1);if(compare(a[lo+1],a[lo+2])>0){swap(lo+1,lo+2);if(compare(a[lo],a[lo+1])>0)swap(lo,lo+1);}return;}
        swap(lo,lo+Math.floor(size/2));var left=lo,right=hi;
        while(true){do{++left;}while(left<hi&&compare(a[left],a[lo])<=0);do{--right;}while(right>lo&&compare(a[right],a[lo])>=0);if(right<left)break;swap(left,right);}
        swap(lo,right);
        if(right-lo>=hi-left){flashQuickSort(a,left,hi,compare);flashQuickSort(a,lo,right,compare);}
        else{flashQuickSort(a,lo,right,compare);flashQuickSort(a,left,hi,compare);}
    }
    function numericCompare(a,b){
        if(typeof a==='number'&&typeof b==='number'&&(a|0)===a&&(b|0)===b&&a>=-268435456&&a<268435456&&b>=-268435456&&b<268435456){var diff=((a<<3)-(b<<3))|0;return diff>0?1:diff<0?-1:0;}
        a=Number(a);b=Number(b);return a!==a?(b!==b?0:1):b!==b?-1:a>b?1:a<b?-1:0;
    }
    function fieldCompare(a,b,options){if(options&16)return numericCompare(a,b);a=String(a);b=String(b);if(options&1){a=a.toLowerCase();b=b.toLowerCase();}return a>b?1:a<b?-1:0;}
    function sortedResult(array,compare,options){
        var values=[],unique=true;for(var i=0;i<array.length;i++)values.push([i,array[i]]);
        flashQuickSort(values,0,values.length,function(a,b){
            if(a[1]===undefined||b[1]===undefined){if(a[1]===b[1]){unique=false;return 0;}return a[1]===undefined?1:-1;}
            var order=compare(a[1],b[1]);if(order===0)unique=false;return options&2?-order:order;
        });
        if((options&4)&&!unique)return 0;
        var result=options&8?[]:array;for(var i=0;i<values.length;i++)result[i]=options&8?values[i][0]:values[i][1];return result;
    }
    R.arraySort=function(array,comparator,options){
        if(typeof comparator!=='function'){options=comparator||0;comparator=null;}else options=options||0;
        return sortedResult(array,comparator?function(a,b){return Number(comparator.call(array,a,b))|0;}:function(a,b){return fieldCompare(a,b,options);},options);
    };
    R.arraySortOn=function(array,names,options){
        names=typeof names==='string'?[names]:names;var flags=typeof options==='number'?[options]:options||[0],first=flags[0]||0;
        return sortedResult(array,function(a,b){for(var i=0;i<names.length;i++){var option=flags[Math.min(i,flags.length-1)]||0,order=fieldCompare(a[names[i]],b[names[i]],option);if(order)return option&2?-order:order;}return 0;},first&12);
    };
    R.depthOrderBackend='original';R.depthOrderBatches=0;R.nativeDepthSorts=0;
    function installDepthBatch(){
        var Class=R.classes['com.nitrome.beatemup.FightingGameMain'];
        if(!Class||!nativeArrayMove)return;
        var original=Class.prototype.obf_01010407,text=Function.prototype.toString.call(original),hash=2166136261;
        for(var i=0;i<text.length;i++)hash=Math.imul(hash^text.charCodeAt(i),16777619)>>>0;
        // This is a source-specific lowering, not a replacement for container
        // APIs. Keep the independently extracted game class file unchanged.
        if(text.length!==841||hash!==1648948742)return;
        var language=org.apache.royale.utils.Language,as=language.as,is=language.is;
        var compare=Class.prototype.obf_0101090d,compareText=Function.prototype.toString.call(compare),compareHash=2166136261;
        for(var i=0;i<compareText.length;i++)compareHash=Math.imul(compareHash^compareText.charCodeAt(i),16777619)>>>0;
        var numericSort=compareText.length===333&&compareHash===747202807?nativeDepthSort:null;
        var sourceSort=R.arraySort,sourceClosure=language.closure,sourceBind=goog.bind,sortBindings=new WeakMap();
        var setIndex=DisplayObjectContainer.prototype.setChildIndex,getIndex=DisplayObjectContainer.prototype.getChildIndex;
        function dataMethod(object,name,value){
            while(object){var descriptor=Object.getOwnPropertyDescriptor(object,name);if(descriptor)return descriptor.value===value;object=Object.getPrototypeOf(object);}
            return false;
        }
        function batch(game,list){
            if(language.as!==as||language.is!==is||flash.display.DisplayObject!==DisplayObject||
               Object.prototype.hasOwnProperty.call(DisplayObject,Symbol.hasInstance))return false;
            var descriptor=Object.getOwnPropertyDescriptor(game,'midLayer'),layer=descriptor&&descriptor.value;
            if(!layer||!dataMethod(layer,'setChildIndex',setIndex)||!dataMethod(layer,'getChildIndex',getIndex))return false;
            descriptor=Object.getOwnPropertyDescriptor(layer,'_children');
            var children=descriptor&&descriptor.value,n=list.length;
            if(!children||children.length!==n||!nativeArrayMove(children,0,0))return false;
            // Validate the entire permutation before touching the live array.
            // In particular, comparator mutations must retain the old API path.
            var remaining=new Set(children);
            if(remaining.size!==n)return false;
            for(var i=0;i<n;i++)if(!(list[i] instanceof DisplayObject)||!remaining.delete(list[i]))return false;
            // The source moves each sorted child to index zero, producing this
            // reverse order. The existing array identity and AVM2 tie order stay.
            for(var i=0;i<n;i++)children[i]=list[n-1-i];
            if(n>1)renderChanged(layer,4);
            ++R.depthOrderBatches;return true;
        }
        Class.prototype.obf_01010407=function(){
            var list=new Array(),date=null,i=0,child=null;
            // Retain the source's collection, Date calls and closure binding.
            while(i<this.midLayer.numChildren){
                child=this.midLayer.getChildAt(i);
                if(child.cIndex==undefined||child.cIndex==0){date=new Date();child.cIndex=date.getTime();}
                list.push(this.midLayer.getChildAt(i));i++;
            }
            var sort=R.arraySort,method=this.obf_0101090d,own=Object.prototype.hasOwnProperty.call(this,'obf_0101090d');
            var bound=language.closure(method,this,'obf_0101090d');
            var known=method===compare&&(!own||bound===compare)||sortBindings.has(this)&&bound===sortBindings.get(this);
            if(numericSort&&sort===sourceSort&&global.Number===sourceNumber&&known&&
               dataMethod(language,'closure',sourceClosure)&&dataMethod(goog,'bind',sourceBind)&&
               dataMethod(Array.prototype,'push',sourceArrayPush)&&dataMethod(Math,'floor',sourceMathFloor)){
                sortBindings.set(this,bound);
                if(numericSort(list))++R.nativeDepthSorts;else sort.call(R,list,bound);
            }else sort.call(R,list,bound);
            if(!batch(this,list))for(i=0;i<list.length;i++)this.midLayer.setChildIndex(language.as(list[i],flash.display.DisplayObject,true),0);
        };
        R.depthOrderBackend='source-batched-reorder';
    }
    R.loaderInfo={url:'file:///game/source.swf',loaderURL:'file:///game/source.swf',bytesLoaded:2077141,bytesTotal:2077141,parameters:{},applicationDomain:{getDefinition:flash.utils.getDefinitionByName}};
    function installNativeRockCollision(){
        if(typeof nativeRockCollisionFactory!=='function')return;
        var C=global.com&&com.nitrome&&com.nitrome.beatemup,G=C&&C.FightingGameMain;if(!G)return;
        var source=G.prototype.rockCollision,text=Function.prototype.toString.call(source),hash=2166136261;
        for(var n=0;n<text.length;n++){hash^=text.charCodeAt(n);hash=Math.imul(hash,16777619)>>>0;}
        if(text.length!==4601||hash!==3051984479)return;
        var post=function rockPostHit(param1,_loc3_,__rockType,__rockDefs){var _loc2_=true;if (param1.invincible <= 0) {
              if (__rockType != 112 && __rockType != 113) {
                _loc3_.dangerous = __rockDefs.ELEMENTDANGEROUS[__rockType];
              } else {
                _loc3_.dangerous = false;
                _loc2_ = false;
                if (__rockType == 112 && this.obf_01010502 == 1) {
                  _loc3_.dangerous = true;
                  _loc2_ = true;
                }
                if (__rockType == 113 && this.obf_01010502 == 0) {
                  _loc3_.dangerous = true;
                  _loc2_ = true;
                }
              }
            } else {
              _loc3_.dangerous = false;
              if (__rockType == 112 && this.obf_01010502 == 0) {
                _loc2_ = false;
              }
              if (__rockType == 113 && this.obf_01010502 == 1) {
                _loc2_ = false;
              }
              if (__rockDefs.ELEMENTDANGEROUS[__rockType]) {
                _loc2_ = false;
              }
            };return _loc2_;};
        var tail=function rockCollision(param1,_loc3_,_loc2_,_loc6_,__rockZ,__rockY,__rockKill,__rockDefs,__deClass0,__rockElement){var _loc4_=false,_loc5_=false;while (_loc6_ < this.obf_0101060d.length) { var __rockElement=this.obf_0101060d[_loc6_],__rockType=__rockElement.definition.elementType;
      if (__rockDefs.ELEMENTLAYERS[__rockType] == __rockDefs.MIDLAYER && (__rockDefs.ELEMENTDEPTHSFRONT[__rockType] > 0 || __rockDefs.ELEMENTDEPTHSBACK[__rockType] > 0)) {
        if (__rockZ >= __rockElement.zPos - __rockDefs.ELEMENTDEPTHSFRONT[__rockType] && __rockZ <= __rockElement.zPos + __rockDefs.ELEMENTDEPTHSBACK[__rockType] && __rockY <= __rockDefs.ELEMENTHEIGHTS[__rockType]) {
          if (__rockKill.hitTestObject(__rockElement.killArea)) {
            _loc2_ = true;
            _loc3_.clip = __rockElement;
            if (param1.invincible <= 0) {
              if (__rockType != 112 && __rockType != 113) {
                _loc3_.dangerous = __rockDefs.ELEMENTDANGEROUS[__rockType];
              } else {
                _loc3_.dangerous = false;
                _loc2_ = false;
                if (__rockType == 112 && this.obf_01010502 == 1) {
                  _loc3_.dangerous = true;
                  _loc2_ = true;
                }
                if (__rockType == 113 && this.obf_01010502 == 0) {
                  _loc3_.dangerous = true;
                  _loc2_ = true;
                }
              }
            } else {
              _loc3_.dangerous = false;
              if (__rockType == 112 && this.obf_01010502 == 0) {
                _loc2_ = false;
              }
              if (__rockType == 113 && this.obf_01010502 == 1) {
                _loc2_ = false;
              }
              if (__rockDefs.ELEMENTDANGEROUS[__rockType]) {
                _loc2_ = false;
              }
            }
          }
        }
      }
      _loc6_++;
    }
if (!(param1 instanceof com.nitrome.beatemup.Minotaur)) {
    _loc6_ = 0;
    while (_loc6_ < this.obf_0101090e.length) {
      if (this.obf_0101090e[_loc6_].mode == com.nitrome.beatemup.Projectile.STILL) {
        if (param1.killArea.hitTestObject(this.obf_0101090e[_loc6_]._hitArea)) {
          if (!this.obf_0101090e[_loc6_].treasureSpilt && param1.zPos - this.obf_0101090e[_loc6_].zPos > -10 && param1.zPos - this.obf_0101090e[_loc6_].zPos < 10 && param1.yPos < 15) {
            _loc2_ = true;
            _loc3_.clip = this.obf_0101090e[_loc6_];
            _loc3_.dangerous = false;
          }
        }
      }
      _loc6_++;
    }
  }
  if (this.talos) {
    if (this.talos.alive && param1.killArea.hitTestObject(this.talos.leftFoot.foot.foot.killArea) || !this.talos.alive && param1.killArea.hitTestObject(this.talos.leftFoot.foot.foot.killAreaDown)) {
      if (param1.zPos - this.talos.leftFoot.zPos < 20 && param1.zPos - this.talos.leftFoot.zPos > -25) {
        _loc2_ = true;
        _loc3_.clip = this.talos.leftFoot;
        if (this.talos.footMode == __deClass0.FOOTDROP && (this.talos.mode == __deClass0.ACCURATELEFTSTAMP || this.talos.mode == __deClass0.LEFTSTAMP) || this.talos.mode == __deClass0.WALKIN && !this.talos.footDown(__deClass0.LEFT)) {
          _loc3_.dangerous = true;
          _loc4_ = true;
        } else {
          _loc3_.dangerous = false;
        }
        _loc5_ = true;
      }
    }
    if (this.talos.alive && param1.killArea.hitTestObject(this.talos.rightFoot.foot.foot.killArea) || !this.talos.alive && param1.killArea.hitTestObject(this.talos.rightFoot.foot.foot.killAreaDown)) {
      if (param1.zPos - this.talos.rightFoot.zPos < 20 && param1.zPos - this.talos.rightFoot.zPos > -25) {
        _loc2_ = true;
        _loc3_.clip = this.talos.rightFoot;
        if (this.talos.footMode == __deClass0.FOOTDROP && (this.talos.mode == __deClass0.ACCURATERIGHTSTAMP || this.talos.mode == __deClass0.RIGHTSTAMP) || this.talos.mode == __deClass0.WALKIN && !this.talos.footDown(__deClass0.RIGHT)) {
          _loc3_.dangerous = true;
          _loc4_ = true;
        } else {
          _loc3_.dangerous = false;
        }
        _loc5_ = true;
      }
    }
  }
  _loc3_.collided = _loc2_;
  _loc3_.talosStamp = _loc4_;
  _loc3_.talos = _loc5_;
  return _loc3_;
};
        G.prototype.rockCollision=nativeRockCollisionFactory(source,tail,post,C.ElementDefs,C.Talos,R,G);
        R.rockCollisionBackend=G.prototype.rockCollision===source?'source':'source-bound-frame-candidates';
    }
    function installNativeCpuCollision(){
        if(typeof nativeCpuCollisionFactory!=='function')return;
        var C=global.com&&com.nitrome&&com.nitrome.beatemup,G=C&&C.FightingGameMain;if(!G)return;
        var source=G.prototype.cpuCollide,text=Function.prototype.toString.call(source),hash=2166136261;
        for(var n=0;n<text.length;n++){hash^=text.charCodeAt(n);hash=Math.imul(hash,16777619)>>>0;}
        if(text.length!==982||hash!==100070128)return;
        // Continuation of the exact original loop. A native probe that would
        // require a getter/coercion resumes here at the current index,without
        // repeating prior callbacks or discarding the last colliding object.
        var tail=function cpuCollide(param1,result,collided,index,z,mode,kill,down,cyclopse){
            while(index<this.activeBaddies.length){var other=this.activeBaddies[index];
                if(Math.abs(z-other.zPos)<15&&other.mode!=down&&mode!=down&&param1!=other&&!(other instanceof cyclopse)){
                    if(kill.hitTestObject(other.killArea)){collided=true;result.clip=other;}
                }
                index++;
            }
            result.collided=collided;return result;
        };
        G.prototype.cpuCollide=nativeCpuCollisionFactory(source,tail,C.CharacterFinal,C.Cyclopse);
        R.cpuCollisionBackend='native-numeric-loop-with-source-continuation';
    }
    R.install=function(movie,classes){R.movie=movie;for(var i=0;i<classes.length;i++){var name=classes[i][0],Class=classes[i][1];Class.$qname=name;R.classes[name]=Class;
            // The bound compiler helper may bypass interface searches only
            // for a real extracted class declaration, never an interface.
            var info=Object.getOwnPropertyDescriptor(Class.prototype,'ROYALE_CLASS_INFO');
            if(info&&info.value&&info.value.names&&info.value.names[0].kind==='class')Object.defineProperty(Class,'$deConcreteClass',{value:true});
        }
        var slots=global.DoubleEdgedEntry&&global.DoubleEdgedEntry.primitiveSlots||[];
        for(var i=0;i<slots.length;i++)(function(slot){var Class=R.classes[slot[0]],name=slot[1],key=slot[0]+'.'+name,initial=slot[2]==='uint'?Class.prototype[name]>>>0:Class.prototype[name]|0;
            var get=function(){return this._typedSlotValues&&Object.prototype.hasOwnProperty.call(this._typedSlotValues,key)?this._typedSlotValues[key]:initial;},set=function(value){if(!Object.prototype.hasOwnProperty.call(this,'_typedSlotValues'))this._typedSlotValues=Object.create(null);this._typedSlotValues[key]=slot[2]==='uint'?value>>>0:value|0;};
            if(typeof nativeTypedSlotPair==='function'){var pair=nativeTypedSlotPair(key,initial,slot[2]==='uint',get,set);get=pair[0];set=pair[1];}
            property(Class.prototype,name,get,set);
        })(slots[i]);
        org.apache.royale.utils.Language.sort=R.arraySort;org.apache.royale.utils.Language.sortOn=R.arraySortOn;
        installDepthBatch();installNativeCpuCollision();installNativeRockCollision();
        flash.system.Security.sandboxType=(movie.file_attributes&1)?'localWithNetwork':'localWithFile';
        for(var id in movie.symbols){var name=movie.symbols[id],Class=R.classes[name];if(!Class)throw new Error('Uncompiled symbol '+name);Class.$symbol=Number(id);R.byId[id]=Class;}
        R.stage=new Stage();R.root=make(0,R.stage,'root1');R.stage._children.push(R.root);return R.root;
    };
    function broadcast(type){var list=alive(R.broadcasts[type]||[]);for(var i=0;i<list.length;i++)list[i].dispatchEvent(new Event(type));}
    function localScripts(o){if(o._pendingScript&&o._lastScript!==o._frame&&o._scripts&&o._scripts[o._frame]){o._pendingScript=false;o._lastScript=o._frame;o._runningScript=true;try{o._scripts[o._frame]();}catch(error){scriptError(error,'frame script');}finally{o._runningScript=false;}
            if(o._queuedGoto){var target=o._queuedGoto;o._queuedGoto=null;goto(o,target[0],target[1]);constructChildren(o);}
        }
    }
    function scripts(o){if(!o._hasScriptSubtree||!pendingScriptBranches.has(o))return;
        // Idle clips need no interpreted call into the frame-script runner.
        // Keep the runner itself for real work, including queued goto/error order.
        if(o._pendingScript&&o._lastScript!==o._frame&&o._scripts&&o._scripts[o._frame])localScripts(o);
        if(o._children){var a=o._children.slice();for(var i=0;i<a.length;i++)scripts(a[i]);}}
    function enter(o,inheritedSkip){
        if(!(o._motionValid?o._hasMotion:motionTree(o))){if(o._skipAdvance)o._skipAdvance=false;return;}
        var skip=inheritedSkip||o._skipAdvance;
        if(o._children){var children=o._children.slice();for(var i=children.length-1;i>=0;i--){var child=children[i];
            if(child._motionValid&&!child._hasMotion){if(child._skipAdvance)child._skipAdvance=false;continue;}
            // A fresh non-playing leaf has no timeline or descendant to advance.
            // Materialize exactly the same motion-cache fields as motionTree.
            if(!child._motionValid&&!child._skipAdvance&&!child._playing&&!child._children){child._hasMotion=false;child._motionValid=true;continue;}
            enter(child,skip);
        }}
        if(skip){o._skipAdvance=false;invalidateMotion(o);return;}
        if(o._playing&&o._definition&&o._definition.frames&&o._definition.frames.length>1){
            o._frame=o._frame%o._definition.frames.length+1;o._pendingScript=true;o._lastScript=0;o._needsConstruct=true;o._constructFull=o._frame===1;
            // Existing PlaceObject attributes are observable during enterFrame.
            // AS3 constructors for newly placed objects are a separate remaining gate.
            construct(o,false);queuePendingScript(o);
        }
    }
    // Native traversal retains the source snapshot,reverse child order and
    // calls the same construction/script helpers. Complex private descriptors
    // use the source finish/loop bodies copied verbatim below.
    if(typeof nativeTimelineWalkerFactory==='function'){
        var finishEnteredSource=function(o,skip){
        if(skip){o._skipAdvance=false;invalidateMotion(o);return;}
        if(o._playing&&o._definition&&o._definition.frames&&o._definition.frames.length>1){
            o._frame=o._frame%o._definition.frames.length+1;o._pendingScript=true;o._lastScript=0;o._needsConstruct=true;o._constructFull=o._frame===1;
            // Existing PlaceObject attributes are observable during enterFrame.
            // AS3 constructors for newly placed objects are a separate remaining gate.
            construct(o,false);queuePendingScript(o);
        }
        };
        var walkChildrenSource=function(children,skip){for(var i=children.length-1;i>=0;i--){var child=children[i];
            if(child._motionValid&&!child._hasMotion){if(child._skipAdvance)child._skipAdvance=false;continue;}
            // A fresh non-playing leaf has no timeline or descendant to advance.
            // Materialize exactly the same motion-cache fields as motionTree.
            if(!child._motionValid&&!child._skipAdvance&&!child._playing&&!child._children){child._hasMotion=false;child._motionValid=true;continue;}
            enter(child,skip);
        }};
        enter=nativeTimelineWalkerFactory(enter,motionTree,invalidateMotion,construct,queuePendingScript,finishEnteredSource,walkChildrenSource,Array.prototype.slice);
    }
    R.tick=function(){
        ++R.frame;R.phase='enter';var orphanRoots=orphans();
        for(var i=0;i<orphanRoots.length;i++)enter(orphanRoots[i],false);enter(R.stage,false);
        broadcast('enterFrame');R.phase='construct';
        while(R.constructQueue.length){var queued=R.constructQueue.shift();if(queued._constructionQueued)constructChildren(queued);}
        broadcast('frameConstructed');R.phase='scripts';orphanRoots=orphans();for(var i=0;i<orphanRoots.length;i++)scripts(orphanRoots[i]);scripts(R.stage);
        // Flash drains scripts registered during the script phase, including
        // newly created children outside the traversal's original child list.
        while(R.scriptCleanup.length){var clip=R.scriptCleanup.shift();clip._pendingScript=true;clip._lastScript=0;localScripts(clip);}
        R.phase='exit';broadcast('exitFrame');cleanupOrphans();R.phase='idle';
        var tasks=R.deferred.splice(0);for(var i=0;i<tasks.length;i++)tasks[i]();
        var now=currentTime(),list=R.timers.slice();for(var i=0;i<list.length;i++){var t=list[i];if(t.running&&now>=t._next){++t.currentCount;t._next+=t.delay;t.dispatchEvent(new TimerEvent('timer'));if(t.repeatCount&&t.currentCount>=t.repeatCount){t.stop();t.dispatchEvent(new TimerEvent('timerComplete'));}}}
        var kept=[];for(var i=0;i<R.sounds.length;i++){var c=R.sounds[i];if(c._stopped)continue;var complete=c._nativeAudio&&typeof nativeAudioActive==='function'?!nativeAudioActive(c._nativeAudio):R.frame-c._started>=c._durationFrames;if(complete){c._stopped=true;c.dispatchEvent(new Event('soundComplete'));}else kept.push(c);}R.sounds=kept;
        refreshPendingScripts();return R.frame;
    };
    function focusMouse(object){var enabled=object.tabEnabled===undefined?object.buttonMode||object instanceof NativeButton||object instanceof TextField&&object.type==='input':object.tabEnabled;R.stage.focus=object!==R.stage&&enabled?object:null;}
    R.key=function(code,down){var e=new KeyboardEvent(down?'keyDown':'keyUp',true,false,0,code);(R.stage.focus||R.stage).dispatchEvent(e);};
    R.clickObject=function(object){focusMouse(object);object.dispatchEvent(new MouseEvent('mouseDown',true));object.dispatchEvent(new MouseEvent('mouseUp',true));object.dispatchEvent(new MouseEvent('click',true));};
    function topmost(o,x,y){
        if(!o.visible)return null;
        if(o._children&&o.mouseChildren){for(var i=o._children.length-1;i>=0;i--){var c=topmost(o._children[i],x,y);if(c)return c;}}
        if(o instanceof Sprite||o instanceof TextField||o instanceof NativeButton){if(o.mouseEnabled&&o.hitTestPoint(x,y,false)&&shapePoint(o,x,y,true))return o;}
        return null;
    }
    R.pointer=function(x,y,down){
        R.mouseX=+x;R.mouseY=+y;var target=topmost(R.stage,x,y)||R.stage;
        if(target!==R.hover){if(R.hover)R.hover.dispatchEvent(new MouseEvent('mouseOut',true));R.hover=target;target.dispatchEvent(new MouseEvent('mouseOver',true));}
        target.dispatchEvent(new MouseEvent('mouseMove',true));
        if(down&&!R.mouseDown){R.pressed=target;focusMouse(target);target.dispatchEvent(new MouseEvent('mouseDown',true));}
        if(!down&&R.mouseDown){target.dispatchEvent(new MouseEvent('mouseUp',true));if(target===R.pressed)target.dispatchEvent(new MouseEvent('click',true));R.pressed=null;}
        R.mouseDown=!!down;return target.name;
    };
    R.click=function(x,y){R.pointer(x,y,true);return R.pointer(x,y,false);};
    R.make=make;R.matrixMultiply=matrixMultiply;return R;
})(this);

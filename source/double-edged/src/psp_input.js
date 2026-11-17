/* Live PSP pad adapter. All game actions enter through original keyboard or
 * mouse events; game state, physics and enemy counts are never assigned here. */
var PSPInput={last:0,keys:{},focus:null,x:275,y:230,pointer:false,player:0};
PSPInput.release=function(){for(var key in this.keys)if(this.keys[key])FlashRuntime.key(Number(key),false);this.keys={};};
PSPInput.afterLoading=function(){this.release();this.last=0;this.pointer=false;this._layoutKey=null;};
PSPInput.buttons=function(){
    var result=[],stage=FlashRuntime.stage,root=FlashRuntime.root;
    var scope=com.nitrome.beatemup.Controller.paused&&root.pauseMessage&&root.pauseMessage.stage?root.pauseMessage:
        root.clear_panel&&root.clear_panel.stage&&root.clear_panel.visible&&root.clear_panel.currentFrame>=21?root.clear_panel:root;
    function visit(o){
        if(!o.visible||o.alpha<=0||o.name==='buttonBlanker')return;
        if(o.mouseEnabled&&(o.buttonMode||o instanceof flash.display.SimpleButton)){
            var b=o.getBounds(stage);
            if(b.width>0&&b.height>0&&b.x+b.width>0&&b.y+b.height>0&&b.x<550&&b.y<400)
                result.push({object:o,x:Math.max(0,Math.min(550,b.x+b.width/2)),y:Math.max(0,Math.min(400,b.y+b.height/2)),name:o.constructor.$qname||o.name});
            if(!o.mouseChildren)return;
        }
        if(o._children)for(var i=0;i<o._children.length;i++)visit(o._children[i]);
    }
    visit(scope);return result;
};
PSPInput.choose=function(list){
    if(this._userFocus)for(var i=0;i<list.length;i++)if(list[i].object===this.focus)return list[i];
    var wanted=['PlayButton','Play1PButton','normalLevelButton','ContinueGameButton','NextLevelButton'];
    for(var k=0;k<wanted.length;k++)for(var i=0;i<list.length;i++)if(list[i].name.split('.').pop()===wanted[k]){this._preferReady=true;return list[i];}
    for(var i=0;i<list.length;i++)if(list[i].object===this.focus)return list[i];
    return list[0]||null;
};
PSPInput.moveFocus=function(list,selected,dx,dy){
    var next=null,best=Infinity;
    for(var i=0;i<list.length;i++){
        var b=list[i];if(b===selected)continue;
        var x=b.x-selected.x,y=b.y-selected.y,along=x*dx+y*dy,across=Math.abs(x*dy-y*dx);
        if(along>1){var score=along+across*2.5;if(score<best){best=score;next=b;}}
    }
    if(next)return next;
    for(var i=0;i<list.length;i++){var b=list[i];if(b===selected)continue;var score=(b.x-selected.x)*dx+(b.y-selected.y)*dy+Math.abs((b.x-selected.x)*dy-(b.y-selected.y)*dx)*2.5;if(score<best){best=score;next=b;}}
    return next||selected;
};
PSPInput.update=function(mask,analogX,analogY){
    var edge=mask&~this.last;this.last=mask;
    var root=FlashRuntime.root,g=root.myFightingGame,controller=com.nitrome.beatemup.Controller;
    var inGame=!!(g&&root.currentFrame>=130),paused=controller.paused;
    var menu=!inGame||paused||!!(root.clear_panel&&root.clear_panel.stage&&root.clear_panel.visible&&root.clear_panel.currentFrame>=21);
    // A result button can retain Flash keyboard focus after its ancestor is
    // removed. Refocus the playfield through an ordinary pointer click.
    if(this._wasMenu&&!menu){FlashRuntime.click(10,10);this.blockedGame=mask&0xc0f0;}
    this._wasMenu=menu;
    if(!menu){
        this.blockedGame=(this.blockedGame||0)&mask;mask&=~this.blockedGame;
        this._layoutKey=null;
        this.pointer=false;
        var custom=com.nitrome.game.Key.custom||[38,40,37,39,188,190,87,83,65,68,71,72],held={};
        var physical=[0x10,0x40,0x80,0x20,0x8000,0x4000],base=this.player*6;
        for(var i=0;i<6;i++)if(mask&physical[i])held[custom[base+i]]=true;
        for(var key in this.keys)if(this.keys[key]&&!held[key])FlashRuntime.key(Number(key),false);
        for(var key in held)if(!this.keys[key])FlashRuntime.key(Number(key),true);this.keys=held;
        if((edge&8)&&root.pauseGame){this.release();FlashRuntime.clickObject(root.pauseGame);}
        return;
    }
    this.release();
    // Stage-entry frames show the original transition before battle.
    // Wait for its completion before accepting another menu confirmation.
    if(root.currentFrame<30||(root.currentFrame>=100&&root.currentFrame<130)){this.pointer=false;this.focus=null;return;}
    // A single PSP enters the original one-player button directly. The WLAN
    // adapter chooses the original two-player button for its connected pair.
    if(root.currentFrame===39&&typeof DELinkInput==='undefined'){
        var soloButtons=this.buttons();for(var si=0;si<soloButtons.length;si++)if(soloButtons[si].name==='com.nitrome.game.Play1PButton'){FlashRuntime.clickObject(soloButtons[si].object);break;}
        this.focus=null;this.pointer=false;return;
    }
    if(paused&&(edge&8)&&root.pauseMessage&&root.pauseMessage.currentFrame===30){FlashRuntime.key(32,true);this.keys[32]=true;return;}
    // Discrete selection only: analog drift never moves focus or enables a cursor.
    this.pointer=false;
    var layout=root.currentFrame+':'+(root.pauseMessage?root.pauseMessage.currentFrame:0)+':'+(root.clear_panel?root.clear_panel.currentFrame:0);
    if(layout!==this._layoutKey){this.focus=null;this._userFocus=false;this._preferReady=false;}
    if(!(edge&0x60f0)&&layout===this._layoutKey&&(this._userFocus||this._preferReady)&&this.focus&&this.focus.stage&&this.focus.visible)return;
    this._layoutKey=layout;
    var list=this.buttons(),selected=this.choose(list);this.pointer=false;
    if(selected){
        if(edge&0xf0)this._userFocus=true;
        if(edge&0x10)selected=this.moveFocus(list,selected,0,-1);
        if(edge&0x40)selected=this.moveFocus(list,selected,0,1);
        if(edge&0x80)selected=this.moveFocus(list,selected,-1,0);
        if(edge&0x20)selected=this.moveFocus(list,selected,1,0);
        this.focus=selected.object;this.x=selected.x;this.y=selected.y;FlashRuntime.pointer(this.x,this.y,false);
        if(edge&0x4000)FlashRuntime.clickObject(selected.object);
    }
    if(edge&0x2000){
        var candidates=['ContinueGameButton','BackToMapButton','BackToTitleButton'];
        for(var k=0;k<candidates.length;k++){var found=false;for(var i=0;i<list.length;i++)if(list[i].name.split('.').pop()===candidates[k]){FlashRuntime.clickObject(list[i].object);found=true;break;}if(found)break;}
    }
};
function pspStep(mask,analogX,analogY){PSPInput.update(mask,analogX,analogY);FlashRuntime.tick();}
function pspSnapshot(){
    var r=FlashRuntime.root,g=r.myFightingGame,c=com.nitrome.beatemup.Controller;
    var result={tick:FlashRuntime.frame,frame:r.currentFrame,rng:FlashRuntime.rng,level:c.selectedLevel,paused:c.paused,pad:PSPInput.last,focus:PSPInput.focus?PSPInput.focus.constructor.$qname:null,music:NitromeGame.music,sfx:NitromeGame.sfx};
    result.focusInstance=PSPInput.focus?PSPInput.focus.name:null;result.profile=FlashRuntime.peekSave?FlashRuntime.peekSave('so_doubleedged1'):null;
    if(g){
        result.game={numPlayers:g.numPlayers,baddieCount:g.baddieCount,baddiesKilled:g.baddiesKilled,gameComplete:g.gameComplete};
        function actor(p){if(!p)return null;var v={frame:p.currentFrame},keys=['xPos','yPos','zPos','mode','health','xSpeed','ySpeed','zSpeed','score','alive','specialPercent','invincible','attackFrame','attackDelay','attackMove','attackPause','weaponType','randomX','randomZ','pauseDelay','collisionDelay'];for(var i=0;i<keys.length;i++)v[keys[i]]=p[keys[i]];return v;}
        result.player=actor(g.p1Character);result.player2=actor(g.p2Character);result.enemy=actor(g.activeBaddies[0]);result.enemies=[];for(var i=0;i<g.activeBaddies.length;i++)result.enemies.push(actor(g.activeBaddies[i]));
    }
    var audio=FlashRuntime.audioEvents.splice(0);if(audio.length)result.audio=audio;return JSON.stringify(result);
}

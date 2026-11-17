// Two physical PSP pads enter the original AS3 keyboard/mouse event handlers.
// P1 owns menus; either player's Start can pause or resume the original game.
var DELinkInput={keys:{},lastPause:0,wasMenu:true};
DELinkInput.physical=function(bits){var masks=[0x10,0x40,0x80,0x20,0x8000,0x4000,0x2000,8],result=0;for(var i=0;i<8;i++)if(bits&(1<<i))result|=masks[i];return result;};
DELinkInput.release=function(){for(var key in this.keys)FlashRuntime.key(Number(key),false);this.keys={};};
DELinkInput.step=function(first,x,y,second){
    var a=this.physical(first),b=this.physical(second),pause=(a|b)&8,menuMask=(a&~8)|pause;
    var r=FlashRuntime.root,g=r.myFightingGame,c=com.nitrome.beatemup.Controller;
    var menu=!g||r.currentFrame<130||c.paused||r.clear_panel&&r.clear_panel.currentFrame>=21;
    if(menu){
        this.release();
        // The connection lobby already selected a two-player session. Enter
        // its original 2P button through an ordinary source mouse event.
        if(r.currentFrame===39){
            PSPInput.release();var buttons=PSPInput.buttons();for(var i=0;i<buttons.length;i++)if(buttons[i].name==='com.nitrome.game.Play2PButton'){FlashRuntime.click(buttons[i].x,buttons[i].y);break;}
            PSPInput.last=menuMask;
        }else PSPInput.update(menuMask,x,y);
    }else{
        if(this.wasMenu){PSPInput.release();FlashRuntime.click(10,10);}
        var custom=com.nitrome.game.Key.custom,desired={};
        for(var i=0;i<6;i++){if(first&(1<<i))desired[custom[i]]=true;if(g.numPlayers>1&&second&(1<<i))desired[custom[6+i]]=true;}
        for(var key in this.keys)if(!desired[key])FlashRuntime.key(Number(key),false);
        for(var key in desired)if(!this.keys[key])FlashRuntime.key(Number(key),true);this.keys=desired;
        if(pause&&!this.lastPause&&r.pauseGame){this.release();FlashRuntime.clickObject(r.pauseGame);}
        PSPInput.pointer=false;PSPInput.last=menuMask;
    }
    this.lastPause=pause;this.wasMenu=menu;FlashRuntime.tick();
};
DELinkInput.signature=function(){
    var r=FlashRuntime.root,g=r.myFightingGame,c=com.nitrome.beatemup.Controller;
    var values=[FlashRuntime.frame,r.currentFrame,FlashRuntime.rng,FlashRuntime.rngCalls,c.selectedLevel,c.gameOver,c.totalScore,c.p1Score,c.p2Score,c.winner,c.paused,r.clear_panel?r.clear_panel.currentFrame:0,r.pauseMessage?r.pauseMessage.currentFrame:0];
    var fields=['xPos','yPos','zPos','mode','health','xSpeed','ySpeed','zSpeed','score','alive','specialPercent','invincible','attackFrame','attackDelay','attackMove','attackPause','weaponType','facing','pickedUp','riding','statuefied','isLizard','randomX','randomZ','pauseDelay','collisionDelay','cIndex'];
    function actor(p){values.push(!!p);if(!p)return;values.push(p._symbol,p.currentFrame);for(var i=0;i<fields.length;i++)values.push(p[fields[i]]);}
    if(g){values.push(g.numPlayers,g.baddieCount,g.baddiesKilled,g.gameComplete,g.obf_01010c02,g.obf_01010401);actor(g.p1Character);actor(g.p2Character);values.push(g.activeBaddies.length);for(var i=0;i<g.activeBaddies.length;i++)actor(g.activeBaddies[i]);
        var t=g.talos;values.push(!!t);if(t){values.push(t.mode,t.footMode,t.alive,t.leftHealth,t.rightHealth,t.invincible,t.talosFallen);for(var i=0;i<2;i++){var f=i?t.rightFoot:t.leftFoot;values.push(f.x,f.y,f.xPos,f.yPos,f.zPos,f.currentFrame,f.foot.foot.achilles.currentFrame);}}
    }else values.push(null);
    values.push(this.lastPause,PSPInput.last,PSPInput.x,PSPInput.y);return JSON.stringify(values);
};
function pspLinkStep(first,x,y,second){DELinkInput.step(first,x,y,second);}
function pspLinkSignature(){return DELinkInput.signature();}

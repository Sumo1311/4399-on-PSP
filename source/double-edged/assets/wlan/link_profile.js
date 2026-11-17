// The original game persists custom keys and level unlocks. Room settings use
// the host's source preferences; cooperative progress has its own save file.
var DEProfile={};
DEProfile.encode=function(settings,progress,controls){
    settings=JSON.parse(settings||'{}');progress=JSON.parse(progress||'{}');controls=JSON.parse(controls||'{}');
    var wire=[],flags=0;for(var i=0;i<96;i++)wire.push(0);wire[0]=1;
    if(settings.musicon!==undefined){flags|=1;if(settings.musicon)flags|=2;}
    if(settings.sfxon!==undefined){flags|=4;if(settings.sfxon)flags|=8;}
    var keys=progress.custom_keys||controls.custom_keys;
    if(keys){if(keys.length!==12)throw new Error('Invalid saved controls');flags|=16;for(var i=0;i<12;i++){var code=keys[i];if(typeof code!=='number'||code!==(code|0)||code<0||code>255)throw new Error('Invalid saved controls');wire[2+i]=code;}}
    var unlocked=progress.levels_unlocked;
    if(unlocked){if(unlocked.length!==12)throw new Error('Invalid saved progress');flags|=32;for(var i=0;i<12;i++){if(unlocked[i]!==0&&unlocked[i]!==1)throw new Error('Invalid saved progress');wire[14+i]=unlocked[i];}}
    wire[1]=flags;return wire;
};
DEProfile.decode=function(wire){
    if(wire.length!==96||wire[0]!==1||(wire[1]&192))throw new Error('Different game version');
    for(var i=26;i<96;i++)if(wire[i]!==0)throw new Error('Different game version');
    var settings={},progress={},flags=wire[1];
    if(flags&1)settings.musicon=!!(flags&2);if(flags&4)settings.sfxon=!!(flags&8);
    if(flags&16){progress.custom_keys=[];for(var i=0;i<12;i++){var code=wire[2+i];if(code!==(code|0)||code<0||code>255)throw new Error('Invalid shared controls');progress.custom_keys.push(code);}}
    if(flags&32){progress.levels_unlocked=[];for(var i=0;i<12;i++){var value=wire[14+i];if(value!==0&&value!==1)throw new Error('Invalid shared progress');progress.levels_unlocked.push(value);}}
    return {settings:JSON.stringify(settings),progress:JSON.stringify(progress)};
};

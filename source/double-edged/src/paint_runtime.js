/* Development native-paint packet builder. Filters and non-normal blend groups
 * are counted as unqualified until the group compositor is connected. */
var PaintRuntime={stride:24,capacity:8192,count:0,textNodes:[],unqualifiedFilters:0,unqualifiedBlends:0,unqualifiedGraphics:0};
PaintRuntime.buffer=null;
PaintRuntime.data=null;
PaintRuntime.command=function(op){var p=this.count++;if(p>=this.capacity)throw new Error('Paint packet capacity exceeded');this.data[p*this.stride]=op;return p*this.stride;};
PaintRuntime.emit=function(o,parentColor,asMask){
    if(!o.visible&&!asMask)return;
    var c=o._color,color=[];
    for(var i=0;i<4;i++){color[i]=parentColor[i]*c[i]/256;color[i+4]=parentColor[i]*c[i+4]/256+parentColor[i+4];}
    if(color[3]<=0&&!asMask)return;
    if(o._filters||o.filters&&o.filters.length)++this.unqualifiedFilters;
    if(o._blend>1||o.blendMode!=='normal')++this.unqualifiedBlends;
    if(o.graphics&&o.graphics.records.length)++this.unqualifiedGraphics;
    if(o._children&&!(o._definition&&o._definition.kind===34)){
        var endDepths=[];
        for(var i=0;i<o._children.length;i++){
            var child=o._children[i],depth=child._timelineDepth;
            while(endDepths.length&&depth>endDepths[endDepths.length-1]){this.command(4);endDepths.pop();}
            if(child._clipDepth){this.command(2);this.emit(child,color,true);this.command(3);endDepths.push(child._clipDepth);}
            else this.emit(child,color,asMask);
        }
        while(endDepths.length){this.command(4);endDepths.pop();}
        return;
    }
    if(o._symbol<0)return;
    var at=this.command(1),data=this.data,m=o._globalMatrix();
    data[at+1]=o._symbol;data[at+2]=o._ratio||0;data[at+3]=-1;
    for(var i=0;i<6;i++)data[at+4+i]=m[i];
    for(var i=0;i<8;i++)data[at+10+i]=color[i];
    if(o instanceof flash.text.TextField){data[at+3]=this.textNodes.length;this.textNodes.push(o);}
};
PaintRuntime.build=function(){if(!this.buffer){this.buffer=new ArrayBuffer(this.stride*this.capacity*4);this.data=new Float32Array(this.buffer);}this.count=0;this.textNodes=[];this.unqualifiedFilters=this.unqualifiedBlends=this.unqualifiedGraphics=0;this.emit(FlashRuntime.root,[256,256,256,256,0,0,0,0],false);return this.count;};

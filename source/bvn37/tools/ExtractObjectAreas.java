// SPDX-License-Identifier: GPL-3.0-or-later
import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.exporters.commonshape.ExportRectangle;
import com.jpexs.decompiler.flash.exporters.commonshape.Matrix;
import com.jpexs.decompiler.flash.tags.base.BoundedTag;
import com.jpexs.decompiler.flash.timeline.Frame;
import com.jpexs.decompiler.flash.timeline.Timeline;
import com.jpexs.decompiler.flash.timeline.Timelined;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

/** Constructor-time BaseGameSprite local bounds; no visual-pose approximation. */
final class ExtractObjectAreas {
    static ExportRectangle bounds(Frame frame,Matrix transform,Set<Timeline> ancestors){
        ExportRectangle result=null;
        for(var layer:frame.layers.values()){
            var matrix=transform.concatenate(FlashLayers.matrix(layer));ExportRectangle next=null;
            if(layer.getCharacter() instanceof Timelined sprite){
                var timeline=sprite.getTimeline();int count=timeline.getFrameCount();
                if(count>0&&ancestors.add(timeline)){
                    next=bounds(timeline.getFrame(Math.floorMod(layer.time,count)),matrix,ancestors);ancestors.remove(timeline);
                }
            }else if(layer.getCharacter() instanceof BoundedTag)next=FlashLayers.bounds(layer,matrix);
            if(next!=null){
                if(result==null)result=next;
                else result=new ExportRectangle(Math.min(result.xMin,next.xMin),Math.min(result.yMin,next.yMin),
                    Math.max(result.xMax,next.xMax),Math.max(result.yMax,next.yMax));
            }
        }
        return result;
    }
    public static void main(String[] args)throws Exception{
        Locale.setDefault(Locale.ROOT);SWF swf=null;String source=null,bank=null;Timeline owner=null;
        try(var out=Files.newBufferedWriter(Path.of(args[1]))){
            for(String line:Files.readAllLines(Path.of(args[0]))){
                var fields=line.split("\t");
                if(fields[0].equals("bank")){
                    bank=fields[1];String path=fields[2];
                    if(!path.equals(source)){swf=FlashLayers.open(Path.of(path));source=path;}
                    if(fields[4].equals("1")){
                        var main=swf.getCharacterByClass("main_mc");owner=main instanceof Timelined clip?clip.getTimeline():swf.getTimeline();
                    }else{
                        var actor=swf.getCharacterByClass(fields[3]);
                        if(!(actor instanceof Timelined clip))throw new IllegalArgumentException("No owner class for "+bank+": "+fields[3]);
                        owner=clip.getTimeline();
                    }
                    FlashLayers.restorePlacementReferences(swf,owner);
                    continue;
                }
                int index=Integer.parseInt(fields[1]),frame=Integer.parseInt(fields[2]),initial=Integer.parseInt(fields[4]);
                Timeline object=owner;
                if(!fields[3].equals("--root")){
                    var placed=FlashLayers.named(owner.getFrame(frame-1),fields[3]);
                    if(placed==null||!(placed.getCharacter() instanceof Timelined clip))
                        throw new IllegalArgumentException("No source object "+bank+" "+frame+" "+fields[3]);
                    object=clip.getTimeline();
                    if(Math.floorMod(placed.time,object.getFrameCount())!=initial)
                        throw new IllegalArgumentException("Initial frame differs for "+bank+" "+fields[3]);
                }
                if(initial>=object.getFrameCount())throw new IllegalArgumentException("Source initial frame outside timeline");
                var seen=new HashSet<Timeline>();seen.add(object);var rect=bounds(object.getFrame(initial),new Matrix(),seen);
                if(rect==null)rect=new ExportRectangle(0,0,0,0);
                out.write(bank+"\t"+index+"\t"+rect.xMin/20+"\t"+rect.yMin/20+"\t"+(rect.xMax-rect.xMin)/20+"\t"+(rect.yMax-rect.yMin)/20+"\n");
            }
        }
    }
}

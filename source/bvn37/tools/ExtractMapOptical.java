// SPDX-License-Identifier: GPL-3.0-or-later
import com.jpexs.decompiler.flash.timeline.DepthState;
import com.jpexs.decompiler.flash.timeline.Timelined;
import com.jpexs.decompiler.flash.types.CXFORMWITHALPHA;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.Locale;

/** Static foreground children, source optical bounds and native-composite references. */
final class ExtractMapOptical {
    static CXFORMWITHALPHA alpha(DepthState child,int value){
        var original=child.colorTransForm;
        var transform=original==null?new CXFORMWITHALPHA():new CXFORMWITHALPHA(original);
        transform.hasMultTerms=true;
        transform.redMultTerm=original==null?256:original.getRedMulti();
        transform.greenMultTerm=original==null?256:original.getGreenMulti();
        transform.blueMultTerm=original==null?256:original.getBlueMulti();
        transform.alphaMultTerm=value;
        return transform;
    }
    public static void main(String[] args)throws Exception{
        Locale.setDefault(Locale.ROOT);
        var swf=FlashLayers.open(Path.of(args[0]));var output=Path.of(args[1]);
        Files.createDirectories(output);Files.createDirectories(output.resolve("children"));
        Files.createDirectories(output.resolve("references"));
        var root=swf.getTimeline();FlashLayers.restorePlacementReferences(swf,root);
        var frame=root.getFrame(0);var front=FlashLayers.named(frame,"front");
        if(front==null||!(front.getCharacter() instanceof Timelined sprite))throw new IllegalArgumentException("No foreground sprite");
        var timeline=sprite.getTimeline();var children=new ArrayList<>(timeline.getFrame(0).layers.values());
        children.sort((a,b)->Integer.compare(a.depth,b.depth));
        if(children.isEmpty()||children.size()>12)throw new IllegalArgumentException("Unexpected child count");
        for(var child:children){
            if(!(child.getCharacter() instanceof Timelined c)||c.getTimeline().getFrameCount()!=1)
                throw new IllegalArgumentException("Expected audited static MovieClip");
            if(child.colorTransForm!=null&&child.colorTransForm.getAlphaMulti()!=256)
                throw new IllegalArgumentException("Unexpected authored initial alpha");
        }
        var transform=FlashLayers.matrix(front);
        var rect=FlashLayers.pixelBounds(FlashLayers.bounds(front,transform));
        var bottom=FlashLayers.named(frame,"line_bottom");
        var offset=600-FlashLayers.matrix(bottom).translateY/20;
        for(var placed:frame.layers.values())placed.isVisible=placed==front;
        try(var data=Files.newBufferedWriter(output.resolve("optical.tsv"))){
            data.write("canvas\t"+rect.Xmin/20.0+"\t"+(rect.Ymin/20.0+offset)+"\t"+(rect.Xmax-rect.Xmin)/20+"\t"+(rect.Ymax-rect.Ymin)/20+"\n");
            for(int i=0;i<children.size();++i){
                var child=children.get(i);var bounds=FlashLayers.bounds(child,FlashLayers.matrix(child));
                data.write("child\t"+i+"\t"+child.depth+"\t"+child.characterId+"\t"+
                    (bounds.xMin/20+transform.translateX/20)+"\t"+(bounds.yMin/20+transform.translateY/20+offset)+"\t"+
                    (bounds.xMax-bounds.xMin)/20+"\t"+(bounds.yMax-bounds.yMin)/20+"\n");
                for(var other:children)other.isVisible=other==child;
                for(int a:new int[]{256,128}){
                    child.colorTransForm=alpha(child,a);swf.clearImageCache();
                    FlashLayers.save(output.resolve("children/"+i+"-"+a+".png"),FlashLayers.render(root,0,rect));
                }
                child.colorTransForm=alpha(child,256);
            }
            var masks=new LinkedHashSet<Integer>();int all=(1<<children.size())-1;
            if(args.length>2&&args[2].equals("--all-masks"))for(int mask=0;mask<=all;++mask)masks.add(mask);
            masks.add(0);masks.add(all);masks.add(all&0x555);masks.add(all&0xaaa);
            for(int i=0;i<children.size();++i)masks.add(1<<i);
            int random=0x50bface;
            for(int i=0;i<16;++i){random^=random<<13;random^=random>>>17;random^=random<<5;masks.add(random&all);}
            for(int mask:masks){
                for(int i=0;i<children.size();++i){var child=children.get(i);child.isVisible=true;child.colorTransForm=alpha(child,(mask&(1<<i))!=0?128:256);}
                swf.clearImageCache();FlashLayers.save(output.resolve("references/"+mask+".png"),FlashLayers.render(root,0,rect));
                data.write("reference\t"+mask+"\n");
            }
        }
        System.out.println("Exported "+children.size()+" optical children and source-composite references");
    }
}

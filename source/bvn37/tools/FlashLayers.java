import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.exporters.commonshape.ExportRectangle;
import com.jpexs.decompiler.flash.exporters.commonshape.Matrix;
import com.jpexs.decompiler.flash.tags.base.BoundedTag;
import com.jpexs.decompiler.flash.timeline.DepthState;
import com.jpexs.decompiler.flash.timeline.Frame;
import com.jpexs.decompiler.flash.timeline.Timeline;
import com.jpexs.decompiler.flash.timeline.Timelined;
import com.jpexs.decompiler.flash.types.RECT;

import java.awt.image.BufferedImage;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashSet;
import java.util.Set;
import javax.imageio.ImageIO;

final class FlashLayers {
    static final double TWIPS = 20;

    private FlashLayers() {}

    static SWF open(Path path) throws Exception {
        try (var stream = Files.newInputStream(path)) {
            return new SWF(stream, false);
        }
    }

    static DepthState named(Frame frame, String name) {
        for (DepthState layer : frame.layers.values()) {
            if (name.equals(layer.instanceName)) return layer;
        }
        return null;
    }

    static void restorePlacementReferences(SWF movie, Timeline timeline) {
        restorePlacementReferences(movie, timeline, new HashSet<>(), new HashSet<>());
    }

    private static void restorePlacementReferences(SWF movie, Timeline timeline, Set<Timeline> visited,
                                                   Set<Timeline> ancestors) {
        if (!visited.add(timeline)) return;
        ancestors.add(timeline);
        for (int index = 0; index < timeline.getFrameCount(); index++) {
            Frame previous = index > 0 ? timeline.getFrame(index - 1) : null;
            for (DepthState layer : timeline.getFrame(index).layers.values()) {
                // The timeline can drop a reference while parsing legacy maps, even when both SWF tags survived.
                if (layer.getCharacter() == null && layer.className == null && layer.placeObjectTag != null) {
                    int declared = layer.placeObjectTag.getCharacterId();
                    if (declared < 0 && previous != null && layer.placeObjectTag.flagMove()) {
                        DepthState prior = previous.layers.get(layer.depth);
                        if (prior != null) declared = prior.characterId;
                    }
                    if (declared >= 0 && movie.getCharacter(declared) != null) layer.characterId = declared;
                }
                if (layer.getCharacter() instanceof Timelined clip) {
                    Timeline child = clip.getTimeline();
                    if (ancestors.contains(child)) {
                        // An SWF sprite cannot instantiate an ancestor of its own display list.
                        layer.characterId = -1;
                        layer.className = null;
                        layer.isVisible = false;
                    } else {
                        restorePlacementReferences(movie, child, visited, ancestors);
                    }
                }
            }
        }
        ancestors.remove(timeline);
    }

    static Matrix matrix(DepthState layer) {
        return new Matrix(layer.getDrawingMatrix());
    }

    static ExportRectangle bounds(DepthState layer, Matrix transform) {
        if (!(layer.getCharacter() instanceof BoundedTag bounded)) {
            throw new IllegalArgumentException("Missing bounds for character " + layer.characterId);
        }
        return transform.transform(new ExportRectangle(bounded.getRect()));
    }

    static RECT pixelBounds(ExportRectangle bounds) {
        int left = 20 * (int) Math.floor(bounds.xMin / TWIPS);
        int right = 20 * (int) Math.ceil(bounds.xMax / TWIPS);
        int top = 20 * (int) Math.floor(bounds.yMin / TWIPS);
        int bottom = 20 * (int) Math.ceil(bounds.yMax / TWIPS);
        if (left >= right || top >= bottom) throw new IllegalArgumentException("Empty display bounds");
        return new RECT(left, right, top, bottom);
    }

    static BufferedImage render(Timeline timeline, int frame, RECT rect) {
        var canvas = SWF.frameToImageGet(timeline, frame, 0, null, 0, rect, new Matrix(), null,
            null, 1.0, true, 2).getBufferedImage();
        return canvas.getSubimage(0, 0, (rect.Xmax - rect.Xmin) / 20, (rect.Ymax - rect.Ymin) / 20);
    }

    static void save(Path target, BufferedImage image) throws IOException {
        if (!ImageIO.write(image, "PNG", target.toFile())) throw new IOException("PNG writer unavailable");
    }
}

import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.timeline.DepthState;
import com.jpexs.decompiler.flash.timeline.Frame;
import com.jpexs.decompiler.flash.timeline.Timelined;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Locale;
import java.util.Set;

/** Exports the map markers used by MapMain and the first pose of each display layer. */
final class ExtractStage {
    private static final int GAME_HEIGHT = 600;
    private static final Set<String> HIDDEN_LOGOS = Set.of("logo4399", "logo_mine");
    private static final String[] VISUAL_LAYERS = {"bg", "map", "front", "front_fix"};

    public static void main(String[] args) throws Exception {
        if (args.length != 2) throw new IllegalArgumentException("Usage: ExtractStage SOURCE.swf OUTPUT");
        Locale.setDefault(Locale.ROOT);
        extract(FlashLayers.open(Path.of(args[0])), Path.of(args[1]));
    }

    private static double x(DepthState layer) {
        return layer == null ? 0 : FlashLayers.matrix(layer).translateX / FlashLayers.TWIPS;
    }

    private static double y(DepthState layer) {
        return layer == null ? 0 : FlashLayers.matrix(layer).translateY / FlashLayers.TWIPS;
    }

    private static void extract(SWF movie, Path output) throws IOException {
        var timeline = movie.getTimeline();
        FlashLayers.restorePlacementReferences(movie, timeline);
        Frame first = timeline.getFrame(0);
        if (FlashLayers.named(first, "map") == null) throw new IllegalArgumentException("No map layer in the first frame");
        Files.createDirectories(output);

        double left = x(FlashLayers.named(first, "line_left"));
        double right = x(FlashLayers.named(first, "line_right"));
        double bottom = y(FlashLayers.named(first, "line_bottom"));
        double offset = GAME_HEIGHT - bottom;

        try (var data = Files.newBufferedWriter(output.resolve("stage.tsv"))) {
            data.write("bounds\t" + left + "\t" + right + "\t" + GAME_HEIGHT + "\t"
                + (y(FlashLayers.named(first, "line_player_bottom")) + offset) + "\n");
            for (String name : new String[]{"p1", "p2"}) {
                DepthState spawn = FlashLayers.named(first, name);
                if (spawn != null) data.write(name + "\t" + x(spawn) + "\t" + (y(spawn) + offset) + "\n");
            }

            DepthState floor = FlashLayers.named(first, "floor");
            if (floor != null && floor.getCharacter() instanceof Timelined sprite) {
                for (DepthState platform : sprite.getTimeline().getFrame(0).layers.values()) {
                    var shape = FlashLayers.bounds(platform, FlashLayers.matrix(platform));
                    double from = x(floor) + x(platform);
                    double to = from + (shape.xMax - shape.xMin) / FlashLayers.TWIPS;
                    data.write("floor\t" + from + "\t" + to + "\t" + (y(floor) + y(platform) + offset) + "\n");
                }
            }

            data.write("timeline\t" + timeline.getFrameCount() + "\n");
            for (String name : VISUAL_LAYERS) {
                DepthState layer = FlashLayers.named(first, name);
                if (layer == null) continue;
                int frames = hideDefaultLogos(layer);
                var bounds = FlashLayers.bounds(layer, FlashLayers.matrix(layer));
                var rect = FlashLayers.pixelBounds(bounds);
                if (name.equals("map")) data.write("camera\t" + (bounds.xMax - bounds.xMin) / FlashLayers.TWIPS + "\n");
                for (DepthState placed : first.layers.values()) placed.isVisible = placed == layer;
                var image = FlashLayers.render(timeline, 0, rect);
                FlashLayers.save(output.resolve(name + ".png"), image);
                double displayY = rect.Ymin / FlashLayers.TWIPS + (name.equals("bg") ? 0 : offset);
                data.write("layer\t" + name + "\t" + rect.Xmin / FlashLayers.TWIPS + "\t"
                    + displayY + "\t" + image.getWidth() + "\t" + image.getHeight() + "\t" + frames + "\n");
            }
        }
    }

    private static int hideDefaultLogos(DepthState layer) {
        if (!(layer.getCharacter() instanceof Timelined sprite)) return 1;
        var timeline = sprite.getTimeline();
        for (DepthState child : timeline.getFrame(0).layers.values()) {
            if (child.instanceName != null && HIDDEN_LOGOS.contains(child.instanceName)) {
                child.isVisible = false;
            }
        }
        return timeline.getFrameCount();
    }

}

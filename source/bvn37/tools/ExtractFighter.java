import com.jpexs.decompiler.flash.exporters.commonshape.ExportRectangle;
import com.jpexs.decompiler.flash.exporters.commonshape.Matrix;
import com.jpexs.decompiler.flash.tags.base.BoundedTag;
import com.jpexs.decompiler.flash.tags.base.ImageTag;
import com.jpexs.decompiler.flash.timeline.DepthState;
import com.jpexs.decompiler.flash.timeline.Frame;
import com.jpexs.decompiler.flash.timeline.Timeline;
import com.jpexs.decompiler.flash.timeline.Timelined;
import com.jpexs.decompiler.flash.types.MATRIX;
import com.jpexs.decompiler.flash.types.RECT;

import java.awt.geom.AffineTransform;
import java.io.BufferedWriter;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Exports authored frame labels, unscaled hit geometry and selected visual poses. */
final class ExtractFighter {
    private static final Set<String> HELD_STATES = Set.of("站立", "起跳", "跳", "跳中", "落", "击飞", "击飞_弹", "击飞_倒", "防御恢复");
    private static final Set<String> AI_RANGES = Set.of("kanmian", "kj1mian", "kj2mian", "tkanmian",
        "zh1mian", "zh2mian", "zh3mian", "tzmian", "bsmian", "sbsmian", "cbsmian");
    private record ChildRequest(String kind, int frame, String name) {}

    public static void main(String[] args) throws Exception {
        int options = 4;
        String sourceActor = null;
        if (args.length >= 6 && args[4].equals("--actor-class")) {
            sourceActor = args[5];
            options = 6;
        }
        boolean metadataOnly = args.length == options + 1 && args[options].equals("--metadata");
        boolean withObjects = args.length >= options + 2 && args[options].equals("--objects");
        int rangeStart = withObjects ? options + 2 : options;
        if (args.length < 4 || (!metadataOnly && (args.length - rangeStart) % 2 != 0)) {
            throw new IllegalArgumentException("Usage: ExtractFighter SOURCE.swf START_FRAME|--assist HIT_IDS OUTPUT "
                + "[--actor-class CLASS] [--metadata | [--objects REQUESTS] FIRST LAST ...]");
        }
        var swf = FlashLayers.open(Path.of(args[0]));
        boolean assist = args[1].equals("--assist");
        int startFrame = assist ? 0 : Integer.parseInt(args[1]);
        var hitIds = new HashSet<>(Files.readAllLines(Path.of(args[2])));
        var output = Path.of(args[3]);
        // The loader instantiates the exported main_mc class when the pack has one.
        var main = swf.getCharacterByClass("main_mc");
        var root = main instanceof Timelined clip ? clip.getTimeline() : swf.getTimeline();
        FlashLayers.restorePlacementReferences(swf, root);
        if ((!assist && startFrame < 1) || startFrame >= root.getFrameCount()) {
            throw new IllegalArgumentException("Variant is outside main_mc");
        }
        var variant = root.getFrame(startFrame);
        DepthState actor = assist ? null : FlashLayers.named(variant, "mc");
        if (sourceActor != null) {
            var source = swf.getCharacterByClass(sourceActor);
            if (actor == null || main == null || actor.getCharacter() != null || actor.placeObjectTag == null ||
                actor.placeObjectTag.getCharacterId() != main.getCharacterId() || !(source instanceof Timelined child)) {
                throw new IllegalArgumentException("The authored mc repair does not match this SWF");
            }
            // The wrapper's placement points back to itself. Keep its transform and restore the authored actor.
            actor.characterId = source.getCharacterId();
            actor.isVisible = true;
            FlashLayers.restorePlacementReferences(swf, child.getTimeline());
        }
        if (!assist && (actor == null || !(actor.getCharacter() instanceof Timelined))) {
            throw new IllegalArgumentException("Variant does not place its fighter in mc");
        }
        var timeline = assist ? root : ((Timelined) actor.getCharacter()).getTimeline();
        var placement = assist ? new Matrix() : FlashLayers.matrix(actor);
        var selected = new boolean[timeline.getFrameCount()];
        if (args.length == options) Arrays.fill(selected, true);
        for (int i = rangeStart; !metadataOnly && i < args.length; i += 2) {
            int first = Integer.parseInt(args[i]) - 1;
            int last = Integer.parseInt(args[i + 1]);
            if (first < 0 || first >= last || last > selected.length) {
                throw new IllegalArgumentException("Pose range is outside the timeline");
            }
            Arrays.fill(selected, first, last, true);
        }
        Files.createDirectories(metadataOnly ? output : output.resolve("poses"));
        if (!assist) for (var layer : variant.layers.values()) layer.isVisible = layer == actor;

        try (var data = Files.newBufferedWriter(output.resolve("fighter.tsv"))) {
            data.write("variant\t" + startFrame + "\t" + (assist ? 0 : actor.characterId)
                + "\t" + timeline.getFrameCount() + "\n");
            if (!assist) {
                for (String name : actor.getCharacter().getClassNames()) {
                    data.write("symbol\t" + startFrame + "\t" + name + "\n");
                }
                writeAiRanges(data, timeline, placement);
            }
            var detached = new HashMap<Long, Integer>();
            var probes = new HashSet<String>();
            var anchors = new HashSet<String>();
            AlternateLayer alternateLayer = null;
            StateLayers stateLayers = null;
            int screenCharacter = -1;
            var mirroredInstances = new HashMap<Long, Integer>();
            var childRequests = new HashMap<String, List<ChildRequest>>();
            if (withObjects) {
                var requests = Files.readAllLines(Path.of(args[options + 1]));
                for (String row : requests) {
                    String[] request = row.split("\t", 4);
                    if (request[0].startsWith("child")) {
                        childRequests.computeIfAbsent(request[2], key -> new ArrayList<>())
                            .add(new ChildRequest(request[0], Integer.parseInt(request[1]), request[3]));
                    }
                }
                for (String row : requests) {
                    String[] request = row.split("\t", 3);
                    String kind = request[0];
                    if (kind.startsWith("child")) continue;
                    if (kind.equals("probe")) {
                        probes.add(request[2]);
                        continue;
                    }
                    if (kind.equals("anchor")) {
                        anchors.add(request[2]);
                        continue;
                    }
                    if (kind.equals("flightLayer")) {
                        if (assist || alternateLayer != null) throw new IllegalArgumentException("Unexpected flight layer");
                        alternateLayer = new AlternateLayer("flightPose", "flight", request[2], 1, -1);
                        continue;
                    }
                    if (kind.equals("bankaiLayer")) {
                        if (assist || alternateLayer != null) throw new IllegalArgumentException("Unexpected Bankai layer");
                        int first = Integer.parseInt(request[1]);
                        var layer = FlashLayers.named(timeline.getFrame(first - 1), request[2]);
                        if (layer == null) throw new IllegalArgumentException("No Bankai layer on frame " + first);
                        alternateLayer = new AlternateLayer("bankaiPose", "bankai", request[2], first, layer.instanceId);
                        continue;
                    }
                    if (kind.equals("facingLayer")) {
                        if (assist) throw new IllegalArgumentException("Unexpected fighter facing layer");
                        int first = Integer.parseInt(request[1]);
                        var layer = FlashLayers.named(timeline.getFrame(first - 1), request[2]);
                        if (layer == null || mirroredInstances.putIfAbsent(layer.instanceId, first) != null) {
                            throw new IllegalArgumentException("No distinct facing layer on frame " + first);
                        }
                        continue;
                    }
                    if (kind.equals("screenLayer")) {
                        if (assist || screenCharacter != -1) throw new IllegalArgumentException("Unexpected screen layer");
                        screenCharacter = Integer.parseInt(request[2]);
                        continue;
                    }
                    if (kind.equals("waterLayer") || kind.equals("twinLionLayer")) {
                        if (assist || stateLayers != null) throw new IllegalArgumentException("Unexpected state layers");
                        var range = request[1].split(":", 2);
                        var names = request[2].split("\\|", -1);
                        String pose = kind.equals("waterLayer") ? "waterPose" : "twinLionPose";
                        stateLayers = new StateLayers(pose, Integer.parseInt(range[0]), Integer.parseInt(range[1]),
                            Set.of(names[0].split(",")), names[1].isEmpty() ? Set.of() : Set.of(names[1].split(",")));
                        continue;
                    }
                    if (kind.equals("face")) {
                        String name = request[2];
                        if (!(swf.getCharacterByClass(name) instanceof ImageTag bitmap)) {
                            throw new IllegalArgumentException("Missing fighter portrait class " + name);
                        }
                        var image = bitmap.getImageCached().getBufferedImage();
                        FlashLayers.save(output.resolve("poses/face-" + name + ".png"), image);
                        data.write("face\t0\t" + name + "\t0\t0\t" + image.getWidth() + "\t" + image.getHeight() + "\n");
                        continue;
                    }
                    int frame = Integer.parseInt(request[1]);
                    var parentFrame = timeline.getFrame(frame - 1);
                    var object = FlashLayers.named(parentFrame, request[2]);
                    if (assist && object == null) {
                        for (int retry = frame; retry < timeline.getFrameCount(); retry++) {
                            if (FlashLayers.named(timeline.getFrame(retry), request[2]) != null) {
                                throw new IllegalArgumentException("Deferred " + kind + " " + request[2]
                                    + " first appears on frame " + (retry + 1));
                            }
                        }
                        data.write("missingObject\t" + frame + "\t" + kind + "\t" + request[2] + "\n");
                        continue;
                    }
                    if (object == null || !(object.getCharacter() instanceof Timelined sprite)) {
                        throw new IllegalArgumentException("No " + kind + " " + request[2] + " on frame " + frame);
                    }
                    detached.merge(object.instanceId, frame, Math::min);
                    writeDetached(data, output, root, assist ? frame - 1 : startFrame, actor, parentFrame, object, sprite.getTimeline(),
                        placement, kind, frame + "\t" + request[2], hitIds, childRequests);
                }
            }
            if (alternateLayer != null && screenCharacter != -1) {
                throw new IllegalArgumentException("An alternate pose needs its own screen-layer order");
            }
            if (stateLayers != null && (alternateLayer != null || screenCharacter != -1)) {
                throw new IllegalArgumentException("State layers need their own display order");
            }
            if (!mirroredInstances.isEmpty() && (stateLayers != null || alternateLayer != null || screenCharacter != -1)) {
                throw new IllegalArgumentException("Facing layers need their own display order");
            }
            for (int index = 0; index < timeline.getFrameCount(); index++) {
                var frame = timeline.getFrame(index);
                for (String label : frame.labels) data.write("label\t" + (index + 1) + "\t" + label + "\n");
                ExportRectangle visual = null;
                ExportRectangle display = null;
                ExportRectangle alternateBounds = null;
                DepthState alternate = null;
                DepthState screen = null;
                ExportRectangle screenBounds = null;
                var toggled = new ArrayList<DepthState>();
                int highestVisualDepth = -1;
                for (DepthState layer : frame.layers.values()) {
                    String name = layer.instanceName;
                    if (anchors.contains(name)) {
                        var transform = FlashLayers.matrix(layer);
                        data.write("anchor\t" + (index + 1) + "\t" + name + "\t" +
                            transform.translateX / 20.0 + "\t" + transform.translateY / 20.0 + "\n");
                    }
                    if ("AImain".equals(name)) {
                        layer.isVisible = false;
                        continue;
                    }
                    if (detached.getOrDefault(layer.instanceId, Integer.MAX_VALUE) <= index + 1) {
                        layer.isVisible = false;
                        continue;
                    }
                    if (!assist && selected[index]) {
                        var bounds = layerBounds(layer, placement, false);
                        if (bounds != null && bounds.xMax > bounds.xMin && bounds.yMax > bounds.yMin) {
                            display = union(display, bounds);
                        }
                    }
                    String hit = hitId(name, hitIds);
                    boolean body = "bdmn".equals(name);
                    boolean probe = probes.contains(name);
                    boolean marker = body || probe || hit != null || (name != null && name.contains("atm"));
                    if (body || hit != null) {
                        var rect = FlashLayers.bounds(layer, placement.concatenate(FlashLayers.matrix(layer)));
                        writeBox(data, body ? "body" : "hit", index + 1, body ? name : hit, rect);
                    }
                    if (probe) {
                        var rect = FlashLayers.bounds(layer, placement.concatenate(FlashLayers.matrix(layer)));
                        writeBox(data, "probe", index + 1, name, rect);
                    }
                    if (marker) {
                        layer.isVisible = false;
                        continue;
                    }
                    if (stateLayers != null && selected[index] && stateLayers.matches(name, index + 1)) {
                        toggled.add(layer);
                        stateLayers.show(layer, false);
                    }
                    if (selected[index] && layer.isVisible && layer.getCharacter() instanceof BoundedTag) {
                        var rect = FlashLayers.bounds(layer, placement.concatenate(FlashLayers.matrix(layer)));
                        if (rect.xMax <= rect.xMin || rect.yMax <= rect.yMin) continue;
                        if (alternateLayer != null && alternateLayer.matches(layer, index + 1)) {
                            alternate = layer;
                            alternateBounds = rect;
                        } else if (screenCharacter != -1 && layer.characterId == screenCharacter) {
                            if (screen != null || layer.blendMode != 4) {
                                throw new IllegalArgumentException("Expected one SWF screen layer on frame " + (index + 1));
                            }
                            screen = layer;
                            screenBounds = rect;
                        } else {
                            highestVisualDepth = Math.max(highestVisualDepth, layer.depth);
                            visual = union(visual, rect);
                        }
                    }
                }
                if (display != null) writeBox(data, "display", index + 1, "", display);
                if (!toggled.isEmpty()) {
                    actor.time = index;
                    writeStatePose(data, output, root, startFrame, frame, placement, index + 1, -1, stateLayers, toggled);
                }
                if (alternate != null) {
                    if (frame.labels.stream().anyMatch(HELD_STATES::contains)) {
                        throw new IllegalArgumentException("Alternate pose needs a held pose cycle");
                    }
                    actor.time = index;
                    var bounds = visual == null ? alternateBounds : union(alternateBounds, visual);
                    writePose(data, output, root, startFrame, FlashLayers.pixelBounds(bounds),
                        alternateLayer.kind + "\t" + (index + 1), alternateLayer.prefix + "-" + (index + 1));
                    alternate.isVisible = false;
                }
                if (screen != null) {
                    if (screen.depth < highestVisualDepth || frame.labels.stream().anyMatch(HELD_STATES::contains)) {
                        throw new IllegalArgumentException("Screen layer needs its own display order on frame " + (index + 1));
                    }
                    actor.time = index;
                    writeScreenPose(data, output, root, startFrame, frame, index + 1, screen, screenBounds);
                    screen.isVisible = false;
                }
                if (visual == null) {
                    if (selected[index]) data.write("blank\t" + (index + 1) + "\n");
                    if (alternate != null) alternate.isVisible = true;
                    continue;
                }
                var rect = FlashLayers.pixelBounds(visual);
                if (actor != null) actor.time = index;
                writePose(data, output, root, assist ? index : startFrame, rect, "pose\t" + (index + 1), String.valueOf(index + 1));
                if (alternate != null) alternate.isVisible = true;
                if (!mirroredInstances.isEmpty()) {
                    writeFacingPose(data, output, root, startFrame, frame, placement, index + 1, mirroredInstances);
                }
                if (!assist && frame.labels.stream().anyMatch(HELD_STATES::contains)) {
                    writeHeldCycle(data, output, root, startFrame, frame, index + 1, rect, placement, stateLayers, toggled);
                }
            }
        }
    }

    private record AlternateLayer(String kind, String prefix, String name, int first, long instance) {
        boolean matches(DepthState layer, int frame) {
            return frame >= first && name.equals(layer.instanceName) && (instance < 0 || layer.instanceId == instance);
        }
    }

    private record StateLayers(String kind, int first, int last, Set<String> enabled, Set<String> disabled) {
        boolean matches(String name, int frame) {
            return name != null && frame >= first && frame <= last && (enabled.contains(name) || disabled.contains(name));
        }

        void show(DepthState layer, boolean alternate) {
            layer.isVisible = enabled.contains(layer.instanceName) == alternate;
        }
    }

    private static void writeStatePose(BufferedWriter data, Path output, Timeline root, int startFrame,
                                       Frame frame, Matrix placement, int number, int phase, StateLayers state,
                                       List<DepthState> toggled) throws IOException {
        toggled.forEach(layer -> state.show(layer, true));
        try {
            var bounds = frameBounds(frame, placement, true);
            if (bounds == null) throw new IllegalArgumentException("Empty " + state.kind + " at frame " + number);
            writePose(data, output, root, startFrame, FlashLayers.pixelBounds(bounds),
                state.kind + "\t" + number + "\t" + phase, state.kind + "-" + number + "-" + phase);
        } finally {
            toggled.forEach(layer -> state.show(layer, false));
        }
    }

    private static void writeDetached(BufferedWriter data, Path output, Timeline root, int startFrame,
                                      DepthState actor, Frame frame, DepthState object, Timeline sprite,
                                      Matrix placement, String kind, String id, HashSet<String> hitIds,
                                      Map<String, List<ChildRequest>> childRequests) throws IOException {
        boolean bullet = kind.equals("projectile") || kind.equals("nestedProjectile");
        if (!bullet && !kind.equals("attacker")) throw new IllegalArgumentException("Unknown object kind " + kind);
        var child = FlashLayers.matrix(object);
        double x = child.translateX;
        double y = child.translateY + placement.translateY;
        int count = sprite.getFrameCount();
        if (count == 0) throw new IllegalArgumentException("Empty detached timeline " + id);
        int originalTime = object.time;
        int initial = Math.floorMod(originalTime, count);
        double unit = FlashLayers.TWIPS;
        data.write(kind + "\t" + id + "\t" + count + "\t" + initial + "\t" + x / unit + "\t" + y / unit
            + "\t" + placement.translateX / unit);
        if (bullet) {
            // Bullet stores its initial bounds in the owner's local coordinates before reparenting.
            var transform = placement.concatenate(child);
            var main = FlashLayers.named(sprite.getFrame(initial), "main");
            var hit = main == null ? frameBounds(sprite.getFrame(initial), transform, false)
                : layerBounds(main, transform, false);
            if (hit == null || hit.xMax <= hit.xMin || hit.yMax <= hit.yMin) {
                throw new IllegalArgumentException("Projectile has no initial collision area: " + id);
            }
            double originX = x + placement.translateX;
            data.write("\t" + (hit.xMin - originX) / unit + "\t" + (hit.yMin - y) / unit + "\t"
                + (hit.xMax - originX) / unit + "\t" + (hit.yMax - y) / unit);
        }
        data.write("\n");
        if (!bullet) {
            for (String symbol : object.getCharacter().getClassNames()) data.write(kind + "Symbol\t" + id + "\t" + symbol + "\n");
        }

        var actorMatrix = actor == null ? null : actor.temporaryMatrix;
        var objectMatrix = object.temporaryMatrix;
        if (actor != null) actor.temporaryMatrix = new Matrix().toMATRIX();
        var transform = detachedTransform(child, bullet);
        object.temporaryMatrix = transform.toMATRIX();
        var layers = frame.layers.values().toArray(DepthState[]::new);
        var visibility = new boolean[layers.length];
        for (int i = 0; i < layers.length; i++) {
            visibility[i] = layers[i].isVisible;
            layers[i].isVisible = layers[i] == object;
        }
        if (actor != null) actor.time = frame.frame;
        var launchedChildren = new HashMap<Long, Integer>();
        var probeNames = new HashSet<String>();
        var anchorNames = new HashSet<String>();
        if (!bullet) {
            for (String symbol : object.getCharacter().getClassNames().stream().sorted().toList()) {
                for (ChildRequest launch : childRequests.getOrDefault(symbol, List.of())) {
                    if (launch.kind.equals("childProbe")) {
                        probeNames.add(launch.name);
                        continue;
                    }
                    if (launch.kind.equals("childAnchor")) {
                        anchorNames.add(launch.name);
                        continue;
                    }
                    Frame source = sprite.getFrame(launch.frame - 1);
                    DepthState shot = FlashLayers.named(source, launch.name);
                    if (shot == null || !(shot.getCharacter() instanceof Timelined shotSprite)) {
                        throw new IllegalArgumentException("No child projectile " + launch.name + " on " + symbol
                            + " frame " + launch.frame);
                    }
                    launchedChildren.merge(shot.instanceId, launch.frame - 1, Math::min);
                    // The attacker already occupies its world origin. Unlike a fighter, it has no wrapper offset.
                    writeDetached(data, output, root, startFrame, object, source, shot, shotSprite.getTimeline(),
                        new Matrix(), "nestedProjectile", id + "\t" + launch.frame + "\t" + launch.name,
                        hitIds, Map.of());
                }
            }
        }
        var hitCache = new HashMap<Long, ExportRectangle>();
        for (int phase = 0; phase < count; phase++) {
            Frame subframe = sprite.getFrame(phase);
            for (String label : subframe.labels) data.write(kind + "Label\t" + id + "\t" + phase + "\t" + label + "\n");
            var hidden = new ArrayList<DepthState>();
            if (!bullet) {
                for (DepthState layer : subframe.layers.values()) {
                    if (anchorNames.contains(layer.instanceName)) {
                        var anchor = FlashLayers.matrix(layer);
                        data.write(kind + "Anchor\t" + id + "\t" + phase + "\t" + layer.instanceName
                            + "\t" + anchor.translateX / unit + "\t" + anchor.translateY / unit + "\n");
                    }
                    if (launchedChildren.getOrDefault(layer.instanceId, Integer.MAX_VALUE) <= phase) {
                        if (layer.isVisible) {
                            layer.isVisible = false;
                            hidden.add(layer);
                        }
                        continue;
                    }
                    String hit = hitId(layer.instanceName, hitIds);
                    if (hit != null) {
                        var box = hitCache.computeIfAbsent(layer.instanceId, key -> layerBounds(layer, new Matrix(), false));
                        if (box != null) writeBox(data, kind + "Hit", frame.frame + 1,
                            object.instanceName + "\t" + phase + "\t" + hit, box);
                    }
                    boolean probe = probeNames.contains(layer.instanceName);
                    if (probe) {
                        var box = layerBounds(layer, new Matrix(), false);
                        if (box != null) writeBox(data, "attackerProbe", frame.frame + 1,
                            object.instanceName + "\t" + phase + "\t" + layer.instanceName, box);
                    }
                    if (layer.isVisible && layer.instanceName != null && (hit != null || probe || layer.instanceName.contains("atm"))) {
                        layer.isVisible = false;
                        hidden.add(layer);
                    }
                }
            }
            var bounds = frameBounds(subframe, transform, true);
            if (bounds == null || bounds.xMax <= bounds.xMin || bounds.yMax <= bounds.yMin) {
                data.write(kind + "Blank\t" + id + "\t" + phase + "\n");
            } else {
                var rect = FlashLayers.pixelBounds(bounds);
                object.time = phase;
                var image = FlashLayers.render(root, startFrame, rect);
                String filename = kind + "-" + id.replace('\t', '-') + "-" + phase;
                FlashLayers.save(output.resolve("poses/" + filename + ".png"), image);
                data.write(kind + "Pose\t" + id + "\t" + phase + "\t" + filename + "\t" + rect.Xmin / unit
                    + "\t" + rect.Ymin / unit + "\t" + image.getWidth() + "\t" + image.getHeight() + "\n");
            }
            for (DepthState layer : hidden) layer.isVisible = true;
        }
        object.time = originalTime;
        if (actor != null) actor.temporaryMatrix = actorMatrix;
        object.temporaryMatrix = objectMatrix;
        for (int i = 0; i < layers.length; i++) layers[i].isVisible = visibility[i];
    }

    private static Matrix detachedTransform(Matrix placement, boolean bullet) {
        var m = placement.toTransform();
        double xScale = Math.hypot(m.getScaleX(), m.getShearY());
        double scale = bullet || xScale == 0 ? 1 : xScale;
        m.setTransform(m.getScaleX() / scale, m.getShearY() / scale, m.getShearX(), m.getScaleY(), 0, 0);
        if (bullet) {
            double angle = Math.atan2(m.getShearY(), m.getScaleX());
            // Bullet keeps its original scale but stores rotation in an AS3 int.
            m.preConcatenate(AffineTransform.getRotateInstance(Math.toRadians((int) Math.toDegrees(angle)) - angle));
        }
        return new Matrix(m);
    }

    private static ExportRectangle frameBounds(Frame frame, Matrix placement, boolean visibleOnly) {
        ExportRectangle bounds = null;
        for (DepthState layer : frame.layers.values()) {
            if (visibleOnly && !layer.isVisible) continue;
            var next = layerBounds(layer, placement, visibleOnly);
            if (next != null) bounds = union(bounds, next);
        }
        return bounds;
    }

    private static void writeAiRanges(BufferedWriter data, Timeline timeline, Matrix placement) throws IOException {
        for (int i = 0; i < timeline.getFrameCount(); i++) {
            var instance = FlashLayers.named(timeline.getFrame(i), "AImain");
            if (instance == null || !(instance.getCharacter() instanceof Timelined clip)) continue;
            var ranges = clip.getTimeline();
            if (ranges.getFrameCount() < 2) continue;
            var transform = placement.concatenate(FlashLayers.matrix(instance));
            for (var layer : ranges.getFrame(1).layers.values()) {
                if (layer.instanceName == null || !AI_RANGES.contains(layer.instanceName)) continue;
                var bounds = layerBounds(layer, transform, false);
                if (bounds != null) writeBox(data, "range", 2, layer.instanceName, bounds);
            }
            return;
        }
    }

    private static ExportRectangle layerBounds(DepthState layer, Matrix placement, boolean visibleOnly) {
        var transform = placement.concatenate(FlashLayers.matrix(layer));
        if (layer.getCharacter() instanceof Timelined sprite) {
            Timeline timeline = sprite.getTimeline();
            int count = timeline.getFrameCount();
            return count > 0 ? frameBounds(timeline.getFrame(Math.floorMod(layer.time, count)), transform, visibleOnly) : null;
        }
        return layer.getCharacter() instanceof BoundedTag ? FlashLayers.bounds(layer, transform) : null;
    }

    private static void writeHeldCycle(BufferedWriter data, Path output, Timeline root, int startFrame,
                                       Frame frame, int number, RECT rect, Matrix placement, StateLayers state,
                                       List<DepthState> toggled) throws IOException {
        var children = frame.layers.values().stream().filter(layer -> (layer.isVisible || toggled.contains(layer))
            && layer.getCharacter() instanceof Timelined child && child.getTimeline().getFrameCount() > 1).toList();
        if (children.isEmpty()) return;
        var timelines = children.stream().map(layer -> ((Timelined) layer.getCharacter()).getTimeline()).toList();
        int[] original = children.stream().mapToInt(layer -> layer.time).toArray();
        int[] phases = new int[children.size()];
        var seen = new HashMap<String, Integer>();
        // FighterMC.goFrame advances the child timelines once, then renderAnimate advances them at 30 Hz.
        for (int step = 0; ; step++) {
            for (int i = 0; i < children.size(); i++) {
                Timeline child = timelines.get(i);
                if (!child.getFrame(phases[i]).labels.contains("stop")) {
                    phases[i] = (phases[i] + 1) % child.getFrameCount();
                }
                children.get(i).time = phases[i];
            }
            Integer repeat = seen.putIfAbsent(Arrays.toString(phases), step);
            if (repeat != null) {
                data.write("cycle\t" + number + "\t" + step + "\t" + repeat + "\n");
                break;
            }
            if (step == 4096) throw new IllegalArgumentException("Locomotion child cycle is too large");
            var currentBounds = toggled.isEmpty() ? rect : FlashLayers.pixelBounds(frameBounds(frame, placement, true));
            writePose(data, output, root, startFrame, currentBounds, "subpose\t" + number + "\t" + step, number + "-" + step);
            if (!toggled.isEmpty()) {
                writeStatePose(data, output, root, startFrame, frame, placement, number, step, state, toggled);
            }
        }
        for (int i = 0; i < children.size(); i++) children.get(i).time = original[i];
    }

    private static void writePose(BufferedWriter data, Path output, Timeline root, int startFrame,
                                  RECT rect, String key, String file) throws IOException {
        var image = FlashLayers.render(root, startFrame, rect);
        FlashLayers.save(output.resolve("poses/" + file + ".png"), image);
        data.write(key + "\t" + rect.Xmin / FlashLayers.TWIPS + "\t" + rect.Ymin / FlashLayers.TWIPS + "\t"
            + image.getWidth() + "\t" + image.getHeight() + "\n");
    }

    private static void writeFacingPose(BufferedWriter data, Path output, Timeline root, int startFrame,
                                        Frame frame, Matrix placement, int number,
                                        Map<Long, Integer> firstFrames) throws IOException {
        var layers = frame.layers.values().stream().filter(layer -> layer.isVisible
            && firstFrames.getOrDefault(layer.instanceId, Integer.MAX_VALUE) <= number).toList();
        if (layers.isEmpty()) return;
        var previous = new HashMap<DepthState, MATRIX>();
        try {
            for (var layer : layers) {
                var authored = layer.getDrawingMatrix();
                var local = new Matrix(authored);
                if (local.rotateSkew0 != 0 || local.rotateSkew1 != 0 || local.scaleX <= 0) {
                    throw new IllegalArgumentException("Expected an upright facing layer on frame " + number);
                }
                previous.put(layer, layer.temporaryMatrix);
                var flipped = authored == null ? new MATRIX() : new MATRIX(authored);
                flipped.hasScale = true;
                flipped.scaleX = -1;
                flipped.scaleY = (float)local.scaleY;
                layer.temporaryMatrix = flipped;
            }
            var bounds = frameBounds(frame, placement, true);
            writePose(data, output, root, startFrame, FlashLayers.pixelBounds(bounds),
                "facingPose\t" + number, "facing-" + number);
        } finally {
            previous.forEach((layer, matrix) -> layer.temporaryMatrix = matrix);
        }
    }

    private static void writeScreenPose(BufferedWriter data, Path output, Timeline root, int startFrame,
                                        Frame frame, int number, DepthState screen, ExportRectangle bounds) throws IOException {
        var otherLayers = frame.layers.values().stream().filter(layer -> layer != screen && layer.isVisible).toList();
        otherLayers.forEach(layer -> layer.isVisible = false);
        int blend = screen.blendMode;
        screen.blendMode = 0;
        try {
            writePose(data, output, root, startFrame, FlashLayers.pixelBounds(bounds),
                "screenPose\t" + number, "screen-" + number);
        } finally {
            screen.blendMode = blend;
            otherLayers.forEach(layer -> layer.isVisible = true);
        }
    }

    private static String hitId(String name, HashSet<String> hitIds) {
        if (name == null) return null;
        if (hitIds.contains(name)) return name;
        int index = name.indexOf("atm");
        if (index == -1) return null;
        String bare = name.substring(0, index) + name.substring(index + 3);
        return hitIds.contains(bare) ? bare : null;
    }

    private static void writeBox(BufferedWriter out, String kind, int frame, String name, ExportRectangle rect)
        throws IOException {
        out.write(kind + "\t" + frame + "\t" + name + "\t" + rect.xMin / FlashLayers.TWIPS + "\t"
            + rect.yMin / FlashLayers.TWIPS + "\t" + rect.xMax / FlashLayers.TWIPS + "\t"
            + rect.yMax / FlashLayers.TWIPS + "\n");
    }

    private static ExportRectangle union(ExportRectangle a, ExportRectangle b) {
        if (a == null) return b;
        a.xMin = Math.min(a.xMin, b.xMin);
        a.yMin = Math.min(a.yMin, b.yMin);
        a.xMax = Math.max(a.xMax, b.xMax);
        a.yMax = Math.max(a.yMax, b.yMax);
        return a;
    }
}

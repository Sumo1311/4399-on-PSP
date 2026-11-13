#!/bin/sh
# Build the PSP versus preview and its source-derived assets.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
: "${PSPDEV:?Set PSPDEV to the PSP toolchain directory}"
: "${JPEXS_DIR:?Set JPEXS_DIR to the directory containing ffdec.jar and lib/}"
: "${BVN_UI_FONT:?Set BVN_UI_FONT to a CJK font usable by Pillow}"
export PATH="$PSPDEV/bin:$PATH"
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1793232000}"
cmake=${CMAKE:-cmake}
java=${BVN_JAVA:-java}
javac=${BVN_JAVAC:-javac}

out=${1:-"$root/work/versus-build"}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
mkdir -p "$out/classes"
package="$out/psp/versus"
cd "$root"

"$cmake" -S . -B "$out/psp" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$PSPDEV/psp/share/pspdev.cmake"
"$cmake" --build "$out/psp" --target bvn_versus --parallel "${BUILD_JOBS:-4}"

classpath="$out/classes:$JPEXS_DIR/ffdec.jar:$JPEXS_DIR/lib/*"
"$javac" -cp "$classpath" -d "$out/classes" tools/FlashLayers.java tools/ExtractStage.java tools/ExtractFighter.java
python3 tools/pack_versus_ui.py "$package" --font "$BVN_UI_FONT"
python3 tools/pack_versus_audio.py "$package/AUDIO.BVA" --jpexs "$JPEXS_DIR" --java "$java"
python3 tools/pack_counter_effects.py reference "$package/REACTION.BVF" "$out/counter-effects"
while IFS= read -r code; do
    [ "$code" != RANDOM ] || continue
    map=$(printf '%s' "$code" | tr '[:upper:]' '[:lower:]')
    art="$out/maps/$map"
    src="reference/tag-assets/shared/assets/assets/packs/maps/$map/$map.swf"
    mkdir -p "$art"
    "$java" -Xmx1g -cp "$classpath" ExtractStage "$src" "$art" > "$art/extract.log" 2>&1
    python3 tools/pack_stage.py "$art" "$package/$code.BVN"
done < "$package/STAGES.TXT"

while read -r variant file family internal_frame; do
    set --
    if [ "$variant" != "$family" ] && [ -d "$out/actors/$family/scripts" ]; then
        set -- --decompiled "$out/actors/$family/scripts"
    fi
    source=$variant
    if [ -n "$internal_frame" ]; then
        source=$family
        set -- "$@" --internal-frame "$internal_frame"
    fi
    python3 tools/build_actions.py "$source" "$out/$file.BVF" --jpexs "$JPEXS_DIR" --classes "$out/classes" \
        --java "$java" --scratch "$out/actors" --all-actions "$@" > "$out/$variant.log" 2>&1
    cp "$out/$file.BVF" "$package/"
done < tools/versus_fighters.txt

while read -r name file _; do
    python3 tools/build_assist.py "$name" "$out/$file.BVS" --jpexs "$JPEXS_DIR" --classes "$out/classes" \
        --java "$java" --scratch "$out/assisters" > "$out/assist-$name.log" 2>&1
    cp "$out/$file.BVS" "$package/"
done < tools/versus_assists.txt
cp reference/upstream/LICENSE "$package/LICENSE.txt"
printf 'PSP versus preview: %s\n' "$package"

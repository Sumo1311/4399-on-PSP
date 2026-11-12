# Replacing the LGPL thread library

> Release availability: All GitHub Releases and their attached game/relinking archives were removed on 2026-11-12. The instructions below describe the previously distributed materials and remain applicable to existing local copies.

`Relink-Materials-2026.11.11.zip`, previously attached to a GitHub Release, contained the exact application object files used by the delivered runtimes, their link configurations, EBOOT templates, the complete pthread-embedded corresponding source archive and a copy of `tools/relink.py`. Game asset packs were obtained from the normal installation ZIPs. These materials were distributed alongside the binaries rather than on request.

The thread library's original PSP source is pinned at [97fe4ce006b420894f2bcaeb530d1f1f53111fc2](https://github.com/pspdev/pthread-embedded/tree/97fe4ce006b420894f2bcaeb530d1f1f53111fc2). Its LGPL and other original notices are retained. The application objects preserve relocations, and the included Box2D static archive is independently relinkable.

Install PSPDEV with Allegrex GCC 15.2.0 and PSPSDK. The release was linked with the toolchain identified in `toolchain-builds/` in the archive. A different compatible SDK or a modified thread library may intentionally produce different runtime bytes.

Extract `pthread-embedded-source.tar.gz` from the relinking archive. Build a modified copy in that extracted source directory, using the upstream PSP Makefile. This does not install it into your SDK.

```sh
export PSPDEV=/path/to/pspdev
export PATH="$PSPDEV/bin:$PATH"
make -C /path/to/extracted-pthread-source/platform/psp
```

Relink the desired game, using the modified library directory before the SDK's library search paths.

```sh
python3 tools/relink.py /path/to/Relink-Materials ElfStory_PSP \
  --sdk "$PSPDEV" --pthread-lib-dir /path/to/extracted-pthread-source/platform/psp \
  --output /path/to/new/EBOOT.PBP
```

The game identifiers are `ElfStory_PSP`, `ForestTemple_PSP`, `Flipside_PSP` and `Badminton2_PSP`. To compare against the delivered runtime with an unmodified compatible toolchain, omit `--pthread-lib-dir` and add `--check-identical`.

The release check with the recorded verification SDK successfully relinked all four applications. Flipside and Badminton matched their released runtime bytes exactly. Elf Story and Forest Temple were originally linked with an older SDK installation which is no longer present; the available SDK produces different runtime bytes. Their original object files are included, and their delivered EBOOTs are preserved unchanged. The toolchain-build records identify the SDK used for this relinking verification. They do not assert that every SDK library binary used by the earlier deliveries was identical.

The tool writes a new EBOOT, preserves its SFO, icon and background sections, and replaces only `DATA.PSP`. Copy it into a separate copy of the corresponding complete installed game directory to use the original external data files. The packaging and relinking tools impose no prohibition on modifying the LGPL library or reverse engineering to debug such modifications.

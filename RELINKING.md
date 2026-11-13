# Replacing the LGPL thread library

`Relink-Materials-2026.11.13.zip`, attached to release `v2026.11.13`, contains the application object files for the four current official programs, link configurations, EBOOT templates, the complete pthread-embedded corresponding source archive, modified application files and `relink.py`. Obtain external game assets from the installation ZIPs attached to the same release.

Extract the archive to obtain the `Performance-Build-Materials` directory. `toolchain.json` records the verification toolchain and static-library identities. All four complete EBOOT.PBP files were reproduced byte for byte from the retained complete local source after applying the retained SFO and XMB resources.

The thread library's original PSP source is pinned at [97fe4ce006b420894f2bcaeb530d1f1f53111fc2](https://github.com/pspdev/pthread-embedded/tree/97fe4ce006b420894f2bcaeb530d1f1f53111fc2). Its LGPL and other notices are retained. Application objects preserve relocations; the Flipside Box2D archive is independently relinkable.

Use a compatible PSPDEV installation with Allegrex GCC 15.2.0 and PSPSDK. Extract `pthread-embedded-source.tar.gz` and build a modified library in the extracted source directory:

```sh
export PSPDEV=/path/to/pspdev
export PATH="$PSPDEV/bin:$PATH"
make -C /path/to/extracted-pthread-source/platform/psp
```

Relink a game with the replacement thread library:

```sh
python3 /path/to/Performance-Build-Materials/relink.py \
  /path/to/Performance-Build-Materials ElfStory_PSP \
  --sdk "$PSPDEV" --pthread-lib-dir /path/to/modified/library \
  --output /path/to/new/EBOOT.PBP
```

Game identifiers are `ElfStory_PSP`, `ForestTemple_PSP`, `Flipside_PSP` and `Badminton2_PSP`. To check the original program with the verification SDK, omit `--pthread-lib-dir` and add `--check-identical`. A different compatible SDK or modified library can intentionally produce different bytes.

The tool preserves the SFO, icon and background sections and replaces DATA.PSP. Place the new EBOOT in a separate copy of the corresponding complete game installation. Original-game and third-party asset rights remain unchanged. The packaging and relinking tools impose no prohibition on modifying the LGPL library or reverse engineering to debug such modifications.

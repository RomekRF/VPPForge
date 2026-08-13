# VPP Forge · Desktop

The Red Faction VPP workbench, by Romek (RomekRF). Open any `.vpp` and
browse, preview, edit and rebuild it in its own window — the modern
successor to Descent Manager VPPBUILDER32 and VPVIEW32.

## Features

**Viewing**

- Byte-exact VPP v1 reader and writer, verified against the retail game
  files.
- Viewers for TGA, DDS, PCX and VBM textures, V3M static meshes, VF
  fonts, RFL levels, tables and audio.
- V3C characters load as real skeletons and play RFA animations in the
  3D viewer, with Play/Pause, a scrubber and a time readout. Select an
  RFA on its own and it previews on a matching character from the VPP.
- VFX effects render with their textures and play their animation:
  morph, per-frame and keyframed transforms, and UV animation.
- Multi-frame VBM textures animate on the models that use them.
- The 3D viewer reports model dimensions in RF units (metres) and lists
  the textures a mesh references, with one click to select the mesh and
  everything it uses.

**Editing**

- Add, remove, rename, reorder and duplicate files; undo and redo.
- Drag-select in the file list, select-all per file type, filtering, and
  full keyboard navigation.
- Replace all TGA with DDS in one click: automatic DXT1/DXT5 by alpha,
  with a guard against dimensions the game's D3D11 renderer rejects.
- Extract opens the native Windows folder picker and unpacks straight
  into the folder you choose.

**Saving**

- Save writes the VPP back in place with an atomic replace, so an
  interrupted save cannot corrupt the file. Save As writes elsewhere.
- Autosave keeps opened VPPs written to disk, and the first overwrite of
  a session leaves a `.vpp.bak` of the original.

**The app itself**

- Updates itself: it checks for new releases in the background, and
  Help › Check for updates installs one and restarts, with no reinstall.
- Optional per-user setup on first run (no admin) that adds the `.vpp`
  file association, a Start Menu entry and an Apps uninstall entry.
  Decline it and the exe simply runs.
- Start screen with recent files, light and dark themes, swappable panel
  layout, and preferences that persist between sessions.
- Also ships as a single self-contained HTML file that runs in Chrome or
  Edge with nothing to install.

## Download

From the [Releases page](https://github.com/RomekRF/VPPForge/releases/latest):

**`VPPForge-Desktop-Win64.zip`** — the desktop app. Unzip and run
`vppforge.exe`. Installing is optional: the first launch offers to
associate `.vpp` files and add a Start Menu entry, and declining just
runs the app. From here on it updates itself, so this is the only manual
download you need.

**`VPPForge-Browser.html`** — the whole app in one file. Open it in
Chrome or Edge; nothing to install and nothing for antivirus to scan.
You lose only what needs the OS: files open by drag-and-drop rather than
double-click, and saving downloads a new `.vpp` instead of writing back
over the original.

**`vppforge.exe`** — the same executable that is inside the zip, published
on its own. This is what the in-app updater downloads.

SmartScreen or Defender may warn on first launch because the exe is not
code-signed; choose "More info", then "Run anyway". The browser build
avoids this entirely.

## How it works

`vppforge.exe` embeds the entire app, serves it over a private
token-guarded loopback bridge, and opens it in an app-mode window using
the Edge or Chrome already on every Windows 10/11 machine. The bridge
gives it what a plain page cannot have: the double-clicked file opens
automatically, Open, Save and Extract use real Windows dialogs, saves
write the VPP back in place with an atomic replace, and settings and
recent files live in `%LOCALAPPDATA%\VPPForge`. Update checks and
downloads go over HTTPS to this repository's releases.

## Building

CI builds every push. To build locally, with MSYS2 or any MinGW-w64
toolchain on PATH:

    python3 tools/embed.py app/vpp-forge.html src/app_html.h
    x86_64-w64-mingw32-windres resources/vppforge.rc -O coff -o res.o
    x86_64-w64-mingw32-gcc -O2 -municode -mwindows src/vppforge.c res.o -o vppforge.exe -lws2_32 -lcomdlg32 -ladvapi32 -lshell32 -luser32 -lole32 -luuid -lwininet -static -s

For an unreleased build, open the **Actions** tab and download the
artifacts from the latest green `build-windows` run.

To publish a release, bump `src/version.h` and push a matching version
tag (`git tag v1.20.0 && git push origin v1.20.0`); the workflow builds
it and attaches all three downloads to a GitHub Release automatically.

## Layout

    src/version.h        the version, shared by the exe and its file properties
    src/vppforge.c       native shell: server, bridge, dialogs, install, updater
    app/vpp-forge.html   the full application (also runs standalone in a browser)
    app/vfx.js           the VFX engine, as a standalone module for reuse
    tools/embed.py       embeds the html into the exe at build time
    resources/           icon, logo and the Windows version resource
    installer/           end-user README packaged with the exe
    .github/workflows/   automatic Windows build and release

## Credits

VPP Forge by Romek (RomekRF). Successor to Descent Manager VPPBUILDER32
and VPVIEW32, original code by Heiko Herrmann, Descent Network. The V3M
engine mirrors Redux V3mParser and RF Static Mesh Tools, and bone
conventions were cross-checked against GooberRF/redux. Format
specifications: rafalh/rf-reversed.

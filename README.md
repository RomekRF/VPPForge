# VPP Forge · Desktop

Native Windows build of VPP Forge, the Red Faction VPP workbench,
by Romek (RomekRF). Install it, double-click any `.vpp`, and it opens
in its own window — the modern successor to Descent Manager
VPPBUILDER32 and VPVIEW32.

## Features

- Browse, preview, build and save byte-exact VPP v1 files, verified
  against the retail game files.
- Viewers for TGA/DDS/PCX/VBM textures, V3M static meshes, VF fonts,
  levels, tables and audio.
- V3C characters load as real skeletons and play RFA animations in the
  3D viewer, with Play/Pause, a scrubber and a time readout.
- VFX effects render with their textures and play their animation
  (morph, per-frame and keyframed transforms, UV animation).
- Replace all TGA with DDS in one click: automatic DXT1/DXT5 by alpha,
  with a guard against sizes the game's D3D11 renderer rejects.
- Classic toolbar (New, Open, Save, Save As, Add, Remove, Extract).
  Extract opens the native Windows folder picker and unpacks straight
  into the folder you choose.
- Saving writes the VPP back in place with an atomic replace, so an
  interrupted save cannot corrupt the file. Autosave keeps opened VPPs
  written to disk, and the first overwrite of a session leaves a
  `.vpp.bak` of the original.
- Undo/redo, duplicate, drag-select, select-all per file type,
  filtering, reordering, rename-in-place, drag-and-drop.
- Start screen with recent files; light/dark theme and swappable panel
  layout; all preferences persist between sessions.
- Installs itself on first run (per-user, no admin): `.vpp` association,
  Start Menu entry, Windows Apps uninstall entry. Newer builds update
  the installed copy automatically.

## Get the exe

Grab the latest build from the
[Releases page](https://github.com/RomekRF/VPPForge/releases/latest):
download `VPPForge-Desktop-Win64.zip`, unzip it and run `vppforge.exe`.
SmartScreen or Defender may warn on first launch because the exe is not
code-signed; choose "More info", then "Run anyway".

For an unreleased build, open the **Actions** tab, pick the latest green
`build-windows` run and download **VPPForge-Desktop-Win64** from the
Artifacts box.

To publish a release, push a version tag such as `v1.17.0`; the workflow
builds it and attaches the zip to a GitHub Release automatically.

## What the exe does

`vppforge.exe` embeds the entire app, serves it over a private
token-guarded loopback bridge, and opens it in an app-mode window using
the Edge or Chrome already on every Windows 10/11 machine. The bridge
gives it what a plain page can never have: the double-clicked file opens
automatically, Open/Save/Extract use real Windows dialogs, saves write
the VPP back **in place** with an atomic replace, and settings and
recents live in `%LOCALAPPDATA%\VPPForge`.

## Build locally instead (optional)

With MSYS2 or any MinGW-w64 toolchain on PATH:

    python3 tools/embed.py app/vpp-forge.html src/app_html.h
    x86_64-w64-mingw32-windres resources/vppforge.rc -O coff -o res.o
    x86_64-w64-mingw32-gcc -O2 -municode -mwindows src/vppforge.c res.o -o vppforge.exe -lws2_32 -lcomdlg32 -ladvapi32 -lshell32 -luser32 -lole32 -luuid -static -s

## Layout

    src/version.h        the version, shared by the exe and its file properties
    app/vpp-forge.html   the full application (also works standalone in a browser)
    src/vppforge.c       native shell: server, bridge, dialogs, install, window launch
    tools/embed.py       embeds the html into the exe at build time
    resources/           icon, logo source and Windows version info
    installer/           end-user README packaged with the exe
    .github/workflows/   automatic Windows build

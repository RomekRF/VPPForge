# VPP Forge · Desktop

Native Windows build of VPP Forge, the Red Faction `.vpp` archive workbench.
Install it, double-click any `.vpp`, and it opens in its own window —
the modern successor to Descent Manager VPPBUILDER32 and VPVIEW32.

## Features

- Browse, preview, build and save byte-exact VPP v1 archives, verified
  against the retail game files.
- Viewers for TGA/DDS/PCX/VBM textures, V3M/V3C meshes, VF fonts,
  RFA animations, levels, tables and audio.
- Classic toolbar (New, Open, Save, Add, Remove, Extract); Extract
  unpacks straight into any folder via the native picker.
- Start screen with recent files, drag-and-drop, filtering, reordering,
  rename-in-place.
- Light/dark theme and swappable panel layout; all preferences persist.
- Installs itself on first run (per-user, no admin): `.vpp` association,
  Start Menu entry, Windows Apps uninstall entry. Newer builds update
  the installed copy automatically.

## Get the exe

Open the **Actions** tab, pick the latest green `build-windows` run and
download **VPPForge-Desktop-Win64** from the Artifacts box. Inside is
`vppforge.exe` and a short end-user README.

To publish a release, push a tag such as `v1.7`; the workflow attaches
the zip to a GitHub Release automatically.

## What the exe does

`vppforge.exe` embeds the entire app, serves it over a private
token-guarded loopback bridge, and opens it in an app-mode window using
the Edge or Chrome already on every Windows 10/11 machine. The bridge
gives it what a plain page can never have: the double-clicked file opens
automatically, Open/Save/Extract use real Windows dialogs, saves write
the archive back **in place** with an atomic replace, and settings and
recents live in `%LOCALAPPDATA%\VPPForge`.

## Build locally instead (optional)

With MSYS2 or any MinGW-w64 toolchain on PATH:

    python3 tools/embed.py app/vpp-forge.html src/app_html.h
    x86_64-w64-mingw32-windres resources/vppforge.rc -O coff -o res.o
    x86_64-w64-mingw32-gcc -O2 -municode -mwindows src/vppforge.c res.o -o vppforge.exe -lws2_32 -lcomdlg32 -ladvapi32 -lshell32 -luser32 -lole32 -luuid -static -s

## Layout

    app/vpp-forge.html   the full application (also works standalone in a browser)
    src/vppforge.c       native shell: server, bridge, dialogs, install, window launch
    tools/embed.py       embeds the html into the exe at build time
    resources/           icon, logo source and Windows version info
    installer/           end-user README packaged with the exe
    .github/workflows/   automatic Windows build

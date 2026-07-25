# VPP Forge · Desktop

Native Windows build of VPP Forge, the Red Faction `.vpp` archive workbench.
Install it, double-click any `.vpp`, and it opens in its own window,
exactly like the classic VPPBuilder32 did.

## Get the exe without installing any tools

1. Create a repository on GitHub and upload this whole folder
   (drag and drop onto the repo page works).
2. Open the **Actions** tab. The `build-windows` workflow runs on its own
   and finishes in about a minute.
3. Open the finished run and download **VPPForge-Desktop-Win64** from
   the Artifacts box. Inside is `vppforge.exe` plus the installer.
4. To publish a release for your community, push a tag such as `v1.0`;
   the workflow attaches the zip to a GitHub Release automatically.

## What the exe does

`vppforge.exe` embeds the entire app, serves it to a private
token-guarded loopback bridge, and opens it in an app-mode window using
the Edge or Chrome already on every Windows 10/11 machine. The bridge
gives it what a plain page can never have: the double-clicked file opens
automatically, Open and Save use real Windows dialogs, and Save writes
the archive back **in place** with an atomic replace.

## Build locally instead (optional)

With MSYS2 or any MinGW-w64 toolchain on PATH:

    python3 tools/embed.py app/vpp-forge.html src/app_html.h
    x86_64-w64-mingw32-windres resources/vppforge.rc -O coff -o res.o
    x86_64-w64-mingw32-gcc -O2 -municode -mwindows src/vppforge.c res.o -o vppforge.exe -lws2_32 -lcomdlg32 -ladvapi32 -lshell32 -luser32 -static -s

## Layout

    app/vpp-forge.html   the full application (also works standalone in a browser)
    src/vppforge.c       native shell: server, bridge, dialogs, window launch
    tools/embed.py       embeds the html into the exe at build time
    resources/           icon + Windows version info
    installer/           Install / Uninstall bats + end-user README
    .github/workflows/   automatic Windows build

VPP FORGE · Red Faction VPP workbench
=====================================

WHAT'S IN HERE
  vppforge.exe            the desktop app
  VPPForge-Browser.html   the same app as a single web page, no install

  Use the exe for the full experience. Use the HTML if you would rather
  not run an exe: open it in Chrome or Edge and everything works except
  the parts that need Windows itself (see BROWSER VERSION below).

SETUP
  Run vppforge.exe. Installing is optional. On first launch it asks
  whether to set itself up:
    Yes - .vpp files open in VPP Forge on double-click, a Start Menu
          entry appears, and it's listed in Windows Apps for uninstall.
    No  - it just runs, and never asks again. Nothing is written
          outside your settings folder.
  A short welcome page walks you through the basics (reopen it any
  time from Help > Getting started).
  If an older VPP Forge is already installed, there is no prompt:
  the new version silently updates the installed copy the first time
  you run it, and .vpp files open with the new version from then on.
  If Windows still opens another program afterwards, right-click a
  .vpp once, choose "Open with", pick VPP Forge, tick "Always".

  SmartScreen may warn on first run because the exe is not code-signed.
  Choose "More info", then "Run anyway".

USING IT
  - Open a .vpp by double-click, drag and drop, the Open button, or
    the Recent list on the start screen.
  - Toolbar: New, Open, Save VPP, Add, Remove, Extract. Extract asks
    for a destination folder and unpacks the selected files there.
  - Select a file to preview it: textures, meshes, animations,
    levels, fonts, tables and audio.
  - Arrow keys move through the list, Space plays and pauses an
    animation or a sound, F2 renames, Del removes, Alt Up/Down
    reorders, Ctrl S saves, Ctrl E extracts, Ctrl F filters.
    The full list is under Help > Keyboard shortcuts.
  - New versions install themselves: Help > Check for updates.
  - Light/dark theme and the panel-swap button sit at the top right;
    theme, layout and panel sizes are remembered between sessions.

BROWSER VERSION
  Open VPPForge-Browser.html in Chrome or Edge. Same viewers, same
  editing, nothing to install and nothing for antivirus to scan.
  Differences from the exe:
    - Open files by dragging them onto the page or the Open button;
      double-clicking a .vpp in Explorer won't route here.
    - Saving downloads a new .vpp through the browser instead of
      writing back over the original file.
    - Extract saves a single file or a ZIP to your Downloads folder
      instead of asking for a destination folder.

REMOVE
  Windows Settings > Apps > Installed apps > VPP Forge > Uninstall,
  or run: vppforge.exe /uninstall
  This removes the app, the .vpp association, the Start Menu entry
  and saved settings. If you never installed it, just delete the exe.

NOTES
  - Windows 10 or 11, 64-bit. Uses your installed Edge or Chrome as
    the display engine, so there is nothing else to install.
  - Saving writes the VPP back to its original location in place.
  - The writer is byte-exact and verified against retail files.

CREDITS
  VPP Forge by Romek (RomekRF).
  Successor to Descent Manager VPPBUILDER32 and VPVIEW32,
  original code by Heiko Herrmann, Descent Network.
  V3M mesh engine mirrors Redux V3mParser and RF Static Mesh Tools.
  Format specifications: rafalh/rf-reversed.

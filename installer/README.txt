VPP FORGE · Red Faction archive workbench
=========================================

SETUP
  Run vppforge.exe. On first launch it asks whether to set itself up:
  say Yes and .vpp files open in VPP Forge on double-click, a Start
  Menu entry appears, and it's listed in Windows Apps.
  If an older VPP Forge is already installed, there is no prompt:
  the new version silently updates the installed copy the first time
  you run it, and .vpp files open with the new version from then on.
  If Windows still opens another program afterwards, right-click a
  .vpp once, choose "Open with", pick VPP Forge, tick "Always".

  SmartScreen may warn on first run because the exe is not code-signed.
  Choose "More info", then "Run anyway".

REMOVE
  Windows Settings > Apps > Installed apps > VPP Forge > Uninstall,
  or run: vppforge.exe /uninstall

NOTES
  - Windows 10 or 11, 64-bit. Uses your installed Edge or Chrome as
    the display engine, so there is nothing else to install.
  - Saving writes the archive back to its original location in place.
  - The writer is byte-exact and verified against retail files.

CREDITS
  Successor to Descent Manager VPPBUILDER32 and VPVIEW32,
  original code by Heiko Herrmann, Descent Network.
  V3M mesh engine mirrors Redux V3mParser and RF Static Mesh Tools.
  Format specifications: rafalh/rf-reversed.

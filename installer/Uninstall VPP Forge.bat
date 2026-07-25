@echo off
title Uninstall VPP Forge
reg delete "HKCU\Software\Classes\.vpp" /f >nul 2>&1
reg delete "HKCU\Software\Classes\VPPForge.Archive" /f >nul 2>&1
del "%APPDATA%\Microsoft\Windows\Start Menu\Programs\VPP Forge.lnk" >nul 2>&1
rmdir /S /Q "%LOCALAPPDATA%\Programs\VPPForge" >nul 2>&1
echo VPP Forge removed. File associations for .vpp are cleared.
pause

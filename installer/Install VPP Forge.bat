@echo off
title Install VPP Forge
set "DEST=%LOCALAPPDATA%\Programs\VPPForge"
echo Installing VPP Forge to %DEST%
mkdir "%DEST%" 2>nul
copy /Y "%~dp0vppforge.exe" "%DEST%\vppforge.exe" >nul
if errorlevel 1 (
  echo Could not copy vppforge.exe. Close any running VPP Forge windows and try again.
  pause
  exit /b 1
)
reg add "HKCU\Software\Classes\VPPForge.Archive" /ve /d "Red Faction Archive" /f >nul
reg add "HKCU\Software\Classes\VPPForge.Archive\DefaultIcon" /ve /d "\"%DEST%\vppforge.exe\",0" /f >nul
reg add "HKCU\Software\Classes\VPPForge.Archive\shell\open" /ve /d "Open with VPP Forge" /f >nul
reg add "HKCU\Software\Classes\VPPForge.Archive\shell\open\command" /ve /d "\"%DEST%\vppforge.exe\" \"%%1\"" /f >nul
reg add "HKCU\Software\Classes\.vpp" /ve /d "VPPForge.Archive" /f >nul
powershell -NoProfile -Command "$s=(New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath('Programs')+'\VPP Forge.lnk');$s.TargetPath='%DEST%\vppforge.exe';$s.IconLocation='%DEST%\vppforge.exe,0';$s.Save()" >nul 2>&1
echo.
echo Done. Double-click any .vpp file to open it in VPP Forge.
echo If Windows still opens another program, right-click a .vpp once,
echo choose "Open with", pick VPP Forge, and tick "Always".
echo.
pause

300 Heroes Import for 3ds Max 2016-2027
=======================================

Imports 300 Heroes (300英雄) models into 3ds Max: both file formats of the
game, .model (EG3D / ezgame) and .x (JUMPX V5.01 - not DirectX .x files).
Meshes, UVs, vertex colours, materials with textures, skeleton, skin weights
and all animation clips, including the compressed monster animations.

Install
  1. Extract the whole ZIP.
  2. Close 3ds Max.
  3. Double-click Install.bat. Windows asks for admin rights (install for
     all users). If you decline, the plugin is installed for you only.
  4. Start 3ds Max: File -> Import -> "300 Heroes (EG3D)".
     The import dialog with all options is also available as the
     MacroScript "EG3D Import" (Customize User Interface, category "EG3D").

  Alternative: use Eg3dImport-<version>-Setup.exe from the release page.

Requirements
  - Windows 10/11, 64-bit, 3ds Max 2016 to 2027
  - The model files (.model / .x, with their .dds textures next to them)
    from your own copy of the game - the plugin contains no game data
  - Microsoft Visual C++ runtime, version 14.50 or newer:
    https://aka.ms/vc14/vc_redist.x64.exe
    (Install.bat tells you if it is missing; the Setup.exe installs it)

Uninstall
  Uninstall.bat - or, if you used the Setup.exe, Windows Settings - Apps.

Windows warning ("Windows protected your PC")
  The files are not digitally signed yet, so Windows SmartScreen warns about
  programs from the internet that it does not know yet. The checksums
  (SHA256SUMS.txt) on the release page show that the file is unchanged.

More information, source code and bug reports:
  https://github.com/DennisHerrm/300-hero-importer-3ds-max

Disclaimer
  Unofficial, free fan project. It is NOT made by, affiliated with, endorsed
  or sponsored by the makers or publishers of 300 Heroes or by Autodesk.
  300 Heroes is a product of its respective owners; 3ds Max is a trademark
  of Autodesk. No game data is included.

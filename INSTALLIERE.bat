@echo off
REM ============================================================
REM  EG3D Import - Paket erstellen + installieren
REM
REM  1. Baut die Paketstruktur aus output\ und scripts\
REM  2. Kopiert das fertige Paket nach ApplicationPlugins
REM
REM  Zielordner (pro Benutzer, kein Admin noetig):
REM    %APPDATA%\Autodesk\ApplicationPlugins\Eg3dImport
REM  ausgeschrieben:
REM    C:\Users\<du>\AppData\Roaming\Autodesk\ApplicationPlugins\Eg3dImport
REM
REM  Voraussetzung: BAUE_ALLE.bat wurde bereits ausgefuehrt.
REM ============================================================

setlocal enabledelayedexpansion
cd /d "%~dp0"

set "SRC=%~dp0output"
set "SCRIPTS=%~dp0scripts"
set "PKG=%~dp0package\Eg3dImport"
set "DEST=%APPDATA%\Autodesk\ApplicationPlugins\Eg3dImport"

echo.
echo ========================================
echo  EG3D Import - Paket erstellen
echo ========================================
echo.

if not exist "%SRC%" (
    echo FEHLER: output\ Ordner nicht gefunden!
    echo Bitte zuerst BAUE_ALLE.bat ausfuehren.
    pause
    exit /b 1
)

if not exist "%PKG%\PackageContents.xml" (
    echo FEHLER: package\Eg3dImport\PackageContents.xml fehlt!
    pause
    exit /b 1
)

REM ============================================================
REM  Ist eine bereits installierte DLU gesperrt?
REM
REM  Geprueft wird die echte Bedingung: die vorhandene Datei zum
REM  Anhaengen oeffnen, ohne etwas zu schreiben. Klappt das, ist
REM  sie frei. Eine tasklist-Abfrage auf 3dsmax.exe waere die
REM  falsche Pruefung - sie testet, ob ein Prozess laeuft, statt
REM  ob die Datei schreibbar ist.
REM
REM  Bei der ERSTEN Installation gibt es nichts zu pruefen - die
REM  Schleife laeuft dann ins Leere.
REM
REM  Notfalls ueberspringen:  set EG3D_SKIP_LOCKCHECK=1
REM ============================================================

set "LOCKED="

if defined EG3D_SKIP_LOCKCHECK (
    echo Sperrpruefung uebersprungen ^(EG3D_SKIP_LOCKCHECK^).
    echo.
) else (
    if exist "%DEST%" (
        for %%V in (2016 2017 2018 2019 2020 2021 2022 2023 2024 2025 2026 2027) do (
            if exist "%DEST%\Contents\%%V\Eg3dImport.dlu" (
                (call ) 2>nul 1>>"%DEST%\Contents\%%V\Eg3dImport.dlu" || set "LOCKED=1"
            )
        )
    )
)

if defined LOCKED (
    echo FEHLER: Eine bereits installierte Eg3dImport.dlu ist gesperrt.
    echo.
    echo Das heisst fast immer: 3ds Max laeuft noch und hat das Plugin
    echo geladen. Bitte alle Max-Instanzen schliessen und erneut starten.
    echo.
    pause
    exit /b 1
)

REM ---- Alte Paketinhalte entfernen, damit keine Reste ----
REM ---- aus einem frueheren Build mitgeschleppt werden  ----
REM ---- PackageContents.xml selbst bleibt stehen.       ----
if exist "%PKG%\Contents" rmdir /s /q "%PKG%\Contents" >nul 2>&1

echo Kopiere Plugins in das Paket...
echo.

set FOUND=0
for %%V in (2016 2017 2018 2019 2020 2021 2022 2023 2024 2025 2026 2027) do (
    if exist "%SRC%\%%V\Eg3dImport.dlu" (
        mkdir "%PKG%\Contents\%%V" >nul 2>&1
        copy /Y "%SRC%\%%V\Eg3dImport.dlu" "%PKG%\Contents\%%V\" >nul
        echo  %%V: OK
        set /a FOUND+=1
    ) else (
        echo  %%V: UEBERSPRUNGEN  [keine DLU in output\%%V\]
    )
)

echo.
if %FOUND%==0 (
    echo FEHLER: Keine kompilierten Plugins gefunden!
    echo Bitte zuerst BAUE_ALLE.bat ausfuehren.
    pause
    exit /b 1
)

REM ---- Skripte: Oberflaeche und Menueeintraege ----
REM Die liegen im Quellbaum unter scripts\ und werden bei jedem
REM Lauf frisch ins Paket kopiert. Deshalb ueberlebt ein
REM Bearbeiten von scripts\*.mcr das naechste INSTALLIERE.bat.
echo Kopiere Skripte in das Paket...

mkdir "%PKG%\Contents\MacroScripts"          >nul 2>&1

REM Alle .mcr aus scripts\ kopieren, statt einzelne Namen hier
REM zu wiederholen. Bis 0.1.0 standen die Namen fest verdrahtet -
REM darunter eine Datei, die es nie gab (Eg3dImportAlt.mcr, ein
REM Rest aus dem BAF-Vorbild). Das Skript brach dann ab, obwohl
REM alle zwoelf Plugins fertig gebaut waren.
REM
REM Eine Namensliste an zwei Stellen zu pflegen (hier und in
REM PackageContents.xml) geht genau einmal gut. Deshalb hier ein
REM Platzhalter, in der XML ebenso - und PRUEFE_PAKET.py gleicht
REM beide gegen den tatsaechlichen Ordnerinhalt ab.
copy /Y "%SCRIPTS%\*.mcr" "%PKG%\Contents\MacroScripts\" >nul

set "MCR_COUNT=0"
for %%F in ("%PKG%\Contents\MacroScripts\*.mcr") do set /a MCR_COUNT+=1

if "%MCR_COUNT%"=="0" (
    echo FEHLER: Keine .mcr aus %SCRIPTS% kopiert.
    echo Erwartet wird mindestens scripts\Eg3dImport.mcr.
    pause
    exit /b 1
)
echo  Skripte: OK (%MCR_COUNT% Datei^(en^))
echo.

echo Installiere nach:
echo   %DEST%
echo.

if exist "%DEST%" rmdir /s /q "%DEST%" >nul 2>&1
mkdir "%DEST%" >nul 2>&1

if not exist "%DEST%" (
    echo FEHLER: Zielordner konnte nicht angelegt werden.
    echo   %DEST%
    pause
    exit /b 1
)

xcopy /E /I /Y "%PKG%\*" "%DEST%\" >nul
if errorlevel 1 (
    echo FEHLER beim Kopieren nach %DEST%
    pause
    exit /b 1
)

REM ---- Gegenpruefen, dass wirklich alles angekommen ist. ----
REM ---- xcopy meldet nicht jeden Teilausfall als Fehler.  ----
set "MISSING="
if not exist "%DEST%\PackageContents.xml"                                 set "MISSING=PackageContents.xml"
if not exist "%DEST%\Contents\MacroScripts\Eg3dImport.mcr"               set "MISSING=Eg3dImport.mcr"

if defined MISSING (
    echo FEHLER: Im Zielordner fehlt: !MISSING!
    pause
    exit /b 1
)

echo.
echo ========================================
echo  Installiert.
echo ========================================
echo.
echo 3ds Max starten. Danach:
echo.
echo   Datei  -^>  Importieren  -^>  300 Heroes Model ^(*.model, *.x^)
echo.
echo Das Makro steht ausserdem unter
echo   Customize -^> Customize User Interface -^> Category "DH Tools"
echo.
echo Test im Listener:
echo   Eg3dCpp.version^(^)
echo.
pause
exit /b 0

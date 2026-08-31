@echo off
REM ============================================================
REM  Gegenprobe: C++-Parser gegen die Python-Referenz
REM
REM    VERGLEICHE.bat <ordner-oder-datei> [--all]
REM
REM  Ohne --all wird nur die Zusammenfassung verglichen (schnell,
REM  ein paar Sekunden). Mit --all geht jeder Vertex, jedes
REM  Dreieck, jede Bind-Matrix, jeder Keyframe und jeder Bone-
REM  Frame einzeln durch.
REM
REM  ZUR GROESSE: --all ueber den ganzen Testordner ergibt rund
REM  470 MB je Seite und 6 Millionen Zeilen. Das ist Absicht -
REM  wer nur die Kopfzahlen vergleicht, findet genau die Fehler
REM  nicht, die sich in der Bind-Pose wegkuerzen.
REM
REM  Voraussetzung: BAUE_ALLE.bat wurde ausgefuehrt (fuer
REM  output\tools\eg3ddump.exe) und Python 3 liegt im PATH.
REM ============================================================

setlocal
cd /d "%~dp0"

set "TARGET=%~1"
set "MODE=%~2"

if "%TARGET%"=="" (
    echo.
    echo   VERGLEICHE.bat ^<ordner-oder-datei^> [--all]
    echo.
    echo   Beispiel:
    echo     VERGLEICHE.bat C:\300heroes\models
    echo     VERGLEICHE.bat C:\300heroes\models --all
    echo.
    exit /b 2
)

set "DUMP=%~dp0output\tools\eg3ddump.exe"
if not exist "%DUMP%" (
    echo FEHLER: %DUMP% fehlt. Bitte zuerst BAUE_ALLE.bat ausfuehren.
    exit /b 1
)

where python >nul 2>&1
if errorlevel 1 (
    echo FEHLER: python liegt nicht im PATH.
    exit /b 1
)

echo.
echo ========================================
echo  Gegenprobe
echo ========================================
echo  Ziel:  %TARGET%
echo  Modus: %MODE%
echo.

echo  C++ ...
"%DUMP%" "%TARGET%" %MODE% -o "%~dp0vergleich_cpp.txt"
if errorlevel 1 (
    echo  FEHLER beim C++-Dump.
    exit /b 1
)

echo  Python ...
python "%~dp0tools\pydump.py" "%TARGET%" %MODE% -o "%~dp0vergleich_py.txt"
if errorlevel 1 (
    echo  FEHLER beim Python-Dump.
    exit /b 1
)

echo.
echo  Vergleiche ...
REM /L = zeilenweise vergleichen. fc gibt 0 zurueck, wenn die
REM Dateien gleich sind.
fc /L "%~dp0vergleich_cpp.txt" "%~dp0vergleich_py.txt" > "%~dp0vergleich_diff.txt"
if errorlevel 1 (
    echo.
    echo  ABWEICHUNGEN GEFUNDEN. Die ersten Zeilen:
    echo  ------------------------------------------------------------
    powershell -NoProfile -Command "Get-Content '%~dp0vergleich_diff.txt' -TotalCount 40" 2>nul
    echo  ------------------------------------------------------------
    echo  Vollstaendig: vergleich_diff.txt
    echo.
    exit /b 1
)

echo.
echo  KEINE ABWEICHUNGEN.
echo.
del /Q "%~dp0vergleich_diff.txt" >nul 2>&1
exit /b 0

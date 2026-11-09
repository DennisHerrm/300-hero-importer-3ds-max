@echo off
REM ============================================================
REM  EG3D Import - Deinstallieren
REM  Entfernt das Paket aus ApplicationPlugins.
REM ============================================================

setlocal enabledelayedexpansion
set "DEST=%APPDATA%\Autodesk\ApplicationPlugins\Eg3dImport"
REM Max sucht Pakete an ZWEI Stellen: benutzerweit unter %APPDATA%
REM (dort installiert INSTALLIERE.bat, ohne Adminrechte) und
REM maschinenweit unter %PROGRAMDATA%. Liegt dort noch eine alte
REM Fassung, laedt Max beide nebeneinander - deshalb wird die
REM zweite Stelle wenigstens gemeldet.
set "SYS=%PROGRAMDATA%\Autodesk\ApplicationPlugins\Eg3dImport"

echo.
echo Entferne: %DEST%
echo.

if not exist "%DEST%" (
    echo Nichts zu tun - Paket ist nicht installiert.
    pause
    exit /b 0
)

REM Echter Sperrtest statt einer tasklist-Abfrage: die vorhandene
REM Datei zum Anhaengen oeffnen, ohne etwas zu schreiben. Klappt
REM das nicht, haelt ein laufendes Max sie offen.
set "LOCKED="
for %%V in (2016 2017 2018 2019 2020 2021 2022 2023 2024 2025 2026 2027) do (
    if exist "%DEST%\Contents\%%V\Eg3dImport.dlu" (
        (call ) 2>nul 1>>"%DEST%\Contents\%%V\Eg3dImport.dlu" || set "LOCKED=1"
    )
)

if defined LOCKED (
    echo FEHLER: Die Plugin-Datei ist gesperrt.
    echo 3ds Max laeuft vermutlich noch. Bitte schliessen und erneut starten.
    pause
    exit /b 1
)

rmdir /s /q "%DEST%"

REM %PROGRAMDATA% wird NICHT stillschweigend geloescht: dort
REM installiert dieses Skript nicht, der Ordner kann also von
REM einem Admin-Setup oder aus dem App Store stammen. Ihn ohne
REM Rueckfrage zu entfernen waere ein Uebergriff - gemeldet wird
REM er trotzdem, weil Max sonst weiter die alte Fassung laedt und
REM niemand versteht, warum das Deinstallieren nichts geaendert
REM hat.
if exist "%SYS%" (
    echo.
    echo HINWEIS: Eine zweite Installation liegt unter
    echo   %SYS%
    echo Die stammt nicht von diesem Skript. Solange sie dort
    echo liegt, laedt 3ds Max sie weiter.
)
if exist "%DEST%" (
    echo FEHLER: Ordner konnte nicht entfernt werden.
    pause
    exit /b 1
)

echo Entfernt.
pause
exit /b 0

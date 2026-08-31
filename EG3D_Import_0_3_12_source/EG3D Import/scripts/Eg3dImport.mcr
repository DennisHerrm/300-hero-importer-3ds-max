-- ============================================================
--  EG3D Import - Oberflaeche
--
--  Die Arbeit steckt in C++ (Eg3dCpp.xxx), hier liegt nur die
--  Bedienung. Grund: MAXScript ist fuer zwanzigtausend Vertices
--  zu langsam, und C++ ist fuer einen Dialog zu umstaendlich.
--
--  Die Beschriftung ist englisch, die Kommentare sind deutsch -
--  wie bei BAF Toolkit 1.7.0.
-- ============================================================

macroScript Eg3dImport
    category:"EG3D"
    buttonText:"EG3D Import"
    tooltip:"300 Heroes .model / .x importieren"
(
    -- ------------------------------------------------------
    --  Ist das Plugin ueberhaupt geladen?
    --
    --  Ohne diese Pruefung bekommt der Benutzer beim ersten
    --  Knopfdruck einen MAXScript-Laufzeitfehler mit
    --  Aufrufstapel - und sucht den Fehler in seiner Datei
    --  statt in der fehlenden DLL.
    -- ------------------------------------------------------
    if (isKindOf Eg3dCpp Interface) != true then
    (
        messageBox "Eg3dImport.dlu ist nicht geladen.\n\nBitte INSTALLIERE.bat ausfuehren und 3ds Max neu starten." \
            title:"EG3D Import"
    )
    else
    (
        try (destroyDialog Eg3dImportRollout) catch ()

        rollout Eg3dImportRollout "EG3D Import" width:340
        (
            -- ----------------------------------------------------
            --  Aufbau nach den Autodesk-Konventionen
            --
            --  MAXScript stapelt Controls von selbst untereinander
            --  und richtet sie an den Command-Panel-Konventionen
            --  aus. Fuer alles andere sind "across:", "offset:" und
            --  "align:" der dokumentierte Weg - nicht der Visual
            --  MaxScript Editor, der laut Doku "isn't always
            --  particularly accurate when you then launch your
            --  script", besonders bei Spinnern.
            --
            --  Zusammengehoerendes steht paarweise nebeneinander
            --  (across:2), der Rest in "group"-Bloecken. Vier
            --  Gruppen statt sieben: File, What to build,
            --  Animation, Scene, Diagnostics.
            --
            --  Alle Beschriftungen und Tooltips auf Englisch. Die
            --  Tooltips sagen, was eine Option TUT, nicht wie sie
            --  heisst - und nennen die Grenze dort, wo sie zaehlt.
            -- ----------------------------------------------------
            group "File"
            (
                -- across:2 teilt die Zeile in zwei GLEICHE Spalten.
                -- Ein 250 breites Feld laeuft damit in die zweite
                -- Spalte und liegt unter dem Knopf. Die Summe der
                -- Breiten muss in die Zeile passen, und mit
                -- offset: wird der Knopf an das Feld
                -- herangezogen.
                edittext edFile "" width:252 across:2 readOnly:true \
                    align:#left offset:[-6,0]
                button btnBrowse "..." width:38 height:20 \
                    align:#right offset:[4,1] \
                    tooltip:"Browse for a .model (EG3D) or .x (JUMPX) file."

                -- Labels brechen NICHT von selbst um. Deshalb drei
                -- feste Zeilen, die in MAXScript gefuellt werden -
                -- besser als ein Text, der rechts abgeschnitten
                -- wird und dessen Rest niemand sieht.
                label lblKind "" align:#left width:300
                label lblSum1 "" align:#left width:300
                label lblSum2 "" align:#left width:300
            )

            group "What to build"
            (
                checkbox cbSkeleton "Skeleton" checked:true across:2 \
                    tooltip:"Create the bone hierarchy. Turn off to import geometry only."
                checkbox cbMeshes "Meshes" checked:true \
                    tooltip:"Create mesh objects from the model's submeshes."
                checkbox cbSkin "Skin" checked:true across:2 \
                    tooltip:"Add a Skin modifier and transfer the vertex weights."
                checkbox cbMaterials "Materials" checked:true \
                    tooltip:"Build materials and assign them to the meshes."
                checkbox cbAnimation "Animation" checked:true across:2 \
                    tooltip:"Import the animation clips as keyframes."
                checkbox cbVisibility "Visibility" checked:true \
                    tooltip:"Animate mesh visibility from the file's on/off channels. Bones are never affected."
            )

            group "Animation"
            (
                checkbox cbSequence "Sequence mode" checked:true \
                    tooltip:"Lay the clips end to end on one timeline instead of overlapping them."
                checkbox cbNoteTrack "Note track with clip names" checked:true \
                    tooltip:"Write clip names to a note track on the scene root, so exporters and the Sequence Manager can read them."
                checkbox cbZeroScale "Ignore zero-scale bones" checked:false \
                    tooltip:"Some props are hidden by scaling their bone to almost zero instead of using a visibility flag - on 336_skin1.model the two sword bones sit at 0.001 in all 31 clips. This forces such keys back to 1 and deliberately deviates from the file."
                checkbox cbLinear "Linear keys (no overshoot)" checked:true \
                    tooltip:"Max uses TCB controllers by default, which overshoot between keys. For baked game data with a key on every frame that shows up as jitter."
                checkbox cbReduce "Drop unchanged keys" checked:false \
                    tooltip:"Skip keys identical to their neighbours. The format stores every bone on every frame - this can remove most of them."
                spinner spRest "Rest frames before animation " \
                    range:[0, 500, 0] type:#integer fieldWidth:50 \
                    tooltip:"0 = no transition. With rest frames Max interpolates from the rest pose into the first clip frame, and things rotate and scale along the way."
            )

            group "Scene"
            (
                spinner spScale "Scale " range:[0.001, 1000.0, 1.0] \
                    type:#float fieldWidth:60 across:2 \
                    tooltip:"Uniform scale applied to the whole import."
                checkbox cbFlipUv "Flip UV V" checked:true \
                    tooltip:"These formats come from a DirectX pipeline, where V runs downwards. Max counts it upwards - without the flip the texture sits mirrored vertically."
                checkbox cbBoneObjects "Real bones" checked:true \
                    tooltip:"Create real Bone objects instead of dummy helpers. Bone length follows the distance to the next child."
                checkbox cbLayers "Sort into layers" checked:true \
                    tooltip:"Create one layer per model, with bones and meshes kept apart - for example '039_skin8 Bones' and '039_skin8 Mesh'."
                checkbox cbMedit "Fill Material Editor" checked:true \
                    tooltip:"Put the materials that are actually used into the Compact Material Editor. It holds 24 slots at most."
            )

            group "Diagnostics"
            (
                button btnLog "Show log" width:145 height:22 across:2 \
                    enabled:false \
                    tooltip:"Show the log of the last import in a window."
                button btnSaveLog "Save log..." width:145 height:22 \
                    enabled:false \
                    tooltip:"Write the log and a scene dump to one text file."
                spinner spDumpFrame "Compare frame " range:[0, 10000, 30] \
                    type:#integer fieldWidth:50 \
                    tooltip:"This animation frame is put next to frame 0 in the scene dump, so a jump between rest pose and animation is visible as a number."
            )

            button btnImport "Import" width:150 height:28 across:2 enabled:false
            button btnDump   "Dump..." width:150 height:28 enabled:false

            label lblStatus "" align:#left height:32

            -- --------------------------------------------------
            --  Optionen in die C++-Seite schieben


            fn pushOptions =
            (
                Eg3dCpp.setOption "skeleton"   (if cbSkeleton.checked   then 1 else 0)
                Eg3dCpp.setOption "meshes"     (if cbMeshes.checked     then 1 else 0)
                Eg3dCpp.setOption "skin"       (if cbSkin.checked       then 1 else 0)
                Eg3dCpp.setOption "materials"  (if cbMaterials.checked  then 1 else 0)
                Eg3dCpp.setOption "animation"  (if cbAnimation.checked  then 1 else 0)
                Eg3dCpp.setOption "sequence"   (if cbSequence.checked   then 1 else 0)
                Eg3dCpp.setOption "noteTrack"  (if cbNoteTrack.checked  then 1 else 0)
                Eg3dCpp.setOption "visibility" (if cbVisibility.checked then 1 else 0)
                Eg3dCpp.setOption "boneObjects" (if cbBoneObjects.checked then 1 else 0)
                Eg3dCpp.setScale spScale.value
                Eg3dCpp.setFlipUvV (if cbFlipUv.checked then 1 else 0)
                Eg3dCpp.setLinearKeys (if cbLinear.checked then 1 else 0)
                Eg3dCpp.setIgnoreZeroScale (if cbZeroScale.checked then 1 else 0)
                -- Das Log ist IMMER ausfuehrlich.
                --
                -- Es gab einen Schalter dafuer, und er stand
                -- meistens aus - genau dann, wenn etwas schieflief
                -- und man die Zeilen gebraucht haette. Ein Import
                -- dauert Sekunden; die paar hundert Zeilen mehr
                -- kosten nichts und ersparen einen zweiten Lauf.
                Eg3dCpp.setDebug 1
                Eg3dCpp.setRestFrames spRest.value
                Eg3dCpp.setReduceKeys (if cbReduce.checked then 1 else 0)
            )

            -- ----------------------------------------------------
            --  Note Track
            --
            --  Der Track haengt an rootNode - der SZENENWURZEL,
            --  nicht an einem Bone. rootNode ist eine
            --  MAXScript-Konstante, keine Variable; sie ist
            --  einfach da. Exporter suchen den Track genau dort.
            --
            --  Deshalb steht das hier und nicht in C++: ueber
            --  Interface::GetRootNode() laesst sich zwar ein
            --  Track anlegen, aber ob das derselbe Knoten ist,
            --  den MAXScript unter rootNode versteht, waere eine
            --  ungeprueft Annahme. Der XFBIN-Importer macht es
            --  nachweislich so, also hier auch.
            --
            --  Zwei Keys je Clip, am Anfang und am Ende - dieselbe
            --  Form, die das Animation-Merge-Werkzeug liest.
            -- ----------------------------------------------------
            -- ----------------------------------------------------
            --  Materialien in den Compact Material Editor
            --
            --  Ueber meditMaterials - ein MAXScript-Array, das
            --  genau die Sample-Slots des Editors abbildet. Das
            --  ist der Weg, den auch die Autodesk-Beispiele gehen.
            --
            --  ZWEI GRENZEN, die man kennen muss:
            --
            --  1. Der Compact Material Editor hat je nach
            --     Einstellung 6, 15 oder HOECHSTENS 24 Slots.
            --     039_skin8.model hat 50 Materialien - die passen
            --     dort grundsaetzlich nicht alle hinein. Der
            --     Editor ist kein Materialverwalter; alle
            --     Materialien haengen ohnehin an den Meshes und
            --     sind ueber den Slate Material Editor oder
            --     "Get Material -> Scene" erreichbar.
            --
            --  2. Deshalb kommen nur die tatsaechlich BENUTZTEN
            --     Materialien hinein, in der Reihenfolge der
            --     Objekte. Bei 039_skin8 sind das 11 statt 50.
            --
            --  Gefuellt wird aus der Auswahl, die der Import
            --  hinterlaesst - nicht aus der ganzen Szene. Sonst
            --  wuerde ein zweiter Import die Materialien des
            --  ersten mit einsammeln.
            -- ----------------------------------------------------
            fn populateMaterialEditor =
            (
                if not cbMedit.checked then return 0

                local aSource = selection as array
                if aSource.count == 0 then aSource = objects as array

                local aMats = #()
                for o in aSource do
                (
                    if (isProperty o #material) and (o.material != undefined) then
                        appendIfUnique aMats o.material
                )

                if aMats.count == 0 then return 0

                local iSlots = meditMaterials.count
                local iPut = amin aMats.count iSlots

                for i = 1 to iPut do meditMaterials[i] = aMats[i]

                if aMats.count > iSlots then
                (
                    format "[EG3D] % Materialien benutzt, % Slots im Compact \
Material Editor - der Rest steht ueber Get Material -> Scene bereit.\n" \
                        aMats.count iSlots
                )
                iPut
            )

            -- ----------------------------------------------------
            --  Layer: je Modell getrennt, Bones und Mesh getrennt
            --
            --  Bei zwei Charakteren in einer Datei sind vier Layer
            --  brauchbarer als eine Liste mit 292 Objekten:
            --
            --      039_skin8 Bones / 039_skin8 Mesh
            --      144_skin10_dance Bones / ... Mesh
            --
            --  Die Zuordnung laeuft ueber den obersten Vorfahren.
            --  Das ist der Knoten, unter dem je ein Charakter
            --  haengt - besser als eine Namensregel auf "Bip",
            --  weil die Effekt- und Waffenknoten dort nicht
            --  mitmachen wuerden.
            -- ----------------------------------------------------
            fn topAncestor o =
            (
                local cur = o
                while cur.parent != undefined do cur = cur.parent
                cur
            )

            fn buildLayers =
            (
                if not cbLayers.checked then return 0

                local aSource = selection as array
                if aSource.count == 0 then aSource = objects as array
                if aSource.count == 0 then return 0

                local iMoved = 0
                for o in aSource do
                (
                    local root = topAncestor o
                    local sKind = "Mesh"
                    if (isKindOf o BoneGeometry) or (isKindOf o Point) or \
                       (isKindOf o Dummy) then sKind = "Bones"

                    local sName = root.name + " " + sKind
                    local lay = LayerManager.getLayerFromName sName
                    if lay == undefined then
                        lay = LayerManager.newLayerFromName sName

                    if lay != undefined then
                    (
                        lay.addNode o
                        iMoved += 1
                    )
                )
                iMoved
            )

            fn buildNoteTrack =
            (
                if not cbNoteTrack.checked then return 0

                local sRanges = Eg3dCpp.clipRanges()
                if sRanges == undefined or sRanges == "" then return 0

                -- Alte Tracks weg, sonst sammeln sich bei jedem
                -- Import neue an.
                while ((numNoteTracks rootNode) != 0) do
                (
                    deleteNoteTrack rootNode (getNoteTrack rootNode 1)
                )

                local NT = notetrack "animations"
                addNoteTrack rootNode NT

                local iCount = 0
                for sLine in (filterString sRanges "\n") do
                (
                    -- name <TAB> startFrame <TAB> endFrame
                    local aParts = filterString sLine "\t"
                    if aParts.count >= 3 then
                    (
                        local sName = aParts[1]
                        local iFrom = aParts[2] as integer
                        local iTo   = aParts[3] as integer

                        if iFrom != undefined and iTo != undefined then
                        (
                            local k0 = addNewNoteKey NT.keys iFrom
                            k0.value = sName
                            local k1 = addNewNoteKey NT.keys iTo
                            k1.value = sName
                            iCount += 2
                        )
                    )
                )
                iCount
            )

            -- --------------------------------------------------
            --  Warnungen anzeigen, aber nicht als Fehler
            --
            --  Eine Warnung heisst: der Import lief, und dabei
            --  ist etwas aufgefallen. Sie in eine Fehlerbox zu
            --  packen wuerde brauchbare Ergebnisse wie Ausfaelle
            --  aussehen lassen - der Bestand hat Dateien mit
            --  singulaeren Bind-Matrizen und NaN-Frames, und die
            --  importieren trotzdem sinnvoll.
            -- --------------------------------------------------
            fn showWarnings =
            (
                local w = Eg3dCpp.warnings()
                if w != undefined and w != "" then
                (
                    format "[EG3D] Warnungen:\n%\n" w
                    lblStatus.text = "Imported with warnings - see Listener (F11)."
                )
            )

            -- NICHT "openFile" nennen: so heisst eine EINGEBAUTE
            -- MAXScript-Funktion. Eine eigene Funktion mit
            -- demselben Namen verdeckt sie, und jeder spaetere
            -- Aufruf der echten landet bei dieser hier.
            --
            -- Genau das ist passiert: der Szenendump rief
            -- "openFile out mode:\"a\"" auf, um die Logdatei zum
            -- Anhaengen zu oeffnen - und bekam stattdessen den
            -- Modell-Lader, der die .txt-Datei nicht als EG3D
            -- erkannte. Die Meldung "weder EG3D noch JUMPX" kam
            -- also aus dem Import, nicht aus dem Dump.
            -- Verteilt die Zusammenfassung auf zwei Zeilen, weil
            -- ein Label nicht umbricht. Getrennt wird am letzten
            -- Komma, das noch in die erste Zeile passt - so
            -- bleiben zusammengehoerende Angaben beieinander.
            fn setSummary sText =
            (
                lblSum1.text = ""
                lblSum2.text = ""
                if sText == undefined or sText == "" then return 0

                local iMax = 46
                if sText.count <= iMax then
                (
                    lblSum1.text = sText
                )
                else
                (
                    local iCut = 0
                    for i = 1 to (amin iMax sText.count) do
                        if sText[i] == "," then iCut = i

                    if iCut == 0 then iCut = iMax
                    lblSum1.text = trimRight (substring sText 1 iCut) " ,"
                    lblSum2.text = trimLeft \
                        (substring sText (iCut + 1) -1) " "
                )
                1
            )

            fn loadModelFile path =
            (
                if (Eg3dCpp.open path) == 0 then
                (
                    lblKind.text = ""
                    lblSum1.text = ""
                lblSum2.text = ""
                    btnImport.enabled = false
                    btnDump.enabled = false
                    messageBox (Eg3dCpp.lastError()) title:"EG3D Import"
                    false
                )
                else
                (
                    edFile.text = path
                    lblKind.text = "Format: " + (toUpper (Eg3dCpp.kind()))
                    setSummary (Eg3dCpp.summary())
                    btnImport.enabled = true
                    btnDump.enabled = true
                    lblStatus.text = ""
                    showWarnings()
                    true
                )
            )

            on btnBrowse pressed do
            (
                -- Dieser Dialog ist der verlaessliche Weg. Max'
                -- eigener Import-Dialog gibt .model bei
                -- "All formats" an den eingebauten CATIA-V4-
                -- Importer, weil der dieselbe Endung beansprucht.
                local path = getOpenFileName caption:"300 Heroes model" \
                    types:"300 Heroes (*.model;*.x)|*.model;*.x|All files (*.*)|*.*|"
                if path != undefined then loadModelFile path
            )

            on btnImport pressed do
            (
                pushOptions()
                lblStatus.text = "Importing..."

                -- undo off, weil der Import selbst schon
                -- theHold.Suspend() setzt. Doppelt schadet nicht,
                -- aber ein Undo ueber einen halben Import ist
                -- schlimmer als gar keines.
                local ok = 0
                undo off (ok = Eg3dCpp.import())

                if ok == 0 then
                (
                    lblStatus.text = "Import failed."
                    messageBox (Eg3dCpp.lastError()) title:"EG3D Import"
                )
                else
                (
                    local iNotes = 0
                    try (iNotes = buildNoteTrack())
                    catch
                    (
                        format "[EG3D] Note Track fehlgeschlagen: %\n" \
                            (getCurrentException())
                    )

                    local iLayers = 0
                    try (iLayers = buildLayers())
                    catch
                    (
                        format "[EG3D] Layer fehlgeschlagen: %\n" \
                            (getCurrentException())
                    )

                    local iMedit = 0
                    try (iMedit = populateMaterialEditor())
                    catch
                    (
                        format "[EG3D] Material Editor fehlgeschlagen: %\n" \
                            (getCurrentException())
                    )

                    lblStatus.text = Eg3dCpp.stats()
                    format "[EG3D] %\n" (Eg3dCpp.stats())
                    if iNotes > 0 then
                        format "[EG3D] Note Track: % Keys auf rootNode\n" iNotes
                    if iMedit > 0 then
                        format "[EG3D] Material Editor: % Slot(s) gefuellt\n" iMedit
                    if iLayers > 0 then
                        format "[EG3D] Layer: % Objekte einsortiert\n" iLayers
                    showWarnings()

                    -- Auf die Ruhelage stellen, nicht mitten in
                    -- eine Animation.
                    sliderTime = 0
                    max zoomext sel all
                )

                -- Die Protokollknoepfe werden auch bei einem
                -- FEHLGESCHLAGENEN Import freigeschaltet. Genau
                -- dann braucht man sie: die letzte Protokollzeile
                -- sagt, welcher Schritt nicht mehr gelaufen ist.
                btnLog.enabled = true
                btnSaveLog.enabled = true
            )

            on btnLog pressed do
            (
                local entries = Eg3dCpp.log()
                if entries == undefined or entries == "" then
                    messageBox "Kein Protokoll - noch nichts importiert." \
                        title:"EG3D Import"
                else
                (
                    format "\n[EG3D] ---- Protokoll ----\n%\n" entries
                    -- Der Listener oeffnet sich nicht von selbst,
                    -- und ein Protokoll, das niemand sieht, ist
                    -- keines.
                    actionMan.executeAction 0 "40472"
                )
            )

            -- ----------------------------------------------------
            --  Szenendump: was steht WIRKLICH in Max?
            --
            --  Das Protokoll sagt, was der Importer gerechnet
            --  hat. Diese Datei sagt, was danach in der Szene
            --  steht - je Bone die Matrix bei Frame 0 und bei
            --  einem Animationsframe.
            --
            --  Genau das fehlt bisher zur Diagnose: bei einer
            --  falschen Pose ist von aussen nicht zu sehen, ob
            --  der Fehler in der Rechnung liegt oder darin, wie
            --  Max die Matrix uebernommen hat.
            -- ----------------------------------------------------
            -- Schreibt in einen BEREITS OFFENEN Datei-Handle,
            -- damit Protokoll und Szenendump in derselben Datei
            -- landen.
            fn dumpSceneTo f animFrame =
            (
                if f == undefined then return 0

                local aBones = for o in objects where \
                    (isKindOf o BoneGeometry) or (isKindOf o Point) collect o

                format "Bones: %\n\n" aBones.count to:f

                for b in aBones do
                (
                    format "bone %\n" b.name to:f
                    format "  parent %\n" \
                        (if b.parent == undefined then "-" else b.parent.name) to:f

                    at time 0 (
                        local m = b.transform
                        format "  f0   [%] [%] [%] T[%]\n" \
                            m.row1 m.row2 m.row3 m.row4 to:f
                    )
                    at time animFrame (
                        local m = b.transform
                        format "  f%   [%] [%] [%] T[%]\n" \
                            animFrame m.row1 m.row2 m.row3 m.row4 to:f
                    )
                )

                aBones.count
            )

            -- ----------------------------------------------------
            --  EIN Log fuer alles
            --
            --  Vorher waren es drei Dateien - Protokoll,
            --  Warnungen und Szenendump getrennt. Zum Weiterreichen
            --  ist das unpraktisch, und beim Vergleichen zweier
            --  Laeufe verliert man den Ueberblick, welche Datei zu
            --  welchem Versuch gehoerte.
            --
            --  Jetzt schreibt EIN Knopf alles hintereinander in
            --  eine Datei: erst was der Importer gerechnet hat,
            --  dann was danach in der Szene steht.
            -- ----------------------------------------------------
            -- ACHTUNG: In einem Event-Handler (on ... pressed do)
            -- gibt es KEIN return - das ist keine Funktion. Der
            -- Ablauf wird deshalb ueber verschachtelte if
            -- gesteuert.
            --
            -- Genau daran ist der Log-Knopf in 0.2.9 gescheitert:
            -- das return brach den Handler nicht ab, sondern
            -- verhielt sich unerwartet, und die Fehlermeldung
            -- stammte noch aus einem frueheren open-Versuch.
            on btnSaveLog pressed do
            (
                local out = getSaveFileName caption:"Save log" \
                    types:"Text (*.txt)|*.txt|"

                if out != undefined then
                (
                    if (Eg3dCpp.saveLog out) == 0 then
                    (
                        messageBox (Eg3dCpp.lastError()) title:"EG3D Import"
                    )
                    else
                    (
                        -- Teil 2: der Szenendump, an dieselbe
                        -- Datei angehaengt.
                        local n = 0
                        try
                        (
                            -- mode:"a" - nicht "a+". Das liefert
                            -- false, und der naechste format-Aufruf
                            -- meldet "to: requires character
                            -- stream, got: false".
                            local f = openFile out mode:"a"
                            if f != undefined and f != false then
                            (
                                format "\n--- Szenendump (Frame % gegen Frame 0) ---\n" \
                                    spDumpFrame.value to:f
                                n = dumpSceneTo f spDumpFrame.value
                                close f
                            )
                        )
                        catch
                        (
                            format "[EG3D] Szenendump fehlgeschlagen: %\n" \
                                (getCurrentException())
                        )

                        lblStatus.text = ("Log written (" + (n as string) + \
                                          " bones).")
                    )
                )
            )

            on btnDump pressed do
            (
                local out = getSaveFileName caption:"Dump to" types:"Text (*.txt)|*.txt|"
                if out != undefined then
                (
                    if (Eg3dCpp.dump out) == 0 then
                        messageBox (Eg3dCpp.lastError()) title:"EG3D Import"
                    else
                        lblStatus.text = "Dump written."
                )
            )

            on Eg3dImportRollout open do
            (

                -- Wer ueber File -> Import gekommen ist, hat die
                -- Datei schon gewaehlt. Sie hier noch einmal
                -- auswaehlen zu lassen waere ein Klick, den
                -- niemand versteht.
                --
                -- pendingFile liefert den hinterlegten Pfad und
                -- loescht ihn dabei - sonst greift der naechste
                -- Aufruf ueber das Menue wieder zur Datei von
                -- vorhin.
                local pending = Eg3dCpp.pendingFile()
                if pending != undefined and pending != "" then
                    loadModelFile pending
                else if Eg3dCpp.isOpen() == 1 then
                (
                    edFile.text = ""
                    lblKind.text = "Format: " + (toUpper (Eg3dCpp.kind()))
                    setSummary (Eg3dCpp.summary())
                    btnImport.enabled = true
                    btnDump.enabled = true
                )
            )
        )

        createDialog Eg3dImportRollout
    )
)

cmake_minimum_required(VERSION 3.20)
project(eg3dimport-max LANGUAGES CXX)

# ------------------------------------------------------------
#  C++17, nicht 20 - dieselbe Begruendung wie bei XFBIN Import
#  und BAF Toolkit: C++17 uebersetzt auch gegen die Header der
#  aelteren Max-SDKs, C++20 nicht.
#
#  <charconv> wird ausdruecklich NICHT gebraucht. Die
#  Zahlenausgabe rechnet in eg3d_common.cpp selbst (siehe
#  FormatFloat dort), damit die Tie-Regel beim Runden in beiden
#  Sprachen im Quelltext steht statt in der jeweiligen
#  Laufzeitbibliothek. Nebeneffekt: keine Weiche fuer Toolsets
#  vor VS 2019 16.4, alle zwoelf Jahrgaenge gehen durch
#  denselben Code.
# ------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MAX_VERSION "0" CACHE STRING "3ds Max Jahrgang, z.B. 2016")

set(3DSMAX_SDK_DIR "C:/Program Files/Autodesk/3ds Max 2025 SDK/maxsdk"
    CACHE PATH "3ds Max SDK root (maxsdk-Ordner)")

option(EG3D_BUILD_TOOL   "Eigenstaendiges eg3ddump.exe mitbauen" ON)
option(EG3D_BUILD_PLUGIN "Max-Plugin bauen (braucht das Max SDK)" ON)
option(EG3D_STRICT       "Strenge Compiler-Warnungen (/W4 /permissive-)" ON)

# ============================================================
#  Gemeinsame Parser-Quellen
#
#  Bewusst als Liste statt als eigene Bibliothek: das Plugin
#  braucht /Zp8 (Struct-Packing des Max SDK), das Werkzeug nicht.
#  Eine gemeinsame Bibliothek muesste sich fuer eines von beiden
#  entscheiden.
#
#  KEINE Datei hier bindet einen Max-Header ein. Das ist der
#  Grund, warum ein Formatfehler in Sekunden auffaellt statt
#  nach zwoelf SDK-Builds.
# ============================================================
set(EG3D_PARSER_SOURCES
    src/eg3d_common.h
    src/eg3d_common.cpp
    src/eg3d_parallel.h
    src/eg3d_parallel.cpp
    src/eg3d_inflate.h
    src/eg3d_inflate.cpp
    src/eg3d_json.h
    src/eg3d_json.cpp
    src/eg3d_model.h
    src/eg3d_model.cpp
    src/eg3d_x.h
    src/eg3d_x.cpp
    src/eg3d_dump.h
    src/eg3d_dump.cpp
)

# ------------------------------------------------------------
#  Keine Kontraktion von Gleitkommaoperationen.
#
#  Der Compiler darf a*b+c zu einem einzigen FMA-Befehl
#  zusammenziehen. Das ist schneller UND genauer - es faellt nur
#  ein Zwischenrundungsschritt weg. Trotzdem unerwuenscht: die
#  Quantisierung (bias + roh * scale) und die Matrizenmultiplikation
#  bestehen genau aus solchen Summen, und ob kontrahiert wird,
#  haengt vom Compiler und den Zielbefehlssaetzen ab.
#
#  Bei BAF Toolkit gemessen: mit Kontraktion aendern sich 18 von
#  21.166 Zeilen. Der Vergleich gegen die Python-Referenz haette
#  dort Abweichungen gemeldet, an denen nichts falsch ist - und
#  die Suche danach waere lang geworden.
# ------------------------------------------------------------
if(MSVC)
    set(EG3D_FP_FLAGS /fp:precise)
else()
    set(EG3D_FP_FLAGS -ffp-contract=off)
endif()

# ============================================================
#  Plugin-Target -> Eg3dImport.dlu
# ============================================================
if(EG3D_BUILD_PLUGIN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/eg3dimport.cpp")

    if(NOT EXISTS "${3DSMAX_SDK_DIR}/include/max.h")
        message(FATAL_ERROR
            "3ds Max SDK nicht gefunden unter: ${3DSMAX_SDK_DIR}\n"
            "Entweder -D3DSMAX_SDK_DIR=... setzen oder mit "
            "-DEG3D_BUILD_PLUGIN=OFF nur das Werkzeug bauen.")
    endif()

    add_library(Eg3dImport SHARED
        ${EG3D_PARSER_SOURCES}
        src/eg3dimport.h
        src/eg3dimport.cpp
        src/eg3dimport_dll.cpp
        src/eg3dimport.rc
        eg3dimport.def
    )

    # Die Oberflaeche haengt als Ressource in der DLL, aber der
    # Pfad in der .rc ist relativ zur .rc selbst. Ohne das findet
    # der Ressourcencompiler scripts/Eg3dImport.mcr nicht, und die
    # DLL baut - nur ohne Oberflaeche darin.
    set_source_files_properties(src/eg3dimport.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Eg3dImport.mcr")

    set_target_properties(Eg3dImport PROPERTIES
        SUFFIX ".dlu" PREFIX "" OUTPUT_NAME "Eg3dImport")

    # 3ds Max laeuft ausschliesslich mit der dynamischen CRT (/MD).
    # Ein versehentliches /MT ergibt getrennte Heaps und damit
    # Abstuerze, die erst weit spaeter auffallen - Autodesk nennt
    # das in "Common Problems and Solutions" als Problem 5.
    set_property(TARGET Eg3dImport PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

    if(CMAKE_SIZEOF_VOID_P LESS 8)
        message(FATAL_ERROR "Eg3dImport laesst sich nur als x64 bauen (cmake -A x64).")
    endif()

    target_include_directories(Eg3dImport SYSTEM PRIVATE "${3DSMAX_SDK_DIR}/include")
    target_link_directories(Eg3dImport PRIVATE "${3DSMAX_SDK_DIR}/lib/x64/Release")

    target_link_libraries(Eg3dImport PRIVATE
        core.lib maxutil.lib bmm.lib Geom.lib mesh.lib
        Maxscrpt.lib MNMath.lib Paramblk2.lib comctl32.lib)

    target_compile_definitions(Eg3dImport PRIVATE
        _UNICODE UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

    if(MSVC)
        target_compile_options(Eg3dImport PRIVATE
            /EHsc /Zp8 /bigobj /utf-8 /Zc:__cplusplus /external:W0 ${EG3D_FP_FLAGS})

        # ------------------------------------------------------
        #  /permissive- nur, wenn die SDK-Header es aushalten.
        #
        #  Nicht der eigene Quelltext ist das Problem, sondern die
        #  Header von Autodesk: /Zc:strictStrings ergibt C2440,
        #  /Zc:rvalueCast ergibt C2102, dazu C2216 in winutil.h.
        #  Eine Jahresgrenze waere geraten - bei XFBIN Import
        #  bauten 2016-2018 mit dem Schalter durch und 2019 nicht.
        #  Also einmal ausprobieren und im Cache behalten.
        # ------------------------------------------------------
        set(_permissive_ok FALSE)
        if(EG3D_STRICT)
            include(CheckCXXSourceCompiles)
            set(CMAKE_REQUIRED_INCLUDES "${3DSMAX_SDK_DIR}/include")
            set(CMAKE_REQUIRED_FLAGS    "/permissive- /Zp8 /EHsc /bigobj")
            set(CMAKE_REQUIRED_DEFINITIONS
                -D_UNICODE -DUNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
            set(CMAKE_REQUIRED_QUIET TRUE)
            check_cxx_source_compiles(
                "#include <max.h>\nint main() { return 0; }"
                EG3D_SDK_LIKES_PERMISSIVE)
            unset(CMAKE_REQUIRED_INCLUDES)
            unset(CMAKE_REQUIRED_FLAGS)
            unset(CMAKE_REQUIRED_DEFINITIONS)
            unset(CMAKE_REQUIRED_QUIET)
            if(EG3D_SDK_LIKES_PERMISSIVE)
                set(_permissive_ok TRUE)
            endif()
        endif()

        if(EG3D_STRICT AND _permissive_ok)
            message(STATUS "SDK vertraegt /permissive- - Konformitaetspruefung an")
            target_compile_options(Eg3dImport PRIVATE /W4 /permissive- /wd4100 /wd4458)
        elseif(EG3D_STRICT)
            message(STATUS "SDK-Header vertragen /permissive- nicht - nur /W4")
            target_compile_options(Eg3dImport PRIVATE /W4 /wd4100 /wd4458)
        else()
            target_compile_options(Eg3dImport PRIVATE /W3)
        endif()

        target_link_options(Eg3dImport PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    endif()

elseif(EG3D_BUILD_PLUGIN)
    message(STATUS "src/eg3dimport.cpp fehlt - nur das Werkzeug wird gebaut")
endif()

# ============================================================
#  Werkzeug-Target -> eg3ddump.exe (ohne Max SDK)
# ============================================================
if(EG3D_BUILD_TOOL)
    add_executable(eg3ddump ${EG3D_PARSER_SOURCES} tools/eg3ddump_main.cpp)
    find_package(Threads REQUIRED)
    target_link_libraries(eg3ddump PRIVATE Threads::Threads)
    if(MSVC)
        set_property(TARGET eg3ddump PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
        target_compile_options(eg3ddump PRIVATE
            /EHsc /utf-8 /Zc:__cplusplus ${EG3D_FP_FLAGS})
        if(EG3D_STRICT)
            target_compile_options(eg3ddump PRIVATE /W4 /permissive-)
        endif()
        target_compile_definitions(eg3ddump PRIVATE
            _UNICODE UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    else()
        target_compile_options(eg3ddump PRIVATE -Wall -Wextra ${EG3D_FP_FLAGS})
    endif()
endif()

message(STATUS "Eg3dImport: Max ${MAX_VERSION}  Plugin=${EG3D_BUILD_PLUGIN}  "
               "Werkzeug=${EG3D_BUILD_TOOL}  Strict=${EG3D_STRICT}  C++${CMAKE_CXX_STANDARD}")

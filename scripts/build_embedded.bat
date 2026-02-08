@echo off
REM MoonLang Embedded Runtime Build Script
REM Builds minimal runtime for embedded targets
REM Copyright (c) 2026 greenteng.com

setlocal enabledelayedexpansion

echo.
echo === MoonLang Embedded Build System ===
echo.

REM Default target
set TARGET=embedded

REM Parse arguments
:parse_args
if "%1"=="" goto :start_build
if "%1"=="--target" (
    set TARGET=%2
    shift
    shift
    goto :parse_args
)
if "%1"=="-h" goto :show_help
if "%1"=="--help" goto :show_help
shift
goto :parse_args

:show_help
echo Usage: build_embedded.bat [--target TYPE]
echo.
echo Targets:
echo   native     Full-featured build (default desktop)
echo   embedded   Embedded Linux (no GUI)
echo   mcu        Minimal MCU build (no GUI/Network/DLL)
echo.
echo Examples:
echo   build_embedded.bat --target embedded
echo   build_embedded.bat --target mcu
echo.
goto :eof

:start_build
cd /d %~dp0..
REM Current directory is repository root

echo Building for target: %TARGET%
echo.

REM Set preprocessor defines based on target
set DEFINES=-DNDEBUG -DUNICODE -D_UNICODE

if "%TARGET%"=="mcu" (
    echo [MCU Mode] Minimal build - disabling GUI, Network, DLL, Async
    set DEFINES=%DEFINES% -DMOON_TARGET_MCU -DMOON_NO_GUI -DMOON_NO_NETWORK -DMOON_NO_DLL -DMOON_NO_ASYNC
    set OUTPUT_SUFFIX=_mcu
) else if "%TARGET%"=="embedded" (
    echo [Embedded Mode] Disabling GUI
    set DEFINES=%DEFINES% -DMOON_TARGET_EMBEDDED -DMOON_NO_GUI
    set OUTPUT_SUFFIX=_embedded
) else (
    echo [Native Mode] Full-featured build
    set DEFINES=%DEFINES% -DMOON_TARGET_NATIVE
    set OUTPUT_SUFFIX=
)

REM Create output directory
set OUT_DIR=build_%TARGET%
if not exist %OUT_DIR% mkdir %OUT_DIR%

echo.
echo === Compiling Runtime Objects ===
echo.

REM Compile moonrt.cpp with target-specific defines
echo Compiling moonrt.cpp for %TARGET%...
cl /c /O2 /EHsc /std:c++17 %DEFINES% ^
   /I"src\llvm" /I"webview2\include" ^
   src\llvm\moonrt.cpp /Fo"%OUT_DIR%\moonrt.obj"
if %errorlevel% neq 0 goto :error

REM Compile moonrt_async.cpp
echo Compiling moonrt_async.cpp...
cl /c /O2 /EHsc /std:c++17 %DEFINES% ^
   /I"src\llvm" ^
   src\llvm\moonrt_async.cpp /Fo"%OUT_DIR%\moonrt_async.obj"
if %errorlevel% neq 0 goto :error

REM Compile moonrt_channel.cpp
echo Compiling moonrt_channel.cpp...
cl /c /O2 /EHsc /std:c++17 %DEFINES% ^
   /I"src\llvm" ^
   src\llvm\moonrt_channel.cpp /Fo"%OUT_DIR%\moonrt_channel.obj"
if %errorlevel% neq 0 goto :error

REM Compile moonrt_regex.cpp (skip for MCU if desired)
if not "%TARGET%"=="mcu" (
    echo Compiling moonrt_regex.cpp...
    cl /c /O2 /EHsc /std:c++17 %DEFINES% ^
       /I"src\llvm" ^
       src\llvm\moonrt_regex.cpp /Fo"%OUT_DIR%\moonrt_regex.obj"
    if %errorlevel% neq 0 goto :error
)

REM Compile GUI only for native target
if "%TARGET%"=="native" (
    echo Compiling moonrt_gui.cpp...
    cl /c /O2 /EHsc /std:c++17 %DEFINES% ^
       /I"src\llvm" /I"webview2\include" ^
       src\llvm\moonrt_gui.cpp /Fo"%OUT_DIR%\moonrt_gui.obj"
    if %errorlevel% neq 0 goto :error
)

echo.
echo === Creating Static Library ===
echo.

REM Create static library
set LIB_NAME=libmoonrt%OUTPUT_SUFFIX%.lib
echo Creating %LIB_NAME%...

set OBJ_FILES=%OUT_DIR%\moonrt.obj %OUT_DIR%\moonrt_async.obj %OUT_DIR%\moonrt_channel.obj

if not "%TARGET%"=="mcu" (
    set OBJ_FILES=%OBJ_FILES% %OUT_DIR%\moonrt_regex.obj
)

if "%TARGET%"=="native" (
    set OBJ_FILES=%OBJ_FILES% %OUT_DIR%\moonrt_gui.obj
)

lib /nologo /OUT:%OUT_DIR%\%LIB_NAME% %OBJ_FILES%
if %errorlevel% neq 0 goto :error

echo.
echo === Build Complete ===
echo.
echo Output: %OUT_DIR%\%LIB_NAME%
echo Target: %TARGET%
echo.

REM Show what's included
echo Features included:
echo   - Core runtime: Yes
echo   - String/List/Dict: Yes
echo   - Math functions: Yes
echo   - File operations: Yes

if "%TARGET%"=="native" (
    echo   - GUI ^(WebView2^): Yes
    echo   - Network ^(TCP/UDP^): Yes
    echo   - DLL loading: Yes
    echo   - Regular expressions: Yes
) else if "%TARGET%"=="embedded" (
    echo   - GUI ^(WebView2^): No
    echo   - Network ^(TCP/UDP^): Yes
    echo   - DLL loading: Yes
    echo   - Regular expressions: Yes
) else if "%TARGET%"=="mcu" (
    echo   - GUI ^(WebView2^): No
    echo   - Network ^(TCP/UDP^): No
    echo   - DLL loading: No
    echo   - Regular expressions: Optional
)

echo.
goto :eof

:error
echo.
echo Build failed!
exit /b 1

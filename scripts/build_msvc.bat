@echo off
REM Build MoonScript Runtime with MSVC
REM Creates moonrt.lib with full WebView2 support

setlocal enabledelayedexpansion

echo.
echo === Building MoonScript Runtime (MSVC) ===
echo.

REM Setup MSVC environment
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %errorlevel% neq 0 (
    echo Error: Could not setup MSVC environment
    exit /b 1
)

cd /d %~dp0..
REM Current directory is repository root

REM Create output directory
if not exist "msvc_build" mkdir msvc_build

REM Compile runtime files (use /MD for dynamic CRT to match user programs)
echo Compiling moonrt.cpp...
cl /c /O2 /EHsc /std:c++17 /MD /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"src\llvm" /I"webview2\include" ^
   src\llvm\moonrt.cpp /Fo"msvc_build\moonrt.obj"
if %errorlevel% neq 0 goto :error

echo Compiling moonrt_async.cpp...
cl /c /O2 /EHsc /std:c++17 /MD /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"src\llvm" ^
   src\llvm\moonrt_async.cpp /Fo"msvc_build\moonrt_async.obj"
if %errorlevel% neq 0 goto :error

echo Compiling moonrt_channel.cpp...
cl /c /O2 /EHsc /std:c++17 /MD /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"src\llvm" ^
   src\llvm\moonrt_channel.cpp /Fo"msvc_build\moonrt_channel.obj"
if %errorlevel% neq 0 goto :error

echo Compiling moonrt_gui.cpp (with WebView2)...
cl /c /O2 /EHsc /std:c++17 /MD /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"src\llvm" /I"webview2\include" ^
   src\llvm\moonrt_gui.cpp /Fo"msvc_build\moonrt_gui.obj"
if %errorlevel% neq 0 goto :error

echo Compiling moonrt_regex.cpp...
cl /c /O2 /EHsc /std:c++17 /MD /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"src\llvm" ^
   src\llvm\moonrt_regex.cpp /Fo"msvc_build\moonrt_regex.obj"
if %errorlevel% neq 0 goto :error

REM Create static library
echo Creating moonrt.lib...
lib /nologo /OUT:moonrt.lib ^
    msvc_build\moonrt.obj ^
    msvc_build\moonrt_async.obj ^
    msvc_build\moonrt_channel.obj ^
    msvc_build\moonrt_gui.obj
if %errorlevel% neq 0 goto :error

echo.
echo === Runtime Build Successful ===
echo Output: moonrt.lib
echo.

REM Cleanup
rmdir /s /q msvc_build 2>nul

endlocal
exit /b 0

:error
echo.
echo Build failed!
rmdir /s /q msvc_build 2>nul
endlocal
exit /b 1

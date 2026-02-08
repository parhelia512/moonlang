@echo off
REM Build MoonScript GUI DLL for Python
REM Creates moon_gui.dll with WebView2 support

setlocal enabledelayedexpansion

echo.
echo === Building MoonScript GUI DLL ===
echo.

REM Setup MSVC environment
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %errorlevel% neq 0 (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
    if %errorlevel% neq 0 (
        echo Error: Could not setup MSVC environment
        exit /b 1
    )
)

cd /d %~dp0

echo Compiling resource file...
rc /nologo moon_gui_dll.rc
if %errorlevel% neq 0 (
    echo Warning: Resource compilation failed, continuing without version info
)

echo Compiling moon_gui_dll.cpp...
cl /LD /O2 /EHsc /std:c++17 /MD /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"webview2\include" ^
   moon_gui_dll.cpp moon_gui_dll.res ^
   /link /OUT:moon_gui.dll ^
   user32.lib gdi32.lib ole32.lib dwmapi.lib shcore.lib advapi32.lib ^
   "webview2\lib\x64\WebView2LoaderStatic.lib"

if %errorlevel% neq 0 (
    echo.
    echo Build failed!
    exit /b 1
)

echo.
echo === Build Successful ===
echo Output: moon_gui.dll
echo.

REM Copy to python_gui folder
echo Copying to python_gui folder...
copy /Y moon_gui.dll python_gui\moon_gui.dll >nul
if %errorlevel% neq 0 (
    echo Warning: Could not copy to python_gui folder
) else (
    echo Copied to python_gui\moon_gui.dll
)

REM Cleanup
if exist moon_gui_dll.obj del moon_gui_dll.obj
if exist moon_gui_dll.exp del moon_gui_dll.exp
if exist moon_gui_dll.lib del moon_gui_dll.lib
if exist moon_gui_dll.res del moon_gui_dll.res

echo.
echo Done!

endlocal
exit /b 0

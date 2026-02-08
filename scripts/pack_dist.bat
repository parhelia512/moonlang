@echo off
REM Pack MoonScript Compiler for distribution (Standalone MSVC)
REM Includes link.exe, rc.exe and all required libraries

setlocal enabledelayedexpansion

set DIST_DIR=dist\moonscript

echo.
echo === Packing MoonScript Distribution (Standalone) ===
echo.

REM Setup MSVC environment to get paths
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %errorlevel% neq 0 (
    echo Error: Could not setup MSVC environment
    exit /b 1
)

cd /d %~dp0..
REM From here current directory is repository root

REM Create distribution directory
if exist dist rmdir /s /q dist
mkdir %DIST_DIR%
mkdir %DIST_DIR%\lib

echo.
echo === Copying Compiler ===
copy moonc.exe %DIST_DIR%\
if %errorlevel% neq 0 goto :error

REM Copy package manager
if exist mpkg.exe (
    echo Copying mpkg.exe...
    copy mpkg.exe %DIST_DIR%\
)

REM Copy uninstall and icon
if exist uninstall.exe (
    echo Copying uninstall.exe...
    copy uninstall.exe %DIST_DIR%\
)
if exist icon.ico (
    echo Copying icon.ico...
    copy icon.ico %DIST_DIR%\
)

REM Copy DLL dependencies
if exist ServicingCommon.dll (
    echo Copying ServicingCommon.dll...
    copy ServicingCommon.dll %DIST_DIR%\
)
if exist sqlite3.dll (
    echo Copying sqlite3.dll...
    copy sqlite3.dll %DIST_DIR%\
)

REM Copy runtime library (from src\llvm where rebuild creates it)
if exist src\llvm\moonrt.lib (
    copy src\llvm\moonrt.lib %DIST_DIR%\
) else if exist moonrt.lib (
    copy moonrt.lib %DIST_DIR%\
) else (
    echo Warning: moonrt.lib not found!
)

REM Copy WebView2 static library
if exist "webview2\lib\x64\WebView2LoaderStatic.lib" (
    echo Copying WebView2LoaderStatic.lib...
    copy "webview2\lib\x64\WebView2LoaderStatic.lib" %DIST_DIR%\
)

echo.
echo === Copying MSVC Tools ===

REM Copy link.exe and its dependencies
set MSVC_BIN=%VCToolsInstallDir%bin\Hostx64\x64
echo Copying from %MSVC_BIN%...
copy "%MSVC_BIN%\link.exe" %DIST_DIR%\
copy "%MSVC_BIN%\cvtres.exe" %DIST_DIR%\ 2>nul
copy "%MSVC_BIN%\mspdbcore.dll" %DIST_DIR%\ 2>nul
copy "%MSVC_BIN%\mspdb140.dll" %DIST_DIR%\ 2>nul
copy "%MSVC_BIN%\msobj140.dll" %DIST_DIR%\ 2>nul
copy "%MSVC_BIN%\tbbmalloc.dll" %DIST_DIR%\ 2>nul

REM Copy rc.exe
set SDK_BIN=%WindowsSdkDir%bin\%WindowsSDKVersion%x64
echo Copying from %SDK_BIN%...
copy "%SDK_BIN%\rc.exe" %DIST_DIR%\
copy "%SDK_BIN%\rcdll.dll" %DIST_DIR%\ 2>nul

echo.
echo === Copying Libraries ===

REM Copy MSVC libraries (both static and dynamic CRT)
set MSVC_LIB=%VCToolsInstallDir%lib\x64
echo Copying MSVC libs from %MSVC_LIB%...
REM Static CRT (for compatibility)
copy "%MSVC_LIB%\libcmt.lib" %DIST_DIR%\lib\ 2>nul
copy "%MSVC_LIB%\libvcruntime.lib" %DIST_DIR%\lib\ 2>nul
copy "%MSVC_LIB%\libcpmt.lib" %DIST_DIR%\lib\ 2>nul
REM Dynamic CRT (default, smaller size)
copy "%MSVC_LIB%\msvcrt.lib" %DIST_DIR%\lib\
copy "%MSVC_LIB%\vcruntime.lib" %DIST_DIR%\lib\ 2>nul
copy "%MSVC_LIB%\msvcprt.lib" %DIST_DIR%\lib\ 2>nul
copy "%MSVC_LIB%\oldnames.lib" %DIST_DIR%\lib\ 2>nul

REM Copy Windows SDK ucrt libraries
set UCRT_LIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%ucrt\x64
echo Copying UCRT libs from %UCRT_LIB%...
copy "%UCRT_LIB%\libucrt.lib" %DIST_DIR%\lib\ 2>nul
copy "%UCRT_LIB%\ucrt.lib" %DIST_DIR%\lib\ 2>nul

REM Copy dynamic CRT from Windows SDK (msvcrt.lib is usually in um directory)
REM Also try MSVC lib directory (already copied above)
set UM_LIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x64
if exist "%UM_LIB%\msvcrt.lib" (
    copy "%UM_LIB%\msvcrt.lib" %DIST_DIR%\lib\ 2>nul
)

REM Copy Windows SDK um libraries (system libs)
set UM_LIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x64
echo Copying Windows SDK libs from %UM_LIB%...
copy "%UM_LIB%\kernel32.lib" %DIST_DIR%\lib\
copy "%UM_LIB%\user32.lib" %DIST_DIR%\lib\
copy "%UM_LIB%\ws2_32.lib" %DIST_DIR%\lib\
copy "%UM_LIB%\shell32.lib" %DIST_DIR%\lib\
copy "%UM_LIB%\advapi32.lib" %DIST_DIR%\lib\
copy "%UM_LIB%\shlwapi.lib" %DIST_DIR%\lib\
copy "%UM_LIB%\ole32.lib" %DIST_DIR%\lib\
copy "%UM_LIB%\gdi32.lib" %DIST_DIR%\lib\
copy "%UM_LIB%\uuid.lib" %DIST_DIR%\lib\
copy "%UM_LIB%\dwmapi.lib" %DIST_DIR%\lib\
copy "%UM_LIB%\crypt32.lib" %DIST_DIR%\lib\

REM Copy OpenSSL libraries if available (for TLS support)
if exist "lib\openssl\lib\libssl.lib" (
    echo Copying OpenSSL libs...
    copy "lib\openssl\lib\libssl.lib" %DIST_DIR%\lib\
    copy "lib\openssl\lib\libcrypto.lib" %DIST_DIR%\lib\
)

REM Copy OpenSSL DLLs (required at runtime for TLS)
if exist "libcrypto-3-x64.dll" (
    echo Copying OpenSSL DLLs...
    copy "libcrypto-3-x64.dll" %DIST_DIR%\
)
if exist "libssl-3-x64.dll" (
    copy "libssl-3-x64.dll" %DIST_DIR%\
)

REM Copy stdlib for development/testing
echo.
echo === Copying Standard Library ===
if exist "stdlib" (
    xcopy /E /I /Y "stdlib" "%DIST_DIR%\stdlib\"
    echo Copied stdlib to distribution.
) else (
    echo Warning: stdlib directory not found
)

echo.
echo === Creating README ===
(
echo MoonScript Native Compiler v1.0.0
echo =================================
echo.
echo A standalone MoonScript to native executable compiler.
echo No additional dependencies required - just extract and use!
echo.
echo Usage:
echo   moonc input.moon -o output.exe
echo   mpkg install ^<package^>
echo.
echo Compiler Options:
echo   -o ^<file^>             Output file name
echo   --icon ^<file^>         Icon file for window/taskbar/tray
echo   --company ^<name^>      Company name
echo   --copyright ^<text^>    Copyright notice
echo   --description ^<text^>  File description
echo   --file-version ^<ver^>  Version ^(e.g. 1.0.0.0^)
echo   --product-name ^<name^> Product name
echo   -h, --help            Show help
echo.
echo Package Manager ^(mpkg^):
echo   mpkg search ^<query^>       Search packages
echo   mpkg install ^<name^>       Install a package
echo   mpkg installed             List installed packages
echo   mpkg update ^<name^>        Update a package
echo   mpkg uninstall ^<name^>     Uninstall a package
echo   mpkg init ^<name^>          Initialize new package
echo   mpkg publish ^<file^>       Publish package to registry
echo.
echo Standard Library:
echo   from "stdlib/io.moon" import File
echo   from "stdlib/net.moon" import TcpClient
echo   from "stdlib/http.moon" import HttpServer
echo   from "stdlib/tls.moon" import TlsClient
echo   from "stdlib/https.moon" import HttpsServer
echo   from "stdlib/gui.moon" import Window
echo.
echo GUI Support ^(WebView2^):
echo   - DPI aware ^(auto scaling^)
echo   - Centered window by default
echo   - Transparent background ^(Electron-style^)
echo   - System tray support
echo   - CSS drag support ^(-webkit-app-region: drag^)
echo   - No console window for GUI apps
echo   - Custom icon support
echo.
echo Window Options: center, x, y, frameless, transparent,
echo                 topmost, resizable, alpha, clickThrough
echo.
echo System Tray: gui_tray_create, gui_tray_set_menu,
echo              gui_tray_on_click, gui_tray_remove
echo.
echo JS API: MoonGUI.send, close, minimize, maximize, drag
echo.
echo Requires WebView2 Runtime ^(Win11 built-in, Win10 download^)
echo.
echo Copyright ^(c^) 2026 greenteng.com
) > %DIST_DIR%\README.txt

echo.
echo === Distribution Package Created ===
echo Location: %DIST_DIR%
echo.
dir /b %DIST_DIR%
echo.
echo lib/:
dir /b %DIST_DIR%\lib
echo.

REM Create ZIP
echo Creating ZIP archive...
powershell -command "Compress-Archive -Path '%DIST_DIR%\*' -DestinationPath 'dist\moonlang.zip' -Force"

echo.
echo === Done! ===
echo ZIP: dist\moonlang.zip
echo.

endlocal
exit /b 0

:error
echo.
echo Packing failed!
endlocal
exit /b 1

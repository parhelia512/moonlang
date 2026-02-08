@echo off
echo Linking with LLVM libraries...

REM Check if resource file exists
set RES_FILE=
if exist src\llvm\moonc_llvm.res (
    set RES_FILE=src\llvm\moonc_llvm.res
    echo Including resource file with icon and version info...
)

REM Check if PCRE2 library exists
set PCRE2_LIB=
if exist "lib\pcre2\build\pcre2.lib" (
    set "PCRE2_LIB=lib\pcre2\build\pcre2.lib"
    echo Including PCRE2 library for regex support...
)

link /nologo /OUT:moonc.exe src\llvm\moonc_llvm.obj src\llvm\moonrt.obj src\llvm\moonrt_async.obj src\llvm\moonrt_channel.obj src\llvm\moonrt_gui.obj src\llvm\moonrt_regex.obj src\frontend\alias_loader.obj src\frontend\lexer.obj src\frontend\parser.obj src\llvm\llvm_codegen.obj %RES_FILE% %PCRE2_LIB% @llvm_libs.rsp kernel32.lib user32.lib shell32.lib ole32.lib uuid.lib advapi32.lib ws2_32.lib ntdll.lib version.lib shlwapi.lib gdi32.lib "webview2\lib\x64\WebView2LoaderStatic.lib"

if %errorlevel% neq 0 (
    echo Linking failed!
    exit /b 1
)

echo Successfully created moonc.exe
echo   Icon: icon.ico
echo   Copyright: greenteng.com

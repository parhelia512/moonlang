@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d %~dp0..
REM Current directory is repository root
set LLVM_DIR=C:\LLVM-dev\clang+llvm-21.1.8-x86_64-pc-windows-msvc
set LLVM_INCLUDE=%LLVM_DIR%\include

echo === Compiling moonrt.obj ===
cl /nologo /EHsc /std:c++17 /O2 /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /c src\llvm\moonrt.cpp /Fo:src\llvm\moonrt.obj /I. /Isrc\llvm /I"%LLVM_INCLUDE%"
if errorlevel 1 exit /b 1

echo === Compiling moonrt_async.obj ===
cl /nologo /EHsc /std:c++17 /O2 /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /c src\llvm\moonrt_async.cpp /Fo:src\llvm\moonrt_async.obj /I. /Isrc\llvm
if errorlevel 1 exit /b 1

echo === Compiling moonrt_channel.obj ===
cl /nologo /EHsc /std:c++17 /O2 /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /c src\llvm\moonrt_channel.cpp /Fo:src\llvm\moonrt_channel.obj /I. /Isrc\llvm
if errorlevel 1 exit /b 1

echo === Compiling moonrt_gui.obj ===
cl /nologo /EHsc /std:c++17 /O2 /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /c src\llvm\moonrt_gui.cpp /Fo:src\llvm\moonrt_gui.obj /I. /Isrc\llvm
if errorlevel 1 exit /b 1

echo === Compiling moonrt_regex.obj ===
cl /nologo /EHsc /std:c++17 /O2 /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /c src\llvm\moonrt_regex.cpp /Fo:src\llvm\moonrt_regex.obj /I. /Isrc\llvm
if errorlevel 1 exit /b 1

echo === Compiling frontend (lexer, parser, alias_loader) ===
cl /nologo /EHsc /std:c++17 /O2 /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /c src\frontend\lexer.cpp /Fo:src\frontend\lexer.obj /Isrc\frontend
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /O2 /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /c src\frontend\parser.cpp /Fo:src\frontend\parser.obj /Isrc\frontend
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /O2 /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /c src\frontend\alias_loader.cpp /Fo:src\frontend\alias_loader.obj /Isrc\frontend
if errorlevel 1 exit /b 1

echo === Compiling llvm_codegen.obj ===
cl /nologo /EHsc /std:c++17 /O2 /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /c src\llvm\llvm_codegen.cpp /Fo:src\llvm\llvm_codegen.obj /I. /Isrc\llvm /Isrc\frontend /I"%LLVM_INCLUDE%"
if errorlevel 1 exit /b 1

echo === Compiling moonc_llvm.obj ===
cl /nologo /EHsc /std:c++17 /O2 /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /c src\llvm\moonc_llvm.cpp /Fo:src\llvm\moonc_llvm.obj /I. /Isrc\llvm /Isrc\frontend /I"%LLVM_INCLUDE%"
if errorlevel 1 exit /b 1

echo === Compiling resource file ===
rc /nologo /fo src\llvm\moonc_llvm.res src\llvm\moonc_llvm.rc
if errorlevel 1 (
    echo Warning: Resource compilation failed, continuing without icon...
) else (
    echo Resource file compiled successfully
)

echo === Linking ===
call scripts\link_llvm.cmd

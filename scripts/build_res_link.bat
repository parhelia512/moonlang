@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d %~dp0..
REM Current directory is repository root

echo === Compiling resource file ===
rc /nologo /fo src\llvm\moonc_llvm.res src\llvm\moonc_llvm.rc
if errorlevel 1 (
    echo Warning: Resource compilation failed, continuing without icon...
) else (
    echo Resource file compiled successfully
)

echo === Linking ===
call scripts\link_llvm.cmd

@echo off
REM Complete rebuild of MoonScript compiler and runtime with MSVC
REM This rebuilds everything from scratch including PCRE2 and OpenSSL
REM
REM 模块化运行时结构:
REM   moonrt.cpp        - 统一入口 (包含所有核心模块)
REM   moonrt_regex.cpp  - 正则表达式 (使用PCRE2)
REM   moonrt_tls.cpp    - TLS/SSL支持 (使用OpenSSL)
REM   moonrt_async.cpp  - 异步支持
REM   moonrt_channel.cpp - Channel支持
REM   moonrt_gui.cpp    - GUI支持

setlocal enabledelayedexpansion

echo.
echo === Complete MoonScript Rebuild (MSVC) ===
echo.

REM Setup MSVC environment
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %errorlevel% neq 0 (
    echo Error: Could not setup MSVC environment
    exit /b 1
)

cd /d %~dp0..
REM From here current directory is repository root (src/, lib/, scripts/ are here)

REM Configuration
set LLVM_DIR=C:\LLVM-dev\clang+llvm-21.1.8-x86_64-pc-windows-msvc
set PCRE2_DIR=lib\pcre2
set OPENSSL_DIR=lib\openssl
set USE_PCRE2=0
set USE_OPENSSL=0

REM Check for OpenSSL
if exist "%OPENSSL_DIR%\lib\libssl.lib" (
    if exist "%OPENSSL_DIR%\include\openssl\ssl.h" (
        echo OpenSSL found - enabling TLS support
        set USE_OPENSSL=1
    )
) else (
    echo OpenSSL not found - TLS support disabled
    echo To enable TLS, install OpenSSL libraries to lib\openssl\
    echo See lib\openssl\README.md for instructions
)

REM Check for PCRE2
if exist "%PCRE2_DIR%\src\pcre2.h" (
    echo PCRE2 found - enabling PCRE2 regex support
    set USE_PCRE2=1
) else (
    echo PCRE2 not found - using std::regex fallback
    echo To enable PCRE2, run: lib\pcre2\download_pcre2.bat
)

REM Generate version.h from version.json
echo.
echo === Generating version.h from version.json ===
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\gen_version.ps1"

REM Step 0: Build PCRE2 if available
if %USE_PCRE2%==1 (
    echo.
    echo === Step 0: Building PCRE2 ===
    
    REM Create build directory
    if not exist "%PCRE2_DIR%\build" mkdir "%PCRE2_DIR%\build"
    
    REM Compile PCRE2 source files (PCRE2 10.45+ has additional files)
    set PCRE2_OBJS=
    for %%F in (
        pcre2_auto_possess
        pcre2_chartables
        pcre2_chkdint
        pcre2_compile
        pcre2_compile_class
        pcre2_compile_cgroup
        pcre2_config
        pcre2_context
        pcre2_convert
        pcre2_dfa_match
        pcre2_error
        pcre2_extuni
        pcre2_find_bracket
        pcre2_jit_compile
        pcre2_maketables
        pcre2_match
        pcre2_match_data
        pcre2_match_next
        pcre2_newline
        pcre2_ord2utf
        pcre2_pattern_info
        pcre2_script_run
        pcre2_serialize
        pcre2_string_utils
        pcre2_study
        pcre2_substitute
        pcre2_substring
        pcre2_tables
        pcre2_ucd
        pcre2_valid_utf
        pcre2_xclass
    ) do (
        if exist "%PCRE2_DIR%\src\%%F.c" (
            echo Compiling %%F.c...
            cl /c /O2 /DNDEBUG /DPCRE2_CODE_UNIT_WIDTH=8 /DPCRE2_STATIC /DHAVE_CONFIG_H /DSUPPORT_UNICODE ^
               /I"%PCRE2_DIR%\src" ^
               "%PCRE2_DIR%\src\%%F.c" /Fo"%PCRE2_DIR%\build\%%F.obj" >nul 2>&1
            if %errorlevel% neq 0 (
                echo Warning: Failed to compile %%F.c
            ) else (
                set "PCRE2_OBJS=!PCRE2_OBJS! %PCRE2_DIR%\build\%%F.obj"
            )
        )
    )
    
    REM Create PCRE2 static library
    if defined PCRE2_OBJS (
        echo Creating pcre2.lib...
        lib /OUT:"%PCRE2_DIR%\build\pcre2.lib" !PCRE2_OBJS! >nul 2>&1
        if %errorlevel% neq 0 (
            echo Warning: Failed to create pcre2.lib - falling back to std::regex
            set USE_PCRE2=0
        )
    )
)

REM Step 1: Build runtime library
echo.
echo === Step 1: Building Runtime Library ===

REM Set compiler flags based on PCRE2 availability
set REGEX_FLAGS=
if %USE_PCRE2%==1 (
    set "REGEX_FLAGS=/I%PCRE2_DIR%\src /DPCRE2_STATIC"
) else (
    set "REGEX_FLAGS=/DMOON_REGEX_STD"
)

echo Compiling moonrt.cpp (unified entry - includes all modules)...
cl /c /O2 /EHsc /std:c++17 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"src\llvm" /I"webview2\include" ^
   src\llvm\moonrt.cpp /Fo"src\llvm\moonrt.obj"
if %errorlevel% neq 0 goto :error

echo Compiling moonrt_async.cpp...
cl /c /O2 /EHsc /std:c++17 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"src\llvm" ^
   src\llvm\moonrt_async.cpp /Fo"src\llvm\moonrt_async.obj"
if %errorlevel% neq 0 goto :error

echo Compiling moonrt_channel.cpp...
cl /c /O2 /EHsc /std:c++17 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"src\llvm" ^
   src\llvm\moonrt_channel.cpp /Fo"src\llvm\moonrt_channel.obj"
if %errorlevel% neq 0 goto :error

echo Compiling moonrt_gui.cpp...
cl /c /O2 /EHsc /std:c++17 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"src\llvm" /I"webview2\include" ^
   src\llvm\moonrt_gui.cpp /Fo"src\llvm\moonrt_gui.obj"
if %errorlevel% neq 0 goto :error

echo Compiling moonrt_regex.cpp...
cl /c /O2 /EHsc /std:c++17 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"src\llvm" %REGEX_FLAGS% ^
   src\llvm\moonrt_regex.cpp /Fo"src\llvm\moonrt_regex.obj"
if %errorlevel% neq 0 goto :error

REM Compile TLS module if OpenSSL is available
set TLS_FLAGS=/DMOON_NO_TLS
set TLS_LIBS=
if %USE_OPENSSL%==1 goto :compile_tls_with_openssl
goto :compile_tls_stub

:compile_tls_with_openssl
echo Compiling moonrt_tls.cpp...
cl /c /O2 /EHsc /std:c++17 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"src\llvm" /I"%OPENSSL_DIR%\include" /DMOON_HAS_TLS ^
   src\llvm\moonrt_tls.cpp /Fo"src\llvm\moonrt_tls.obj"
if %errorlevel% neq 0 (
    echo Warning: Failed to compile TLS module - TLS support disabled
    set USE_OPENSSL=0
    goto :compile_tls_stub
)
set TLS_FLAGS=/DMOON_HAS_TLS
set "TLS_LIBS=%OPENSSL_DIR%\lib\libssl.lib %OPENSSL_DIR%\lib\libcrypto.lib"
goto :tls_done

:compile_tls_stub
echo Compiling moonrt_tls.cpp (stub)...
cl /c /O2 /EHsc /std:c++17 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"src\llvm" /DMOON_NO_TLS ^
   src\llvm\moonrt_tls.cpp /Fo"src\llvm\moonrt_tls.obj"
if %errorlevel% neq 0 goto :error

:tls_done

echo Compiling moonrt_ffi.cpp...
cl /c /O2 /EHsc /std:c++17 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"src\llvm" ^
   src\llvm\moonrt_ffi.cpp /Fo"src\llvm\moonrt_ffi.obj"
if %errorlevel% neq 0 goto :error

echo Compiling moonrt_ffi_parser.cpp...
cl /c /O2 /EHsc /std:c++17 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"src\llvm" ^
   src\llvm\moonrt_ffi_parser.cpp /Fo"src\llvm\moonrt_ffi_parser.obj"
if %errorlevel% neq 0 goto :error

echo Compiling moonrt_ffi_callback.cpp...
cl /c /O2 /EHsc /std:c++17 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"src\llvm" ^
   src\llvm\moonrt_ffi_callback.cpp /Fo"src\llvm\moonrt_ffi_callback.obj"
if %errorlevel% neq 0 goto :error

REM Create runtime library
echo Creating moonrt.lib...
set RT_OBJS=src\llvm\moonrt.obj src\llvm\moonrt_async.obj src\llvm\moonrt_channel.obj src\llvm\moonrt_gui.obj src\llvm\moonrt_regex.obj src\llvm\moonrt_tls.obj src\llvm\moonrt_ffi.obj src\llvm\moonrt_ffi_parser.obj src\llvm\moonrt_ffi_callback.obj
set RT_LIBS=
if %USE_PCRE2%==1 (
    set "RT_LIBS=%RT_LIBS% %PCRE2_DIR%\build\pcre2.lib"
)
if %USE_OPENSSL%==1 (
    set "RT_LIBS=%RT_LIBS% %TLS_LIBS%"
)
if defined RT_LIBS (
    lib /OUT:src\llvm\moonrt.lib %RT_OBJS% %RT_LIBS%
) else (
    lib /OUT:src\llvm\moonrt.lib %RT_OBJS%
)
if %errorlevel% neq 0 goto :error

REM Step 2: Compile compiler components
echo.
echo === Step 2: Compiling Compiler Components ===

echo Compiling frontend (alias_loader, lexer, parser)...
cl /c /O2 /EHsc /std:c++17 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE /I"src\frontend" src\frontend\alias_loader.cpp /Fo"src\frontend\alias_loader.obj"
if %errorlevel% neq 0 goto :error
cl /c /O2 /EHsc /std:c++17 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE /I"src\frontend" src\frontend\lexer.cpp /Fo"src\frontend\lexer.obj"
if %errorlevel% neq 0 goto :error
cl /c /O2 /EHsc /std:c++17 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE /I"src\frontend" src\frontend\parser.cpp /Fo"src\frontend\parser.obj"
if %errorlevel% neq 0 goto :error

echo Compiling llvm_codegen.cpp...
cl /c /O2 /EHsc /std:c++17 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"%LLVM_DIR%\include" ^
   /I"src\llvm" ^
   /I"src\frontend" ^
   src\llvm\llvm_codegen.cpp /Fo"src\llvm\llvm_codegen.obj"
if %errorlevel% neq 0 goto :error

echo Compiling moonc_llvm.cpp...
cl /c /O2 /EHsc /std:c++17 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"%LLVM_DIR%\include" ^
   /I"src\llvm" ^
   /I"src\frontend" ^
   src\llvm\moonc_llvm.cpp /Fo"src\llvm\moonc_llvm.obj"
if %errorlevel% neq 0 goto :error

REM Compile resource file
echo Compiling resource file...
rc /nologo /fo src\llvm\moonc_llvm.res src\llvm\moonc_llvm.rc 2>nul
if %errorlevel% neq 0 (
    echo Warning: Resource compilation failed, continuing without icon...
)

REM Step 3: Link compiler
echo.
echo === Step 3: Linking Compiler ===
call scripts\link_llvm.cmd
if %errorlevel% neq 0 goto :error

REM Step 4: Package distribution
echo.
echo === Step 4: Packaging Distribution ===
call scripts\pack_dist.bat
if %errorlevel% neq 0 goto :error

echo.
echo === Build Complete! ===
echo.
echo Output:
echo   moonc.exe      - Compiler
echo   moonrt.lib     - Runtime library
echo   dist\moonscript.zip - Distribution package
echo.
echo Runtime modules:
echo   moonrt_core.cpp    - Core (type system, memory, refcount)
echo   moonrt_math.cpp    - Math operations
echo   moonrt_string.cpp  - String operations
echo   moonrt_list.cpp    - List operations
echo   moonrt_dict.cpp    - Dictionary operations
echo   moonrt_builtin.cpp - Built-in functions
echo   moonrt_io.cpp      - File I/O
echo   moonrt_json.cpp    - JSON support
echo   moonrt_network.cpp - Network support
echo   moonrt_dll.cpp     - DLL loading
echo   moonrt_regex.cpp   - Regex (PCRE2=%USE_PCRE2%)
echo   moonrt_tls.cpp     - TLS/SSL (OpenSSL=%USE_OPENSSL%)
echo.

endlocal
exit /b 0

:error
echo.
echo Build failed!
endlocal
exit /b 1

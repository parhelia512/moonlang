// MoonLang LLVM Code Generator - Emission Module
// IR/bitcode/object/executable emission functions
// Copyright (c) 2026 greenteng.com

// Note: This file is included by llvm_codegen.cpp and should not be compiled separately.

// ============================================================================
// Static Helper Functions
// ============================================================================

// Get the directory where this executable is located
static std::string getExeDirectory() {
    namespace fs = std::filesystem;
#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string exePath(path);
    size_t lastSlash = exePath.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return exePath.substr(0, lastSlash);
    }
    return ".";
#elif defined(__APPLE__)
    // macOS: use _NSGetExecutablePath
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        char realPath[PATH_MAX];
        if (realpath(path, realPath)) {
            std::string exePath(realPath);
            size_t lastSlash = exePath.find_last_of("/");
            if (lastSlash != std::string::npos) {
                return exePath.substr(0, lastSlash);
            }
        }
    }
    return ".";
#else
    // Linux: use /proc/self/exe
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        std::string exePath(path);
        size_t lastSlash = exePath.find_last_of("/");
        if (lastSlash != std::string::npos) {
            return exePath.substr(0, lastSlash);
        }
    }
    return ".";
#endif
}

// Check if a command exists (returns true if found)
static bool commandExists(const std::string& cmd) {
#ifdef _WIN32
    std::string checkCmd = "where " + cmd + " >nul 2>&1";
#else
    std::string checkCmd = "which " + cmd + " >/dev/null 2>&1";
#endif
    return system(checkCmd.c_str()) == 0;
}

// Find link.exe - check same directory as compiler (standalone distribution)
static std::string findMSVCLink() {
#ifdef _WIN32
    namespace fs = std::filesystem;
    std::string exeDir = getExeDirectory();
    
    // Check same directory as this executable (standalone distribution)
    std::string localLink = exeDir + "\\link.exe";
    if (fs::exists(localLink)) {
        return localLink;
    }
    
    // Fallback: check PATH (for development or VS environment)
    char pathBuffer[MAX_PATH];
    if (SearchPathA(NULL, "link.exe", NULL, MAX_PATH, pathBuffer, NULL)) {
        return pathBuffer;
    }
#endif
    return "";
}

// Find MSVC lib directory (same directory as link.exe) - for backward compatibility
static std::string findMSVCLib() {
#ifdef _WIN32
    namespace fs = std::filesystem;
    std::string exeDir = getExeDirectory();
    
    // Check lib subdirectory
    std::string libDir = exeDir + "\\lib";
    if (fs::exists(libDir)) {
        return libDir;
    }
#endif
    return "";
}

// Find runtime library in same directory as executable
static std::string findRuntimeLib() {
    namespace fs = std::filesystem;
    std::string exeDir = getExeDirectory();
    
#ifdef _WIN32
    // Check for MSVC format (moonrt.lib)
    std::string msvcLib = exeDir + "\\moonrt.lib";
    if (fs::exists(msvcLib)) {
        return msvcLib;
    }
    
    // Check in lib subdirectory
    std::string msvcLibInLib = exeDir + "\\lib\\moonrt.lib";
    if (fs::exists(msvcLibInLib)) {
        return msvcLibInLib;
    }
#else
    // Linux: Check for libmoonrt.a
    std::string linuxLib = exeDir + "/libmoonrt.a";
    if (fs::exists(linuxLib)) {
        return linuxLib;
    }
    
    // Check in lib subdirectory
    std::string linuxLibInLib = exeDir + "/lib/libmoonrt.a";
    if (fs::exists(linuxLibInLib)) {
        return linuxLibInLib;
    }
    
    // Check in parent directory (for build directory structure)
    std::string linuxLibParent = exeDir + "/../libmoonrt.a";
    if (fs::exists(linuxLibParent)) {
        return fs::canonical(linuxLibParent).string();
    }
#endif
    return "";
}

// ============================================================================
// Main Compilation
// ============================================================================

bool LLVMCodeGen::compile(const Program& program, const std::string& moduleName) {
    module->setModuleIdentifier(moduleName);
    
    // =====================================================
    // Pass 1: Forward declare all top-level functions
    // =====================================================
    for (const auto& stmt : program.statements) {
        if (auto* funcDecl = std::get_if<FuncDecl>(&stmt->value)) {
            // Create function type
            std::vector<Type*> paramTypes;
            for (size_t i = 0; i < funcDecl->params.size(); i++) {
                paramTypes.push_back(moonValuePtrType);
            }
            
            FunctionType* funcType = FunctionType::get(moonValuePtrType, paramTypes, false);
            Function* func = Function::Create(funcType, Function::ExternalLinkage, 
                                               "moon_fn_" + funcDecl->name, module.get());
            
            // If function is exported and we're building a shared library, mark it for export
            if (funcDecl->isExported && buildingSharedLib) {
#ifdef _WIN32
                func->setDLLStorageClass(GlobalValue::DLLExportStorageClass);
#else
                func->setVisibility(GlobalValue::DefaultVisibility);
#endif
                // Track exported functions for header generation
                exportedFunctions.push_back({funcDecl->name, (int)funcDecl->params.size()});
            }
            
            functions[funcDecl->name] = func;
        }
    }
    
    // =====================================================
    // Pass 2: Create entry point(s) and generate all code
    // =====================================================
    
    if (buildingSharedLib) {
        // ========== Shared Library Mode ==========
        // Create moon_dll_init() and moon_dll_cleanup() instead of main()
        
        // Create moon_dll_init() - initializes runtime and runs top-level code
        FunctionType* initType = FunctionType::get(Type::getVoidTy(*context), {}, false);
        Function* initFunc = Function::Create(
            initType,
            Function::ExternalLinkage,
            "moon_dll_init",
            module.get()
        );
        
        // Mark as DLL export
#ifdef _WIN32
        initFunc->setDLLStorageClass(GlobalValue::DLLExportStorageClass);
#else
        initFunc->setVisibility(GlobalValue::DefaultVisibility);
#endif
        
        // Create entry block for init function
        BasicBlock* initEntry = BasicBlock::Create(*context, "entry", initFunc);
        builder->SetInsertPoint(initEntry);
        
        // Initialize runtime with null arguments (DLL doesn't have command line)
        Value* zeroInt = ConstantInt::get(Type::getInt32Ty(*context), 0);
        Value* nullPtr = ConstantPointerNull::get(PointerType::get(PointerType::get(Type::getInt8Ty(*context), 0), 0));
        builder->CreateCall(getRuntimeFunction("moon_runtime_init"), {zeroInt, nullPtr});
        
        currentFunction = initFunc;
        mainFunction = initFunc;
        
        // Generate code for each statement (top-level code runs on init)
        for (const auto& stmt : program.statements) {
            generateStatement(stmt);
            if (hasError()) return false;
        }
        
        // Return from init
        builder->CreateRetVoid();
        
        // Create moon_dll_cleanup() - cleans up runtime
        FunctionType* cleanupType = FunctionType::get(Type::getVoidTy(*context), {}, false);
        Function* cleanupFunc = Function::Create(
            cleanupType,
            Function::ExternalLinkage,
            "moon_dll_cleanup",
            module.get()
        );
        
        // Mark as DLL export
#ifdef _WIN32
        cleanupFunc->setDLLStorageClass(GlobalValue::DLLExportStorageClass);
#else
        cleanupFunc->setVisibility(GlobalValue::DefaultVisibility);
#endif
        
        BasicBlock* cleanupEntry = BasicBlock::Create(*context, "entry", cleanupFunc);
        builder->SetInsertPoint(cleanupEntry);
        builder->CreateCall(getRuntimeFunction("moon_runtime_cleanup"), {});
        builder->CreateRetVoid();
        
        // ========== Export Runtime Helper Functions ==========
        // These functions allow other languages to create MoonValue objects
        
        auto exportFunction = [this](const char* name) {
            Function* fn = getRuntimeFunction(name);
            if (fn) {
#ifdef _WIN32
                fn->setDLLStorageClass(GlobalValue::DLLExportStorageClass);
#else
                fn->setVisibility(GlobalValue::DefaultVisibility);
#endif
            }
        };
        
        exportFunction("moon_string");
        exportFunction("moon_int");
        exportFunction("moon_float");
        exportFunction("moon_bool");
        exportFunction("moon_null");
        exportFunction("moon_get_int");
        exportFunction("moon_get_float");
        exportFunction("moon_get_string");
        exportFunction("moon_get_bool");
        
    } else {
        // ========== Executable Mode ==========
        // Create main function
        FunctionType* mainType = FunctionType::get(
            Type::getInt32Ty(*context),
            {Type::getInt32Ty(*context), PointerType::get(PointerType::get(Type::getInt8Ty(*context), 0), 0)},
            false
        );
        
        Function* mainFunc = Function::Create(
            mainType,
            Function::ExternalLinkage,
            "main",
            module.get()
        );
        
        // Create entry block
        BasicBlock* entry = BasicBlock::Create(*context, "entry", mainFunc);
        builder->SetInsertPoint(entry);
        
        // Save main function arguments
        auto argIt = mainFunc->arg_begin();
        Value* argc = &*argIt++;
        Value* argv = &*argIt;
        
        // Initialize runtime
        builder->CreateCall(getRuntimeFunction("moon_runtime_init"), {argc, argv});
        
        currentFunction = mainFunc;
        mainFunction = mainFunc;  // Save reference to main function
        
        // Generate code for each statement
        for (const auto& stmt : program.statements) {
            generateStatement(stmt);
            if (hasError()) return false;
        }
        
        // Cleanup runtime and return
        builder->CreateCall(getRuntimeFunction("moon_runtime_cleanup"), {});
        builder->CreateRet(ConstantInt::get(Type::getInt32Ty(*context), 0));
    }
    
    // Verify the module
    std::string verifyError;
    raw_string_ostream verifyStream(verifyError);
    if (verifyModule(*module, &verifyStream)) {
        setError("Module verification failed: " + verifyError);
        return false;
    }
    
    return true;
}

// ============================================================================
// Output Functions
// ============================================================================

std::string LLVMCodeGen::getIR() const {
    std::string ir;
    raw_string_ostream stream(ir);
    module->print(stream, nullptr);
    return ir;
}

bool LLVMCodeGen::emitIR(const std::string& filename) {
    std::error_code ec;
    raw_fd_ostream file(filename, ec, sys::fs::OF_Text);
    if (ec) {
        setError("Cannot open file: " + filename);
        return false;
    }
    module->print(file, nullptr);
    return true;
}

bool LLVMCodeGen::emitBitcode(const std::string& filename) {
    std::error_code ec;
    raw_fd_ostream file(filename, ec, sys::fs::OF_None);
    if (ec) {
        setError("Cannot open file: " + filename);
        return false;
    }
    WriteBitcodeToFile(*module, file);
    return true;
}

bool LLVMCodeGen::emitObject(const std::string& filename) {
    // Determine target triple
    std::string targetTriple;
    if (!customTargetTriple.empty()) {
        // Use custom target triple for cross-compilation
        targetTriple = customTargetTriple;
    } else {
        // Use default target for native compilation
#ifdef _WIN32
        targetTriple = "x86_64-pc-windows-msvc";  // MSVC target
#else
        targetTriple = sys::getDefaultTargetTriple();
#endif
    }

    // LLVM 21+ changed setTargetTriple to accept llvm::Triple instead of StringRef
#if LLVM_VERSION_MAJOR >= 21
    module->setTargetTriple(llvm::Triple(targetTriple));
#else
    module->setTargetTriple(targetTriple);
#endif
    
    std::string error;
    auto target = TargetRegistry::lookupTarget(targetTriple, error);
    if (!target) {
        setError("Cannot find target '" + targetTriple + "': " + error);
        return false;
    }
    
    // Use custom CPU and features if specified
    auto cpu = customTargetCPU.empty() ? "generic" : customTargetCPU.c_str();
    auto features = customTargetFeatures.empty() ? "" : customTargetFeatures.c_str();
    TargetOptions opt;
    
    // Linux requires PIC (Position Independent Code) for PIE executables
#ifdef _WIN32
    auto rm = std::optional<Reloc::Model>();  // Default for Windows
#else
    auto rm = std::optional<Reloc::Model>(Reloc::PIC_);  // PIC for Linux
#endif
    
    // Use O3 optimization level for maximum performance
    auto targetMachine = target->createTargetMachine(
        targetTriple, cpu, features, opt, rm, 
        std::nullopt, CodeGenOptLevel::Aggressive);  // O3 optimization
    
    module->setDataLayout(targetMachine->createDataLayout());
    
    std::error_code ec;
    raw_fd_ostream dest(filename, ec, sys::fs::OF_None);
    if (ec) {
        setError("Cannot open file: " + filename);
        return false;
    }
    
    // Add aggressive optimization passes for O3 level
    legacy::PassManager pass;
    
    // Memory optimization - promote stack allocations to registers
    pass.add(createPromoteMemoryToRegisterPass());
    
    // Scalar optimizations (first pass)
    pass.add(createInstructionCombiningPass());
    pass.add(createReassociatePass());
    pass.add(createGVNPass());
    pass.add(createCFGSimplificationPass());
    
    // Tail call elimination for recursive functions
    pass.add(createTailCallEliminationPass());
    
    // Loop optimizations (available in legacy pass manager)
    pass.add(createLICMPass());                 // Loop-invariant code motion
    pass.add(createLoopUnrollPass());           // Unroll loops for better pipelining
    
    // Scalar optimizations (second pass)
    pass.add(createInstructionCombiningPass());
    pass.add(createGVNPass());
    
    // Dead code elimination
    pass.add(createDeadCodeEliminationPass());
    
    // Final cleanup
    pass.add(createInstructionCombiningPass());
    pass.add(createCFGSimplificationPass());
    
    auto fileType = CodeGenFileType::ObjectFile;
    
    if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType)) {
        setError("Cannot emit object file");
        return false;
    }
    
    pass.run(*module);
    dest.flush();
    
    return true;
}

bool LLVMCodeGen::emitExecutable(const std::string& filename, const std::string& runtimeLib, 
                                  const std::string& iconPath, const VersionInfo& versionInfo) {
    namespace fs = std::filesystem;
    
    // First emit object file (.o for MinGW)
    std::string objFile = filename + ".o";
    if (!emitObject(objFile)) {
        return false;
    }
    
    // Auto-find runtime library if not specified
    std::string rtLib = runtimeLib;
    if (rtLib.empty()) {
        rtLib = findRuntimeLib();
    }
    
    std::string linkCmd;
    int result;
    
#ifdef _WIN32
    // Use MSVC link.exe for linking
    std::string linker = findMSVCLink();
    if (linker.empty()) {
        std::remove(objFile.c_str());
        setError("Linking failed: link.exe not found. Make sure MSVC tools are in PATH or the same directory as the compiler");
        return false;
    }
    
    std::string exeDir = getExeDirectory();
    std::string libDir = findMSVCLib();
    
    // Add the compiler's directory to PATH so link.exe and rc.exe can be found
    std::string currentPath = getenv("PATH") ? getenv("PATH") : "";
    std::string newPath = exeDir + ";" + currentPath;
    _putenv_s("PATH", newPath.c_str());
    
    // Generate resource file with icon and version info
    std::string resFile;
    bool hasResource = false;
    
    if (!iconPath.empty() || true) {  // Always add version info
        std::string baseName = fs::path(filename).stem().string();
        std::string rcFile = filename + ".rc";
        resFile = filename + ".res";
        
        std::ofstream rcOut(rcFile);
        rcOut << "// Auto-generated resource file\n\n";
        
        // Add icon if specified
        if (!iconPath.empty() && fs::exists(iconPath)) {
            rcOut << "// Application Icon\n";
            rcOut << "1 ICON \"" << iconPath << "\"\n\n";
        }
        
        // Determine version info values (defaults from version.h)
        std::string company = versionInfo.company.empty() ? MOONLANG_COMPANY : versionInfo.company;
        std::string description = versionInfo.description.empty() ? 
            (baseName + " - MoonLang Application") : versionInfo.description;
        std::string copyright = versionInfo.copyright.empty() ? 
            (std::string(MOONLANG_COPYRIGHT) + ". All rights reserved.") : versionInfo.copyright;
        std::string fileVersion = versionInfo.version.empty() ? MOONLANG_VERSION_STRING_FULL : versionInfo.version;
        std::string productName = versionInfo.productName.empty() ? 
            MOONLANG_PRODUCT_NAME : versionInfo.productName;
        std::string productVersion = versionInfo.productVersion.empty() ? 
            MOONLANG_VERSION_STRING_FULL : versionInfo.productVersion;
        
        // Convert version string to comma format
        std::string fileVersionComma = fileVersion;
        std::string productVersionComma = productVersion;
        for (auto& c : fileVersionComma) if (c == '.') c = ',';
        for (auto& c : productVersionComma) if (c == '.') c = ',';
        
        rcOut << "// Version Info\n";
        rcOut << "1 VERSIONINFO\n";
        rcOut << "FILEVERSION " << fileVersionComma << "\n";
        rcOut << "PRODUCTVERSION " << productVersionComma << "\n";
        rcOut << "FILEFLAGSMASK 0x3fL\n";
        rcOut << "FILEFLAGS 0x0L\n";
        rcOut << "FILEOS 0x40004L\n";
        rcOut << "FILETYPE 0x1L\n";
        rcOut << "FILESUBTYPE 0x0L\n";
        rcOut << "BEGIN\n";
        rcOut << "    BLOCK \"StringFileInfo\"\n";
        rcOut << "    BEGIN\n";
        rcOut << "        BLOCK \"040904b0\"\n";
        rcOut << "        BEGIN\n";
        rcOut << "            VALUE \"CompanyName\", \"" << company << "\"\n";
        rcOut << "            VALUE \"FileDescription\", \"" << description << "\"\n";
        rcOut << "            VALUE \"FileVersion\", \"" << fileVersion << "\"\n";
        rcOut << "            VALUE \"InternalName\", \"" << baseName << "\"\n";
        rcOut << "            VALUE \"LegalCopyright\", \"" << copyright << "\"\n";
        rcOut << "            VALUE \"OriginalFilename\", \"" << baseName << ".exe\"\n";
        rcOut << "            VALUE \"ProductName\", \"" << productName << "\"\n";
        rcOut << "            VALUE \"ProductVersion\", \"" << productVersion << "\"\n";
        rcOut << "        END\n";
        rcOut << "    END\n";
        rcOut << "    BLOCK \"VarFileInfo\"\n";
        rcOut << "    BEGIN\n";
        rcOut << "        VALUE \"Translation\", 0x409, 1200\n";
        rcOut << "    END\n";
        rcOut << "END\n";
        rcOut.close();
        
        // Compile resource file with rc.exe (should be found via PATH)
        std::string rcCmd = "rc.exe /nologo /fo \"" + resFile + "\" \"" + rcFile + "\" 2>nul";
        int resResult = system(rcCmd.c_str());
        hasResource = (resResult == 0 && fs::exists(resFile));
        
        if (!hasResource) {
            std::cerr << "  Warning: Resource compilation failed (rc.exe not found)\n";
        }
        
        std::remove(rcFile.c_str());
    }
    
    // Build MSVC link command - use just "link.exe" since we added dir to PATH earlier
    linkCmd = "link.exe /nologo /OUT:\"" + filename + "\"";
    
    // Add object file
    linkCmd += " \"" + objFile + "\"";
    
    // Add resource file if available
    if (hasResource) {
        linkCmd += " \"" + resFile + "\"";
    }
    
    // Add runtime library
    if (!rtLib.empty()) {
        linkCmd += " \"" + rtLib + "\"";
    }
    
    // Add WebView2 static library only if GUI functions are used
    if (usesGUI) {
        std::string webview2Lib = exeDir + "\\WebView2LoaderStatic.lib";
        if (fs::exists(webview2Lib)) {
            linkCmd += " \"" + webview2Lib + "\"";
        }
    }
    
    // Add lib path (for system libraries)
    if (!libDir.empty()) {
        linkCmd += " /LIBPATH:\"" + libDir + "\"";
    }
    
    // Link system and CRT libraries
    // Required CRT libraries for dynamic linking (msvcrt instead of libcmt)
    linkCmd += " msvcrt.lib vcruntime.lib ucrt.lib";  // Dynamic CRT
    linkCmd += " kernel32.lib";  // Always needed for Windows API
    
    // Conditionally link libraries based on features used
    if (usesGUI) {
        linkCmd += " user32.lib gdi32.lib ole32.lib shell32.lib advapi32.lib";  // GUI needs these (advapi32 for WebView2 ETW/registry)
    }
    if (usesNetwork) {
        linkCmd += " ws2_32.lib";  // Network needs this
    }
    if (usesTLS) {
        linkCmd += " crypt32.lib";  // TLS/OpenSSL needs this
    }
    // Note: shlwapi.lib, uuid.lib are rarely needed, 
    // only link if specific functions require them
    
    // Use WINDOWS subsystem for GUI apps (no console window), CONSOLE for others
    if (usesGUI) {
        linkCmd += " /SUBSYSTEM:WINDOWS";
        linkCmd += " /ENTRY:mainCRTStartup";  // Keep using main() as entry point
    } else {
        linkCmd += " /SUBSYSTEM:CONSOLE";
    }
    linkCmd += " /MACHINE:X64";
    linkCmd += " /DEFAULTLIB:msvcrt.lib";  // Prefer dynamic CRT
    linkCmd += " /NODEFAULTLIB:libcmt.lib";  // Exclude static CRT to avoid conflicts
    
    result = system(linkCmd.c_str());
    std::remove(objFile.c_str());
    if (hasResource) {
        std::remove(resFile.c_str());
    }
    
    if (result != 0) {
        setError("Linking failed. Check that all required libraries are in the lib folder.");
        return false;
    }
    
    return true;
#else
    // Linux/Mac - use clang or gcc
    linkCmd = "clang -o " + filename + " " + objFile;
    
    // Add runtime library
    if (!rtLib.empty()) {
        linkCmd += " " + rtLib;
    }
    
#ifdef __APPLE__
    // macOS - Add required system libraries
    linkCmd += " -lpthread -lm -lc++";
    
    // Suppress linker version mismatch warnings
    linkCmd += " -Wl,-w";
    
    // Static link OpenSSL (so users don't need to install it)
    // Try Apple Silicon path first, then Intel path
    const char* opensslPaths[] = {
        "/opt/homebrew/opt/openssl@3/lib",
        "/usr/local/opt/openssl@3/lib",
        "/opt/homebrew/opt/openssl/lib",
        "/usr/local/opt/openssl/lib"
    };
    std::string sslStatic, cryptoStatic;
    for (const char* path : opensslPaths) {
        std::string testSsl = std::string(path) + "/libssl.a";
        std::string testCrypto = std::string(path) + "/libcrypto.a";
        if (std::filesystem::exists(testSsl) && std::filesystem::exists(testCrypto)) {
            sslStatic = testSsl;
            cryptoStatic = testCrypto;
            break;
        }
    }
    if (!sslStatic.empty()) {
        // Static linking - embed OpenSSL into the binary
        linkCmd += " " + sslStatic + " " + cryptoStatic;
    } else {
        // Fallback to dynamic linking
        linkCmd += " -L/opt/homebrew/opt/openssl@3/lib -L/usr/local/opt/openssl@3/lib";
        linkCmd += " -lssl -lcrypto";
    }
    
    // Add macOS frameworks for GUI
    if (usesGUI) {
        linkCmd += " -framework Cocoa -framework WebKit -framework AppKit";
    }
#else
    // Linux - Add required system libraries
    linkCmd += " -lpthread -ldl -lm -lstdc++";
    
    // Always add OpenSSL libraries on Linux (runtime includes TLS code)
    linkCmd += " -lssl -lcrypto";
    
    // Add GTK and WebKit libraries if GUI is used (using pkg-config)
    if (usesGUI) {
        // Use pkg-config to get the correct flags
        linkCmd += " $(pkg-config --libs gtk+-3.0 2>/dev/null)";
        // Try webkit2gtk-4.1 first, fall back to 4.0
        linkCmd += " $(pkg-config --libs webkit2gtk-4.1 2>/dev/null || pkg-config --libs webkit2gtk-4.0 2>/dev/null)";
    }
    
    // Use -Wl,-rpath for library search path
    linkCmd += " -Wl,-rpath,'$ORIGIN'";
#endif
    
    result = system(linkCmd.c_str());
    std::remove(objFile.c_str());
    
    if (result != 0) {
        setError("Linking failed");
        return false;
    }
    
    return true;
#endif
}

bool LLVMCodeGen::emitSharedLibrary(const std::string& filename, const std::string& runtimeLib) {
    namespace fs = std::filesystem;
    
    // First emit object file
    std::string objFile = filename + ".o";
    if (!emitObject(objFile)) {
        return false;
    }
    
    // Auto-find runtime library if not specified
    std::string rtLib = runtimeLib;
    if (rtLib.empty()) {
        rtLib = findRuntimeLib();
    }
    
    std::string linkCmd;
    int result;
    
#ifdef _WIN32
    // Use MSVC link.exe to create DLL
    std::string linker = findMSVCLink();
    if (linker.empty()) {
        std::remove(objFile.c_str());
        setError("Linking failed: link.exe not found. Make sure MSVC tools are in PATH");
        return false;
    }
    
    std::string exeDir = getExeDirectory();
    std::string libDir = findMSVCLib();
    
    // Add the compiler's directory to PATH
    std::string currentPath = getenv("PATH") ? getenv("PATH") : "";
    std::string newPath = exeDir + ";" + currentPath;
    _putenv_s("PATH", newPath.c_str());
    
    // Determine output filenames
    std::string dllFile = filename;
    if (dllFile.size() < 4 || dllFile.substr(dllFile.size() - 4) != ".dll") {
        dllFile += ".dll";
    }
    std::string libFile = dllFile.substr(0, dllFile.size() - 4) + ".lib";
    
    // Build MSVC link command for DLL
    linkCmd = "link.exe /nologo /DLL /OUT:\"" + dllFile + "\"";
    linkCmd += " /IMPLIB:\"" + libFile + "\"";
    
    // Add object file
    linkCmd += " \"" + objFile + "\"";
    
    // Add runtime library
    if (!rtLib.empty()) {
        linkCmd += " \"" + rtLib + "\"";
    }
    
    // Add WebView2 static library if GUI functions are used
    if (usesGUI) {
        std::string webview2Lib = exeDir + "\\WebView2LoaderStatic.lib";
        if (fs::exists(webview2Lib)) {
            linkCmd += " \"" + webview2Lib + "\"";
        }
    }
    
    // Add lib path
    if (!libDir.empty()) {
        linkCmd += " /LIBPATH:\"" + libDir + "\"";
    }
    
    // Link CRT and system libraries
    linkCmd += " msvcrt.lib vcruntime.lib ucrt.lib";  // Dynamic CRT
    linkCmd += " kernel32.lib";
    if (usesGUI) {
        linkCmd += " user32.lib gdi32.lib ole32.lib shell32.lib advapi32.lib";
    }
    if (usesNetwork) {
        linkCmd += " ws2_32.lib";
    }
    if (usesTLS) {
        linkCmd += " crypt32.lib";  // TLS/OpenSSL needs this
    }
    
    // Export runtime helper functions for other languages to use
    linkCmd += " /EXPORT:moon_string";
    linkCmd += " /EXPORT:moon_int";
    linkCmd += " /EXPORT:moon_float";
    linkCmd += " /EXPORT:moon_bool";
    linkCmd += " /EXPORT:moon_null";
    linkCmd += " /EXPORT:moon_to_int";
    linkCmd += " /EXPORT:moon_to_float";
    linkCmd += " /EXPORT:moon_to_string";
    linkCmd += " /EXPORT:moon_to_bool";
    linkCmd += " /EXPORT:moon_is_null";
    linkCmd += " /EXPORT:moon_is_int";
    linkCmd += " /EXPORT:moon_is_float";
    linkCmd += " /EXPORT:moon_is_string";
    linkCmd += " /EXPORT:moon_is_bool";
    linkCmd += " /EXPORT:moon_is_list";
    linkCmd += " /EXPORT:moon_is_dict";
    linkCmd += " /EXPORT:moon_list_new";
    linkCmd += " /EXPORT:moon_dict_new";
    
    linkCmd += " /MACHINE:X64";
    linkCmd += " /DEFAULTLIB:msvcrt.lib";  // Prefer dynamic CRT
    linkCmd += " /NODEFAULTLIB:libcmt.lib";  // Exclude static CRT
    
    result = system(linkCmd.c_str());
    std::remove(objFile.c_str());
    
    if (result != 0) {
        setError("DLL linking failed. Check that all required libraries are available.");
        return false;
    }
    
    return true;
#else
#ifdef __APPLE__
    // macOS - use clang to create dynamic library (.dylib)
    std::string soFile = filename;
    if (soFile.size() < 6 || soFile.substr(soFile.size() - 6) != ".dylib") {
        soFile += ".dylib";
    }
    
    linkCmd = "clang -dynamiclib -o " + soFile + " " + objFile;
    
    // Add runtime library
    if (!rtLib.empty()) {
        linkCmd += " " + rtLib;
    }
    
    // Add required system libraries for macOS
    linkCmd += " -lpthread -lm -lc++";
    
    // Suppress linker version mismatch warnings
    linkCmd += " -Wl,-w";
    
    // Static link OpenSSL (so users don't need to install it)
    const char* opensslPathsDylib[] = {
        "/opt/homebrew/opt/openssl@3/lib",
        "/usr/local/opt/openssl@3/lib",
        "/opt/homebrew/opt/openssl/lib",
        "/usr/local/opt/openssl/lib"
    };
    std::string sslStaticDylib, cryptoStaticDylib;
    for (const char* path : opensslPathsDylib) {
        std::string testSsl = std::string(path) + "/libssl.a";
        std::string testCrypto = std::string(path) + "/libcrypto.a";
        if (std::filesystem::exists(testSsl) && std::filesystem::exists(testCrypto)) {
            sslStaticDylib = testSsl;
            cryptoStaticDylib = testCrypto;
            break;
        }
    }
    if (!sslStaticDylib.empty()) {
        // Static linking - embed OpenSSL into the binary
        linkCmd += " " + sslStaticDylib + " " + cryptoStaticDylib;
    } else {
        // Fallback to dynamic linking
        linkCmd += " -L/opt/homebrew/opt/openssl@3/lib -L/usr/local/opt/openssl@3/lib";
        linkCmd += " -lssl -lcrypto";
    }
    
    // Add macOS frameworks for GUI
    if (usesGUI) {
        linkCmd += " -framework Cocoa -framework WebKit -framework AppKit";
    }
#else
    // Linux - use clang or gcc to create shared object
    std::string soFile = filename;
    if (soFile.size() < 3 || soFile.substr(soFile.size() - 3) != ".so") {
        soFile += ".so";
    }
    
    linkCmd = "clang -shared -fPIC -o " + soFile + " " + objFile;
    
    // Add runtime library
    if (!rtLib.empty()) {
        linkCmd += " " + rtLib;
    }
    
    // Add required system libraries
    linkCmd += " -lpthread -ldl -lm -lstdc++";
    
    // Always add OpenSSL libraries on Linux (runtime includes TLS code)
    linkCmd += " -lssl -lcrypto";
    
    // Add GTK and WebKit libraries if GUI is used
    if (usesGUI) {
        linkCmd += " $(pkg-config --libs gtk+-3.0 2>/dev/null)";
        linkCmd += " $(pkg-config --libs webkit2gtk-4.1 2>/dev/null || pkg-config --libs webkit2gtk-4.0 2>/dev/null)";
    }
    
    // Use -Wl,-rpath for library search path
    linkCmd += " -Wl,-rpath,'$ORIGIN'";
#endif
    
    result = system(linkCmd.c_str());
    std::remove(objFile.c_str());
    
    if (result != 0) {
        setError("Shared library linking failed");
        return false;
    }
    
    return true;
#endif
}

// ============================================================================
// Statement Generation Module
// ============================================================================
// This file contains all statement generation functions for the LLVM code generator.
//
// IMPORTANT: This file should be included by llvm_codegen.cpp, not compiled separately.
// It is meant to be included inline to keep the statement generation code organized
// while maintaining a single compilation unit.
// ============================================================================

// ============================================================================
// Statement Generation
// ============================================================================

void LLVMCodeGen::generateStatement(const StmtPtr& stmt) {
    // Track current line for compile-time error messages
    if (stmt->line > 0) {
        currentLine = stmt->line;
    }
    
    // Emit debug location for runtime error tracking
    if (stmt->line > 0 && !sourceFile.empty()) {
        Value* fileStr = builder->CreateGlobalStringPtr(sourceFile);
        Value* lineVal = ConstantInt::get(Type::getInt32Ty(*context), stmt->line);
        Value* funcStr = Constant::getNullValue(PointerType::get(Type::getInt8Ty(*context), 0));
        builder->CreateCall(getRuntimeFunction("moon_set_debug_location"), {fileStr, lineVal, funcStr});
    }
    
    std::visit([this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        
        if constexpr (std::is_same_v<T, ExpressionStmt>) {
            generateExpressionStmt(arg);
        }
        else if constexpr (std::is_same_v<T, AssignStmt>) {
            generateAssignStmt(arg);
        }
        else if constexpr (std::is_same_v<T, IfStmt>) {
            generateIfStmt(arg);
        }
        else if constexpr (std::is_same_v<T, WhileStmt>) {
            generateWhileStmt(arg);
        }
        else if constexpr (std::is_same_v<T, ForInStmt>) {
            generateForInStmt(arg);
        }
        else if constexpr (std::is_same_v<T, ForRangeStmt>) {
            generateForRangeStmt(arg);
        }
        else if constexpr (std::is_same_v<T, FuncDecl>) {
            generateFuncDecl(arg);
        }
        else if constexpr (std::is_same_v<T, ReturnStmt>) {
            generateReturnStmt(arg);
        }
        else if constexpr (std::is_same_v<T, BreakStmt>) {
            generateBreakStmt();
        }
        else if constexpr (std::is_same_v<T, ContinueStmt>) {
            generateContinueStmt();
        }
        else if constexpr (std::is_same_v<T, TryStmt>) {
            generateTryStmt(arg);
        }
        else if constexpr (std::is_same_v<T, ThrowStmt>) {
            generateThrowStmt(arg);
        }
        else if constexpr (std::is_same_v<T, SwitchStmt>) {
            generateSwitchStmt(arg);
        }
        else if constexpr (std::is_same_v<T, ClassDecl>) {
            generateClassDecl(arg);
        }
        else if constexpr (std::is_same_v<T, ImportStmt>) {
            generateImportStmt(arg);
        }
        else if constexpr (std::is_same_v<T, MoonStmt>) {
            // moon async statement - launch in background thread
            if (auto* call = std::get_if<CallExpr>(&arg.callExpr->value)) {
                // Get the function value
                Value* funcVal = generateExpression(call->callee);
                
                // Build args array
                int argc = call->arguments.size();
                Value* argsArray = builder->CreateAlloca(moonValuePtrType,
                    ConstantInt::get(Type::getInt32Ty(*context), argc));
                
                std::vector<Value*> argVals;
                for (int i = 0; i < argc; i++) {
                    Value* val = generateExpression(call->arguments[i]);
                    argVals.push_back(val);
                    Value* ptr = builder->CreateGEP(moonValuePtrType, argsArray,
                        ConstantInt::get(Type::getInt32Ty(*context), i));
                    builder->CreateStore(val, ptr);
                }
                
                // Call moon_async(func, args, argc)
                builder->CreateCall(getRuntimeFunction("moon_async"),
                    {funcVal, argsArray, ConstantInt::get(Type::getInt32Ty(*context), argc)});
                
                // Release local refs (async will retain what it needs)
                for (auto& val : argVals) {
                    builder->CreateCall(getRuntimeFunction("moon_release"), {val});
                }
                builder->CreateCall(getRuntimeFunction("moon_release"), {funcVal});
            }
        }
        else if constexpr (std::is_same_v<T, ChanSendStmt>) {
            // Channel send: ch <- value
            Value* ch = generateExpression(arg.channel);
            Value* val = generateExpression(arg.value);
            builder->CreateCall(getRuntimeFunction("moon_chan_send"), {ch, val});
            builder->CreateCall(getRuntimeFunction("moon_release"), {ch});
            builder->CreateCall(getRuntimeFunction("moon_release"), {val});
        }
        else if constexpr (std::is_same_v<T, GlobalStmt>) {
            // Mark variables as referencing globals
            for (const auto& name : arg.names) {
                declaredGlobals.insert(name);
            }
        }
    }, stmt->value);
}

void LLVMCodeGen::generateExpressionStmt(const ExpressionStmt& stmt) {
    Value* val = generateExpression(stmt.expression);
    // Release the value since it's not used
    if (val) {
        builder->CreateCall(getRuntimeFunction("moon_release"), {val});
    }
}

void LLVMCodeGen::generateAssignStmt(const AssignStmt& stmt) {
    // Handle different target types
    if (auto* id = std::get_if<Identifier>(&stmt.target->value)) {
        // Check if we should use native assignment
        // IMPORTANT: Don't use native optimization for global scope variables
        // because they need to be accessible from nested functions
        bool isGlobalScope = (currentFunction == mainFunction);
        bool isExistingGlobal = globalVars.count(id->name) > 0;
        bool isExistingLocal = namedValues.count(id->name) > 0;
        
        NativeType valueType = inferExpressionType(stmt.value);
        
        // If the variable is already native, or the value is native and variable is new
        auto existingType = variableTypes.find(id->name);
        bool useNative = false;
        
        // Allow native optimization:
        // 1. In non-global scope (functions), OR
        // 2. In global scope inside optimized for loop (with loop-local promotion)
        bool allowNative = !isGlobalScope || inOptimizedForLoop;
        
        if (allowNative) {
            if (existingType != variableTypes.end() && 
                (nativeIntVars.count(id->name) || nativeFloatVars.count(id->name))) {
                // Variable exists as native - keep its native type if compatible
                if (existingType->second == NativeType::NativeInt && 
                    (valueType == NativeType::NativeInt || valueType == NativeType::NativeFloat)) {
                    useNative = true;
                } else if (existingType->second == NativeType::NativeFloat && 
                           (valueType == NativeType::NativeFloat || valueType == NativeType::NativeInt)) {
                    useNative = true;
                }
            } else if (!isExistingLocal && (valueType == NativeType::NativeInt || valueType == NativeType::NativeFloat)) {
                // New variable OR existing global being promoted in optimized loop
                if (!isExistingGlobal && !inOptimizedForLoop) {
                    // New variable outside for loop - enable native
                    // Inside optimized for loop, don't create new native vars 
                    // (they might be re-assigned with dynamic values later in the loop)
                    useNative = true;
                } else if (inOptimizedForLoop && isExistingGlobal) {
                    // Existing global in optimized loop
                    // Check if already promoted (by pre-scan) - if so, just use native
                    if (nativeIntVars.count(id->name) || nativeFloatVars.count(id->name)) {
                        useNative = true;
                    } else {
                        // Not yet promoted - promote now (shouldn't happen with pre-scan, but keep as fallback)
                        Value* globalVal = builder->CreateLoad(moonValuePtrType, globalVars[id->name]);
                        Value* nativeVal = (valueType == NativeType::NativeInt) 
                            ? unboxToInt(globalVal) 
                            : unboxToFloat(globalVal);
                        builder->CreateCall(getRuntimeFunction("moon_release"), {globalVal});
                        
                        if (valueType == NativeType::NativeInt) {
                            storeNativeInt(id->name, nativeVal);
                        } else {
                            storeNativeFloat(id->name, nativeVal);
                        }
                        
                        promotedGlobals.push_back(id->name);
                        useNative = true;
                    }
                }
            }
        }
        
        if (useNative) {
            // Generate native expression and store natively
            TypedValue typedVal = generateNativeExpression(stmt.value);
            
            // Determine target type (existing native type or inferred type)
            NativeType targetType = valueType;
            if (existingType != variableTypes.end() && 
                (existingType->second == NativeType::NativeInt || existingType->second == NativeType::NativeFloat)) {
                targetType = existingType->second;
            }
            
            if (targetType == NativeType::NativeInt) {
                Value* intVal = typedVal.value;
                if (typedVal.type == NativeType::NativeFloat) {
                    intVal = builder->CreateFPToSI(intVal, Type::getInt64Ty(*context));
                } else if (typedVal.type == NativeType::Dynamic) {
                    // Unbox from MoonValue
                    intVal = unboxToInt(typedVal.value);
                    builder->CreateCall(getRuntimeFunction("moon_release"), {typedVal.value});
                }
                storeNativeInt(id->name, intVal);
                return;
            }
            if (targetType == NativeType::NativeFloat) {
                Value* floatVal = typedVal.value;
                if (typedVal.type == NativeType::NativeInt) {
                    floatVal = builder->CreateSIToFP(floatVal, Type::getDoubleTy(*context));
                } else if (typedVal.type == NativeType::Dynamic) {
                    // Unbox from MoonValue
                    floatVal = unboxToFloat(typedVal.value);
                    builder->CreateCall(getRuntimeFunction("moon_release"), {typedVal.value});
                }
                storeNativeFloat(id->name, floatVal);
                return;
            }
        }
        
        // Fall back to dynamic assignment
        // IMPORTANT: If variable was previously native, we need to demote it to dynamic
        // This happens when assigning a dynamic value (e.g., function call result) to a native variable
        bool wasNative = nativeIntVars.count(id->name) || nativeFloatVars.count(id->name);
        if (wasNative) {
            // Variable is being demoted from native to dynamic
            // We need to:
            // 1. Box the current native value and store in dynamic storage
            // 2. Clear native tracking
            
            // First, ensure dynamic storage exists by boxing current native value
            if (nativeIntVars.count(id->name)) {
                Value* nativeVal = builder->CreateLoad(Type::getInt64Ty(*context), nativeIntVars[id->name]);
                Value* boxedVal = boxNativeInt(nativeVal);
                // Create dynamic storage if needed
                if (!namedValues.count(id->name) && !globalVars.count(id->name)) {
                    if (currentFunction == mainFunction) {
                        GlobalVariable* gv = new GlobalVariable(
                            *module,
                            moonValuePtrType,
                            false,
                            GlobalValue::InternalLinkage,
                            Constant::getNullValue(moonValuePtrType),
                            "global_" + id->name
                        );
                        globalVars[id->name] = gv;
                        builder->CreateStore(boxedVal, gv);
                    } else {
                        Value* alloca = createAlloca(currentFunction, id->name);
                        namedValues[id->name] = alloca;
                        builder->CreateStore(boxedVal, alloca);
                    }
                }
                nativeIntVars.erase(id->name);
            } else if (nativeFloatVars.count(id->name)) {
                Value* nativeVal = builder->CreateLoad(Type::getDoubleTy(*context), nativeFloatVars[id->name]);
                Value* boxedVal = boxNativeFloat(nativeVal);
                // Create dynamic storage if needed
                if (!namedValues.count(id->name) && !globalVars.count(id->name)) {
                    if (currentFunction == mainFunction) {
                        GlobalVariable* gv = new GlobalVariable(
                            *module,
                            moonValuePtrType,
                            false,
                            GlobalValue::InternalLinkage,
                            Constant::getNullValue(moonValuePtrType),
                            "global_" + id->name
                        );
                        globalVars[id->name] = gv;
                        builder->CreateStore(boxedVal, gv);
                    } else {
                        Value* alloca = createAlloca(currentFunction, id->name);
                        namedValues[id->name] = alloca;
                        builder->CreateStore(boxedVal, alloca);
                    }
                }
                nativeFloatVars.erase(id->name);
            }
            variableTypes.erase(id->name);
        }
        
        Value* val = generateExpression(stmt.value);
        if (!val) return;
        storeVariable(id->name, val);
    }
    else if (auto* idx = std::get_if<IndexExpr>(&stmt.target->value)) {
        // List/dict index assignment
        Value* val = generateExpression(stmt.value);
        if (!val) return;
        
        Value* obj = generateExpression(idx->object);
        
        // Optimization: if index is a native integer, use optimized list set
        NativeType indexType = inferExpressionType(idx->index);
        if (indexType == NativeType::NativeInt) {
            // Generate native index
            TypedValue indexTyped = generateNativeExpression(idx->index);
            Value* nativeIdx = indexTyped.value;
            if (indexTyped.type == NativeType::Dynamic) {
                nativeIdx = unboxToInt(indexTyped.value);
                builder->CreateCall(getRuntimeFunction("moon_release"), {indexTyped.value});
            }
            
            // Use optimized native index set
            builder->CreateCall(getRuntimeFunction("moon_list_set_idx"), {obj, nativeIdx, val});
            builder->CreateCall(getRuntimeFunction("moon_release"), {obj});
            builder->CreateCall(getRuntimeFunction("moon_release"), {val});  // Fix: release val after set
        } else {
            // Standard path: generate index as MoonValue*
            Value* index = generateExpression(idx->index);
            
            // Check if it's a list or dict and call appropriate function
            builder->CreateCall(getRuntimeFunction("moon_list_set"), {obj, index, val});
            
            builder->CreateCall(getRuntimeFunction("moon_release"), {obj});
            builder->CreateCall(getRuntimeFunction("moon_release"), {index});
            builder->CreateCall(getRuntimeFunction("moon_release"), {val});  // Fix: release val after set
        }
    }
    else if (auto* member = std::get_if<MemberExpr>(&stmt.target->value)) {
        // Object member assignment
        Value* val = generateExpression(stmt.value);
        if (!val) return;
        
        Value* obj = generateExpression(member->object);
        Value* fieldName = createGlobalString(member->member);
        
        builder->CreateCall(getRuntimeFunction("moon_object_set"), {obj, fieldName, val});
        builder->CreateCall(getRuntimeFunction("moon_release"), {obj});
        builder->CreateCall(getRuntimeFunction("moon_release"), {val});  // Fix: release val after set
    }
}

void LLVMCodeGen::generateIfStmt(const IfStmt& stmt) {
    Value* cond = generateExpression(stmt.condition);
    Value* condBool = builder->CreateCall(getRuntimeFunction("moon_is_truthy"), {cond});
    builder->CreateCall(getRuntimeFunction("moon_release"), {cond});
    
    Function* func = builder->GetInsertBlock()->getParent();
    
    BasicBlock* thenBB = BasicBlock::Create(*context, "then", func);
    BasicBlock* mergeBB = BasicBlock::Create(*context, "ifcont");
    
    // Handle elif branches
    std::vector<BasicBlock*> elifBBs;
    for (size_t i = 0; i < stmt.elifBranches.size(); i++) {
        elifBBs.push_back(BasicBlock::Create(*context, "elif"));
    }
    
    BasicBlock* elseBB = stmt.elseBranch.empty() ? mergeBB : BasicBlock::Create(*context, "else");
    BasicBlock* nextBB = elifBBs.empty() ? elseBB : elifBBs[0];
    
    builder->CreateCondBr(condBool, thenBB, nextBB);
    
    // Generate then block
    builder->SetInsertPoint(thenBB);
    for (const auto& s : stmt.thenBranch) {
        generateStatement(s);
    }
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(mergeBB);
    }
    
    // Generate elif blocks
    for (size_t i = 0; i < stmt.elifBranches.size(); i++) {
        func->insert(func->end(), elifBBs[i]);
        builder->SetInsertPoint(elifBBs[i]);
        
        Value* elifCond = generateExpression(stmt.elifBranches[i].first);
        Value* elifCondBool = builder->CreateCall(getRuntimeFunction("moon_is_truthy"), {elifCond});
        builder->CreateCall(getRuntimeFunction("moon_release"), {elifCond});
        
        BasicBlock* elifThenBB = BasicBlock::Create(*context, "elifthen", func);
        BasicBlock* nextElifBB = (i + 1 < elifBBs.size()) ? elifBBs[i + 1] : elseBB;
        
        builder->CreateCondBr(elifCondBool, elifThenBB, nextElifBB);
        
        builder->SetInsertPoint(elifThenBB);
        for (const auto& s : stmt.elifBranches[i].second) {
            generateStatement(s);
        }
        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateBr(mergeBB);
        }
    }
    
    // Generate else block
    if (!stmt.elseBranch.empty()) {
        func->insert(func->end(), elseBB);
        builder->SetInsertPoint(elseBB);
        for (const auto& s : stmt.elseBranch) {
            generateStatement(s);
        }
        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateBr(mergeBB);
        }
    }
    
    // Continue with merge block
    func->insert(func->end(), mergeBB);
    builder->SetInsertPoint(mergeBB);
}

void LLVMCodeGen::generateWhileStmt(const WhileStmt& stmt) {
    // Check if we can use optimized native while loop
    if (canOptimizeWhileLoop(stmt)) {
        generateOptimizedWhileLoop(stmt);
        return;
    }
    
    Function* func = builder->GetInsertBlock()->getParent();
    
    // Pre-scan: Find native variables that will be assigned Dynamic values in the loop
    // and demote them BEFORE the loop starts (to avoid repeated demotion inside the loop)
    std::set<std::string> needsDemotion;
    std::function<void(const std::vector<StmtPtr>&)> scanForDemotion = 
        [this, &scanForDemotion, &needsDemotion](const std::vector<StmtPtr>& stmts) {
        for (const auto& s : stmts) {
            std::visit([this, &scanForDemotion, &needsDemotion](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, AssignStmt>) {
                    if (auto* id = std::get_if<Identifier>(&arg.target->value)) {
                        // Check if this variable is native and will be assigned Dynamic value
                        if (nativeIntVars.count(id->name) || nativeFloatVars.count(id->name)) {
                            NativeType valueType = inferExpressionType(arg.value);
                            if (valueType == NativeType::Dynamic) {
                                needsDemotion.insert(id->name);
                            }
                        }
                    }
                }
                else if constexpr (std::is_same_v<T, IfStmt>) {
                    scanForDemotion(arg.thenBranch);
                    for (const auto& elif : arg.elifBranches) {
                        scanForDemotion(elif.second);
                    }
                    scanForDemotion(arg.elseBranch);
                }
                else if constexpr (std::is_same_v<T, WhileStmt>) {
                    scanForDemotion(arg.body);
                }
                else if constexpr (std::is_same_v<T, ForRangeStmt>) {
                    scanForDemotion(arg.body);
                }
            }, s->value);
        }
    };
    scanForDemotion(stmt.body);
    
    // Demote identified variables BEFORE the loop
    for (const auto& name : needsDemotion) {
        if (nativeIntVars.count(name)) {
            Value* nativeVal = builder->CreateLoad(Type::getInt64Ty(*context), nativeIntVars[name]);
            Value* boxedVal = boxNativeInt(nativeVal);
            if (!namedValues.count(name) && !globalVars.count(name)) {
                Value* alloca = createAlloca(currentFunction, name);
                namedValues[name] = alloca;
            }
            if (namedValues.count(name)) {
                builder->CreateStore(boxedVal, namedValues[name]);
            } else if (globalVars.count(name)) {
                Value* oldVal = builder->CreateLoad(moonValuePtrType, globalVars[name]);
                builder->CreateCall(getRuntimeFunction("moon_release"), {oldVal});
                builder->CreateStore(boxedVal, globalVars[name]);
            }
            nativeIntVars.erase(name);
            variableTypes.erase(name);
        } else if (nativeFloatVars.count(name)) {
            Value* nativeVal = builder->CreateLoad(Type::getDoubleTy(*context), nativeFloatVars[name]);
            Value* boxedVal = boxNativeFloat(nativeVal);
            if (!namedValues.count(name) && !globalVars.count(name)) {
                Value* alloca = createAlloca(currentFunction, name);
                namedValues[name] = alloca;
            }
            if (namedValues.count(name)) {
                builder->CreateStore(boxedVal, namedValues[name]);
            } else if (globalVars.count(name)) {
                Value* oldVal = builder->CreateLoad(moonValuePtrType, globalVars[name]);
                builder->CreateCall(getRuntimeFunction("moon_release"), {oldVal});
                builder->CreateStore(boxedVal, globalVars[name]);
            }
            nativeFloatVars.erase(name);
            variableTypes.erase(name);
        }
    }
    
    BasicBlock* condBB = BasicBlock::Create(*context, "whilecond", func);
    BasicBlock* bodyBB = BasicBlock::Create(*context, "whilebody");
    BasicBlock* afterBB = BasicBlock::Create(*context, "whileend");
    
    // Save break/continue targets
    BasicBlock* savedBreak = currentBreakTarget;
    BasicBlock* savedContinue = currentContinueTarget;
    currentBreakTarget = afterBB;
    currentContinueTarget = condBB;
    
    builder->CreateBr(condBB);
    
    // Condition block
    builder->SetInsertPoint(condBB);
    Value* cond = generateExpression(stmt.condition);
    Value* condBool = builder->CreateCall(getRuntimeFunction("moon_is_truthy"), {cond});
    builder->CreateCall(getRuntimeFunction("moon_release"), {cond});
    builder->CreateCondBr(condBool, bodyBB, afterBB);
    
    // Body block
    func->insert(func->end(), bodyBB);
    builder->SetInsertPoint(bodyBB);
    for (const auto& s : stmt.body) {
        generateStatement(s);
    }
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(condBB);
    }
    
    // After block
    func->insert(func->end(), afterBB);
    builder->SetInsertPoint(afterBB);
    
    // Restore break/continue targets
    currentBreakTarget = savedBreak;
    currentContinueTarget = savedContinue;
}

void LLVMCodeGen::generateForInStmt(const ForInStmt& stmt) {
    Function* func = builder->GetInsertBlock()->getParent();
    
    // Get the iterable
    Value* iterable = generateExpression(stmt.iterable);
    
    // Get length
    Value* lenVal = builder->CreateCall(getRuntimeFunction("moon_len"), {iterable});
    Value* len = builder->CreateCall(getRuntimeFunction("moon_to_int"), {lenVal});
    builder->CreateCall(getRuntimeFunction("moon_release"), {lenVal});
    
    // Create index variable
    Value* indexPtr = builder->CreateAlloca(Type::getInt64Ty(*context), nullptr, "foridx");
    builder->CreateStore(ConstantInt::get(Type::getInt64Ty(*context), 0), indexPtr);
    
    BasicBlock* condBB = BasicBlock::Create(*context, "forcond", func);
    BasicBlock* bodyBB = BasicBlock::Create(*context, "forbody");
    BasicBlock* incrBB = BasicBlock::Create(*context, "forincr");
    BasicBlock* afterBB = BasicBlock::Create(*context, "forend");
    
    // Save break/continue targets
    BasicBlock* savedBreak = currentBreakTarget;
    BasicBlock* savedContinue = currentContinueTarget;
    currentBreakTarget = afterBB;
    currentContinueTarget = incrBB;
    
    builder->CreateBr(condBB);
    
    // Condition block
    builder->SetInsertPoint(condBB);
    Value* idx = builder->CreateLoad(Type::getInt64Ty(*context), indexPtr);
    Value* cond = builder->CreateICmpSLT(idx, len);
    builder->CreateCondBr(cond, bodyBB, afterBB);
    
    // Body block
    func->insert(func->end(), bodyBB);
    builder->SetInsertPoint(bodyBB);
    
    // Get current item
    Value* idxVal = builder->CreateCall(getRuntimeFunction("moon_int"), {idx});
    Value* item = builder->CreateCall(getRuntimeFunction("moon_list_get"), {iterable, idxVal});
    builder->CreateCall(getRuntimeFunction("moon_release"), {idxVal});
    
    // Store in loop variable
    storeVariable(stmt.variable, item);
    
    // Generate body
    for (const auto& s : stmt.body) {
        generateStatement(s);
    }
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(incrBB);
    }
    
    // Increment block
    func->insert(func->end(), incrBB);
    builder->SetInsertPoint(incrBB);
    Value* newIdx = builder->CreateAdd(builder->CreateLoad(Type::getInt64Ty(*context), indexPtr),
                                        ConstantInt::get(Type::getInt64Ty(*context), 1));
    builder->CreateStore(newIdx, indexPtr);
    builder->CreateBr(condBB);
    
    // After block
    func->insert(func->end(), afterBB);
    builder->SetInsertPoint(afterBB);
    
    // Release iterable
    builder->CreateCall(getRuntimeFunction("moon_release"), {iterable});
    
    // Restore break/continue targets
    currentBreakTarget = savedBreak;
    currentContinueTarget = savedContinue;
}

void LLVMCodeGen::generateForRangeStmt(const ForRangeStmt& stmt) {
    // Try to use optimized native loop if possible
    if (canOptimizeForLoop(stmt)) {
        generateOptimizedForRange(stmt);
        return;
    }
    
    // Fall back to dynamic loop
    Function* func = builder->GetInsertBlock()->getParent();
    
    // Get start and end values
    Value* startVal = generateExpression(stmt.start);
    Value* endVal = generateExpression(stmt.end);
    Value* start = builder->CreateCall(getRuntimeFunction("moon_to_int"), {startVal});
    Value* end = builder->CreateCall(getRuntimeFunction("moon_to_int"), {endVal});
    builder->CreateCall(getRuntimeFunction("moon_release"), {startVal});
    builder->CreateCall(getRuntimeFunction("moon_release"), {endVal});
    
    // Create index variable
    Value* indexPtr = builder->CreateAlloca(Type::getInt64Ty(*context), nullptr, "foridx");
    builder->CreateStore(start, indexPtr);
    
    BasicBlock* condBB = BasicBlock::Create(*context, "forcond", func);
    BasicBlock* bodyBB = BasicBlock::Create(*context, "forbody");
    BasicBlock* incrBB = BasicBlock::Create(*context, "forincr");
    BasicBlock* afterBB = BasicBlock::Create(*context, "forend");
    
    // Save break/continue targets
    BasicBlock* savedBreak = currentBreakTarget;
    BasicBlock* savedContinue = currentContinueTarget;
    currentBreakTarget = afterBB;
    currentContinueTarget = incrBB;
    
    builder->CreateBr(condBB);
    
    // Condition block
    builder->SetInsertPoint(condBB);
    Value* idx = builder->CreateLoad(Type::getInt64Ty(*context), indexPtr);
    Value* cond = builder->CreateICmpSLE(idx, end);
    builder->CreateCondBr(cond, bodyBB, afterBB);
    
    // Body block
    func->insert(func->end(), bodyBB);
    builder->SetInsertPoint(bodyBB);
    
    // Create MoonValue for loop variable
    Value* idxVal = builder->CreateCall(getRuntimeFunction("moon_int"), 
                                         {builder->CreateLoad(Type::getInt64Ty(*context), indexPtr)});
    storeVariable(stmt.variable, idxVal);
    
    // Generate body
    for (const auto& s : stmt.body) {
        generateStatement(s);
    }
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(incrBB);
    }
    
    // Increment block
    func->insert(func->end(), incrBB);
    builder->SetInsertPoint(incrBB);
    Value* newIdx = builder->CreateAdd(builder->CreateLoad(Type::getInt64Ty(*context), indexPtr),
                                        ConstantInt::get(Type::getInt64Ty(*context), 1));
    builder->CreateStore(newIdx, indexPtr);
    builder->CreateBr(condBB);
    
    // After block
    func->insert(func->end(), afterBB);
    builder->SetInsertPoint(afterBB);
    
    // Restore break/continue targets
    currentBreakTarget = savedBreak;
    currentContinueTarget = savedContinue;
}

void LLVMCodeGen::generateFuncDecl(const FuncDecl& stmt) {
    // Check if this is a pure numeric function - if so, generate native version first
    if (isPureNumericFunction(stmt)) {
        generateNativeFunction(stmt);
    }
    
    // Save current state FIRST (before any modifications)
    Function* savedFunc = currentFunction;
    BasicBlock* savedBlock = builder->GetInsertBlock();
    auto savedVars = namedValues;
    auto savedNativeIntVars = nativeIntVars;
    auto savedNativeFloatVars = nativeFloatVars;
    auto savedVariableTypes = variableTypes;
    auto savedDeclaredGlobals = declaredGlobals;
    bool savedInClosure = inClosure;
    auto savedClosureCaptures = currentClosureCaptures;
    auto savedFunctionParams = currentFunctionParams;
    
    // Check if this is a nested function (inside another user-defined function)
    // We check savedFunc != nullptr AND savedFunc != mainFunction to distinguish
    // between being inside main() vs being inside a user-defined function
    bool isNestedFunction = (savedFunc != nullptr && savedFunc != mainFunction);
    
    // Collect free variables for nested functions
    std::vector<std::string> captureList;
    std::vector<Value*> capturedValues;
    
    if (isNestedFunction) {
        // Build bound variables set (parameters of this function)
        std::set<std::string> boundVars;
        for (const auto& param : stmt.params) {
            boundVars.insert(param.name);
        }
        
        // Collect free variables from the function body
        std::set<std::string> freeVars;
        collectFreeVarsFromStmts(stmt.body, boundVars, freeVars);
        
        // Convert to sorted list for consistent ordering
        captureList.assign(freeVars.begin(), freeVars.end());
        std::sort(captureList.begin(), captureList.end());
        
        // Load captured values BEFORE switching context
        // Use generateIdentifier which handles both native variables and parent closure captures
        for (const auto& varName : captureList) {
            Value* val = generateIdentifier(varName);
            builder->CreateCall(getRuntimeFunction("moon_retain"), {val});
            capturedValues.push_back(val);
        }
    }
    
    // Get the already-declared function from pass 1
    Function* func = functions[stmt.name];
    if (!func) {
        // Function not pre-declared (e.g., nested function), create it now
        std::vector<Type*> paramTypes;
        for (size_t i = 0; i < stmt.params.size(); i++) {
            paramTypes.push_back(moonValuePtrType);
        }
        
        FunctionType* funcType = FunctionType::get(moonValuePtrType, paramTypes, false);
        func = Function::Create(funcType, Function::ExternalLinkage, 
                                           "moon_fn_" + stmt.name, module.get());
        
        // If function is exported and we're building a shared library, mark it for export
        if (stmt.isExported && buildingSharedLib) {
#ifdef _WIN32
            func->setDLLStorageClass(GlobalValue::DLLExportStorageClass);
#else
            func->setVisibility(GlobalValue::DefaultVisibility);
#endif
            exportedFunctions.push_back({stmt.name, (int)stmt.params.size()});
        }
        
        functions[stmt.name] = func;
    }
    
    // Setup new function
    currentFunction = func;
    BasicBlock* entry = BasicBlock::Create(*context, "entry", func);
    builder->SetInsertPoint(entry);
    
    // Set function param count early for recursive calls
    functionParamCounts[stmt.name] = stmt.params.size();
    
    // Clear variable tracking for the new function scope
    namedValues.clear();
    nativeIntVars.clear();
    nativeFloatVars.clear();
    variableTypes.clear();
    declaredGlobals.clear();
    currentFunctionParams.clear();
    
    // Set up closure capture mapping for nested functions
    if (!captureList.empty()) {
        inClosure = true;
        currentClosureCaptures.clear();
        for (size_t i = 0; i < captureList.size(); i++) {
            currentClosureCaptures[captureList[i]] = (int)i;
        }
    } else {
        inClosure = false;
        currentClosureCaptures.clear();
    }
    
    // Store parameters
    auto argIt = func->arg_begin();
    for (const auto& param : stmt.params) {
        Value* argVal = &*argIt++;
        argVal->setName(param.name);
        
        // Create alloca for parameter
        Value* alloca = builder->CreateAlloca(moonValuePtrType, nullptr, param.name);
        builder->CreateStore(argVal, alloca);
        namedValues[param.name] = alloca;
        
        // Track parameter name for cleanup on return
        currentFunctionParams.push_back(param.name);
        
        // Retain the argument
        builder->CreateCall(getRuntimeFunction("moon_retain"), {argVal});
    }
    
    // Track function entry for debug info
    Value* funcNameStr = builder->CreateGlobalStringPtr(stmt.name);
    builder->CreateCall(getRuntimeFunction("moon_enter_function"), {funcNameStr});
    
    // Generate body
    for (const auto& s : stmt.body) {
        generateStatement(s);
    }
    
    // Add implicit return null if needed
    if (!builder->GetInsertBlock()->getTerminator()) {
        // Release ALL local variables (including parameters) before returning
        for (const auto& pair : namedValues) {
            Value* varPtr = pair.second;
            Value* varVal = builder->CreateLoad(moonValuePtrType, varPtr);
            builder->CreateCall(getRuntimeFunction("moon_release"), {varVal});
        }
        builder->CreateCall(getRuntimeFunction("moon_exit_function"), {});
        builder->CreateRet(builder->CreateCall(getRuntimeFunction("moon_null"), {}));
    }
    
    // Restore state (including native type tracking and closure state)
    currentFunction = savedFunc;
    namedValues = savedVars;
    nativeIntVars = savedNativeIntVars;
    nativeFloatVars = savedNativeFloatVars;
    variableTypes = savedVariableTypes;
    declaredGlobals = savedDeclaredGlobals;
    inClosure = savedInClosure;
    currentClosureCaptures = savedClosureCaptures;
    currentFunctionParams = savedFunctionParams;
    builder->SetInsertPoint(savedBlock);
    
    // Create a wrapper function with uniform signature (MoonValue** args, int argc)
    // This is needed for storing functions in variables/dicts and calling via moon_call_func
    Type* valPtrPtrTy = PointerType::get(moonValuePtrType, 0);
    Type* i32Ty = Type::getInt32Ty(*context);
    FunctionType* wrapperType = FunctionType::get(moonValuePtrType, {valPtrPtrTy, i32Ty}, false);
    Function* wrapperFunc = Function::Create(wrapperType, Function::ExternalLinkage,
                                              "moon_wrap_" + stmt.name, module.get());
    
    BasicBlock* wrapperEntry = BasicBlock::Create(*context, "entry", wrapperFunc);
    builder->SetInsertPoint(wrapperEntry);
    
    // Get wrapper arguments
    auto wrapperArgs = wrapperFunc->arg_begin();
    Value* argsArray = &*wrapperArgs++;
    Value* argc = &*wrapperArgs;
    
    // Extract arguments from array and call the original function
    // Handle default parameters: if argc < param count, use default values
    std::vector<Value*> callArgs;
    for (size_t i = 0; i < stmt.params.size(); i++) {
        Value* argIdx = ConstantInt::get(i32Ty, i);
        
        // Check if this argument was provided
        Value* hasArg = builder->CreateICmpSLT(argIdx, argc);
        
        if (stmt.params[i].defaultValue) {
            // Has default value - conditionally use it
            BasicBlock* argProvidedBB = BasicBlock::Create(*context, "arg_provided", wrapperFunc);
            BasicBlock* useDefaultBB = BasicBlock::Create(*context, "use_default", wrapperFunc);
            BasicBlock* mergeBB = BasicBlock::Create(*context, "merge", wrapperFunc);
            
            builder->CreateCondBr(hasArg, argProvidedBB, useDefaultBB);
            
            // Argument provided path
            builder->SetInsertPoint(argProvidedBB);
            Value* argPtr = builder->CreateGEP(moonValuePtrType, argsArray, argIdx);
            Value* providedArg = builder->CreateLoad(moonValuePtrType, argPtr);
            builder->CreateBr(mergeBB);
            BasicBlock* argProvidedEnd = builder->GetInsertBlock();
            
            // Use default value path
            builder->SetInsertPoint(useDefaultBB);
            Value* defaultVal = generateExpression(stmt.params[i].defaultValue);
            builder->CreateBr(mergeBB);
            BasicBlock* useDefaultEnd = builder->GetInsertBlock();
            
            // Merge path
            builder->SetInsertPoint(mergeBB);
            PHINode* phi = builder->CreatePHI(moonValuePtrType, 2);
            phi->addIncoming(providedArg, argProvidedEnd);
            phi->addIncoming(defaultVal, useDefaultEnd);
            callArgs.push_back(phi);
        } else {
            // No default value - just load the argument
            Value* argPtr = builder->CreateGEP(moonValuePtrType, argsArray, argIdx);
            Value* arg = builder->CreateLoad(moonValuePtrType, argPtr);
            callArgs.push_back(arg);
        }
    }
    
    // Call the original function
    Value* result = builder->CreateCall(func, callArgs);
    builder->CreateRet(result);
    
    // Restore insertion point
    builder->SetInsertPoint(savedBlock);
    
    // Store wrapper function reference for calls with fewer arguments
    wrapperFunctions[stmt.name] = wrapperFunc;
    functionParamCounts[stmt.name] = stmt.params.size();
    
    // Store wrapper function as a variable - either as closure or regular function
    if (!captureList.empty()) {
        // Has captures - create closure
        Type* i32Ty = Type::getInt32Ty(*context);
        Value* captureCount = ConstantInt::get(i32Ty, captureList.size());
        Value* capturesArray = builder->CreateAlloca(moonValuePtrType, captureCount, "captures");
        
        // Store captured values in array
        for (size_t i = 0; i < capturedValues.size(); i++) {
            Value* idx = ConstantInt::get(i32Ty, i);
            Value* ptr = builder->CreateGEP(moonValuePtrType, capturesArray, idx);
            builder->CreateStore(capturedValues[i], ptr);
        }
        
        // Create closure using moon_closure_new(func, captures, count)
        Value* closureVal = builder->CreateCall(
            getRuntimeFunction("moon_closure_new"),
            {wrapperFunc, capturesArray, captureCount}
        );
        
        // Release the captured values we loaded (they're now owned by the closure)
        for (auto& val : capturedValues) {
            builder->CreateCall(getRuntimeFunction("moon_release"), {val});
        }
        
        storeVariable(stmt.name, closureVal);
    } else {
        // No captures - use simple function wrapper
        Value* funcVal = builder->CreateCall(getRuntimeFunction("moon_func"), {wrapperFunc});
        storeVariable(stmt.name, funcVal);
    }
}

void LLVMCodeGen::generateReturnStmt(const ReturnStmt& stmt) {
    Value* retVal;
    if (stmt.value) {
        retVal = generateExpression(stmt.value);
    } else {
        retVal = builder->CreateCall(getRuntimeFunction("moon_null"), {});
    }
    
    // Release ALL local variables (including parameters) before returning
    // This balances the moon_retain done for params and variable assignments
    for (const auto& pair : namedValues) {
        Value* varPtr = pair.second;
        Value* varVal = builder->CreateLoad(moonValuePtrType, varPtr);
        builder->CreateCall(getRuntimeFunction("moon_release"), {varVal});
    }
    
    // Track function exit for debug info
    builder->CreateCall(getRuntimeFunction("moon_exit_function"), {});
    builder->CreateRet(retVal);
}

void LLVMCodeGen::generateBreakStmt() {
    if (currentBreakTarget) {
        builder->CreateBr(currentBreakTarget);
    }
}

void LLVMCodeGen::generateContinueStmt() {
    if (currentContinueTarget) {
        builder->CreateBr(currentContinueTarget);
    }
}

void LLVMCodeGen::generateTryStmt(const TryStmt& stmt) {
    Function* func = builder->GetInsertBlock()->getParent();
    
    // Allocate space for jmp_buf
    // On Windows x64, jmp_buf is ~256 bytes (_JUMP_BUFFER struct)
    // We allocate 512 bytes to be safe across all platforms
    // Must be 16-byte aligned for XMM registers on Windows
    Type* jmpBufType = ArrayType::get(Type::getInt64Ty(*context), 64);  // 512 bytes, naturally aligned
    Value* jmpBuf = builder->CreateAlloca(jmpBufType, nullptr, "jmp_buf");
    Value* jmpBufPtr = builder->CreateBitCast(jmpBuf, PointerType::get(Type::getInt8Ty(*context), 0));
    
    // Create basic blocks
    BasicBlock* tryBB = BasicBlock::Create(*context, "try", func);
    BasicBlock* catchBB = BasicBlock::Create(*context, "catch", func);
    BasicBlock* afterBB = BasicBlock::Create(*context, "try_end", func);
    
    // Call setjmp - returns 0 normally, non-zero when jumping back from exception
    // On Windows x64 MSVC, _setjmp takes two parameters: jmp_buf and frame_address (NULL)
#ifdef _WIN32
    Value* nullPtr = Constant::getNullValue(PointerType::get(Type::getInt8Ty(*context), 0));
    Value* setjmpResult = builder->CreateCall(getRuntimeFunction("_setjmp"), {jmpBufPtr, nullPtr});
#else
    Value* setjmpResult = builder->CreateCall(getRuntimeFunction("setjmp"), {jmpBufPtr});
#endif
    
    // Branch based on setjmp result: 0 = normal, non-zero = exception caught
    Value* isException = builder->CreateICmpNE(setjmpResult, 
        ConstantInt::get(Type::getInt32Ty(*context), 0));
    builder->CreateCondBr(isException, catchBB, tryBB);
    
    // Try block - normal execution path
    builder->SetInsertPoint(tryBB);
    
    // Register this try block with the runtime ONLY when entering try (setjmp returned 0)
    builder->CreateCall(getRuntimeFunction("moon_try_begin"), {jmpBufPtr});
    
    for (const auto& s : stmt.tryBody) {
        generateStatement(s);
        // Check if we've already terminated this block (e.g., return statement)
        if (builder->GetInsertBlock()->getTerminator()) {
            break;
        }
    }
    // If try block completed normally, skip catch and go to end
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateCall(getRuntimeFunction("moon_try_end"), {});
        builder->CreateBr(afterBB);
    }
    
    // Catch block - exception handling path
    builder->SetInsertPoint(catchBB);
    
    // Get the exception value and store it in the error variable
    Value* exceptionVal = builder->CreateCall(getRuntimeFunction("moon_get_exception"), {});
    storeVariable(stmt.errorVar, exceptionVal);
    
    // Execute catch body
    for (const auto& s : stmt.catchBody) {
        generateStatement(s);
        if (builder->GetInsertBlock()->getTerminator()) {
            break;
        }
    }
    
    // End the try block (pop from stack after catch completes)
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateCall(getRuntimeFunction("moon_try_end"), {});
        builder->CreateBr(afterBB);
    }
    
    // Continue after try-catch
    builder->SetInsertPoint(afterBB);
}

void LLVMCodeGen::generateThrowStmt(const ThrowStmt& stmt) {
    // Generate the exception value
    Value* exceptionVal = generateExpression(stmt.value);
    
    // Call moon_throw (this will either longjmp to a catch block or exit)
    builder->CreateCall(getRuntimeFunction("moon_throw"), {exceptionVal});
    
    // Add unreachable after throw since control doesn't continue normally
    builder->CreateUnreachable();
}

void LLVMCodeGen::generateSwitchStmt(const SwitchStmt& stmt) {
    Value* switchVal = generateExpression(stmt.value);
    
    Function* func = builder->GetInsertBlock()->getParent();
    BasicBlock* mergeBB = BasicBlock::Create(*context, "switchend");
    
    std::vector<std::pair<BasicBlock*, const std::vector<StmtPtr>*>> caseBodies;
    
    // Create blocks for each case
    for (size_t i = 0; i < stmt.cases.size(); i++) {
        BasicBlock* caseBB = BasicBlock::Create(*context, "case", func);
        caseBodies.push_back({caseBB, &stmt.cases[i].body});
    }
    
    BasicBlock* defaultBB = stmt.defaultBody.empty() ? mergeBB : 
                            BasicBlock::Create(*context, "default", func);
    
    // Generate case comparisons
    BasicBlock* currentCheckBB = builder->GetInsertBlock();
    
    for (size_t i = 0; i < stmt.cases.size(); i++) {
        builder->SetInsertPoint(currentCheckBB);
        
        BasicBlock* nextCheckBB = (i + 1 < stmt.cases.size()) ? 
                                   BasicBlock::Create(*context, "casecheck", func) : nullptr;
        
        // Check all values in this case
        for (size_t j = 0; j < stmt.cases[i].values.size(); j++) {
            Value* caseVal = generateExpression(stmt.cases[i].values[j]);
            Value* eqResult = builder->CreateCall(getRuntimeFunction("moon_eq"), {switchVal, caseVal});
            Value* isEq = builder->CreateCall(getRuntimeFunction("moon_is_truthy"), {eqResult});
            builder->CreateCall(getRuntimeFunction("moon_release"), {eqResult});
            builder->CreateCall(getRuntimeFunction("moon_release"), {caseVal});
            
            BasicBlock* matchBB = caseBodies[i].first;
            BasicBlock* noMatchBB = (j + 1 < stmt.cases[i].values.size()) ?
                                     BasicBlock::Create(*context, "caseor", func) :
                                     (nextCheckBB ? nextCheckBB : defaultBB);
            
            builder->CreateCondBr(isEq, matchBB, noMatchBB);
            
            if (j + 1 < stmt.cases[i].values.size()) {
                builder->SetInsertPoint(noMatchBB);
            }
        }
        
        currentCheckBB = nextCheckBB;
    }
    
    // Generate case bodies
    for (size_t i = 0; i < caseBodies.size(); i++) {
        builder->SetInsertPoint(caseBodies[i].first);
        for (const auto& s : *caseBodies[i].second) {
            generateStatement(s);
        }
        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateBr(mergeBB);
        }
    }
    
    // Generate default body
    if (!stmt.defaultBody.empty()) {
        builder->SetInsertPoint(defaultBB);
        for (const auto& s : stmt.defaultBody) {
            generateStatement(s);
        }
        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateBr(mergeBB);
        }
    }
    
    // Release switch value
    builder->SetInsertPoint(mergeBB);
    func->insert(func->end(), mergeBB);
    builder->CreateCall(getRuntimeFunction("moon_release"), {switchVal});
}

void LLVMCodeGen::generateClassDecl(const ClassDecl& stmt) {
    // Create class
    Value* className = createGlobalString(stmt.name);
    
    // Get parent class pointer (not string!) - must be MoonClass* or NULL
    Value* parentClassPtr;
    if (stmt.parentName.empty()) {
        parentClassPtr = ConstantPointerNull::get(PointerType::get(Type::getInt8Ty(*context), 0));
    } else {
        // Load parent class from classDefinitions
        if (classDefinitions.count(stmt.parentName)) {
            parentClassPtr = builder->CreateLoad(
                PointerType::get(Type::getInt8Ty(*context), 0),
                classDefinitions[stmt.parentName]
            );
        } else {
            // Parent class not found - use NULL (will cause runtime error)
            parentClassPtr = ConstantPointerNull::get(PointerType::get(Type::getInt8Ty(*context), 0));
        }
    }
    
    Value* klass = builder->CreateCall(getRuntimeFunction("moon_class_new"), {className, parentClassPtr});
    
    // Store class pointer in global
    GlobalVariable* classGlobal = new GlobalVariable(
        *module,
        PointerType::get(Type::getInt8Ty(*context), 0),
        false,
        GlobalValue::InternalLinkage,
        ConstantPointerNull::get(PointerType::get(Type::getInt8Ty(*context), 0)),
        "class_" + stmt.name
    );
    builder->CreateStore(klass, classGlobal);
    classDefinitions[stmt.name] = classGlobal;
    
    // Generate methods
    // MoonFunc signature: MoonValue* (*)(MoonValue** args, int argc)
    Type* valPtrTy = moonValuePtrType;
    Type* valPtrPtrTy = PointerType::get(valPtrTy, 0);
    Type* i32Ty = Type::getInt32Ty(*context);
    FunctionType* methodFuncType = FunctionType::get(valPtrTy, {valPtrPtrTy, i32Ty}, false);
    
    for (const auto& method : stmt.methods) {
        std::string methodFuncName = "moon_method_" + stmt.name + "_" + method.name;
        
        Function* methodFunc = Function::Create(
            methodFuncType,
            Function::ExternalLinkage,
            methodFuncName,
            module.get()
        );
        
        // Save current state (including native type tracking)
        Function* savedFunc = currentFunction;
        BasicBlock* savedBlock = builder->GetInsertBlock();
        auto savedVars = namedValues;
        auto savedNativeIntVars = nativeIntVars;
        auto savedNativeFloatVars = nativeFloatVars;
        auto savedVariableTypes = variableTypes;
        std::string savedClassName = currentClassName;
        
        // Setup method function
        currentFunction = methodFunc;
        currentClassName = stmt.name;
        BasicBlock* entry = BasicBlock::Create(*context, "entry", methodFunc);
        builder->SetInsertPoint(entry);
        
        // Get args and argc from function parameters
        auto argIt = methodFunc->arg_begin();
        Value* argsPtr = &*argIt++;
        argsPtr->setName("args");
        Value* argc = &*argIt;
        argc->setName("argc");
        
        // Extract parameters from args array - clear all variable tracking
        namedValues.clear();
        nativeIntVars.clear();
        nativeFloatVars.clear();
        variableTypes.clear();
        int paramIndex = 0;
        
        // For non-static methods, first arg is self
        if (!method.isStatic) {
            Value* selfIdx = ConstantInt::get(i32Ty, paramIndex);
            Value* selfPtr = builder->CreateGEP(valPtrTy, argsPtr, selfIdx);
            Value* selfVal = builder->CreateLoad(valPtrTy, selfPtr);
            
            Value* selfAlloca = builder->CreateAlloca(valPtrTy, nullptr, "self");
            builder->CreateStore(selfVal, selfAlloca);
            namedValues["self"] = selfAlloca;
            builder->CreateCall(getRuntimeFunction("moon_retain"), {selfVal});
            paramIndex++;
        }
        
        // Extract remaining parameters (with default value support)
        for (const auto& param : method.params) {
            Value* paramIdx = ConstantInt::get(i32Ty, paramIndex);
            Value* hasArg = builder->CreateICmpSLT(paramIdx, argc);
            
            Value* paramVal;
            if (param.defaultValue) {
                // Has default value - conditionally use it
                BasicBlock* argProvidedBB = BasicBlock::Create(*context, "arg_provided", methodFunc);
                BasicBlock* useDefaultBB = BasicBlock::Create(*context, "use_default", methodFunc);
                BasicBlock* mergeBB = BasicBlock::Create(*context, "merge", methodFunc);
                
                builder->CreateCondBr(hasArg, argProvidedBB, useDefaultBB);
                
                // Argument provided path
                builder->SetInsertPoint(argProvidedBB);
                Value* paramPtr = builder->CreateGEP(valPtrTy, argsPtr, paramIdx);
                Value* providedVal = builder->CreateLoad(valPtrTy, paramPtr);
                builder->CreateBr(mergeBB);
                BasicBlock* argProvidedEnd = builder->GetInsertBlock();
                
                // Use default value path
                builder->SetInsertPoint(useDefaultBB);
                Value* defaultVal = generateExpression(param.defaultValue);
                builder->CreateBr(mergeBB);
                BasicBlock* useDefaultEnd = builder->GetInsertBlock();
                
                // Merge path
                builder->SetInsertPoint(mergeBB);
                PHINode* phi = builder->CreatePHI(valPtrTy, 2);
                phi->addIncoming(providedVal, argProvidedEnd);
                phi->addIncoming(defaultVal, useDefaultEnd);
                paramVal = phi;
            } else {
                // No default value - just load the argument
                Value* paramPtr = builder->CreateGEP(valPtrTy, argsPtr, paramIdx);
                paramVal = builder->CreateLoad(valPtrTy, paramPtr);
            }
            
            Value* paramAlloca = builder->CreateAlloca(valPtrTy, nullptr, param.name);
            builder->CreateStore(paramVal, paramAlloca);
            namedValues[param.name] = paramAlloca;
            builder->CreateCall(getRuntimeFunction("moon_retain"), {paramVal});
            paramIndex++;
        }
        
        // Generate method body
        for (const auto& s : method.body) {
            generateStatement(s);
        }
        
        // Add implicit return null if needed
        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateRet(builder->CreateCall(getRuntimeFunction("moon_null"), {}));
        }
        
        // Restore state (including native type tracking)
        currentFunction = savedFunc;
        currentClassName = savedClassName;
        namedValues = savedVars;
        nativeIntVars = savedNativeIntVars;
        nativeFloatVars = savedNativeFloatVars;
        variableTypes = savedVariableTypes;
        builder->SetInsertPoint(savedBlock);
        
        // Register method with class
        Value* methodName = createGlobalString(method.name);
        Value* isStatic = ConstantInt::get(Type::getInt1Ty(*context), method.isStatic);
        builder->CreateCall(getRuntimeFunction("moon_class_add_method"), 
                           {klass, methodName, methodFunc, isStatic});
    }
}

void LLVMCodeGen::generateImportStmt(const ImportStmt& stmt) {
    // Import handling would be done at compile time by bundling modules
    // For now, this is a no-op in code generation
}

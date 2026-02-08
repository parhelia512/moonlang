// ============================================================================
// Native Type Optimization Module
// ============================================================================
// This file contains native type optimization functions for LLVM code generation.
// 
// IMPORTANT: This file should be included by llvm_codegen.cpp, not compiled
// separately. It is meant to be included inline to keep the optimization code
// organized while maintaining a single compilation unit.
// ============================================================================

// ============================================================================
// Native Type Optimization Implementation
// ============================================================================

NativeType LLVMCodeGen::inferExpressionType(const ExprPtr& expr) {
    if (!expr) return NativeType::Dynamic;
    
    // Integer literal
    if (auto* lit = std::get_if<IntegerLiteral>(&expr->value)) {
        return NativeType::NativeInt;
    }
    
    // Float literal
    if (auto* lit = std::get_if<FloatLiteral>(&expr->value)) {
        return NativeType::NativeFloat;
    }
    
    // Boolean literal
    if (auto* lit = std::get_if<BoolLiteral>(&expr->value)) {
        return NativeType::NativeBool;
    }
    
    // Identifier - check tracked type
    if (auto* id = std::get_if<Identifier>(&expr->value)) {
        auto it = variableTypes.find(id->name);
        if (it != variableTypes.end()) {
            // Return the tracked type - this allows pre-marking loop variables
            // before their storage is allocated (for type inference during scanning)
            if (it->second == NativeType::NativeInt) {
                return NativeType::NativeInt;
            }
            if (it->second == NativeType::NativeFloat) {
                return NativeType::NativeFloat;
            }
        }
        return NativeType::Dynamic;
    }
    
    // Native function call - check if the function is known to be numeric
    if (auto* call = std::get_if<CallExpr>(&expr->value)) {
        if (auto* id = std::get_if<Identifier>(&call->callee->value)) {
            if (nativeFunctions.count(id->name)) {
                // Native functions return int64
                return NativeType::NativeInt;
            }
        }
    }
    
    // Binary expression - check if both sides can be treated as numeric
    if (auto* bin = std::get_if<BinaryExpr>(&expr->value)) {
        NativeType leftType = inferExpressionType(bin->left);
        NativeType rightType = inferExpressionType(bin->right);
        
        // ONLY use native operations when we KNOW both operands are numeric
        // Do NOT assume Dynamic variables are numeric - they could be lists, strings, etc.
        bool leftNumeric = (leftType == NativeType::NativeInt || leftType == NativeType::NativeFloat);
        bool rightNumeric = (rightType == NativeType::NativeInt || rightType == NativeType::NativeFloat);
        
        // Arithmetic operators
        if (bin->op == "+" || bin->op == "-" || bin->op == "*" || bin->op == "%" ) {
            if (leftNumeric && rightNumeric) {
                // If both are definitely ints, result is int
                if (leftType == NativeType::NativeInt && rightType == NativeType::NativeInt) {
                    return NativeType::NativeInt;
                }
                // If either is float, result is float
                if (leftType == NativeType::NativeFloat || rightType == NativeType::NativeFloat) {
                    return NativeType::NativeFloat;
                }
                return NativeType::NativeInt;
            }
        }
        
        // Division always returns float
        if (bin->op == "/") {
            if (leftNumeric && rightNumeric) {
                return NativeType::NativeFloat;
            }
        }
        
        // Comparison operators
        if (bin->op == "<" || bin->op == ">" || bin->op == "<=" || bin->op == ">=" ||
            bin->op == "==" || bin->op == "!=") {
            if (leftNumeric && rightNumeric) {
                return NativeType::NativeBool;
            }
        }
    }
    
    return NativeType::Dynamic;
}

TypedValue LLVMCodeGen::generateNativeExpression(const ExprPtr& expr) {
    if (!expr) return TypedValue(generateNullLiteral(), NativeType::Dynamic);
    
    NativeType exprType = inferExpressionType(expr);
    
    // Integer literal - return native i64
    if (auto* lit = std::get_if<IntegerLiteral>(&expr->value)) {
        Value* val = ConstantInt::get(Type::getInt64Ty(*context), lit->value);
        return TypedValue(val, NativeType::NativeInt);
    }
    
    // Float literal - return native double
    if (auto* flt = std::get_if<FloatLiteral>(&expr->value)) {
        Value* val = ConstantFP::get(Type::getDoubleTy(*context), flt->value);
        return TypedValue(val, NativeType::NativeFloat);
    }
    
    // Boolean literal
    if (auto* boolLit = std::get_if<BoolLiteral>(&expr->value)) {
        Value* val = ConstantInt::get(Type::getInt1Ty(*context), boolLit->value ? 1 : 0);
        return TypedValue(val, NativeType::NativeBool);
    }
    
    // Identifier - load native if available and actually exists
    if (auto* id = std::get_if<Identifier>(&expr->value)) {
        auto typeIt = variableTypes.find(id->name);
        if (typeIt != variableTypes.end()) {
            if (typeIt->second == NativeType::NativeInt && nativeIntVars.count(id->name)) {
                return TypedValue(loadNativeInt(id->name), NativeType::NativeInt);
            }
            if (typeIt->second == NativeType::NativeFloat && nativeFloatVars.count(id->name)) {
                return TypedValue(loadNativeFloat(id->name), NativeType::NativeFloat);
            }
        }
        // Fall back to dynamic - make sure variable exists first
        if (namedValues.count(id->name) || globalVars.count(id->name)) {
            Value* val = loadVariable(id->name);
            return TypedValue(val, NativeType::Dynamic);
        }
        // Variable doesn't exist anywhere - return null
        return TypedValue(builder->CreateCall(getRuntimeFunction("moon_null"), {}), NativeType::Dynamic);
    }
    
    // Binary expression
    if (auto* bin = std::get_if<BinaryExpr>(&expr->value)) {
        return generateNativeBinaryExpr(*bin, exprType);
    }
    
    // Call expression - check if it's a native function
    if (auto* call = std::get_if<CallExpr>(&expr->value)) {
        if (auto* id = std::get_if<Identifier>(&call->callee->value)) {
            if (nativeFunctions.count(id->name)) {
                // Call native function directly
                Function* nativeFunc = nativeFunctions[id->name];
                std::vector<Value*> nativeArgs;
                
                for (const auto& arg : call->arguments) {
                    TypedValue typedArg = generateNativeExpression(arg);
                    Value* argVal = typedArg.value;
                    
                    if (typedArg.type == NativeType::Dynamic) {
                        argVal = unboxToInt(typedArg.value);
                        builder->CreateCall(getRuntimeFunction("moon_release"), {typedArg.value});
                    } else if (typedArg.type == NativeType::NativeFloat) {
                        argVal = builder->CreateFPToSI(typedArg.value, Type::getInt64Ty(*context));
                    } else if (typedArg.type == NativeType::NativeBool) {
                        argVal = builder->CreateZExt(typedArg.value, Type::getInt64Ty(*context));
                    }
                    
                    nativeArgs.push_back(argVal);
                }
                
                Value* result = builder->CreateCall(nativeFunc, nativeArgs);
                return TypedValue(result, NativeType::NativeInt);
            }
        }
    }
    
    // Fall back to dynamic expression
    return TypedValue(generateExpression(expr), NativeType::Dynamic);
}

TypedValue LLVMCodeGen::generateNativeBinaryExpr(const BinaryExpr& expr, NativeType expectedType) {
    TypedValue left = generateNativeExpression(expr.left);
    TypedValue right = generateNativeExpression(expr.right);
    
    // Convert Dynamic (MoonValue*) operands to native ints
    if (left.type == NativeType::Dynamic) {
        Value* nativeVal = unboxToInt(left.value);
        builder->CreateCall(getRuntimeFunction("moon_release"), {left.value});
        left = TypedValue(nativeVal, NativeType::NativeInt);
    }
    if (right.type == NativeType::Dynamic) {
        Value* nativeVal = unboxToInt(right.value);
        builder->CreateCall(getRuntimeFunction("moon_release"), {right.value});
        right = TypedValue(nativeVal, NativeType::NativeInt);
    }
    
    // Both are native ints
    if (left.type == NativeType::NativeInt && right.type == NativeType::NativeInt) {
        Value* result = nullptr;
        
        if (expr.op == "+") {
            // Fast path: in optimized loops, use pure native addition without overflow checking
            if (inOptimizedForLoop) {
                result = builder->CreateAdd(left.value, right.value, "addtmp");
                return TypedValue(result, NativeType::NativeInt);
            }
            
            // Use overflow-checking intrinsic
            Function* intrinsic = Intrinsic::getDeclaration(module.get(), Intrinsic::sadd_with_overflow, {Type::getInt64Ty(*context)});
            Value* resultStruct = builder->CreateCall(intrinsic, {left.value, right.value}, "addov");
            Value* sumResult = builder->CreateExtractValue(resultStruct, 0, "sum");
            Value* overflowed = builder->CreateExtractValue(resultStruct, 1, "overflow");
            
            // Create blocks for overflow handling
            Function* func = builder->GetInsertBlock()->getParent();
            BasicBlock* overflowBB = BasicBlock::Create(*context, "overflow", func);
            BasicBlock* noOverflowBB = BasicBlock::Create(*context, "nooverflow", func);
            BasicBlock* mergeBB = BasicBlock::Create(*context, "mergeadd", func);
            
            builder->CreateCondBr(overflowed, overflowBB, noOverflowBB);
            
            // Overflow path: call moon_add which will return BigInt
            builder->SetInsertPoint(overflowBB);
            Value* boxedA = boxNativeInt(left.value);
            Value* boxedB = boxNativeInt(right.value);
            Value* bigintResult = builder->CreateCall(getRuntimeFunction("moon_add"), {boxedA, boxedB}, "bigintadd");
            builder->CreateBr(mergeBB);
            BasicBlock* overflowEndBB = builder->GetInsertBlock();
            
            // No overflow path: box as int
            builder->SetInsertPoint(noOverflowBB);
            Value* boxedInt = boxNativeInt(sumResult);
            builder->CreateBr(mergeBB);
            BasicBlock* noOverflowEndBB = builder->GetInsertBlock();
            
            // Merge: return boxed value (dynamic type)
            builder->SetInsertPoint(mergeBB);
            PHINode* phi = builder->CreatePHI(moonValuePtrType, 2, "addresult");
            phi->addIncoming(bigintResult, overflowEndBB);
            phi->addIncoming(boxedInt, noOverflowEndBB);
            
            return TypedValue(phi, NativeType::Dynamic);
        }
        if (expr.op == "-") {
            // Fast path: in optimized loops, use pure native subtraction without overflow checking
            if (inOptimizedForLoop) {
                result = builder->CreateSub(left.value, right.value, "subtmp");
                return TypedValue(result, NativeType::NativeInt);
            }
            
            // Use overflow-checking intrinsic
            Function* intrinsic = Intrinsic::getDeclaration(module.get(), Intrinsic::ssub_with_overflow, {Type::getInt64Ty(*context)});
            Value* resultStruct = builder->CreateCall(intrinsic, {left.value, right.value}, "subov");
            Value* subResult = builder->CreateExtractValue(resultStruct, 0, "sub");
            Value* overflowed = builder->CreateExtractValue(resultStruct, 1, "overflow");
            
            Function* func = builder->GetInsertBlock()->getParent();
            BasicBlock* overflowBB = BasicBlock::Create(*context, "overflow", func);
            BasicBlock* noOverflowBB = BasicBlock::Create(*context, "nooverflow", func);
            BasicBlock* mergeBB = BasicBlock::Create(*context, "mergesub", func);
            
            builder->CreateCondBr(overflowed, overflowBB, noOverflowBB);
            
            // Overflow path: call moon_sub which will return BigInt
            builder->SetInsertPoint(overflowBB);
            Value* boxedA = boxNativeInt(left.value);
            Value* boxedB = boxNativeInt(right.value);
            Value* bigintResult = builder->CreateCall(getRuntimeFunction("moon_sub"), {boxedA, boxedB}, "bigintsub");
            builder->CreateBr(mergeBB);
            BasicBlock* overflowEndBB = builder->GetInsertBlock();
            
            builder->SetInsertPoint(noOverflowBB);
            Value* boxedInt = boxNativeInt(subResult);
            builder->CreateBr(mergeBB);
            BasicBlock* noOverflowEndBB = builder->GetInsertBlock();
            
            builder->SetInsertPoint(mergeBB);
            PHINode* phi = builder->CreatePHI(moonValuePtrType, 2, "subresult");
            phi->addIncoming(bigintResult, overflowEndBB);
            phi->addIncoming(boxedInt, noOverflowEndBB);
            
            return TypedValue(phi, NativeType::Dynamic);
        }
        if (expr.op == "*") {
            // Fast path: in optimized loops, use pure native multiplication without overflow checking
            if (inOptimizedForLoop) {
                result = builder->CreateMul(left.value, right.value, "multmp");
                return TypedValue(result, NativeType::NativeInt);
            }
            
            // Use overflow-checking intrinsic
            Function* intrinsic = Intrinsic::getDeclaration(module.get(), Intrinsic::smul_with_overflow, {Type::getInt64Ty(*context)});
            Value* resultStruct = builder->CreateCall(intrinsic, {left.value, right.value}, "mulov");
            Value* mulResult = builder->CreateExtractValue(resultStruct, 0, "mul");
            Value* overflowed = builder->CreateExtractValue(resultStruct, 1, "overflow");
            
            Function* func = builder->GetInsertBlock()->getParent();
            BasicBlock* overflowBB = BasicBlock::Create(*context, "overflow", func);
            BasicBlock* noOverflowBB = BasicBlock::Create(*context, "nooverflow", func);
            BasicBlock* mergeBB = BasicBlock::Create(*context, "mergemul", func);
            
            builder->CreateCondBr(overflowed, overflowBB, noOverflowBB);
            
            // Overflow path: call moon_mul which will return BigInt
            builder->SetInsertPoint(overflowBB);
            Value* boxedA = boxNativeInt(left.value);
            Value* boxedB = boxNativeInt(right.value);
            Value* bigintResult = builder->CreateCall(getRuntimeFunction("moon_mul"), {boxedA, boxedB}, "bigintmul");
            builder->CreateBr(mergeBB);
            BasicBlock* overflowEndBB = builder->GetInsertBlock();
            
            builder->SetInsertPoint(noOverflowBB);
            Value* boxedInt = boxNativeInt(mulResult);
            builder->CreateBr(mergeBB);
            BasicBlock* noOverflowEndBB = builder->GetInsertBlock();
            
            builder->SetInsertPoint(mergeBB);
            PHINode* phi = builder->CreatePHI(moonValuePtrType, 2, "mulresult");
            phi->addIncoming(bigintResult, overflowEndBB);
            phi->addIncoming(boxedInt, noOverflowEndBB);
            
            return TypedValue(phi, NativeType::Dynamic);
        }
        if (expr.op == "%") {
            result = builder->CreateSRem(left.value, right.value, "modtmp");
            return TypedValue(result, NativeType::NativeInt);
        }
        if (expr.op == "/") {
            // Division returns float
            Value* leftF = builder->CreateSIToFP(left.value, Type::getDoubleTy(*context));
            Value* rightF = builder->CreateSIToFP(right.value, Type::getDoubleTy(*context));
            result = builder->CreateFDiv(leftF, rightF, "divtmp");
            return TypedValue(result, NativeType::NativeFloat);
        }
        
        // Comparison operators
        if (expr.op == "<") {
            result = builder->CreateICmpSLT(left.value, right.value, "cmptmp");
            return TypedValue(result, NativeType::NativeBool);
        }
        if (expr.op == ">") {
            result = builder->CreateICmpSGT(left.value, right.value, "cmptmp");
            return TypedValue(result, NativeType::NativeBool);
        }
        if (expr.op == "<=") {
            result = builder->CreateICmpSLE(left.value, right.value, "cmptmp");
            return TypedValue(result, NativeType::NativeBool);
        }
        if (expr.op == ">=") {
            result = builder->CreateICmpSGE(left.value, right.value, "cmptmp");
            return TypedValue(result, NativeType::NativeBool);
        }
        if (expr.op == "==") {
            result = builder->CreateICmpEQ(left.value, right.value, "cmptmp");
            return TypedValue(result, NativeType::NativeBool);
        }
        if (expr.op == "!=") {
            result = builder->CreateICmpNE(left.value, right.value, "cmptmp");
            return TypedValue(result, NativeType::NativeBool);
        }
        
        // Bitwise operators (only work on ints)
        if (expr.op == "&") {
            result = builder->CreateAnd(left.value, right.value, "andtmp");
            return TypedValue(result, NativeType::NativeInt);
        }
        if (expr.op == "|") {
            result = builder->CreateOr(left.value, right.value, "ortmp");
            return TypedValue(result, NativeType::NativeInt);
        }
        if (expr.op == "^") {
            result = builder->CreateXor(left.value, right.value, "xortmp");
            return TypedValue(result, NativeType::NativeInt);
        }
        if (expr.op == "<<") {
            result = builder->CreateShl(left.value, right.value, "shltmp");
            return TypedValue(result, NativeType::NativeInt);
        }
        if (expr.op == ">>") {
            result = builder->CreateAShr(left.value, right.value, "ashrtmp");
            return TypedValue(result, NativeType::NativeInt);
        }
        
        // Power operator - fall through to runtime for int ** int
        // (could use LLVM powi intrinsic but runtime is simpler)
    }
    
    // Convert to floats if mixed or float operation
    if ((left.type == NativeType::NativeInt || left.type == NativeType::NativeFloat) &&
        (right.type == NativeType::NativeInt || right.type == NativeType::NativeFloat)) {
        
        Value* leftF = left.value;
        Value* rightF = right.value;
        
        if (left.type == NativeType::NativeInt) {
            leftF = builder->CreateSIToFP(left.value, Type::getDoubleTy(*context));
        }
        if (right.type == NativeType::NativeInt) {
            rightF = builder->CreateSIToFP(right.value, Type::getDoubleTy(*context));
        }
        
        Value* result = nullptr;
        
        if (expr.op == "+") {
            result = builder->CreateFAdd(leftF, rightF, "addtmp");
            return TypedValue(result, NativeType::NativeFloat);
        }
        if (expr.op == "-") {
            result = builder->CreateFSub(leftF, rightF, "subtmp");
            return TypedValue(result, NativeType::NativeFloat);
        }
        if (expr.op == "*") {
            result = builder->CreateFMul(leftF, rightF, "multmp");
            return TypedValue(result, NativeType::NativeFloat);
        }
        if (expr.op == "/") {
            result = builder->CreateFDiv(leftF, rightF, "divtmp");
            return TypedValue(result, NativeType::NativeFloat);
        }
        if (expr.op == "**") {
            // Power operation using LLVM pow intrinsic
            Function* powFunc = Intrinsic::getDeclaration(module.get(), Intrinsic::pow, {Type::getDoubleTy(*context)});
            result = builder->CreateCall(powFunc, {leftF, rightF}, "powtmp");
            return TypedValue(result, NativeType::NativeFloat);
        }
        
        // Float comparisons
        if (expr.op == "<") {
            result = builder->CreateFCmpOLT(leftF, rightF, "cmptmp");
            return TypedValue(result, NativeType::NativeBool);
        }
        if (expr.op == ">") {
            result = builder->CreateFCmpOGT(leftF, rightF, "cmptmp");
            return TypedValue(result, NativeType::NativeBool);
        }
        if (expr.op == "<=") {
            result = builder->CreateFCmpOLE(leftF, rightF, "cmptmp");
            return TypedValue(result, NativeType::NativeBool);
        }
        if (expr.op == ">=") {
            result = builder->CreateFCmpOGE(leftF, rightF, "cmptmp");
            return TypedValue(result, NativeType::NativeBool);
        }
        if (expr.op == "==") {
            result = builder->CreateFCmpOEQ(leftF, rightF, "cmptmp");
            return TypedValue(result, NativeType::NativeBool);
        }
        if (expr.op == "!=") {
            result = builder->CreateFCmpONE(leftF, rightF, "cmptmp");
            return TypedValue(result, NativeType::NativeBool);
        }
    }
    
    // Fall back to dynamic - box native values first
    Value* leftVal = left.value;
    Value* rightVal = right.value;
    
    if (left.type == NativeType::NativeInt) {
        leftVal = boxNativeInt(left.value);
    } else if (left.type == NativeType::NativeFloat) {
        leftVal = boxNativeFloat(left.value);
    }
    
    if (right.type == NativeType::NativeInt) {
        rightVal = boxNativeInt(right.value);
    } else if (right.type == NativeType::NativeFloat) {
        rightVal = boxNativeFloat(right.value);
    }
    
    // Use dynamic operation
    std::string funcName;
    if (expr.op == "+") funcName = "moon_add";
    else if (expr.op == "-") funcName = "moon_sub";
    else if (expr.op == "*") funcName = "moon_mul";
    else if (expr.op == "/") funcName = "moon_div";
    else if (expr.op == "%") funcName = "moon_mod";
    else if (expr.op == "==") funcName = "moon_eq";
    else if (expr.op == "!=") funcName = "moon_ne";
    else if (expr.op == "<") funcName = "moon_lt";
    else if (expr.op == "<=") funcName = "moon_le";
    else if (expr.op == ">") funcName = "moon_gt";
    else if (expr.op == ">=") funcName = "moon_ge";
    else {
        // Unsupported operator
        return TypedValue(generateNullLiteral(), NativeType::Dynamic);
    }
    
    Value* result = builder->CreateCall(getRuntimeFunction(funcName), {leftVal, rightVal});
    
    // Release boxed values if we created them
    if (left.type == NativeType::NativeInt || left.type == NativeType::NativeFloat) {
        builder->CreateCall(getRuntimeFunction("moon_release"), {leftVal});
    }
    if (right.type == NativeType::NativeInt || right.type == NativeType::NativeFloat) {
        builder->CreateCall(getRuntimeFunction("moon_release"), {rightVal});
    }
    
    return TypedValue(result, NativeType::Dynamic);
}

Value* LLVMCodeGen::boxNativeInt(Value* nativeVal) {
    return builder->CreateCall(getRuntimeFunction("moon_int"), {nativeVal});
}

Value* LLVMCodeGen::boxNativeFloat(Value* nativeVal) {
    return builder->CreateCall(getRuntimeFunction("moon_float"), {nativeVal});
}

Value* LLVMCodeGen::unboxToInt(Value* moonVal) {
    return builder->CreateCall(getRuntimeFunction("moon_to_int"), {moonVal});
}

Value* LLVMCodeGen::unboxToFloat(Value* moonVal) {
    return builder->CreateCall(getRuntimeFunction("moon_to_float"), {moonVal});
}

void LLVMCodeGen::storeNativeInt(const std::string& name, Value* value) {
    // Get or create native int storage (alloca must be in entry block for proper dominance)
    if (nativeIntVars.find(name) == nativeIntVars.end()) {
        // Save current insert point
        BasicBlock* currentBlock = builder->GetInsertBlock();
        auto insertPoint = builder->GetInsertPoint();
        
        // Create alloca at the start of the entry block
        BasicBlock& entryBlock = currentFunction->getEntryBlock();
        builder->SetInsertPoint(&entryBlock, entryBlock.getFirstInsertionPt());
        Value* alloca = builder->CreateAlloca(Type::getInt64Ty(*context), nullptr, name + "_native");
        nativeIntVars[name] = alloca;
        
        // Restore insert point
        builder->SetInsertPoint(currentBlock, insertPoint);
    }
    builder->CreateStore(value, nativeIntVars[name]);
    variableTypes[name] = NativeType::NativeInt;
}

void LLVMCodeGen::storeNativeFloat(const std::string& name, Value* value) {
    // Get or create native float storage (alloca must be in entry block for proper dominance)
    if (nativeFloatVars.find(name) == nativeFloatVars.end()) {
        // Save current insert point
        BasicBlock* currentBlock = builder->GetInsertBlock();
        auto insertPoint = builder->GetInsertPoint();
        
        // Create alloca at the start of the entry block
        BasicBlock& entryBlock = currentFunction->getEntryBlock();
        builder->SetInsertPoint(&entryBlock, entryBlock.getFirstInsertionPt());
        Value* alloca = builder->CreateAlloca(Type::getDoubleTy(*context), nullptr, name + "_native");
        nativeFloatVars[name] = alloca;
        
        // Restore insert point
        builder->SetInsertPoint(currentBlock, insertPoint);
    }
    builder->CreateStore(value, nativeFloatVars[name]);
    variableTypes[name] = NativeType::NativeFloat;
}

Value* LLVMCodeGen::loadNativeInt(const std::string& name) {
    auto it = nativeIntVars.find(name);
    if (it != nativeIntVars.end()) {
        return builder->CreateLoad(Type::getInt64Ty(*context), it->second);
    }
    // Fall back: unbox from MoonValue (only if variable exists)
    if (namedValues.count(name) || globalVars.count(name)) {
        Value* moonVal = loadVariable(name);
        Value* result = unboxToInt(moonVal);
        builder->CreateCall(getRuntimeFunction("moon_release"), {moonVal});
        return result;
    }
    // Return 0 as fallback
    return ConstantInt::get(Type::getInt64Ty(*context), 0);
}

Value* LLVMCodeGen::loadNativeFloat(const std::string& name) {
    auto it = nativeFloatVars.find(name);
    if (it != nativeFloatVars.end()) {
        return builder->CreateLoad(Type::getDoubleTy(*context), it->second);
    }
    // Fall back: unbox from MoonValue (only if variable exists)
    if (namedValues.count(name) || globalVars.count(name)) {
        Value* moonVal = loadVariable(name);
        Value* result = unboxToFloat(moonVal);
        builder->CreateCall(getRuntimeFunction("moon_release"), {moonVal});
        return result;
    }
    // Return 0.0 as fallback
    return ConstantFP::get(Type::getDoubleTy(*context), 0.0);
}

bool LLVMCodeGen::canOptimizeForLoop(const ForRangeStmt& stmt) {
    // Check if start and end can be treated as numeric values
    // More permissive: allow literals, native vars, local vars, and global vars
    auto isNumericExpr = [this](const ExprPtr& expr) -> bool {
        // Integer literal - always optimizable
        if (std::get_if<IntegerLiteral>(&expr->value)) return true;
        // Float literal - always optimizable
        if (std::get_if<FloatLiteral>(&expr->value)) return true;
        // Identifier - check various storage locations
        if (auto* id = std::get_if<Identifier>(&expr->value)) {
            // Native variable - optimizable
            if (nativeIntVars.count(id->name) || nativeFloatVars.count(id->name)) return true;
            // Local variable - can unbox at runtime
            if (namedValues.count(id->name)) return true;
            // Global variable - can unbox at runtime
            if (globalVars.count(id->name)) return true;
        }
        return false;
    };
    return isNumericExpr(stmt.start) && isNumericExpr(stmt.end);
}

void LLVMCodeGen::generateOptimizedForRange(const ForRangeStmt& stmt) {
    Function* func = builder->GetInsertBlock()->getParent();
    
    // Mark that we're in an optimized for loop (enables local native optimization)
    bool wasInOptimizedLoop = inOptimizedForLoop;
    auto savedPromotedGlobals = promotedGlobals;
    inOptimizedForLoop = true;
    promotedGlobals.clear();
    
    // CRITICAL FIX: Temporarily mark loop variable as NativeInt BEFORE scanning
    // This ensures expressions like "sum = sum + i" correctly infer as numeric
    // (loop variable will be properly allocated later at line 646)
    variableTypes[stmt.variable] = NativeType::NativeInt;
    
    // Pre-scan loop body to find globals that will be assigned numeric values
    // Step 1: ONLY collect names, DO NOT generate any code during scan
    std::vector<std::pair<std::string, NativeType>> globalsToPromote;
    
    // Helper to check if expression tree contains the target variable
    std::function<bool(const ExprPtr&, const std::string&)> containsVariable;
    containsVariable = [&containsVariable](const ExprPtr& expr, const std::string& targetName) -> bool {
        if (!expr) return false;
        if (auto* id = std::get_if<Identifier>(&expr->value)) {
            return id->name == targetName;
        }
        if (auto* bin = std::get_if<BinaryExpr>(&expr->value)) {
            return containsVariable(bin->left, targetName) || containsVariable(bin->right, targetName);
        }
        if (auto* unary = std::get_if<UnaryExpr>(&expr->value)) {
            return containsVariable(unary->operand, targetName);
        }
        return false;
    };
    
    // Helper to detect accumulator pattern: x = expr containing x with arithmetic ops
    auto isAccumulatorPattern = [&containsVariable](const ExprPtr& value, const std::string& targetName) -> bool {
        if (auto* bin = std::get_if<BinaryExpr>(&value->value)) {
            if (bin->op == "+" || bin->op == "-" || bin->op == "*" || bin->op == "/" || bin->op == "%") {
                // Check if expression contains the target variable anywhere
                return containsVariable(value, targetName);
            }
        }
        return false;
    };
    
    // Helper to infer type of expression, treating target variable as given type
    std::function<NativeType(const ExprPtr&, const std::string&, NativeType)> inferWithAssumedType;
    inferWithAssumedType = [this, &inferWithAssumedType](const ExprPtr& expr, const std::string& assumedVar, NativeType assumedType) -> NativeType {
        if (!expr) return NativeType::Dynamic;
        
        // If this is the assumed variable, return assumed type
        if (auto* id = std::get_if<Identifier>(&expr->value)) {
            if (id->name == assumedVar) return assumedType;
            // Otherwise use normal inference
            return inferExpressionType(expr);
        }
        
        // For literals, use normal inference
        if (std::get_if<IntegerLiteral>(&expr->value)) return NativeType::NativeInt;
        if (std::get_if<FloatLiteral>(&expr->value)) return NativeType::NativeFloat;
        
        // For binary expressions, recurse
        if (auto* bin = std::get_if<BinaryExpr>(&expr->value)) {
            NativeType leftType = inferWithAssumedType(bin->left, assumedVar, assumedType);
            NativeType rightType = inferWithAssumedType(bin->right, assumedVar, assumedType);
            
            bool leftNumeric = (leftType == NativeType::NativeInt || leftType == NativeType::NativeFloat);
            bool rightNumeric = (rightType == NativeType::NativeInt || rightType == NativeType::NativeFloat);
            
            if (bin->op == "+" || bin->op == "-" || bin->op == "*") {
                if (leftNumeric && rightNumeric) {
                    // If either is float, result is float
                    if (leftType == NativeType::NativeFloat || rightType == NativeType::NativeFloat) {
                        return NativeType::NativeFloat;
                    }
                    return NativeType::NativeInt;
                }
            }
            if (bin->op == "/") {
                if (leftNumeric && rightNumeric) return NativeType::NativeFloat;
            }
        }
        
        return NativeType::Dynamic;
    };
    
    // Helper to determine the best type for an accumulator based on the expression
    auto getAccumulatorType = [&inferWithAssumedType, &containsVariable](const ExprPtr& value, const std::string& targetName) -> NativeType {
        // First try assuming it's an int accumulator
        NativeType asInt = inferWithAssumedType(value, targetName, NativeType::NativeInt);
        if (asInt == NativeType::NativeInt) return NativeType::NativeInt;
        
        // Then try assuming it's a float accumulator
        NativeType asFloat = inferWithAssumedType(value, targetName, NativeType::NativeFloat);
        if (asFloat == NativeType::NativeFloat) return NativeType::NativeFloat;
        
        return NativeType::Dynamic;
    };
    
    std::function<void(const std::vector<StmtPtr>&)> scanForGlobalsToPromote = 
        [this, &scanForGlobalsToPromote, &globalsToPromote, &isAccumulatorPattern, &getAccumulatorType](const std::vector<StmtPtr>& stmts) {
        for (const auto& s : stmts) {
            std::visit([this, &scanForGlobalsToPromote, &globalsToPromote, &isAccumulatorPattern, &getAccumulatorType](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, AssignStmt>) {
                    if (auto* id = std::get_if<Identifier>(&arg.target->value)) {
                        // Check if this is a global that can be promoted
                        if (globalVars.count(id->name) && !nativeIntVars.count(id->name) && 
                            !nativeFloatVars.count(id->name)) {
                            // First, try normal type inference
                            NativeType valueType = inferExpressionType(arg.value);
                            
                            // If normal inference fails, check for accumulator pattern
                            // e.g., sum = sum + i, or result = result + 1.5 * 2.5 - 0.5
                            if (valueType == NativeType::Dynamic && isAccumulatorPattern(arg.value, id->name)) {
                                NativeType accType = getAccumulatorType(arg.value, id->name);
                                if (accType != NativeType::Dynamic) {
                                    valueType = accType;
                                }
                            }
                            
                            if (valueType == NativeType::NativeInt || valueType == NativeType::NativeFloat) {
                                // Just record the name and type, don't generate code yet
                                globalsToPromote.push_back({id->name, valueType});
                            }
                        }
                    }
                }
                // Recursively scan nested blocks
                else if constexpr (std::is_same_v<T, IfStmt>) {
                    scanForGlobalsToPromote(arg.thenBranch);
                    for (const auto& elif : arg.elifBranches) {
                        scanForGlobalsToPromote(elif.second);
                    }
                    scanForGlobalsToPromote(arg.elseBranch);
                }
                else if constexpr (std::is_same_v<T, WhileStmt>) {
                    scanForGlobalsToPromote(arg.body);
                }
            }, s->value);
        }
    };
    scanForGlobalsToPromote(stmt.body);
    
    // Step 2: Now generate promotion code for collected globals
    for (const auto& [name, valueType] : globalsToPromote) {
        // Skip if already promoted
        if (nativeIntVars.count(name) || nativeFloatVars.count(name)) continue;
        
        Value* globalVal = builder->CreateLoad(moonValuePtrType, globalVars[name]);
        Value* nativeVal = (valueType == NativeType::NativeInt) 
            ? unboxToInt(globalVal) 
            : unboxToFloat(globalVal);
        // Note: Don't release globalVal here! The value is still in globalVars[name]
        // and will be released when we write back after the loop ends.
        // Releasing here causes double-free because globalVars[name] still holds
        // the pointer, and we release it again in the write-back phase.
        if (valueType == NativeType::NativeInt) {
            storeNativeInt(name, nativeVal);
        } else {
            storeNativeFloat(name, nativeVal);
        }
        promotedGlobals.push_back(name);
    }
    
    // Pre-scan loop body to find native variables that will be assigned dynamic values
    // and demote them BEFORE the loop starts (so all iterations use consistent storage)
    // This needs transitive analysis: if A = func(), B = A, C = B, then A, B, C all need demotion
    
    // Step 1: Collect all assignments in the loop (target -> source variable if source is identifier)
    std::set<std::string> dynamicAssigned;  // Variables directly assigned dynamic values
    std::map<std::string, std::set<std::string>> dependencies;  // target -> set of source vars it depends on
    
    std::function<void(const std::vector<StmtPtr>&)> collectAssignments = 
        [this, &collectAssignments, &dynamicAssigned, &dependencies](const std::vector<StmtPtr>& stmts) {
        for (const auto& s : stmts) {
            std::visit([this, &collectAssignments, &dynamicAssigned, &dependencies](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, AssignStmt>) {
                    if (auto* id = std::get_if<Identifier>(&arg.target->value)) {
                        NativeType valueType = inferExpressionType(arg.value);
                        if (valueType == NativeType::Dynamic) {
                            dynamicAssigned.insert(id->name);
                        }
                        // Also track if assigned from another variable
                        if (auto* srcId = std::get_if<Identifier>(&arg.value->value)) {
                            dependencies[id->name].insert(srcId->name);
                        }
                    }
                }
                else if constexpr (std::is_same_v<T, IfStmt>) {
                    collectAssignments(arg.thenBranch);
                    for (const auto& elif : arg.elifBranches) {
                        collectAssignments(elif.second);
                    }
                    collectAssignments(arg.elseBranch);
                }
                else if constexpr (std::is_same_v<T, WhileStmt>) {
                    collectAssignments(arg.body);
                }
                else if constexpr (std::is_same_v<T, ForRangeStmt>) {
                    collectAssignments(arg.body);
                }
            }, s->value);
        }
    };
    collectAssignments(stmt.body);
    
    // Step 2: Propagate - find all variables transitively affected by dynamic assignments
    std::set<std::string> toDemote = dynamicAssigned;
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& [target, sources] : dependencies) {
            if (toDemote.count(target) == 0) {
                for (const auto& src : sources) {
                    if (toDemote.count(src)) {
                        toDemote.insert(target);
                        changed = true;
                        break;
                    }
                }
            }
        }
    }
    
    // Step 3: Demote all affected native variables
    std::vector<std::string> demotedNatives;
    for (const auto& name : toDemote) {
        if (nativeIntVars.count(name)) {
            Value* nativeVal = builder->CreateLoad(Type::getInt64Ty(*context), nativeIntVars[name]);
            Value* boxedVal = boxNativeInt(nativeVal);
            if (!namedValues.count(name) && !globalVars.count(name)) {
                Value* alloca = createAlloca(currentFunction, name);
                namedValues[name] = alloca;
                builder->CreateStore(boxedVal, alloca);
            }
            nativeIntVars.erase(name);
            variableTypes.erase(name);
            demotedNatives.push_back(name);
        } else if (nativeFloatVars.count(name)) {
            Value* nativeVal = builder->CreateLoad(Type::getDoubleTy(*context), nativeFloatVars[name]);
            Value* boxedVal = boxNativeFloat(nativeVal);
            if (!namedValues.count(name) && !globalVars.count(name)) {
                Value* alloca = createAlloca(currentFunction, name);
                namedValues[name] = alloca;
                builder->CreateStore(boxedVal, alloca);
            }
            nativeFloatVars.erase(name);
            variableTypes.erase(name);
            demotedNatives.push_back(name);
        }
    }
    
    // Generate native start and end values
    TypedValue startTyped = generateNativeExpression(stmt.start);
    TypedValue endTyped = generateNativeExpression(stmt.end);
    
    // Convert to i64 - handle Dynamic (MoonValue*), Float, and Int types
    Value* start = startTyped.value;
    Value* end = endTyped.value;
    
    if (startTyped.type == NativeType::Dynamic) {
        // Unbox from MoonValue* to i64
        start = unboxToInt(startTyped.value);
        builder->CreateCall(getRuntimeFunction("moon_release"), {startTyped.value});
    } else if (startTyped.type == NativeType::NativeFloat) {
        start = builder->CreateFPToSI(start, Type::getInt64Ty(*context));
    }
    
    if (endTyped.type == NativeType::Dynamic) {
        // Unbox from MoonValue* to i64
        end = unboxToInt(endTyped.value);
        builder->CreateCall(getRuntimeFunction("moon_release"), {endTyped.value});
    } else if (endTyped.type == NativeType::NativeFloat) {
        end = builder->CreateFPToSI(end, Type::getInt64Ty(*context));
    }
    
    // Create native index variable
    Value* indexPtr = builder->CreateAlloca(Type::getInt64Ty(*context), nullptr, stmt.variable + "_idx");
    builder->CreateStore(start, indexPtr);
    
    // Mark loop variable as native int
    nativeIntVars[stmt.variable] = indexPtr;
    variableTypes[stmt.variable] = NativeType::NativeInt;
    
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
    
    // Condition block - native comparison
    builder->SetInsertPoint(condBB);
    Value* idx = builder->CreateLoad(Type::getInt64Ty(*context), indexPtr);
    Value* cond = builder->CreateICmpSLE(idx, end);
    builder->CreateCondBr(cond, bodyBB, afterBB);
    
    // Body block
    func->insert(func->end(), bodyBB);
    builder->SetInsertPoint(bodyBB);
    
    // Generate body (loop variable is already accessible as native int)
    for (const auto& s : stmt.body) {
        generateStatement(s);
    }
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(incrBB);
    }
    
    // Increment block - native increment
    func->insert(func->end(), incrBB);
    builder->SetInsertPoint(incrBB);
    Value* currentIdx = builder->CreateLoad(Type::getInt64Ty(*context), indexPtr);
    Value* newIdx = builder->CreateAdd(currentIdx, ConstantInt::get(Type::getInt64Ty(*context), 1));
    builder->CreateStore(newIdx, indexPtr);
    builder->CreateBr(condBB);
    
    // After block
    func->insert(func->end(), afterBB);
    builder->SetInsertPoint(afterBB);
    
    // Write back promoted globals
    for (const auto& name : promotedGlobals) {
        if (globalVars.count(name)) {
            auto typeIt = variableTypes.find(name);
            if (typeIt != variableTypes.end()) {
                Value* boxedVal = nullptr;
                if (typeIt->second == NativeType::NativeInt && nativeIntVars.count(name)) {
                    Value* nativeVal = builder->CreateLoad(Type::getInt64Ty(*context), nativeIntVars[name]);
                    boxedVal = boxNativeInt(nativeVal);
                } else if (typeIt->second == NativeType::NativeFloat && nativeFloatVars.count(name)) {
                    Value* nativeVal = builder->CreateLoad(Type::getDoubleTy(*context), nativeFloatVars[name]);
                    boxedVal = boxNativeFloat(nativeVal);
                }
                if (boxedVal) {
                    // Release old global value and store new one
                    Value* oldVal = builder->CreateLoad(moonValuePtrType, globalVars[name]);
                    builder->CreateCall(getRuntimeFunction("moon_release"), {oldVal});
                    builder->CreateStore(boxedVal, globalVars[name]);
                }
            }
            // Clean up native tracking for this promoted global
            nativeIntVars.erase(name);
            nativeFloatVars.erase(name);
            variableTypes.erase(name);
        }
    }
    
    // Clean up native variable tracking (only loop variable)
    nativeIntVars.erase(stmt.variable);
    variableTypes.erase(stmt.variable);
    
    // Restore break/continue targets, optimization flag, and promoted globals list
    currentBreakTarget = savedBreak;
    currentContinueTarget = savedContinue;
    inOptimizedForLoop = wasInOptimizedLoop;
    promotedGlobals = savedPromotedGlobals;
}

bool LLVMCodeGen::canOptimizeWhileLoop(const WhileStmt& stmt) {
    // Check if the condition is a numeric comparison that can be optimized
    auto* cond = std::get_if<BinaryExpr>(&stmt.condition->value);
    if (!cond) return false;
    
    // Must be a comparison operator
    if (cond->op != "<" && cond->op != ">" && cond->op != "<=" && 
        cond->op != ">=" && cond->op != "==" && cond->op != "!=") {
        return false;
    }
    
    // Both sides must be numeric expressions (or promotable)
    auto isNumericExpr = [this](const ExprPtr& expr) -> bool {
        // Integer literal
        if (std::get_if<IntegerLiteral>(&expr->value)) return true;
        // Float literal
        if (std::get_if<FloatLiteral>(&expr->value)) return true;
        // Identifier that's native or can be promoted
        if (auto* id = std::get_if<Identifier>(&expr->value)) {
            if (nativeIntVars.count(id->name) || nativeFloatVars.count(id->name)) return true;
            if (namedValues.count(id->name)) return true;
            if (globalVars.count(id->name)) return true;
        }
        // Binary expression with numeric operands (like p * p)
        if (auto* bin = std::get_if<BinaryExpr>(&expr->value)) {
            if (bin->op == "+" || bin->op == "-" || bin->op == "*" || 
                bin->op == "/" || bin->op == "%") {
                // Recursively check operands
                std::function<bool(const ExprPtr&)> checkNumeric = [this, &checkNumeric](const ExprPtr& e) -> bool {
                    if (std::get_if<IntegerLiteral>(&e->value)) return true;
                    if (std::get_if<FloatLiteral>(&e->value)) return true;
                    if (auto* id = std::get_if<Identifier>(&e->value)) {
                        if (nativeIntVars.count(id->name) || nativeFloatVars.count(id->name)) return true;
                        if (namedValues.count(id->name) || globalVars.count(id->name)) return true;
                    }
                    if (auto* b = std::get_if<BinaryExpr>(&e->value)) {
                        return checkNumeric(b->left) && checkNumeric(b->right);
                    }
                    return false;
                };
                return checkNumeric(bin->left) && checkNumeric(bin->right);
            }
        }
        return false;
    };
    
    return isNumericExpr(cond->left) && isNumericExpr(cond->right);
}

void LLVMCodeGen::generateOptimizedWhileLoop(const WhileStmt& stmt) {
    Function* func = builder->GetInsertBlock()->getParent();
    
    // Mark that we're in an optimized loop
    bool wasInOptimizedLoop = inOptimizedForLoop;
    auto savedPromotedGlobals = promotedGlobals;
    inOptimizedForLoop = true;
    promotedGlobals.clear();
    
    // Pre-scan loop body to find globals that will be assigned numeric values
    // Step 1: ONLY collect names, DO NOT generate any code during scan
    std::vector<std::pair<std::string, NativeType>> globalsToPromoteW;
    
    // Helper to check if expression contains the target variable
    std::function<bool(const ExprPtr&, const std::string&)> containsVarW;
    containsVarW = [&containsVarW](const ExprPtr& expr, const std::string& targetName) -> bool {
        if (!expr) return false;
        if (auto* id = std::get_if<Identifier>(&expr->value)) {
            return id->name == targetName;
        }
        if (auto* bin = std::get_if<BinaryExpr>(&expr->value)) {
            return containsVarW(bin->left, targetName) || containsVarW(bin->right, targetName);
        }
        return false;
    };
    
    // Helper to detect accumulator pattern: x = x OP expr (where OP is +,-,*,/,%)
    auto isAccumPatternW = [&containsVarW](const ExprPtr& value, const std::string& targetName) -> bool {
        if (auto* bin = std::get_if<BinaryExpr>(&value->value)) {
            if (bin->op == "+" || bin->op == "-" || bin->op == "*" || bin->op == "/" || bin->op == "%") {
                return containsVarW(value, targetName);
            }
        }
        return false;
    };
    
    // Also collect local variables (in namedValues) that can be promoted
    std::vector<std::pair<std::string, NativeType>> localsToPromoteW;
    
    std::function<void(const std::vector<StmtPtr>&)> scanForVarsToPromote = 
        [this, &scanForVarsToPromote, &globalsToPromoteW, &localsToPromoteW, &isAccumPatternW](const std::vector<StmtPtr>& stmts) {
        for (const auto& s : stmts) {
            std::visit([this, &scanForVarsToPromote, &globalsToPromoteW, &localsToPromoteW, &isAccumPatternW](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, AssignStmt>) {
                    if (auto* id = std::get_if<Identifier>(&arg.target->value)) {
                        // Skip if already native
                        if (nativeIntVars.count(id->name) || nativeFloatVars.count(id->name)) {
                            return;
                        }
                        
                        // Try normal type inference first
                        NativeType valueType = inferExpressionType(arg.value);
                        
                        // If normal inference fails, check for accumulator pattern
                        // e.g., multiple = multiple + p
                        if (valueType == NativeType::Dynamic && isAccumPatternW(arg.value, id->name)) {
                            // Assume integer type for accumulator patterns in while loops
                            valueType = NativeType::NativeInt;
                        }
                        
                        if (valueType == NativeType::NativeInt || valueType == NativeType::NativeFloat) {
                            // Check if it's a global or local variable
                            if (globalVars.count(id->name)) {
                                globalsToPromoteW.push_back({id->name, valueType});
                            } else if (namedValues.count(id->name)) {
                                // Local variable - mark for promotion
                                localsToPromoteW.push_back({id->name, valueType});
                            }
                        }
                    }
                }
                else if constexpr (std::is_same_v<T, IfStmt>) {
                    scanForVarsToPromote(arg.thenBranch);
                    for (const auto& elif : arg.elifBranches) {
                        scanForVarsToPromote(elif.second);
                    }
                    scanForVarsToPromote(arg.elseBranch);
                }
                else if constexpr (std::is_same_v<T, WhileStmt>) {
                    scanForVarsToPromote(arg.body);
                }
            }, s->value);
        }
    };
    scanForVarsToPromote(stmt.body);
    
    // Also scan the condition for globals that need promotion (just collect names)
    auto* cond = std::get_if<BinaryExpr>(&stmt.condition->value);
    std::function<void(const ExprPtr&)> scanExprForGlobals = [this, &scanExprForGlobals, &globalsToPromoteW](const ExprPtr& expr) {
        if (auto* id = std::get_if<Identifier>(&expr->value)) {
            if (globalVars.count(id->name) && !nativeIntVars.count(id->name) && 
                !nativeFloatVars.count(id->name)) {
                // Just record the name, assume int type for condition variables
                globalsToPromoteW.push_back({id->name, NativeType::NativeInt});
            }
        }
        else if (auto* bin = std::get_if<BinaryExpr>(&expr->value)) {
            scanExprForGlobals(bin->left);
            scanExprForGlobals(bin->right);
        }
    };
    if (cond) {
        scanExprForGlobals(cond->left);
        scanExprForGlobals(cond->right);
    }
    
    // Step 2: Now generate promotion code for collected globals
    for (const auto& [name, valueType] : globalsToPromoteW) {
        // Skip if already promoted
        if (nativeIntVars.count(name) || nativeFloatVars.count(name)) continue;
        
        Value* globalVal = builder->CreateLoad(moonValuePtrType, globalVars[name]);
        Value* nativeVal = (valueType == NativeType::NativeInt) 
            ? unboxToInt(globalVal) 
            : unboxToFloat(globalVal);
        // Note: Don't release globalVal here - same fix as for-range loop
        if (valueType == NativeType::NativeInt) {
            storeNativeInt(name, nativeVal);
        } else {
            storeNativeFloat(name, nativeVal);
        }
        promotedGlobals.push_back(name);
    }
    
    // Step 3: Generate promotion code for collected locals
    std::vector<std::string> promotedLocals;
    for (const auto& [name, valueType] : localsToPromoteW) {
        // Skip if already promoted
        if (nativeIntVars.count(name) || nativeFloatVars.count(name)) continue;
        
        if (namedValues.count(name)) {
            Value* localPtr = namedValues[name];
            Value* localVal = builder->CreateLoad(moonValuePtrType, localPtr);
            Value* nativeVal = (valueType == NativeType::NativeInt) 
                ? unboxToInt(localVal) 
                : unboxToFloat(localVal);
            // Note: Don't release localVal here - will be released in write-back phase
            if (valueType == NativeType::NativeInt) {
                storeNativeInt(name, nativeVal);
            } else {
                storeNativeFloat(name, nativeVal);
            }
            promotedLocals.push_back(name);
        }
    }
    
    // Pre-scan loop body to find native variables that will be assigned dynamic values
    // and demote them BEFORE the loop starts (so all iterations use consistent storage)
    // This needs transitive analysis: if A = func(), B = A, C = B, then A, B, C all need demotion
    
    std::set<std::string> dynamicAssignedW;
    std::map<std::string, std::set<std::string>> dependenciesW;
    
    std::function<void(const std::vector<StmtPtr>&)> collectAssignmentsW = 
        [this, &collectAssignmentsW, &dynamicAssignedW, &dependenciesW](const std::vector<StmtPtr>& stmts) {
        for (const auto& s : stmts) {
            std::visit([this, &collectAssignmentsW, &dynamicAssignedW, &dependenciesW](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, AssignStmt>) {
                    if (auto* id = std::get_if<Identifier>(&arg.target->value)) {
                        NativeType valueType = inferExpressionType(arg.value);
                        if (valueType == NativeType::Dynamic) {
                            dynamicAssignedW.insert(id->name);
                        }
                        if (auto* srcId = std::get_if<Identifier>(&arg.value->value)) {
                            dependenciesW[id->name].insert(srcId->name);
                        }
                    }
                }
                else if constexpr (std::is_same_v<T, IfStmt>) {
                    collectAssignmentsW(arg.thenBranch);
                    for (const auto& elif : arg.elifBranches) {
                        collectAssignmentsW(elif.second);
                    }
                    collectAssignmentsW(arg.elseBranch);
                }
                else if constexpr (std::is_same_v<T, WhileStmt>) {
                    collectAssignmentsW(arg.body);
                }
                else if constexpr (std::is_same_v<T, ForRangeStmt>) {
                    collectAssignmentsW(arg.body);
                }
            }, s->value);
        }
    };
    collectAssignmentsW(stmt.body);
    
    std::set<std::string> toDemoteW = dynamicAssignedW;
    bool changedW = true;
    while (changedW) {
        changedW = false;
        for (const auto& [target, sources] : dependenciesW) {
            if (toDemoteW.count(target) == 0) {
                for (const auto& src : sources) {
                    if (toDemoteW.count(src)) {
                        toDemoteW.insert(target);
                        changedW = true;
                        break;
                    }
                }
            }
        }
    }
    
    std::vector<std::string> demotedNatives;
    for (const auto& name : toDemoteW) {
        if (nativeIntVars.count(name)) {
            Value* nativeVal = builder->CreateLoad(Type::getInt64Ty(*context), nativeIntVars[name]);
            Value* boxedVal = boxNativeInt(nativeVal);
            if (!namedValues.count(name) && !globalVars.count(name)) {
                Value* alloca = createAlloca(currentFunction, name);
                namedValues[name] = alloca;
                builder->CreateStore(boxedVal, alloca);
            }
            nativeIntVars.erase(name);
            variableTypes.erase(name);
            demotedNatives.push_back(name);
        } else if (nativeFloatVars.count(name)) {
            Value* nativeVal = builder->CreateLoad(Type::getDoubleTy(*context), nativeFloatVars[name]);
            Value* boxedVal = boxNativeFloat(nativeVal);
            if (!namedValues.count(name) && !globalVars.count(name)) {
                Value* alloca = createAlloca(currentFunction, name);
                namedValues[name] = alloca;
                builder->CreateStore(boxedVal, alloca);
            }
            nativeFloatVars.erase(name);
            variableTypes.erase(name);
            demotedNatives.push_back(name);
        }
    }
    
    BasicBlock* condBB = BasicBlock::Create(*context, "whilecond_opt", func);
    BasicBlock* bodyBB = BasicBlock::Create(*context, "whilebody_opt");
    BasicBlock* afterBB = BasicBlock::Create(*context, "whileend_opt");
    
    // Save break/continue targets
    BasicBlock* savedBreak = currentBreakTarget;
    BasicBlock* savedContinue = currentContinueTarget;
    currentBreakTarget = afterBB;
    currentContinueTarget = condBB;
    
    builder->CreateBr(condBB);
    
    // Condition block - generate native comparison
    builder->SetInsertPoint(condBB);
    
    // Generate native expressions for both sides of comparison
    TypedValue leftTyped = generateNativeExpression(cond->left);
    TypedValue rightTyped = generateNativeExpression(cond->right);
    
    Value* leftVal = leftTyped.value;
    Value* rightVal = rightTyped.value;
    
    // Convert Dynamic to native if needed
    if (leftTyped.type == NativeType::Dynamic) {
        leftVal = unboxToInt(leftTyped.value);
        builder->CreateCall(getRuntimeFunction("moon_release"), {leftTyped.value});
    }
    if (rightTyped.type == NativeType::Dynamic) {
        rightVal = unboxToInt(rightTyped.value);
        builder->CreateCall(getRuntimeFunction("moon_release"), {rightTyped.value});
    }
    
    // Generate native comparison
    Value* condResult = nullptr;
    bool isFloat = (leftTyped.type == NativeType::NativeFloat || rightTyped.type == NativeType::NativeFloat);
    
    if (isFloat) {
        // Convert to float if needed
        if (leftTyped.type != NativeType::NativeFloat) {
            leftVal = builder->CreateSIToFP(leftVal, Type::getDoubleTy(*context));
        }
        if (rightTyped.type != NativeType::NativeFloat) {
            rightVal = builder->CreateSIToFP(rightVal, Type::getDoubleTy(*context));
        }
        
        if (cond->op == "<") condResult = builder->CreateFCmpOLT(leftVal, rightVal, "cmptmp");
        else if (cond->op == ">") condResult = builder->CreateFCmpOGT(leftVal, rightVal, "cmptmp");
        else if (cond->op == "<=") condResult = builder->CreateFCmpOLE(leftVal, rightVal, "cmptmp");
        else if (cond->op == ">=") condResult = builder->CreateFCmpOGE(leftVal, rightVal, "cmptmp");
        else if (cond->op == "==") condResult = builder->CreateFCmpOEQ(leftVal, rightVal, "cmptmp");
        else if (cond->op == "!=") condResult = builder->CreateFCmpONE(leftVal, rightVal, "cmptmp");
    } else {
        if (cond->op == "<") condResult = builder->CreateICmpSLT(leftVal, rightVal, "cmptmp");
        else if (cond->op == ">") condResult = builder->CreateICmpSGT(leftVal, rightVal, "cmptmp");
        else if (cond->op == "<=") condResult = builder->CreateICmpSLE(leftVal, rightVal, "cmptmp");
        else if (cond->op == ">=") condResult = builder->CreateICmpSGE(leftVal, rightVal, "cmptmp");
        else if (cond->op == "==") condResult = builder->CreateICmpEQ(leftVal, rightVal, "cmptmp");
        else if (cond->op == "!=") condResult = builder->CreateICmpNE(leftVal, rightVal, "cmptmp");
    }
    
    builder->CreateCondBr(condResult, bodyBB, afterBB);
    
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
    
    // Write back promoted globals
    for (const auto& name : promotedGlobals) {
        if (globalVars.count(name)) {
            auto typeIt = variableTypes.find(name);
            if (typeIt != variableTypes.end()) {
                Value* boxedVal = nullptr;
                if (typeIt->second == NativeType::NativeInt && nativeIntVars.count(name)) {
                    Value* nativeVal = builder->CreateLoad(Type::getInt64Ty(*context), nativeIntVars[name]);
                    boxedVal = boxNativeInt(nativeVal);
                } else if (typeIt->second == NativeType::NativeFloat && nativeFloatVars.count(name)) {
                    Value* nativeVal = builder->CreateLoad(Type::getDoubleTy(*context), nativeFloatVars[name]);
                    boxedVal = boxNativeFloat(nativeVal);
                }
                if (boxedVal) {
                    Value* oldVal = builder->CreateLoad(moonValuePtrType, globalVars[name]);
                    builder->CreateCall(getRuntimeFunction("moon_release"), {oldVal});
                    builder->CreateStore(boxedVal, globalVars[name]);
                }
            }
            nativeIntVars.erase(name);
            nativeFloatVars.erase(name);
            variableTypes.erase(name);
        }
    }
    
    // Write back promoted locals
    for (const auto& name : promotedLocals) {
        if (namedValues.count(name)) {
            auto typeIt = variableTypes.find(name);
            if (typeIt != variableTypes.end()) {
                Value* boxedVal = nullptr;
                if (typeIt->second == NativeType::NativeInt && nativeIntVars.count(name)) {
                    Value* nativeVal = builder->CreateLoad(Type::getInt64Ty(*context), nativeIntVars[name]);
                    boxedVal = boxNativeInt(nativeVal);
                } else if (typeIt->second == NativeType::NativeFloat && nativeFloatVars.count(name)) {
                    Value* nativeVal = builder->CreateLoad(Type::getDoubleTy(*context), nativeFloatVars[name]);
                    boxedVal = boxNativeFloat(nativeVal);
                }
                if (boxedVal) {
                    Value* oldVal = builder->CreateLoad(moonValuePtrType, namedValues[name]);
                    builder->CreateCall(getRuntimeFunction("moon_release"), {oldVal});
                    builder->CreateStore(boxedVal, namedValues[name]);
                }
            }
            nativeIntVars.erase(name);
            nativeFloatVars.erase(name);
            variableTypes.erase(name);
        }
    }
    
    // Restore state
    currentBreakTarget = savedBreak;
    currentContinueTarget = savedContinue;
    inOptimizedForLoop = wasInOptimizedLoop;
    promotedGlobals = savedPromotedGlobals;
}

bool LLVMCodeGen::isPureNumericFunction(const FuncDecl& stmt) {
    // Check if function body only contains numeric operations
    // For simplicity, we only optimize simple functions:
    // - Single return statement with arithmetic expression, OR
    // - if-else with returns containing arithmetic expressions
    // All operations must only use function parameters
    
    std::set<std::string> params;
    for (const auto& p : stmt.params) {
        params.insert(p.name);
    }
    
    std::function<bool(const ExprPtr&)> isNumericExpr = [&](const ExprPtr& expr) -> bool {
        if (!expr) return false;
        
        // Integer/Float literal - ok
        if (std::get_if<IntegerLiteral>(&expr->value)) return true;
        if (std::get_if<FloatLiteral>(&expr->value)) return true;
        
        // Parameter reference - ok
        if (auto* id = std::get_if<Identifier>(&expr->value)) {
            return params.count(id->name) > 0;
        }
        
        // Binary expression with numeric operands - ok
        if (auto* bin = std::get_if<BinaryExpr>(&expr->value)) {
            if (bin->op == "+" || bin->op == "-" || bin->op == "*" || 
                bin->op == "/" || bin->op == "%" ||
                bin->op == "<" || bin->op == ">" || bin->op == "<=" || 
                bin->op == ">=" || bin->op == "==" || bin->op == "!=") {
                return isNumericExpr(bin->left) && isNumericExpr(bin->right);
            }
        }
        
        // Recursive call to self - ok if arguments are numeric
        if (auto* call = std::get_if<CallExpr>(&expr->value)) {
            if (auto* id = std::get_if<Identifier>(&call->callee->value)) {
                if (id->name == stmt.name) {
                    // Recursive call - check all arguments are numeric
                    for (const auto& arg : call->arguments) {
                        if (!isNumericExpr(arg)) return false;
                    }
                    return true;
                }
            }
        }
        
        return false;
    };
    
    // For now, only optimize functions with a single return statement
    // or simple if-return patterns (no complex nesting)
    if (stmt.body.size() == 1) {
        auto& s = stmt.body[0];
        if (auto* ret = std::get_if<ReturnStmt>(&s->value)) {
            return !ret->value || isNumericExpr(ret->value);
        }
    }
    
    // Also support: if condition: return x end return y
    // This covers fib pattern: if n <= 1: return n end return fib(n-1) + fib(n-2)
    if (stmt.body.size() == 2) {
        auto& s1 = stmt.body[0];
        auto& s2 = stmt.body[1];
        
        // First statement must be an if with returns in all branches
        if (auto* ifStmt = std::get_if<IfStmt>(&s1->value)) {
            // Check condition is numeric comparison
            if (!isNumericExpr(ifStmt->condition)) return false;
            
            // Check thenBranch has single return
            if (ifStmt->thenBranch.size() != 1) return false;
            auto* thenRet = std::get_if<ReturnStmt>(&ifStmt->thenBranch[0]->value);
            if (!thenRet || !isNumericExpr(thenRet->value)) return false;
            
            // No elif or else in this pattern
            if (!ifStmt->elifBranches.empty() || !ifStmt->elseBranch.empty()) return false;
            
            // Second statement must be return
            auto* elseRet = std::get_if<ReturnStmt>(&s2->value);
            if (!elseRet || !isNumericExpr(elseRet->value)) return false;
            
            return true;
        }
    }
    
    return false;
}

void LLVMCodeGen::generateNativeFunction(const FuncDecl& stmt) {
    // Create native version with i64 parameters and return value
    std::vector<Type*> paramTypes;
    for (size_t i = 0; i < stmt.params.size(); i++) {
        paramTypes.push_back(Type::getInt64Ty(*context));
    }
    
    FunctionType* funcType = FunctionType::get(Type::getInt64Ty(*context), paramTypes, false);
    Function* nativeFunc = Function::Create(funcType, Function::InternalLinkage,
                                            "moon_native_" + stmt.name, module.get());
    
    // Mark for aggressive inlining - this is key for performance
    nativeFunc->addFnAttr(Attribute::AlwaysInline);
    
    nativeFunctions[stmt.name] = nativeFunc;
    
    // Save current state
    Function* savedFunc = currentFunction;
    BasicBlock* savedBlock = builder->GetInsertBlock();
    auto savedVars = namedValues;
    auto savedNativeIntVars = nativeIntVars;
    auto savedNativeFloatVars = nativeFloatVars;
    auto savedVariableTypes = variableTypes;
    bool savedInOptimizedLoop = inOptimizedForLoop;
    
    // Enable fast path for native functions (skip overflow checking)
    inOptimizedForLoop = true;
    
    // Setup function
    currentFunction = nativeFunc;
    BasicBlock* entry = BasicBlock::Create(*context, "entry", nativeFunc);
    builder->SetInsertPoint(entry);
    
    // Clear tracking and setup parameters as native ints
    namedValues.clear();
    nativeIntVars.clear();
    nativeFloatVars.clear();
    variableTypes.clear();
    
    auto argIt = nativeFunc->arg_begin();
    for (const auto& param : stmt.params) {
        Value* argVal = &*argIt++;
        argVal->setName(param.name + "_native");
        
        // Store as native int
        storeNativeInt(param.name, argVal);
    }
    
    // Helper to generate a native return value
    auto generateNativeReturn = [this](const ExprPtr& value) {
        if (value) {
            TypedValue result = generateNativeExpression(value);
            Value* retVal = result.value;
            if (result.type == NativeType::Dynamic) {
                retVal = unboxToInt(result.value);
                builder->CreateCall(getRuntimeFunction("moon_release"), {result.value});
            } else if (result.type == NativeType::NativeFloat) {
                retVal = builder->CreateFPToSI(result.value, Type::getInt64Ty(*context));
            } else if (result.type == NativeType::NativeBool) {
                retVal = builder->CreateZExt(result.value, Type::getInt64Ty(*context));
            }
            builder->CreateRet(retVal);
        } else {
            builder->CreateRet(ConstantInt::get(Type::getInt64Ty(*context), 0));
        }
    };
    
    // Handle the two supported patterns:
    // Pattern 1: Single return statement
    if (stmt.body.size() == 1) {
        if (auto* ret = std::get_if<ReturnStmt>(&stmt.body[0]->value)) {
            generateNativeReturn(ret->value);
        }
    }
    // Pattern 2: if-return + return (for fib-like functions)
    else if (stmt.body.size() == 2) {
        auto* ifStmt = std::get_if<IfStmt>(&stmt.body[0]->value);
        auto* elseRet = std::get_if<ReturnStmt>(&stmt.body[1]->value);
        
        if (ifStmt && elseRet) {
            // Generate native if statement
            BasicBlock* thenBB = BasicBlock::Create(*context, "then", nativeFunc);
            BasicBlock* elseBB = BasicBlock::Create(*context, "else", nativeFunc);
            
            // Generate native condition
            TypedValue condTyped = generateNativeExpression(ifStmt->condition);
            Value* condVal = condTyped.value;
            
            // Convert to bool if needed
            if (condTyped.type == NativeType::NativeInt) {
                condVal = builder->CreateICmpNE(condVal, 
                    ConstantInt::get(Type::getInt64Ty(*context), 0), "ifcond");
            } else if (condTyped.type == NativeType::NativeBool) {
                // Already a bool, use directly
            } else if (condTyped.type == NativeType::Dynamic) {
                condVal = builder->CreateCall(getRuntimeFunction("moon_is_truthy"), {condVal});
                builder->CreateCall(getRuntimeFunction("moon_release"), {condTyped.value});
            }
            
            builder->CreateCondBr(condVal, thenBB, elseBB);
            
            // Then block - generate native return
            builder->SetInsertPoint(thenBB);
            if (!ifStmt->thenBranch.empty()) {
                auto* thenRet = std::get_if<ReturnStmt>(&ifStmt->thenBranch[0]->value);
                if (thenRet) {
                    generateNativeReturn(thenRet->value);
                }
            }
            
            // Else block - generate native return
            builder->SetInsertPoint(elseBB);
            generateNativeReturn(elseRet->value);
        }
    }
    
    // Add implicit return if needed (shouldn't happen with proper patterns)
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateRet(ConstantInt::get(Type::getInt64Ty(*context), 0));
    }
    
    // Restore state
    currentFunction = savedFunc;
    namedValues = savedVars;
    nativeIntVars = savedNativeIntVars;
    nativeFloatVars = savedNativeFloatVars;
    variableTypes = savedVariableTypes;
    inOptimizedForLoop = savedInOptimizedLoop;
    builder->SetInsertPoint(savedBlock);
}

Value* LLVMCodeGen::generateNativeFunctionCall(const std::string& funcName, const std::vector<ExprPtr>& args) {
    Function* nativeFunc = nativeFunctions[funcName];
    if (!nativeFunc) return nullptr;
    
    // Generate native arguments
    std::vector<Value*> nativeArgs;
    for (const auto& arg : args) {
        TypedValue typedArg = generateNativeExpression(arg);
        Value* argVal = typedArg.value;
        
        if (typedArg.type == NativeType::Dynamic) {
            argVal = unboxToInt(typedArg.value);
            builder->CreateCall(getRuntimeFunction("moon_release"), {typedArg.value});
        } else if (typedArg.type == NativeType::NativeFloat) {
            argVal = builder->CreateFPToSI(typedArg.value, Type::getInt64Ty(*context));
        } else if (typedArg.type == NativeType::NativeBool) {
            argVal = builder->CreateZExt(typedArg.value, Type::getInt64Ty(*context));
        }
        
        nativeArgs.push_back(argVal);
    }
    
    // Call native function
    Value* result = builder->CreateCall(nativeFunc, nativeArgs);
    
    // Box result back to MoonValue
    return boxNativeInt(result);
}

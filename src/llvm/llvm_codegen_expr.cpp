// ============================================================================
// Expression Generation Module
// ============================================================================
// This file contains all expression generation functions for the LLVM code generator.
//
// IMPORTANT: This file should be included by llvm_codegen.cpp, not compiled
// separately. It is intended to be included via #include directive to keep
// the codebase modular while maintaining a single compilation unit.
// ============================================================================

// ============================================================================
// Expression Generation
// ============================================================================

Value* LLVMCodeGen::generateExpression(const ExprPtr& expr) {
    return std::visit([this](auto&& arg) -> Value* {
        using T = std::decay_t<decltype(arg)>;
        
        if constexpr (std::is_same_v<T, IntegerLiteral>) {
            return generateIntegerLiteral(arg.value);
        }
        else if constexpr (std::is_same_v<T, FloatLiteral>) {
            return generateFloatLiteral(arg.value);
        }
        else if constexpr (std::is_same_v<T, ::StringLiteral>) {
            return generateStringLiteral(arg.value);
        }
        else if constexpr (std::is_same_v<T, BoolLiteral>) {
            return generateBoolLiteral(arg.value);
        }
        else if constexpr (std::is_same_v<T, NullLiteral>) {
            return generateNullLiteral();
        }
        else if constexpr (std::is_same_v<T, Identifier>) {
            return generateIdentifier(arg.name);
        }
        else if constexpr (std::is_same_v<T, BinaryExpr>) {
            return generateBinaryExpr(arg);
        }
        else if constexpr (std::is_same_v<T, UnaryExpr>) {
            return generateUnaryExpr(arg);
        }
        else if constexpr (std::is_same_v<T, CallExpr>) {
            return generateCallExpr(arg);
        }
        else if constexpr (std::is_same_v<T, IndexExpr>) {
            return generateIndexExpr(arg);
        }
        else if constexpr (std::is_same_v<T, MemberExpr>) {
            return generateMemberExpr(arg);
        }
        else if constexpr (std::is_same_v<T, ListExpr>) {
            return generateListExpr(arg);
        }
        else if constexpr (std::is_same_v<T, DictExpr>) {
            return generateDictExpr(arg);
        }
        else if constexpr (std::is_same_v<T, LambdaExpr>) {
            return generateLambdaExpr(arg);
        }
        else if constexpr (std::is_same_v<T, NewExpr>) {
            return generateNewExpr(arg);
        }
        else if constexpr (std::is_same_v<T, SelfExpr>) {
            return generateSelfExpr();
        }
        else if constexpr (std::is_same_v<T, SuperExpr>) {
            return generateSuperExpr(arg);
        }
        else if constexpr (std::is_same_v<T, ChanRecvExpr>) {
            // Channel receive: <- ch
            Value* ch = generateExpression(arg.channel);
            Value* result = builder->CreateCall(getRuntimeFunction("moon_chan_recv"), {ch});
            builder->CreateCall(getRuntimeFunction("moon_release"), {ch});
            return result;
        }
        
        return generateNullLiteral();
    }, expr->value);
}

Value* LLVMCodeGen::generateIntegerLiteral(int64_t value) {
    Value* intVal = ConstantInt::get(Type::getInt64Ty(*context), value);
    return builder->CreateCall(getRuntimeFunction("moon_int"), {intVal});
}

Value* LLVMCodeGen::generateFloatLiteral(double value) {
    Value* floatVal = ConstantFP::get(Type::getDoubleTy(*context), value);
    return builder->CreateCall(getRuntimeFunction("moon_float"), {floatVal});
}

Value* LLVMCodeGen::generateStringLiteral(const std::string& value) {
    Value* strPtr = createGlobalString(value);
    return builder->CreateCall(getRuntimeFunction("moon_string"), {strPtr});
}

Value* LLVMCodeGen::generateBoolLiteral(bool value) {
    Value* boolVal = ConstantInt::get(Type::getInt1Ty(*context), value ? 1 : 0);
    return builder->CreateCall(getRuntimeFunction("moon_bool"), {boolVal});
}

Value* LLVMCodeGen::generateNullLiteral() {
    return builder->CreateCall(getRuntimeFunction("moon_null"), {});
}

Value* LLVMCodeGen::generateIdentifier(const std::string& name) {
    // Check if this is a captured variable in a closure
    if (inClosure && currentClosureCaptures.count(name)) {
        int captureIndex = currentClosureCaptures[name];
        Value* indexVal = ConstantInt::get(Type::getInt32Ty(*context), captureIndex);
        return builder->CreateCall(getRuntimeFunction("moon_get_capture"), {indexVal});
    }
    
    // Check if this is a native variable - if so, box it for general use
    // Only use native path if the variable actually exists in native storage
    auto typeIt = variableTypes.find(name);
    if (typeIt != variableTypes.end()) {
        if (typeIt->second == NativeType::NativeInt && nativeIntVars.count(name)) {
            Value* nativeVal = loadNativeInt(name);
            return boxNativeInt(nativeVal);
        }
        if (typeIt->second == NativeType::NativeFloat && nativeFloatVars.count(name)) {
            Value* nativeVal = loadNativeFloat(name);
            return boxNativeFloat(nativeVal);
        }
    }
    return loadVariable(name);
}

Value* LLVMCodeGen::generateBinaryExpr(const BinaryExpr& expr) {
    // Try native optimization for numeric operations
    NativeType leftType = inferExpressionType(expr.left);
    NativeType rightType = inferExpressionType(expr.right);
    
    // Use native operations for numeric types (except for logical operators)
    if (expr.op != "and" && expr.op != "&&" && expr.op != "or" && expr.op != "||") {
        if ((leftType == NativeType::NativeInt || leftType == NativeType::NativeFloat) &&
            (rightType == NativeType::NativeInt || rightType == NativeType::NativeFloat)) {
            
            NativeType resultType = inferExpressionType(std::make_shared<Expression>(Expression{expr, 0}));
            TypedValue result = generateNativeBinaryExpr(expr, resultType);
            
            // Box the result if it's native
            if (result.type == NativeType::NativeInt) {
                return boxNativeInt(result.value);
            }
            if (result.type == NativeType::NativeFloat) {
                return boxNativeFloat(result.value);
            }
            if (result.type == NativeType::NativeBool) {
                // Convert native i1 bool to MoonValue* bool
                return builder->CreateCall(getRuntimeFunction("moon_bool"), {result.value});
            }
            
            return result.value;
        }
    }
    
    // Fall back to dynamic operations
    Value* left = generateExpression(expr.left);
    Value* right = generateExpression(expr.right);
    
    std::string funcName;
    if (expr.op == "+") funcName = "moon_add";
    else if (expr.op == "-") funcName = "moon_sub";
    else if (expr.op == "*") funcName = "moon_mul";
    else if (expr.op == "/") funcName = "moon_div";
    else if (expr.op == "%") funcName = "moon_mod";
    else if (expr.op == "**") funcName = "moon_pow";
    else if (expr.op == "==") funcName = "moon_eq";
    else if (expr.op == "!=") funcName = "moon_ne";
    else if (expr.op == "<") funcName = "moon_lt";
    else if (expr.op == "<=") funcName = "moon_le";
    else if (expr.op == ">") funcName = "moon_gt";
    else if (expr.op == ">=") funcName = "moon_ge";
    else if (expr.op == "and" || expr.op == "&&") funcName = "moon_and";
    else if (expr.op == "or" || expr.op == "||") funcName = "moon_or";
    else if (expr.op == "&") funcName = "moon_bit_and";
    else if (expr.op == "|") funcName = "moon_bit_or";
    else if (expr.op == "^") funcName = "moon_bit_xor";
    else if (expr.op == "<<") funcName = "moon_lshift";
    else if (expr.op == ">>") funcName = "moon_rshift";
    else {
        setError("Unknown binary operator: " + expr.op);
        return generateNullLiteral();
    }
    
    Value* result = builder->CreateCall(getRuntimeFunction(funcName), {left, right});
    
    // Release operands
    builder->CreateCall(getRuntimeFunction("moon_release"), {left});
    builder->CreateCall(getRuntimeFunction("moon_release"), {right});
    
    return result;
}

Value* LLVMCodeGen::generateUnaryExpr(const UnaryExpr& expr) {
    Value* operand = generateExpression(expr.operand);
    
    Value* result;
    if (expr.op == "-") {
        result = builder->CreateCall(getRuntimeFunction("moon_neg"), {operand});
    }
    else if (expr.op == "!" || expr.op == "not") {
        result = builder->CreateCall(getRuntimeFunction("moon_not"), {operand});
    }
    else if (expr.op == "~") {
        result = builder->CreateCall(getRuntimeFunction("moon_bit_not"), {operand});
    }
    else {
        setError("Unknown unary operator: " + expr.op);
        return operand;
    }
    
    builder->CreateCall(getRuntimeFunction("moon_release"), {operand});
    return result;
}

Value* LLVMCodeGen::generateCallExpr(const CallExpr& expr) {
    // Get function name
    std::string funcName;
    if (auto* id = std::get_if<Identifier>(&expr.callee->value)) {
        funcName = id->name;
    }
    
    // Check if it's a built-in function
    if (!funcName.empty() && isBuiltinFunction(funcName)) {
        return generateBuiltinCall(funcName, expr.arguments);
    }
    
    // Check if it's a user-defined function
    if (!funcName.empty() && functions.count(funcName)) {
        size_t argCount = expr.arguments.size();
        size_t paramCount = functionParamCounts.count(funcName) ? functionParamCounts[funcName] : 0;
        
        // If all arguments are provided, we can potentially use native version
        if (argCount == paramCount && nativeFunctions.count(funcName)) {
            // Use native version - much faster
            return generateNativeFunctionCall(funcName, expr.arguments);
        }
        
        // If all arguments are provided, use direct call (faster)
        if (argCount == paramCount) {
            std::vector<Value*> args;
            for (const auto& arg : expr.arguments) {
                args.push_back(generateExpression(arg));
            }
            return builder->CreateCall(functions[funcName], args);
        }
        
        // Fewer arguments provided - use wrapper function which handles defaults
        if (wrapperFunctions.count(funcName)) {
            std::vector<Value*> argVals;
            for (const auto& arg : expr.arguments) {
                argVals.push_back(generateExpression(arg));
            }
            
            // Create args array
            Type* i32Ty = Type::getInt32Ty(*context);
            Value* argsArray = builder->CreateAlloca(moonValuePtrType, 
                ConstantInt::get(i32Ty, argCount));
            
            for (size_t i = 0; i < argCount; i++) {
                Value* ptr = builder->CreateGEP(moonValuePtrType, argsArray, 
                    ConstantInt::get(i32Ty, i));
                builder->CreateStore(argVals[i], ptr);
            }
            
            // Call wrapper function directly with args array and argc
            Value* result = builder->CreateCall(wrapperFunctions[funcName],
                {argsArray, ConstantInt::get(i32Ty, argCount)});
            
            // Release args
            for (auto& argVal : argVals) {
                builder->CreateCall(getRuntimeFunction("moon_release"), {argVal});
            }
            
            return result;
        }
    }
    
    // Method call on object or static method call on class
    if (auto* member = std::get_if<MemberExpr>(&expr.callee->value)) {
        // Check if this is a static method call on a class (ClassName.method())
        std::string className;
        if (auto* id = std::get_if<Identifier>(&member->object->value)) {
            if (classDefinitions.count(id->name)) {
                className = id->name;
            }
        }
        
        // Create args array
        std::vector<Value*> argVals;
        for (const auto& arg : expr.arguments) {
            argVals.push_back(generateExpression(arg));
        }
        
        int argc = argVals.size();
        Value* argsArray = builder->CreateAlloca(moonValuePtrType, 
            ConstantInt::get(Type::getInt32Ty(*context), argc));
        
        for (int i = 0; i < argc; i++) {
            Value* ptr = builder->CreateGEP(moonValuePtrType, argsArray, 
                ConstantInt::get(Type::getInt32Ty(*context), i));
            builder->CreateStore(argVals[i], ptr);
        }
        
        Value* result;
        if (!className.empty()) {
            // Static method call: ClassName.method(args)
            // Call the static method via the class object
            Value* klassPtr = builder->CreateLoad(
                PointerType::get(Type::getInt8Ty(*context), 0),
                classDefinitions[className]
            );
            Value* methodName = createGlobalString(member->member);
            result = builder->CreateCall(getRuntimeFunction("moon_class_call_static_method"),
                {klassPtr, methodName, argsArray, ConstantInt::get(Type::getInt32Ty(*context), argc)});
        } else {
            // Instance method call: obj.method(args)
            Value* obj = generateExpression(member->object);
            Value* methodName = createGlobalString(member->member);
            result = builder->CreateCall(getRuntimeFunction("moon_object_call_method"),
                {obj, methodName, argsArray, ConstantInt::get(Type::getInt32Ty(*context), argc)});
            builder->CreateCall(getRuntimeFunction("moon_release"), {obj});
        }
        
        // Release args
        for (auto& argVal : argVals) {
            builder->CreateCall(getRuntimeFunction("moon_release"), {argVal});
        }
        
        return result;
    }
    
    // Call function stored in a variable
    if (!funcName.empty() && (namedValues.count(funcName) || globalVars.count(funcName))) {
        Value* funcVal = loadVariable(funcName);
        
        // Create args array
        std::vector<Value*> argVals;
        for (const auto& arg : expr.arguments) {
            argVals.push_back(generateExpression(arg));
        }
        
        int argc = argVals.size();
        Value* argsArray = builder->CreateAlloca(moonValuePtrType, 
            ConstantInt::get(Type::getInt32Ty(*context), argc));
        
        for (int i = 0; i < argc; i++) {
            Value* ptr = builder->CreateGEP(moonValuePtrType, argsArray, 
                ConstantInt::get(Type::getInt32Ty(*context), i));
            builder->CreateStore(argVals[i], ptr);
        }
        
        Value* result = builder->CreateCall(getRuntimeFunction("moon_call_func"),
            {funcVal, argsArray, ConstantInt::get(Type::getInt32Ty(*context), argc)});
        
        // Release args and funcVal
        for (auto& argVal : argVals) {
            builder->CreateCall(getRuntimeFunction("moon_release"), {argVal});
        }
        builder->CreateCall(getRuntimeFunction("moon_release"), {funcVal});
        
        return result;
    }
    
    // Try to evaluate callee as an expression (e.g., dict lookup result)
    Value* calleeVal = generateExpression(expr.callee);
    if (calleeVal) {
        // Create args array
        std::vector<Value*> argVals;
        for (const auto& arg : expr.arguments) {
            argVals.push_back(generateExpression(arg));
        }
        
        int argc = argVals.size();
        Value* argsArray = builder->CreateAlloca(moonValuePtrType, 
            ConstantInt::get(Type::getInt32Ty(*context), argc));
        
        for (int i = 0; i < argc; i++) {
            Value* ptr = builder->CreateGEP(moonValuePtrType, argsArray, 
                ConstantInt::get(Type::getInt32Ty(*context), i));
            builder->CreateStore(argVals[i], ptr);
        }
        
        Value* result = builder->CreateCall(getRuntimeFunction("moon_call_func"),
            {calleeVal, argsArray, ConstantInt::get(Type::getInt32Ty(*context), argc)});
        
        // Release args and calleeVal
        for (auto& argVal : argVals) {
            builder->CreateCall(getRuntimeFunction("moon_release"), {argVal});
        }
        builder->CreateCall(getRuntimeFunction("moon_release"), {calleeVal});
        
        return result;
    }
    
    setError("Unknown function: " + funcName);
    return generateNullLiteral();
}

Value* LLVMCodeGen::generateIndexExpr(const IndexExpr& expr) {
    Value* obj = generateExpression(expr.object);
    
    // Optimization: if index is a native integer, use optimized list access
    NativeType indexType = inferExpressionType(expr.index);
    if (indexType == NativeType::NativeInt) {
        // Generate native index
        TypedValue indexTyped = generateNativeExpression(expr.index);
        Value* nativeIdx = indexTyped.value;
        if (indexTyped.type == NativeType::Dynamic) {
            // Shouldn't happen, but handle gracefully
            nativeIdx = unboxToInt(indexTyped.value);
            builder->CreateCall(getRuntimeFunction("moon_release"), {indexTyped.value});
        }
        
        // Use optimized native index access
        Value* result = builder->CreateCall(getRuntimeFunction("moon_list_get_idx"), {obj, nativeIdx});
        builder->CreateCall(getRuntimeFunction("moon_release"), {obj});
        return result;
    }
    
    // Standard path: generate index as MoonValue*
    Value* index = generateExpression(expr.index);
    
    // Use list_get for both lists and dicts (runtime handles it)
    Value* result = builder->CreateCall(getRuntimeFunction("moon_list_get"), {obj, index});
    
    builder->CreateCall(getRuntimeFunction("moon_release"), {obj});
    builder->CreateCall(getRuntimeFunction("moon_release"), {index});
    
    return result;
}

Value* LLVMCodeGen::generateMemberExpr(const MemberExpr& expr) {
    Value* obj = generateExpression(expr.object);
    Value* fieldName = createGlobalString(expr.member);
    
    Value* result = builder->CreateCall(getRuntimeFunction("moon_object_get"), {obj, fieldName});
    builder->CreateCall(getRuntimeFunction("moon_release"), {obj});
    
    return result;
}

Value* LLVMCodeGen::generateListExpr(const ListExpr& expr) {
    Value* list = builder->CreateCall(getRuntimeFunction("moon_list_new"), {});
    
    for (const auto& elem : expr.elements) {
        Value* val = generateExpression(elem);
        builder->CreateCall(getRuntimeFunction("moon_list_append"), {list, val});
        builder->CreateCall(getRuntimeFunction("moon_release"), {val});
    }
    
    return list;
}

Value* LLVMCodeGen::generateDictExpr(const DictExpr& expr) {
    Value* dict = builder->CreateCall(getRuntimeFunction("moon_dict_new"), {});
    
    for (const auto& entry : expr.entries) {
        Value* key = generateExpression(entry.key);
        Value* val = generateExpression(entry.value);
        builder->CreateCall(getRuntimeFunction("moon_dict_set"), {dict, key, val});
        builder->CreateCall(getRuntimeFunction("moon_release"), {key});
        builder->CreateCall(getRuntimeFunction("moon_release"), {val});
    }
    
    return dict;
}

Value* LLVMCodeGen::generateLambdaExpr(const LambdaExpr& expr) {
    // Create anonymous function for lambda
    static int lambdaCounter = 0;
    std::string lambdaName = "moon_lambda_" + std::to_string(lambdaCounter++);
    
    // Lambda function type: MoonValue* (*)(MoonValue** args, int argc)
    Type* valPtrTy = moonValuePtrType;
    Type* valPtrPtrTy = PointerType::get(valPtrTy, 0);
    Type* i32Ty = Type::getInt32Ty(*context);
    FunctionType* lambdaFuncType = FunctionType::get(valPtrTy, {valPtrPtrTy, i32Ty}, false);
    
    // Collect free variables (captures) BEFORE entering lambda context
    std::set<std::string> boundVars;
    for (const auto& p : expr.params) {
        boundVars.insert(p.name);
    }
    std::set<std::string> freeVars;
    if (expr.hasBlockBody) {
        // Multi-statement lambda: collect from block body
        collectFreeVarsFromStmts(expr.blockBody, boundVars, freeVars);
    } else {
        // Single-expression lambda: collect from body expression
        collectFreeVars(expr.body, boundVars, freeVars);
    }
    
    // Build capture list with consistent ordering
    std::vector<std::string> captureList(freeVars.begin(), freeVars.end());
    std::sort(captureList.begin(), captureList.end());  // Ensure consistent order
    
    // Load captured values BEFORE switching context
    // Use generateIdentifier instead of loadVariable to handle native variables
    std::vector<Value*> capturedValues;
    for (const auto& varName : captureList) {
        Value* val = generateIdentifier(varName);
        capturedValues.push_back(val);
    }
    
    Function* lambdaFunc = Function::Create(
        lambdaFuncType,
        Function::ExternalLinkage,
        lambdaName,
        module.get()
    );
    
    // Save current state (including native type tracking and closure state)
    Function* savedFunc = currentFunction;
    BasicBlock* savedBlock = builder->GetInsertBlock();
    auto savedVars = namedValues;
    auto savedNativeIntVars = nativeIntVars;
    auto savedNativeFloatVars = nativeFloatVars;
    auto savedVariableTypes = variableTypes;
    bool savedInClosure = inClosure;
    auto savedClosureCaptures = currentClosureCaptures;
    
    // Setup lambda function
    currentFunction = lambdaFunc;
    BasicBlock* entry = BasicBlock::Create(*context, "entry", lambdaFunc);
    builder->SetInsertPoint(entry);
    
    // Get args array from function parameters
    auto argIt = lambdaFunc->arg_begin();
    Value* argsPtr = &*argIt++;
    argsPtr->setName("args");
    
    // Clear variable tracking and set up closure context
    namedValues.clear();
    nativeIntVars.clear();
    nativeFloatVars.clear();
    variableTypes.clear();
    
    // Set up closure capture mapping
    inClosure = !captureList.empty();
    currentClosureCaptures.clear();
    for (size_t i = 0; i < captureList.size(); i++) {
        currentClosureCaptures[captureList[i]] = (int)i;
    }
    
    // Extract parameters from args array (with default value support)
    Value* argc = &*argIt;  // Get argc from function params
    argc->setName("argc");
    for (size_t i = 0; i < expr.params.size(); i++) {
        Value* paramIdx = ConstantInt::get(i32Ty, i);
        Value* hasArg = builder->CreateICmpSLT(paramIdx, argc);
        
        Value* paramVal;
        if (expr.params[i].defaultValue) {
            // Has default value - conditionally use it
            BasicBlock* argProvidedBB = BasicBlock::Create(*context, "arg_provided", lambdaFunc);
            BasicBlock* useDefaultBB = BasicBlock::Create(*context, "use_default", lambdaFunc);
            BasicBlock* mergeBB = BasicBlock::Create(*context, "merge", lambdaFunc);
            
            builder->CreateCondBr(hasArg, argProvidedBB, useDefaultBB);
            
            // Argument provided path
            builder->SetInsertPoint(argProvidedBB);
            Value* paramPtr = builder->CreateGEP(valPtrTy, argsPtr, paramIdx);
            Value* providedVal = builder->CreateLoad(valPtrTy, paramPtr);
            builder->CreateBr(mergeBB);
            BasicBlock* argProvidedEnd = builder->GetInsertBlock();
            
            // Use default value path
            builder->SetInsertPoint(useDefaultBB);
            Value* defaultVal = generateExpression(expr.params[i].defaultValue);
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
        
        Value* paramAlloca = builder->CreateAlloca(valPtrTy, nullptr, expr.params[i].name);
        builder->CreateStore(paramVal, paramAlloca);
        namedValues[expr.params[i].name] = paramAlloca;
        builder->CreateCall(getRuntimeFunction("moon_retain"), {paramVal});
    }
    
    // Generate lambda body
    if (expr.hasBlockBody) {
        // Multi-statement lambda: generate each statement
        for (const auto& stmt : expr.blockBody) {
            generateStatement(stmt);
            // Stop if we already have a terminator (e.g., from return statement)
            if (builder->GetInsertBlock()->getTerminator()) {
                break;
            }
        }
        // If no return statement, add implicit return null
        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateRet(builder->CreateCall(getRuntimeFunction("moon_null"), {}));
        }
    } else {
        // Single-expression lambda: return the expression result
        Value* result = generateExpression(expr.body);
        builder->CreateRet(result);
    }
    
    // Restore state (including native type tracking and closure state)
    currentFunction = savedFunc;
    namedValues = savedVars;
    nativeIntVars = savedNativeIntVars;
    nativeFloatVars = savedNativeFloatVars;
    variableTypes = savedVariableTypes;
    inClosure = savedInClosure;
    currentClosureCaptures = savedClosureCaptures;
    builder->SetInsertPoint(savedBlock);
    
    // Create closure with captured values
    if (captureList.empty()) {
        // No captures - use simple function wrapper
        Value* funcVal = builder->CreateCall(getRuntimeFunction("moon_func"), {lambdaFunc});
        return funcVal;
    } else {
        // Has captures - create closure
        // Allocate array for captures
        Value* captureCount = ConstantInt::get(i32Ty, captureList.size());
        Value* capturesArray = builder->CreateAlloca(valPtrTy, captureCount, "captures");
        
        // Store captured values in array
        for (size_t i = 0; i < capturedValues.size(); i++) {
            Value* ptr = builder->CreateGEP(valPtrTy, capturesArray, 
                ConstantInt::get(i32Ty, i));
            builder->CreateStore(capturedValues[i], ptr);
        }
        
        // Create closure using moon_closure_new(func, captures, count)
        Value* closureVal = builder->CreateCall(
            getRuntimeFunction("moon_closure_new"),
            {lambdaFunc, capturesArray, captureCount}
        );
        
        // Release the captured values we loaded (they're now owned by the closure)
        for (auto& val : capturedValues) {
            builder->CreateCall(getRuntimeFunction("moon_release"), {val});
        }
        
        return closureVal;
    }
}

Value* LLVMCodeGen::generateNewExpr(const NewExpr& expr) {
    // Get class
    if (!classDefinitions.count(expr.className)) {
        setError("Unknown class: " + expr.className);
        return generateNullLiteral();
    }
    
    Value* klassPtr = builder->CreateLoad(
        PointerType::get(Type::getInt8Ty(*context), 0),
        classDefinitions[expr.className]
    );
    
    Value* obj = builder->CreateCall(getRuntimeFunction("moon_object_new"), {klassPtr});
    
    // Call init method with arguments
    // Create args array for init method
    Type* valPtrTy = moonValuePtrType;
    Type* i32Ty = Type::getInt32Ty(*context);
    
    int argc = expr.arguments.size();
    Value* argsArray = builder->CreateAlloca(valPtrTy, 
        ConstantInt::get(i32Ty, argc), "init_args");
    
    // Fill args array
    for (size_t i = 0; i < expr.arguments.size(); i++) {
        Value* argVal = generateExpression(expr.arguments[i]);
        Value* argPtr = builder->CreateGEP(valPtrTy, argsArray, 
            ConstantInt::get(i32Ty, i));
        builder->CreateStore(argVal, argPtr);
    }
    
    // Call init method via runtime (silent if no init method)
    Value* argCount = ConstantInt::get(i32Ty, argc);
    builder->CreateCall(getRuntimeFunction("moon_object_call_init"), 
                       {obj, argsArray, argCount});
    
    // Release args (they are retained by the method if needed)
    for (size_t i = 0; i < expr.arguments.size(); i++) {
        Value* argPtr = builder->CreateGEP(valPtrTy, argsArray, 
            ConstantInt::get(i32Ty, i));
        Value* argVal = builder->CreateLoad(valPtrTy, argPtr);
        builder->CreateCall(getRuntimeFunction("moon_release"), {argVal});
    }
    
    return obj;
}

Value* LLVMCodeGen::generateSelfExpr() {
    // 'self' is typically the first parameter in a method
    return loadVariable("self");
}

Value* LLVMCodeGen::generateSuperExpr(const SuperExpr& expr) {
    // Super calls - call parent class method with current self
    if (currentClassName.empty()) {
        setError("super can only be used inside a class method");
        return generateNullLiteral();
    }
    
    // Get self
    Value* self = loadVariable("self");
    
    // Get current class name and method name
    Value* classNameVal = createGlobalString(currentClassName);
    Value* methodName = createGlobalString(expr.method);
    
    // Build args array
    Type* valPtrTy = moonValuePtrType;
    Type* i32Ty = Type::getInt32Ty(*context);
    int argc = expr.arguments.size();
    
    // Create array for arguments
    Value* argsArray = builder->CreateAlloca(valPtrTy, ConstantInt::get(i32Ty, argc), "super_args");
    
    // Store arguments
    for (size_t i = 0; i < expr.arguments.size(); i++) {
        Value* argVal = generateExpression(expr.arguments[i]);
        Value* ptr = builder->CreateGEP(valPtrTy, argsArray, 
            ConstantInt::get(Type::getInt32Ty(*context), i));
        builder->CreateStore(argVal, ptr);
    }
    
    // Call super method
    Value* result = builder->CreateCall(
        getRuntimeFunction("moon_object_call_super_method"),
        {self, classNameVal, methodName, argsArray, ConstantInt::get(i32Ty, argc)}
    );
    
    // Release args
    for (size_t i = 0; i < expr.arguments.size(); i++) {
        Value* ptr = builder->CreateGEP(valPtrTy, argsArray,
            ConstantInt::get(Type::getInt32Ty(*context), i));
        Value* argVal = builder->CreateLoad(valPtrTy, ptr);
        builder->CreateCall(getRuntimeFunction("moon_release"), {argVal});
    }
    
    // Release self
    builder->CreateCall(getRuntimeFunction("moon_release"), {self});
    
    return result;
}

// ============================================================================
// Closure Support - Free Variable Collection
// ============================================================================

void LLVMCodeGen::collectFreeVars(const ExprPtr& expr,
                                   const std::set<std::string>& boundVars,
                                   std::set<std::string>& freeVars,
                                   const std::set<std::string>& enclosingVars) {
    std::visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        
        if constexpr (std::is_same_v<T, Identifier>) {
            // If variable is in boundVars (parameter or local) or enclosingVars (defined by outer lambda),
            // it doesn't need to be captured by the outermost lambda being analyzed.
            // Only add to freeVars if it's from outside all the nested lambda scopes.
            if (boundVars.find(arg.name) != boundVars.end() ||
                enclosingVars.find(arg.name) != enclosingVars.end()) {
                return;  // Variable is bound locally or by enclosing lambda
            }
            
            // Variable is not bound - check if it exists in the current compilation context
            // (must be captured from outside)
            bool isLocalVar = namedValues.count(arg.name) > 0 ||
                              nativeIntVars.count(arg.name) > 0 ||
                              nativeFloatVars.count(arg.name) > 0 ||
                              currentClosureCaptures.count(arg.name) > 0;
            
            if (functions.find(arg.name) == functions.end() && isLocalVar) {
                freeVars.insert(arg.name);
            }
        }
        else if constexpr (std::is_same_v<T, BinaryExpr>) {
            collectFreeVars(arg.left, boundVars, freeVars, enclosingVars);
            collectFreeVars(arg.right, boundVars, freeVars, enclosingVars);
        }
        else if constexpr (std::is_same_v<T, UnaryExpr>) {
            collectFreeVars(arg.operand, boundVars, freeVars, enclosingVars);
        }
        else if constexpr (std::is_same_v<T, CallExpr>) {
            collectFreeVars(arg.callee, boundVars, freeVars, enclosingVars);
            for (const auto& a : arg.arguments) {
                collectFreeVars(a, boundVars, freeVars, enclosingVars);
            }
        }
        else if constexpr (std::is_same_v<T, IndexExpr>) {
            collectFreeVars(arg.object, boundVars, freeVars, enclosingVars);
            collectFreeVars(arg.index, boundVars, freeVars, enclosingVars);
        }
        else if constexpr (std::is_same_v<T, MemberExpr>) {
            collectFreeVars(arg.object, boundVars, freeVars, enclosingVars);
        }
        else if constexpr (std::is_same_v<T, ListExpr>) {
            for (const auto& e : arg.elements) {
                collectFreeVars(e, boundVars, freeVars, enclosingVars);
            }
        }
        else if constexpr (std::is_same_v<T, DictExpr>) {
            for (const auto& entry : arg.entries) {
                collectFreeVars(entry.key, boundVars, freeVars, enclosingVars);
                collectFreeVars(entry.value, boundVars, freeVars, enclosingVars);
            }
        }
        else if constexpr (std::is_same_v<T, LambdaExpr>) {
            // Nested lambdas: only bind their own params, don't inherit outer boundVars
            // Instead, pass outer boundVars as enclosingVars so nested lambda can capture them
            std::set<std::string> nestedBound;
            for (const auto& p : arg.params) {
                nestedBound.insert(p.name);
            }
            
            // Merge current boundVars into enclosingVars for nested lambda
            std::set<std::string> newEnclosing = enclosingVars;
            newEnclosing.insert(boundVars.begin(), boundVars.end());
            
            if (arg.hasBlockBody) {
                // Multi-statement lambda: collect from block body
                collectFreeVarsFromStmts(arg.blockBody, nestedBound, freeVars, newEnclosing);
            } else {
                // Single-expression lambda: collect from body expression
                collectFreeVars(arg.body, nestedBound, freeVars, newEnclosing);
            }
        }
        else if constexpr (std::is_same_v<T, SelfExpr>) {
            // Capture 'self' if it exists in local scope and not already bound
            if (boundVars.find("self") == boundVars.end() &&
                namedValues.count("self") > 0) {
                freeVars.insert("self");
            }
        }
        // Other expression types (literals, etc.) don't contain variables
    }, expr->value);
}

// ============================================================================
// Closure Support - Free Variable Collection from Statements
// ============================================================================

void LLVMCodeGen::collectFreeVarsFromStmts(const std::vector<StmtPtr>& stmts,
                                            const std::set<std::string>& boundVars,
                                            std::set<std::string>& freeVars,
                                            const std::set<std::string>& enclosingVars) {
    std::set<std::string> localBound = boundVars;
    for (const auto& stmt : stmts) {
        collectFreeVarsFromStmt(stmt, localBound, freeVars, enclosingVars);
    }
}

void LLVMCodeGen::collectFreeVarsFromStmt(const StmtPtr& stmt,
                                           std::set<std::string>& boundVars,
                                           std::set<std::string>& freeVars,
                                           const std::set<std::string>& enclosingVars) {
    std::visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        
        if constexpr (std::is_same_v<T, ExpressionStmt>) {
            collectFreeVars(arg.expression, boundVars, freeVars, enclosingVars);
        }
        else if constexpr (std::is_same_v<T, AssignStmt>) {
            // Check if RHS uses free variables
            collectFreeVars(arg.value, boundVars, freeVars, enclosingVars);
            
            // If assigning to a simple variable, add it to bound vars (local definition)
            if (auto* id = std::get_if<Identifier>(&arg.target->value)) {
                boundVars.insert(id->name);
            } else {
                // For index/member assignment, check the target too
                collectFreeVars(arg.target, boundVars, freeVars, enclosingVars);
            }
        }
        else if constexpr (std::is_same_v<T, IfStmt>) {
            collectFreeVars(arg.condition, boundVars, freeVars, enclosingVars);
            collectFreeVarsFromStmts(arg.thenBranch, boundVars, freeVars, enclosingVars);
            if (!arg.elseBranch.empty()) {
                collectFreeVarsFromStmts(arg.elseBranch, boundVars, freeVars, enclosingVars);
            }
            for (const auto& elif : arg.elifBranches) {
                collectFreeVars(elif.first, boundVars, freeVars, enclosingVars);
                collectFreeVarsFromStmts(elif.second, boundVars, freeVars, enclosingVars);
            }
        }
        else if constexpr (std::is_same_v<T, WhileStmt>) {
            collectFreeVars(arg.condition, boundVars, freeVars, enclosingVars);
            collectFreeVarsFromStmts(arg.body, boundVars, freeVars, enclosingVars);
        }
        else if constexpr (std::is_same_v<T, ForInStmt>) {
            collectFreeVars(arg.iterable, boundVars, freeVars, enclosingVars);
            std::set<std::string> loopBound = boundVars;
            loopBound.insert(arg.variable);
            collectFreeVarsFromStmts(arg.body, loopBound, freeVars, enclosingVars);
        }
        else if constexpr (std::is_same_v<T, ForRangeStmt>) {
            collectFreeVars(arg.start, boundVars, freeVars, enclosingVars);
            collectFreeVars(arg.end, boundVars, freeVars, enclosingVars);
            std::set<std::string> loopBound = boundVars;
            loopBound.insert(arg.variable);
            collectFreeVarsFromStmts(arg.body, loopBound, freeVars, enclosingVars);
        }
        else if constexpr (std::is_same_v<T, ReturnStmt>) {
            if (arg.value) {
                collectFreeVars(arg.value, boundVars, freeVars, enclosingVars);
            }
        }
        else if constexpr (std::is_same_v<T, TryStmt>) {
            collectFreeVarsFromStmts(arg.tryBody, boundVars, freeVars, enclosingVars);
            std::set<std::string> catchBound = boundVars;
            catchBound.insert(arg.errorVar);
            collectFreeVarsFromStmts(arg.catchBody, catchBound, freeVars, enclosingVars);
        }
        else if constexpr (std::is_same_v<T, ThrowStmt>) {
            collectFreeVars(arg.value, boundVars, freeVars, enclosingVars);
        }
        else if constexpr (std::is_same_v<T, SwitchStmt>) {
            collectFreeVars(arg.value, boundVars, freeVars, enclosingVars);
            for (const auto& c : arg.cases) {
                for (const auto& v : c.values) {
                    collectFreeVars(v, boundVars, freeVars, enclosingVars);
                }
                collectFreeVarsFromStmts(c.body, boundVars, freeVars, enclosingVars);
            }
            collectFreeVarsFromStmts(arg.defaultBody, boundVars, freeVars, enclosingVars);
        }
        else if constexpr (std::is_same_v<T, FuncDecl>) {
            // Nested function definitions become bound names
            boundVars.insert(arg.name);
            // IMPORTANT: Descend into nested function body to collect its free variables
            // This enables transitive closure capture - if inner functions need outer variables,
            // intermediate functions must capture them too
            // For nested functions, merge current boundVars into enclosingVars
            std::set<std::string> nestedBound;
            for (const auto& p : arg.params) {
                nestedBound.insert(p.name);
            }
            std::set<std::string> newEnclosing = enclosingVars;
            newEnclosing.insert(boundVars.begin(), boundVars.end());
            collectFreeVarsFromStmts(arg.body, nestedBound, freeVars, newEnclosing);
        }
        else if constexpr (std::is_same_v<T, ClassDecl>) {
            // Class declarations become bound names
            boundVars.insert(arg.name);
        }
        else if constexpr (std::is_same_v<T, MoonStmt>) {
            collectFreeVars(arg.callExpr, boundVars, freeVars, enclosingVars);
        }
        // BreakStmt, ContinueStmt, ImportStmt don't contain free variables
    }, stmt->value);
}

#include "Expr.h"
#include "Type.h"
#include "codegen/CodegenContext.h"
#include "support/Log.h"
#include "Mangle.h"

static llvm::Value* emitLoad(CodegenContext& ctx, llvm::Value* ptr, Type* astType = nullptr) {
    return ctx.loadValue(ptr, astType);
}

// Evaluate an expression node as a value: lvalues are dereferenced, rvalues are
// used as-is. A pointer-typed rvalue (string literal, call result, function
// designator) is already a value and must not be loaded from.
static llvm::Value* emitRValue(CodegenContext& ctx, ExprAST& expr, llvm::Value* v) {
    if (!v) return nullptr;
    return expr.isLValue ? emitLoad(ctx, v, expr.type) : v;
}

// Apply C's default argument promotions so a print argument matches its
// compile-time chosen printf conversion.
static llvm::Value* promotePrintArg(CodegenContext& ctx, llvm::Value* v, PrintArgKind kind) {
    if (!v) return nullptr;
    auto& builder = ctx.getBuilder();
    llvm::LLVMContext& c = ctx.getContext();
    llvm::Type* ty = v->getType();

    switch (kind) {
        case PrintArgKind::Bool: {
            llvm::Value* cond = v;
            if (ty->isIntegerTy() && ty->getIntegerBitWidth() != 1) {
                cond = builder.CreateICmpNE(v, llvm::ConstantInt::get(ty, 0), "boolcond");
            }
            llvm::Value* yes = builder.CreateGlobalString("true", ".boolstr");
            llvm::Value* no = builder.CreateGlobalString("false", ".boolstr");
            return builder.CreateSelect(cond, yes, no, "boolstr");
        }
        case PrintArgKind::Char:
        case PrintArgKind::Int32:
            if (ty->isIntegerTy(32)) return v;
            if (ty->isIntegerTy(1)) return builder.CreateZExt(v, llvm::Type::getInt32Ty(c), "promo");
            return builder.CreateSExtOrTrunc(v, llvm::Type::getInt32Ty(c), "promo");
        case PrintArgKind::UInt32:
            if (ty->isIntegerTy(32)) return v;
            return builder.CreateZExtOrTrunc(v, llvm::Type::getInt32Ty(c), "promo");
        case PrintArgKind::Int64:
            if (ty->isIntegerTy(64)) return v;
            return builder.CreateSExtOrTrunc(v, llvm::Type::getInt64Ty(c), "promo");
        case PrintArgKind::UInt64:
            if (ty->isIntegerTy(64)) return v;
            return builder.CreateZExtOrTrunc(v, llvm::Type::getInt64Ty(c), "promo");
        case PrintArgKind::Float:
            if (ty->isDoubleTy()) return v;
            if (ty->isFloatTy()) return builder.CreateFPExt(v, llvm::Type::getDoubleTy(c), "promo");
            return v;
        case PrintArgKind::CString:
        case PrintArgKind::Pointer:
        case PrintArgKind::ToString:
        default:
            return v;
    }
}

llvm::Value* NumberExprAST::codegen(CodegenContext& ctx) {
    return llvm::ConstantInt::get(ctx.getContext(), llvm::APInt(32, value, true));
}

llvm::Value* FloatExprAST::codegen(CodegenContext& ctx) {
    return llvm::ConstantFP::get(ctx.getContext(), llvm::APFloat(value));
}

llvm::Value* CharExprAST::codegen(CodegenContext& ctx) {
    return llvm::ConstantInt::get(ctx.getContext(), llvm::APInt(8, value));
}

llvm::Value* StringExprAST::codegen(CodegenContext& ctx) {
    return ctx.getBuilder().CreateGlobalString(value, ".str");
}

llvm::Value* VariableExprAST::codegen(CodegenContext& ctx) {
    // Enumerator: materialize the compile-time integer constant.
    if (isEnumConstant) {
        llvm::Type* ty = type ? ctx.getLLVMType(type)
                              : llvm::Type::getInt32Ty(ctx.getContext());
        return llvm::ConstantInt::get(ty, static_cast<uint64_t>(enumValue), /*isSigned=*/true);
    }

    // A function name used as a value yields the function itself.
    if (isFunctionRef) {
        if (llvm::Function* fn = ctx.getModule().getFunction(resolvedFunctionName)) {
            return fn;
        }
        LOGE("unknown function: {}", resolvedFunctionName);
        return nullptr;
    }

    Symbol* sym = ctx.currentScope()->lookup(name);
    if (!sym) {
        LOGE("unknown variable: {}", name);
        return nullptr;
    }
    return ctx.lookupVariableAddr(name);
}

llvm::Value* BinaryExprAST::codegen(CodegenContext& ctx) {
    // Check if this is an overloaded operator call
    if (!mangledCallee.empty()) {
        llvm::Function* calleeFn = ctx.getModule().getFunction(mangledCallee);
        if (!calleeFn) {
            LOGE("unknown operator function: {}", mangledCallee);
            return nullptr;
        }
        
        llvm::Value* lhs = left->codegen(ctx);
        llvm::Value* rhs = right->codegen(ctx);
        if (!lhs || !rhs) return nullptr;
        
        lhs = emitRValue(ctx, *left, lhs);
        rhs = emitRValue(ctx, *right, rhs);
        
        return ctx.getBuilder().CreateCall(calleeFn, {lhs, rhs}, "opcalltmp");
    }
    
    llvm::Value* lhs = left->codegen(ctx);
    llvm::Value* rhs = right->codegen(ctx);
    if (!lhs || !rhs) return nullptr;

    lhs = emitRValue(ctx, *left, lhs);
    rhs = emitRValue(ctx, *right, rhs);

    auto& builder = ctx.getBuilder();

    // Pointer arithmetic (ptr +/- int, int + ptr) must use GEP, not add/sub.
    auto pointerLike = [](Type* t) {
        return t && (t->kind == TypeKind::Pointer || t->kind == TypeKind::Array);
    };
    const bool leftPtr = pointerLike(left->type);
    const bool rightPtr = pointerLike(right->type);
    if ((op == BinaryOp::Add || op == BinaryOp::Sub) && (leftPtr != rightPtr)) {
        llvm::Value* ptrVal = leftPtr ? lhs : rhs;
        llvm::Value* idxVal = leftPtr ? rhs : lhs;
        Type* ptrType = leftPtr ? left->type : right->type;
        Type* pointee = nullptr;
        if (ptrType->kind == TypeKind::Pointer) {
            pointee = ptrType->base;
        } else if (ptrType->kind == TypeKind::Array) {
            pointee = static_cast<ArrayType*>(ptrType)->elementType;
        }
        llvm::Type* elemTy = pointee ? ctx.getLLVMType(pointee)
                                     : llvm::Type::getInt8Ty(ctx.getContext());
        if (!elemTy) elemTy = llvm::Type::getInt8Ty(ctx.getContext());
        if (op == BinaryOp::Sub) {
            idxVal = builder.CreateNeg(idxVal, "negidx");
        }
        return builder.CreateGEP(elemTy, ptrVal, idxVal, "ptradd");
    }

    // Pointer vs integer comparison (e.g. `p == null`, `0 != p`): compare as
    // integers of pointer width. `opType` below would otherwise be the pointer
    // type and leave the integer operand unconverted (ICmp type assert), while
    // converting to the integer's own width could truncate the address.
    if (leftPtr != rightPtr) {
        llvm::Value* intVal = leftPtr ? rhs : lhs;
        llvm::Value* ptrVal = leftPtr ? lhs : rhs;
        if (intVal->getType()->isIntegerTy()) {
            llvm::Type* ptrIntTy =
                ctx.getModule().getDataLayout().getIntPtrType(ctx.getContext());
            llvm::Value* ptrAsInt = builder.CreatePtrToInt(ptrVal, ptrIntTy, "ptrtoint");
            llvm::Value* intWide = ctx.castValue(intVal, ptrIntTy);
            if (leftPtr) {
                lhs = ptrAsInt;
                rhs = intWide;
            } else {
                lhs = intWide;
                rhs = ptrAsInt;
            }
        }
    }

    // Coerce both operands to a common arithmetic type. Floating-point
    // operations must use the FP opcodes (FAdd/FMul/FCmp...), not the integer
    // ones.
    llvm::Type* lhsTy = lhs->getType();
    llvm::Type* rhsTy = rhs->getType();
    llvm::Type* opType = lhsTy;
    if (lhsTy->isFloatingPointTy() || rhsTy->isFloatingPointTy()) {
        if (lhsTy->isFloatingPointTy() && rhsTy->isFloatingPointTy()) {
            opType = (lhsTy->getFPMantissaWidth() >= rhsTy->getFPMantissaWidth()) ? lhsTy : rhsTy;
        } else if (lhsTy->isFloatingPointTy()) {
            opType = lhsTy;
        } else {
            opType = rhsTy;
        }
    } else if (lhsTy->isIntegerTy() && rhsTy->isIntegerTy()) {
        opType = (lhsTy->getIntegerBitWidth() >= rhsTy->getIntegerBitWidth()) ? lhsTy : rhsTy;
    }
    lhs = ctx.castValue(lhs, opType);
    rhs = ctx.castValue(rhs, opType);
    bool isFloat = opType->isFloatingPointTy();

    switch (op) {
        case BinaryOp::Add:    return isFloat ? builder.CreateFAdd(lhs, rhs, "addtmp")
                                              : builder.CreateAdd(lhs, rhs, "addtmp");
        case BinaryOp::Sub:    return isFloat ? builder.CreateFSub(lhs, rhs, "subtmp")
                                              : builder.CreateSub(lhs, rhs, "subtmp");
        case BinaryOp::Mul:    return isFloat ? builder.CreateFMul(lhs, rhs, "multmp")
                                              : builder.CreateMul(lhs, rhs, "multmp");
        case BinaryOp::Div:    return isFloat ? builder.CreateFDiv(lhs, rhs, "divtmp")
                                              : builder.CreateSDiv(lhs, rhs, "divtmp");
        case BinaryOp::Mod:    return isFloat ? builder.CreateFRem(lhs, rhs, "modtmp")
                                              : builder.CreateSRem(lhs, rhs, "modtmp");
        case BinaryOp::Eq:     return isFloat ? builder.CreateFCmpOEQ(lhs, rhs, "eqtmp")
                                              : builder.CreateICmpEQ(lhs, rhs, "eqtmp");
        case BinaryOp::NotEq:  return isFloat ? builder.CreateFCmpONE(lhs, rhs, "netmp")
                                              : builder.CreateICmpNE(lhs, rhs, "netmp");
        case BinaryOp::Lt:     return isFloat ? builder.CreateFCmpOLT(lhs, rhs, "lttmp")
                                              : builder.CreateICmpSLT(lhs, rhs, "lttmp");
        case BinaryOp::Gt:     return isFloat ? builder.CreateFCmpOGT(lhs, rhs, "gttmp")
                                              : builder.CreateICmpSGT(lhs, rhs, "gttmp");
        case BinaryOp::Le:     return isFloat ? builder.CreateFCmpOLE(lhs, rhs, "letmp")
                                              : builder.CreateICmpSLE(lhs, rhs, "letmp");
        case BinaryOp::Ge:     return isFloat ? builder.CreateFCmpOGE(lhs, rhs, "getmp")
                                              : builder.CreateICmpSGE(lhs, rhs, "getmp");
        case BinaryOp::And:    return builder.CreateAnd(lhs, rhs, "andtmp");
        case BinaryOp::Or:     return builder.CreateOr(lhs, rhs, "ortmp");
        case BinaryOp::BitAnd: return builder.CreateAnd(lhs, rhs, "bitandtmp");
        case BinaryOp::BitOr:  return builder.CreateOr(lhs, rhs, "bitortmp");
        case BinaryOp::BitXor: return builder.CreateXor(lhs, rhs, "bitxortmp");
        case BinaryOp::LShift: return builder.CreateShl(lhs, rhs, "lshifttmp");
        case BinaryOp::RShift: return builder.CreateAShr(lhs, rhs, "rshifttmp");
        default:
            LOGE("invalid binary operator");
            return nullptr;
    }
}

llvm::Value* UnaryExprAST::codegen(CodegenContext& ctx) {
    llvm::Value* v = operand->codegen(ctx);
    if (!v) return nullptr;

    auto& builder = ctx.getBuilder();

    switch (op) {
        case UnaryOp::Plus:      return emitRValue(ctx, *operand, v);
        case UnaryOp::Minus: {
            llvm::Value* operandVal = emitRValue(ctx, *operand, v);
            // Floating-point negation must use fneg; integer CreateNeg would
            // expand to `0 - x`, which is not selectable for constants here.
            if (operandVal->getType()->isFloatingPointTy()) {
                return builder.CreateFNeg(operandVal, "negtmp");
            }
            return builder.CreateNeg(operandVal, "negtmp");
        }
        case UnaryOp::Not: {
            // Logical negation: normalize to i1 first, so `!x` is not bitwise
            // complement and `!p` (pointer) is well-typed.
            llvm::Value* operandVal = emitRValue(ctx, *operand, v);
            return builder.CreateNot(ctx.coerceToBool(operandVal), "nottmp");
        }
        case UnaryOp::BitNot:    return builder.CreateNot(emitRValue(ctx, *operand, v), "bitnottmp");
        case UnaryOp::Deref: {
            // `*p` denotes the pointee as an lvalue: yield its address and let
            // consumers load it, mirroring how variable references work.
            llvm::Value* ptrVal = emitRValue(ctx, *operand, v);
            if (operand->type && operand->type->kind == TypeKind::Pointer) {
                return ptrVal;
            }
            // Fallback when the operand type is unknown/lowering cannot tell.
            llvm::Type* pointeeType = ctx.getLLVMType(operand->type ? operand->type->base : nullptr);
            if (!pointeeType) pointeeType = llvm::Type::getInt8Ty(ctx.getContext());
            return builder.CreateLoad(pointeeType, ptrVal, "dereftmp");
        }
        case UnaryOp::PreInc:
        case UnaryOp::PreDec: {
            // The operand is an lvalue; update it in place and yield the new
            // value.
            llvm::Type* loadType = operand->type
                ? ctx.getLLVMType(operand->type)
                : llvm::Type::getInt32Ty(ctx.getContext());
            if (!loadType) loadType = llvm::Type::getInt32Ty(ctx.getContext());
            llvm::Value* oldVal = builder.CreateLoad(loadType, v, "preold");

            llvm::Value* newVal = nullptr;
            if (loadType->isPointerTy()) {
                llvm::Type* elemTy = (operand->type && operand->type->base)
                    ? ctx.getLLVMType(operand->type->base)
                    : llvm::Type::getInt8Ty(ctx.getContext());
                if (!elemTy) elemTy = llvm::Type::getInt8Ty(ctx.getContext());
                llvm::Value* step = llvm::ConstantInt::get(
                    llvm::Type::getInt64Ty(ctx.getContext()),
                    op == UnaryOp::PreInc ? 1 : -1);
                newVal = builder.CreateGEP(elemTy, oldVal, step, "preptr");
            } else {
                llvm::Value* one = llvm::ConstantInt::get(loadType, 1);
                newVal = (op == UnaryOp::PreInc)
                    ? builder.CreateAdd(oldVal, one, "preinc")
                    : builder.CreateSub(oldVal, one, "predec");
            }
            builder.CreateStore(newVal, v);
            return newVal;
        }
        case UnaryOp::AddressOf: return v;
        default:
            LOGE("invalid unary operator");
            return nullptr;
    }
}

// Emit `dprintf(2, fmt, extra...)` then `abort()` (STD-01 / STD-27 / DEC-21).
// `fmt` points at a fixed format string built at compile time (never user data,
// so `%` in a path or message cannot corrupt the call); `extra` are varargs.
static void emitDprintfAndAbort(CodegenContext& ctx, llvm::Value* fmt,
                                const std::vector<llvm::Value*>& extra) {
    llvm::LLVMContext& c = ctx.getContext();
    auto& builder = ctx.getBuilder();
    llvm::Type* i32 = llvm::Type::getInt32Ty(c);
    llvm::Type* ptr = llvm::PointerType::get(c, 0);

    llvm::FunctionType* dprintfTy = llvm::FunctionType::get(i32, {i32, ptr}, true);
    llvm::FunctionCallee dprintfFn = ctx.getModule().getOrInsertFunction("dprintf", dprintfTy);

    std::vector<llvm::Value*> callArgs;
    callArgs.push_back(llvm::ConstantInt::get(i32, 2)); // fd 2 = stderr
    callArgs.push_back(fmt);
    callArgs.insert(callArgs.end(), extra.begin(), extra.end());
    builder.CreateCall(dprintfTy, dprintfFn.getCallee(), callArgs);

    llvm::FunctionType* abortTy = llvm::FunctionType::get(llvm::Type::getVoidTy(c), false);
    llvm::FunctionCallee abortFn = ctx.getModule().getOrInsertFunction("abort", abortTy);
    builder.CreateCall(abortTy, abortFn.getCallee(), {});
}

// "<file>:<line>" for the current node, or "" when unknown.
static std::string nodeSourcePrefix(const ASTNode& node) {
    if (node.sourceFile.empty()) return "";
    return node.sourceFile + ":" + std::to_string(node.sourceLine);
}

llvm::Value* CallExprAST::codegen(CodegenContext& ctx) {
    // Builtin `assert(cond)`: on a false condition, report the call site and
    // abort; on success, fall through. Never silently recovers.
    if (isAssert) {
        llvm::LLVMContext& c = ctx.getContext();
        auto& builder = ctx.getBuilder();
        llvm::Value* cond = args[0]->codegen(ctx);
        if (!cond) return nullptr;
        if (args[0]->isLValue) cond = ctx.loadValue(cond, args[0]->type);
        cond = ctx.coerceToBool(cond);
        if (!cond) return nullptr;

        llvm::Function* fn = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* failBB = llvm::BasicBlock::Create(c, "assert.fail", fn);
        llvm::BasicBlock* okBB = llvm::BasicBlock::Create(c, "assert.ok", fn);
        builder.CreateCondBr(cond, okBB, failBB);

        builder.SetInsertPoint(failBB);
        std::string prefix = nodeSourcePrefix(*this);
        std::string msg = prefix.empty() ? "assertion failed\n"
                                         : prefix + ": assertion failed\n";
        llvm::Value* fmt = builder.CreateGlobalString("%s", ".assertfmt");
        llvm::Value* text = builder.CreateGlobalString(msg, ".assertmsg");
        emitDprintfAndAbort(ctx, fmt, {text});
        builder.CreateUnreachable();

        builder.SetInsertPoint(okBB);
        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(c), 0);
    }

    // Builtin `panic(msg)`: report the call site and the user message, then
    // abort. The block is terminated; a dead continuation keeps later codegen
    // total (and is unreachable).
    if (isPanic) {
        llvm::LLVMContext& c = ctx.getContext();
        auto& builder = ctx.getBuilder();
        llvm::Value* userMsg = args[0]->codegen(ctx);
        if (!userMsg) return nullptr;
        if (args[0]->isLValue) userMsg = ctx.loadValue(userMsg, args[0]->type);
        if (!userMsg) return nullptr;
        if (!userMsg->getType()->isPointerTy()) {
            userMsg = ctx.castValue(userMsg, llvm::PointerType::get(c, 0));
        }

        llvm::Function* fn = builder.GetInsertBlock()->getParent();
        std::string prefix = nodeSourcePrefix(*this);
        std::string fmtStr = prefix.empty() ? "panic: %s\n" : prefix + ": panic: %s\n";
        llvm::Value* fmt = builder.CreateGlobalString(fmtStr, ".panicfmt");
        emitDprintfAndAbort(ctx, fmt, {userMsg});
        builder.CreateUnreachable();

        llvm::BasicBlock* contBB = llvm::BasicBlock::Create(c, "panic.cont", fn);
        builder.SetInsertPoint(contBB);
        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(c), 0);
    }

    // Builtin `print`/`println`: emit a call to the C library's printf with a
    // compile-time-built conversion string.
    if (isPrint) {
        llvm::LLVMContext& c = ctx.getContext();
        auto& builder = ctx.getBuilder();

        llvm::Value* format = builder.CreateGlobalString(printCFormat, ".printfmt");
        llvm::FunctionType* printfTy = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(c), {llvm::PointerType::get(c, 0)}, true);
        llvm::FunctionCallee printfFn = ctx.getModule().getOrInsertFunction("printf", printfTy);

        std::vector<llvm::Value*> callArgs;
        callArgs.push_back(format);
        for (size_t k = 1; k < args.size(); ++k) {
            llvm::Value* v = args[k]->codegen(ctx);
            if (!v) return nullptr;
            if (args[k]->isLValue) v = ctx.loadValue(v, args[k]->type);
            if (k - 1 < printArgKinds.size()) {
                v = promotePrintArg(ctx, v, printArgKinds[k - 1]);
            }
            if (!v) return nullptr;
            callArgs.push_back(v);
        }
        return builder.CreateCall(printfTy, printfFn.getCallee(), callArgs);
    }

    // Indirect call through a function-pointer variable.
    if (isIndirect) {
        llvm::Value* fpAddr = ctx.lookupVariableAddr(callee);
        if (!fpAddr) {
            LOGE("unknown function pointer: {}", callee);
            return nullptr;
        }
        llvm::Value* fnPtr = ctx.getBuilder().CreateLoad(
            llvm::PointerType::get(ctx.getContext(), 0), fpAddr, "fnptr");

        std::vector<llvm::Value*> argsV;
        for (size_t i = 0; i < args.size(); ++i) {
            llvm::Value* argVal = args[i]->codegen(ctx);
            if (!argVal) return nullptr;
            if (args[i]->isLValue) argVal = ctx.loadValue(argVal, args[i]->type);
            if (i < resolvedParamTypes.size()) {
                argVal = ctx.castValue(argVal, ctx.getLLVMType(resolvedParamTypes[i]));
            }
            argsV.push_back(argVal);
        }

        std::vector<llvm::Type*> paramLLVMTypes;
        for (auto* t : resolvedParamTypes) paramLLVMTypes.push_back(ctx.getLLVMType(t));
        llvm::Type* retLLVM = type ? ctx.getLLVMType(type)
                                   : llvm::Type::getVoidTy(ctx.getContext());
        auto* fnType = llvm::FunctionType::get(retLLVM, paramLLVMTypes, false);
        if (retLLVM->isVoidTy()) {
            return ctx.getBuilder().CreateCall(fnType, fnPtr, argsV);
        }
        return ctx.getBuilder().CreateCall(fnType, fnPtr, argsV, "icalltmp");
    }

    // Prefer the overload chosen by semantic analysis so that implicit
    // conversions (e.g. int8 -> int) resolve to the right symbol.
    std::vector<Type*> argTypes;
    if (!resolvedParamTypes.empty()) {
        argTypes = resolvedParamTypes;
    } else {
        for (auto& arg : args) {
            argTypes.push_back(arg->type);
        }
    }
    std::string mangledName = mangleFunction(callee, argTypes);
    
    llvm::Function* calleeFn = ctx.getModule().getFunction(mangledName);
    if (!calleeFn) {
        LOGE("unknown function: {}", callee);
        return nullptr;
    }

    std::vector<llvm::Value*> argsV;
    for (size_t i = 0; i < args.size(); ++i) {
        llvm::Value* argVal = args[i]->codegen(ctx);
        if (!argVal) return nullptr;
        if (args[i]->isLValue) {
            argVal = emitLoad(ctx, argVal, args[i]->type);
        }
        if (i < resolvedParamTypes.size()) {
            argVal = ctx.castValue(argVal, ctx.getLLVMType(resolvedParamTypes[i]));
        }
        argsV.push_back(argVal);
    }

    if (calleeFn->isVarArg()) {
        if (argsV.size() < calleeFn->arg_size()) {
            LOGE("function {} expects at least {} args, got {}", callee, calleeFn->arg_size(), argsV.size());
            return nullptr;
        }
    } else if (argsV.size() != calleeFn->arg_size()) {
        LOGE("function {} expects {} args, got {}", callee, calleeFn->arg_size(), argsV.size());
        return nullptr;
    }

    if (calleeFn->getReturnType()->isVoidTy()) {
        return ctx.getBuilder().CreateCall(calleeFn, argsV);
    }
    return ctx.getBuilder().CreateCall(calleeFn, argsV, "calltmp");
}

llvm::Value* MethodCallExprAST::codegen(CodegenContext& ctx) {
    llvm::Value* objVal = object->codegen(ctx);
    if (!objVal) return nullptr;

    // Get the object's type (should be a pointer to a class type)
    llvm::Type* objLLVMType = objVal->getType();
    if (!objLLVMType->isPointerTy()) return nullptr;

    // Determine the class type name for method mangling
    std::string className;
    Type* objType = object->type;
    if (objType && objType->kind == TypeKind::Pointer && objType->base) {
        objType = objType->base;
    }
    ClassType* classType = nullptr;
    if (objType && objType->kind == TypeKind::Class) {
        classType = static_cast<ClassType*>(objType);
        className = classType->name;
    } else if (objType && objType->kind == TypeKind::Struct) {
        auto* structType = static_cast<StructType*>(objType);
        className = structType->name;
    }

    // Build argument types: this pointer first, then explicit args
    std::vector<Type*> argTypes;
    Type* thisType = new Type(TypeKind::Pointer, objType);
    argTypes.push_back(thisType);
    for (auto& arg : args) {
        argTypes.push_back(arg->type);
    }

    std::string mangledName = mangleFunction(methodName, argTypes);

    llvm::Function* calleeFn = ctx.getModule().getFunction(mangledName);
    if (!calleeFn) {
        // Walk inheritance chain to find the declaring class
        ClassType* searchType = classType;
        while (!calleeFn && searchType && !searchType->baseClass.empty()) {
            ClassType* baseType = TypeContext::instance().getClass(searchType->baseClass);
            if (!baseType) break;
            std::vector<Type*> baseArgTypes;
            Type* baseThisType = new Type(TypeKind::Pointer, baseType);
            baseArgTypes.push_back(baseThisType);
            for (auto& arg : args) {
                baseArgTypes.push_back(arg->type);
            }
            std::string baseMangledName = mangleFunction(methodName, baseArgTypes);
            calleeFn = ctx.getModule().getFunction(baseMangledName);
            if (calleeFn) {
                mangledName = baseMangledName;
                className = baseType->name;
                break;
            }
            searchType = baseType;
        }
    }

    std::vector<llvm::Value*> argsV;
    // Pass object pointer as the this argument
    argsV.push_back(objVal);
    for (auto& arg : args) {
        llvm::Value* argVal = arg->codegen(ctx);
        if (!argVal) return nullptr;
        if (arg->isLValue) {
            argVal = emitLoad(ctx, argVal, arg->type);
        }
        argsV.push_back(argVal);
    }

    if (argsV.size() != calleeFn->arg_size()) {
        LOGE("method {}.{} expects {} args, got {}", className, methodName, calleeFn->arg_size(), argsV.size());
        return nullptr;
    }

    if (calleeFn->getReturnType()->isVoidTy()) {
        return ctx.getBuilder().CreateCall(calleeFn, argsV);
    }
    return ctx.getBuilder().CreateCall(calleeFn, argsV, "calltmp");
}

llvm::Value* AssignmentExprAST::codegen(CodegenContext& ctx) {
    llvm::Value* lhsVal = lhs->codegen(ctx);
    llvm::Value* rhsVal = rhs->codegen(ctx);
    if (!lhsVal || !rhsVal) return nullptr;

    auto& builder = ctx.getBuilder();
    // Only load the RHS when it denotes a location; values such as function
    // designators or '&x' are already the stored value.
    llvm::Value* result = rhs->isLValue ? emitLoad(ctx, rhsVal, rhs->type) : rhsVal;

    if (lhs->type) {
        result = ctx.castValue(result, ctx.getLLVMType(lhs->type));
    }

    if (op != AssignOp::Assign) {
        llvm::Value* loadedLhs = emitLoad(ctx, lhsVal, lhs->type);
        switch (op) {
            case AssignOp::AddAssign:   result = builder.CreateAdd(loadedLhs, result, "addassign"); break;
            case AssignOp::SubAssign:   result = builder.CreateSub(loadedLhs, result, "subassign"); break;
            case AssignOp::MulAssign:   result = builder.CreateMul(loadedLhs, result, "mulassign"); break;
            case AssignOp::DivAssign:   result = builder.CreateSDiv(loadedLhs, result, "divassign"); break;
            case AssignOp::ModAssign:   result = builder.CreateSRem(loadedLhs, result, "modassign"); break;
            case AssignOp::BitAndAssign: result = builder.CreateAnd(loadedLhs, result, "bandassign"); break;
            case AssignOp::BitOrAssign:  result = builder.CreateOr(loadedLhs, result, "borassign"); break;
            case AssignOp::BitXorAssign: result = builder.CreateXor(loadedLhs, result, "bxorassign"); break;
            case AssignOp::LShiftAssign: result = builder.CreateShl(loadedLhs, result, "lshiftassign"); break;
            case AssignOp::RShiftAssign: result = builder.CreateAShr(loadedLhs, result, "rshiftassign"); break;
            default: break;
        }
    }

    builder.CreateStore(result, lhsVal);
    return result;
}

llvm::Value* TernaryExprAST::codegen(CodegenContext& ctx) {
    llvm::Value* condVal = cond->codegen(ctx);
    if (!condVal) return nullptr;
    if (cond->isLValue && cond->type && cond->type->kind != TypeKind::Array) {
        condVal = ctx.loadValue(condVal, cond->type);
    }
    condVal = ctx.coerceToBool(condVal);

    auto& builder = ctx.getBuilder();
    llvm::Function* func = builder.GetInsertBlock()->getParent();

    llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(ctx.getContext(), "ternary.then", func);
    llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(ctx.getContext(), "ternary.else", func);
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.getContext(), "ternary.merge", func);

    builder.CreateCondBr(condVal, thenBB, elseBB);

    builder.SetInsertPoint(thenBB);
    llvm::Value* thenVal = then->codegen(ctx);
    if (!thenVal) return nullptr;
    builder.CreateBr(mergeBB);

    builder.SetInsertPoint(elseBB);
    llvm::Value* elseVal = elseExpr->codegen(ctx);
    if (!elseVal) return nullptr;
    builder.CreateBr(mergeBB);

    builder.SetInsertPoint(mergeBB);
    llvm::PHINode* phi = builder.CreatePHI(thenVal->getType(), 2, "ternarytmp");
    phi->addIncoming(thenVal, thenBB);
    phi->addIncoming(elseVal, elseBB);
    return phi;
}

llvm::Value* CastExprAST::codegen(CodegenContext& ctx) {
    llvm::Value* val = expr->codegen(ctx);
    if (!val) return nullptr;

    llvm::Type* targetLLVMType = ctx.getLLVMType(castType);
    if (!targetLLVMType) return nullptr;

    if (expr->isLValue) {
        val = emitLoad(ctx, val, expr->type);
    }

    // `reinterpret_cast` reinterprets the bit pattern of same-width scalars
    // (notably float <-> int). Pointers and pointer<->integer go through
    // castValue (bitcast / ptrtoint / inttoptr); other pairs fall back to a
    // value conversion.
    if (castKind == CastKind::Reinterpret) {
        llvm::Type* src = val->getType();
        if (src == targetLLVMType) return val;
        const bool sameWidthScalars =
            !src->isPointerTy() && !targetLLVMType->isPointerTy() &&
            src->isSized() && targetLLVMType->isSized() &&
            src->getPrimitiveSizeInBits() == targetLLVMType->getPrimitiveSizeInBits() &&
            (src->isFloatingPointTy() || targetLLVMType->isFloatingPointTy());
        if (sameWidthScalars) {
            return ctx.getBuilder().CreateBitCast(val, targetLLVMType, "reinterpret");
        }
        return ctx.castValue(val, targetLLVMType);
    }

    return ctx.castValue(val, targetLLVMType);
}

llvm::Value* CommaExprAST::codegen(CodegenContext& ctx) {
    left->codegen(ctx);
    return right->codegen(ctx);
}

llvm::Value* PostfixIncDecExprAST::codegen(CodegenContext& ctx) {
    llvm::Value* addr = operand->codegen(ctx);
    if (!addr) return nullptr;

    auto& builder = ctx.getBuilder();
    llvm::Type* loadType = operand->type ? ctx.getLLVMType(operand->type) : llvm::Type::getInt32Ty(ctx.getContext());
    llvm::Value* oldVal = builder.CreateLoad(loadType, addr, "postold");
    llvm::Value* one = llvm::ConstantInt::get(loadType->isIntegerTy() ? loadType : llvm::Type::getInt32Ty(ctx.getContext()), 1);
    llvm::Value* newVal = isIncrement ? builder.CreateAdd(oldVal, one, "postinc") : builder.CreateSub(oldVal, one, "postdec");
    builder.CreateStore(newVal, addr);
    return oldVal;
}

llvm::Value* ArrayAccessExprAST::codegen(CodegenContext& ctx) {
    llvm::Value* arrVal = array->codegen(ctx);
    llvm::Value* idxVal = index->codegen(ctx);
    if (!arrVal || !idxVal) return nullptr;

    if (index->isLValue) {
        idxVal = emitLoad(ctx, idxVal, index->type);
    }

    // Determine the element type and, for a pointer operand, load the pointer
    // value (an array operand is already the address of its first element).
    Type* arrType = array->type;
    llvm::Type* elemTy = llvm::Type::getInt32Ty(ctx.getContext());
    if (arrType && arrType->kind == TypeKind::Array) {
        auto* at = static_cast<ArrayType*>(arrType);
        if (at->elementType) elemTy = ctx.getLLVMType(at->elementType);
    } else if (arrType && arrType->kind == TypeKind::Pointer) {
        if (arrType->base) elemTy = ctx.getLLVMType(arrType->base);
        arrVal = emitRValue(ctx, *array, arrVal);
    }

    auto& builder = ctx.getBuilder();
    return builder.CreateGEP(elemTy, arrVal, idxVal, "arrayidx");
}

// Emit a GEP for a class field, following the base-class chain if the member
// is inherited. The LLVM layout of a derived class is { %Base, ownFields... }.
static llvm::Value* emitClassFieldGEP(CodegenContext& ctx, ClassType* classType,
                                      llvm::Value* objPtr, const std::string& memberName) {
    auto& builder = ctx.getBuilder();
    ClassType* cur = classType;
    llvm::Value* curPtr = objPtr;

    while (cur) {
        for (size_t i = 0; i < cur->fields.size(); ++i) {
            if (cur->fields[i].first == memberName) {
                unsigned idx = static_cast<unsigned>(i);
                // Base sub-object occupies field index 0.
                if (!cur->baseClass.empty()) idx += 1;
                llvm::Type* curLLVM = ctx.getLLVMType(cur);
                if (!curLLVM) return nullptr;
                return builder.CreateStructGEP(curLLVM, curPtr, idx, "member");
            }
        }

        if (cur->baseClass.empty()) break;
        Type* baseType = cur->base;
        if (!baseType || baseType->kind != TypeKind::Class) break;
        auto* base = static_cast<ClassType*>(baseType);
        llvm::Type* curLLVM = ctx.getLLVMType(cur);
        if (!curLLVM) break;
        curPtr = builder.CreateStructGEP(curLLVM, curPtr, 0, "base");
        cur = base;
    }

    return nullptr;
}

llvm::Value* MemberAccessExprAST::codegen(CodegenContext& ctx) {
    llvm::Value* objVal = object->codegen(ctx);
    if (!objVal) return nullptr;

    auto& builder = ctx.getBuilder();
    llvm::Type* objType = nullptr;
    unsigned fieldIndex = 0;

    // Union members all live at offset 0: GEP to field 0 gives the address of
    // the requested member regardless of which member it is.
    if (object->type && object->type->kind == TypeKind::Union) {
        llvm::Type* unionLLVM = ctx.getLLVMType(object->type);
        if (!unionLLVM) return nullptr;
        return builder.CreateStructGEP(unionLLVM, objVal, 0, "unionmember");
    }
    if (object->type && object->type->kind == TypeKind::Pointer &&
        object->type->base && object->type->base->kind == TypeKind::Union) {
        if (object->isLValue) {
            objVal = builder.CreateLoad(llvm::PointerType::get(ctx.getContext(), 0), objVal, "deref");
        }
        llvm::Type* unionLLVM = ctx.getLLVMType(object->type->base);
        if (!unionLLVM) return nullptr;
        return builder.CreateStructGEP(unionLLVM, objVal, 0, "unionmember");
    }

    if (object->type && object->type->kind == TypeKind::Struct) {
        auto* structType = static_cast<StructType*>(object->type);
        for (size_t i = 0; i < structType->fields.size(); ++i) {
            if (structType->fields[i].first == memberName) {
                fieldIndex = i;
                break;
            }
        }
        objType = ctx.getLLVMType(object->type);
    } else if (object->type && object->type->kind == TypeKind::Class) {
        auto* classType = static_cast<ClassType*>(object->type);
        if (auto* gep = emitClassFieldGEP(ctx, classType, objVal, memberName)) {
            return gep;
        }
        objType = ctx.getLLVMType(object->type);
    } else if (object->type && object->type->kind == TypeKind::Pointer &&
               object->type->base && object->type->base->kind == TypeKind::Struct) {
        auto* structType = static_cast<StructType*>(object->type->base);
        for (size_t i = 0; i < structType->fields.size(); ++i) {
            if (structType->fields[i].first == memberName) {
                fieldIndex = i;
                break;
            }
        }
        objType = ctx.getLLVMType(object->type->base);
        // Load the pointer unless the object already produced a pointer value
        // (a cast, a call result, `&x`, ...); only lvalue objects are addresses
        // of a variable that stores the pointer (MEM-10).
        if (object->isLValue) {
            objVal = builder.CreateLoad(llvm::PointerType::get(ctx.getContext(), 0), objVal, "deref");
        }
    } else if (object->type && object->type->kind == TypeKind::Pointer &&
               object->type->base && object->type->base->kind == TypeKind::Class) {
        auto* classType = static_cast<ClassType*>(object->type->base);
        // Load the pointer unless the object already produced a pointer value.
        if (object->isLValue) {
            objVal = builder.CreateLoad(llvm::PointerType::get(ctx.getContext(), 0), objVal, "deref");
        }
        if (auto* gep = emitClassFieldGEP(ctx, classType, objVal, memberName)) {
            return gep;
        }
        objType = ctx.getLLVMType(object->type->base);
    } else if (object->type && object->type->kind == TypeKind::Pointer) {
        objType = ctx.getLLVMType(object->type->base);
    }

    if (!objType) {
        objType = objVal->getType();
    }

    return builder.CreateStructGEP(objType, objVal, fieldIndex, "member");
}

llvm::Value* SizeofExprAST::codegen(CodegenContext& ctx) {
    Type* operandType = sizeofType;
    if (!operandType && expr) operandType = expr->type;
    if (!operandType) return nullptr;

    llvm::Type* llvmType = ctx.getLLVMType(operandType);
    if (!llvmType) return nullptr;

    uint64_t size = ctx.getModule().getDataLayout().getTypeStoreSize(llvmType);
    return llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx.getContext()), size);
}

llvm::Value* InitializerListExprAST::codegen(CodegenContext& ctx) {
    if (initializers.empty()) return nullptr;
    return initializers.back()->codegen(ctx);
}

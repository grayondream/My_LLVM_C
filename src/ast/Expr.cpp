#include "Expr.h"
#include "Type.h"
#include "codegen/CodegenContext.h"
#include "support/Log.h"
#include "Mangle.h"

static llvm::Value* emitLoad(CodegenContext& ctx, llvm::Value* ptr, Type* astType = nullptr) {
    if (!ptr) return nullptr;
    if (!ptr->getType()->isPointerTy()) return ptr;
    llvm::Type* loadType = astType ? ctx.getLLVMType(astType) : llvm::Type::getInt32Ty(ctx.getContext());
    return ctx.getBuilder().CreateLoad(loadType, ptr, "loadtmp");
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
        
        lhs = emitLoad(ctx, lhs, left->type);
        rhs = emitLoad(ctx, rhs, right->type);
        
        return ctx.getBuilder().CreateCall(calleeFn, {lhs, rhs}, "opcalltmp");
    }
    
    llvm::Value* lhs = left->codegen(ctx);
    llvm::Value* rhs = right->codegen(ctx);
    if (!lhs || !rhs) return nullptr;

    lhs = emitLoad(ctx, lhs, left->type);
    rhs = emitLoad(ctx, rhs, right->type);

    auto& builder = ctx.getBuilder();

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
        case UnaryOp::Plus:      return emitLoad(ctx, v, operand->type);
        case UnaryOp::Minus:     return builder.CreateNeg(emitLoad(ctx, v, operand->type), "negtmp");
        case UnaryOp::Not:       return builder.CreateNot(emitLoad(ctx, v, operand->type), "nottmp");
        case UnaryOp::BitNot:    return builder.CreateNot(emitLoad(ctx, v, operand->type), "bitnottmp");
        case UnaryOp::Deref: {
            llvm::Type* pointeeType = ctx.getLLVMType(operand->type ? operand->type->base : nullptr);
            if (!pointeeType) pointeeType = llvm::Type::getInt8Ty(ctx.getContext());
            llvm::Value* ptrVal = emitLoad(ctx, v, operand->type);
            return builder.CreateLoad(pointeeType, ptrVal, "dereftmp");
        }
        case UnaryOp::AddressOf: return v;
        default:
            LOGE("invalid unary operator");
            return nullptr;
    }
}

llvm::Value* CallExprAST::codegen(CodegenContext& ctx) {
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
    llvm::Value* result = emitLoad(ctx, rhsVal, rhs->type);

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
        llvm::Type* idxLLVMType = index->type ? ctx.getLLVMType(index->type) : llvm::Type::getInt32Ty(ctx.getContext());
        idxVal = emitLoad(ctx, idxVal, index->type);
    }

    auto& builder = ctx.getBuilder();
    return builder.CreateGEP(llvm::Type::getInt32Ty(ctx.getContext()), arrVal, idxVal, "arrayidx");
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
        // Load the pointer from the alloca before doing GEP
        objVal = builder.CreateLoad(llvm::PointerType::get(ctx.getContext(), 0), objVal, "deref");
    } else if (object->type && object->type->kind == TypeKind::Pointer &&
               object->type->base && object->type->base->kind == TypeKind::Class) {
        auto* classType = static_cast<ClassType*>(object->type->base);
        // Load the pointer from the alloca before doing GEP
        objVal = builder.CreateLoad(llvm::PointerType::get(ctx.getContext(), 0), objVal, "deref");
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
    llvm::Type* llvmType = ctx.getLLVMType(sizeofType);
    if (!llvmType) return nullptr;

    uint64_t size = ctx.getModule().getDataLayout().getTypeStoreSize(llvmType);
    return llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx.getContext()), size);
}

llvm::Value* InitializerListExprAST::codegen(CodegenContext& ctx) {
    if (initializers.empty()) return nullptr;
    return initializers.back()->codegen(ctx);
}

#include "Expr.h"
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

    switch (op) {
        case BinaryOp::Add:    return builder.CreateAdd(lhs, rhs, "addtmp");
        case BinaryOp::Sub:    return builder.CreateSub(lhs, rhs, "subtmp");
        case BinaryOp::Mul:    return builder.CreateMul(lhs, rhs, "multmp");
        case BinaryOp::Div:    return builder.CreateSDiv(lhs, rhs, "divtmp");
        case BinaryOp::Mod:    return builder.CreateSRem(lhs, rhs, "modtmp");
        case BinaryOp::Eq:     return builder.CreateICmpEQ(lhs, rhs, "eqtmp");
        case BinaryOp::NotEq:  return builder.CreateICmpNE(lhs, rhs, "netmp");
        case BinaryOp::Lt:     return builder.CreateICmpSLT(lhs, rhs, "lttmp");
        case BinaryOp::Gt:     return builder.CreateICmpSGT(lhs, rhs, "gttmp");
        case BinaryOp::Le:     return builder.CreateICmpSLE(lhs, rhs, "letmp");
        case BinaryOp::Ge:     return builder.CreateICmpSGE(lhs, rhs, "getmp");
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
        case UnaryOp::Deref: {
            llvm::Type* pointeeType = ctx.getLLVMType(operand->type ? operand->type->base : nullptr);
            if (!pointeeType) pointeeType = llvm::Type::getInt8Ty(ctx.getContext());
            return builder.CreateLoad(pointeeType, v, "dereftmp");
        }
        case UnaryOp::AddressOf: return v;
        default:
            LOGE("invalid unary operator");
            return nullptr;
    }
}

llvm::Value* CallExprAST::codegen(CodegenContext& ctx) {
    // Compute mangled name from argument types
    std::vector<Type*> argTypes;
    for (auto& arg : args) {
        argTypes.push_back(arg->type);
    }
    std::string mangledName = mangleFunction(callee, argTypes);
    
    llvm::Function* calleeFn = ctx.getModule().getFunction(mangledName);
    if (!calleeFn) {
        LOGE("unknown function: {}", callee);
        return nullptr;
    }

    std::vector<llvm::Value*> argsV;
    for (auto& arg : args) {
        llvm::Value* argVal = arg->codegen(ctx);
        if (!argVal) return nullptr;
        argsV.push_back(argVal);
    }

    if (argsV.size() != calleeFn->arg_size()) {
        LOGE("function {} expects {} args, got {}", callee, calleeFn->arg_size(), argsV.size());
        return nullptr;
    }

    return ctx.getBuilder().CreateCall(calleeFn, argsV, "calltmp");
}

llvm::Value* AssignmentExprAST::codegen(CodegenContext& ctx) {
    llvm::Value* lhsVal = lhs->codegen(ctx);
    llvm::Value* rhsVal = rhs->codegen(ctx);
    if (!lhsVal || !rhsVal) return nullptr;

    auto& builder = ctx.getBuilder();
    llvm::Value* result = emitLoad(ctx, rhsVal, rhs->type);

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

    auto& builder = ctx.getBuilder();
    llvm::Type* srcType = val->getType();

    if (srcType == targetLLVMType) return val;

    if (srcType->isIntegerTy() && targetLLVMType->isIntegerTy()) {
        unsigned srcBits = srcType->getIntegerBitWidth();
        unsigned dstBits = targetLLVMType->getIntegerBitWidth();
        if (srcBits < dstBits) return builder.CreateSExt(val, targetLLVMType, "sexttmp");
        if (srcBits > dstBits) return builder.CreateTrunc(val, targetLLVMType, "trunctmp");
        return val;
    }

    if (srcType->isIntegerTy() && targetLLVMType->isFloatingPointTy()) {
        return builder.CreateSIToFP(val, targetLLVMType, "sitofptmp");
    }
    if (srcType->isFloatingPointTy() && targetLLVMType->isIntegerTy()) {
        return builder.CreateFPToSI(val, targetLLVMType, "fptositmp");
    }
    if (srcType->isFloatingPointTy() && targetLLVMType->isFloatingPointTy()) {
        unsigned srcWidth = srcType->getFPMantissaWidth();
        unsigned dstWidth = targetLLVMType->getFPMantissaWidth();
        if (srcWidth < dstWidth) return builder.CreateFPExt(val, targetLLVMType, "fpexttmp");
        if (srcWidth > dstWidth) return builder.CreateFPTrunc(val, targetLLVMType, "fptrunctmp");
        return val;
    }

    return val;
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

    auto& builder = ctx.getBuilder();
    return builder.CreateGEP(llvm::Type::getInt32Ty(ctx.getContext()), arrVal, idxVal, "arrayidx");
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

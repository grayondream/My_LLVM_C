#include "Expr.h"
#include "codegen/CodegenContext.h"
#include "support/Log.h"

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
    llvm::Value* alloca = ctx.lookupVariable(name);
    return alloca;
}

llvm::Value* BinaryExprAST::codegen(CodegenContext& ctx) {
    llvm::Value* lhs = left->codegen(ctx);
    llvm::Value* rhs = right->codegen(ctx);
    if (!lhs || !rhs) return nullptr;

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
        case UnaryOp::Plus:      return v;
        case UnaryOp::Minus:     return builder.CreateNeg(v, "negtmp");
        case UnaryOp::Not:       return builder.CreateNot(v, "nottmp");
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
    llvm::Function* calleeFn = ctx.getModule().getFunction(callee);
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

#include "Codegen.h"
#include "AST.h"
#include "Log.h"

// ==================== CodegenContext ====================

CodegenContext::CodegenContext()
    : builder(context), module("my_llvm_c", context) {
    pushScope();
}

void CodegenContext::pushScope() {
    Scope* parent = scopes.empty() ? nullptr : scopes.back().get();
    scopes.push_back(std::make_unique<Scope>(parent));
}

void CodegenContext::popScope() {
    if (scopes.size() > 1) {
        scopes.pop_back();
    }
}

Scope* CodegenContext::currentScope() {
    return scopes.back().get();
}

llvm::Value* CodegenContext::lookupVariable(const std::string& name) {
    Symbol* sym = currentScope()->lookup(name);
    if (!sym) return nullptr;
    return builder.CreateLoad(getLLVMType(sym->type), sym->type->base ? nullptr : nullptr, name);
    // Note: variable values are stored via alloca; lookup returns the loaded value
}

void CodegenContext::declareVariable(const std::string& name, llvm::Value* alloca, Type* type) {
    currentScope()->symbols[name] = new Symbol(name, type);
}

llvm::Type* CodegenContext::getLLVMType(Type* type) {
    if (!type) return llvm::Type::getVoidTy(context);

    switch (type->kind) {
        case TypeKind::Int:    return llvm::Type::getInt32Ty(context);
        case TypeKind::Float:  return llvm::Type::getFloatTy(context);
        case TypeKind::Double: return llvm::Type::getDoubleTy(context);
        case TypeKind::Char:   return llvm::Type::getInt8Ty(context);
        case TypeKind::Void:   return llvm::Type::getVoidTy(context);
        case TypeKind::Pointer:
            return llvm::PointerType::get(getLLVMType(type->base), 0);
        default:               return llvm::Type::getInt32Ty(context);
    }
}

// ==================== NumberExprAST ====================

llvm::Value* NumberExprAST::codegen(CodegenContext& ctx) {
    return llvm::ConstantInt::get(ctx.getContext(), llvm::APInt(32, value, true));
}

// ==================== FloatExprAST ====================

llvm::Value* FloatExprAST::codegen(CodegenContext& ctx) {
    return llvm::ConstantFP::get(ctx.getContext(), llvm::APFloat(value));
}

// ==================== CharExprAST ====================

llvm::Value* CharExprAST::codegen(CodegenContext& ctx) {
    return llvm::ConstantInt::get(ctx.getContext(), llvm::APInt(8, value));
}

// ==================== StringExprAST ====================

llvm::Value* StringExprAST::codegen(CodegenContext& ctx) {
    return ctx.getBuilder().CreateGlobalString(value, ".str");
}

// ==================== VariableExprAST ====================

llvm::Value* VariableExprAST::codegen(CodegenContext& ctx) {
    Symbol* sym = ctx.currentScope()->lookup(name);
    if (!sym) {
        LOGE("unknown variable: {}", name);
        return nullptr;
    }
    llvm::Value* alloca = ctx.lookupVariable(name);
    return alloca;
}

// ==================== BinaryExprAST ====================

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

// ==================== UnaryExprAST ====================

llvm::Value* UnaryExprAST::codegen(CodegenContext& ctx) {
    llvm::Value* v = operand->codegen(ctx);
    if (!v) return nullptr;

    auto& builder = ctx.getBuilder();

    switch (op) {
        case UnaryOp::Plus:      return v;
        case UnaryOp::Minus:     return builder.CreateNeg(v, "negtmp");
        case UnaryOp::Not:       return builder.CreateNot(v, "nottmp");
        case UnaryOp::Deref: {
            // In LLVM 20+ with opaque pointers, we need the pointee type from the AST
            llvm::Type* pointeeType = ctx.getLLVMType(operand->type ? operand->type->base : nullptr);
            if (!pointeeType) pointeeType = llvm::Type::getInt8Ty(ctx.getContext());
            return builder.CreateLoad(pointeeType, v, "dereftmp");
        }
        case UnaryOp::AddressOf: return v; // v should already be an lvalue alloca
        default:
            LOGE("invalid unary operator");
            return nullptr;
    }
}

// ==================== CallExprAST ====================

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

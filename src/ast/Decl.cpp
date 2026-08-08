#include "Decl.h"
#include "codegen/CodegenContext.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/GlobalVariable.h"
#include "Mangle.h"

static llvm::Constant* foldToConstant(CodegenContext& ctx, const FoldedValue& fv) {
    switch (fv.type) {
        case FoldedValue::INT:
            return llvm::ConstantInt::get(ctx.getLLVMType(new Type(TypeKind::Int)), fv.intVal);
        case FoldedValue::DOUBLE:
            return llvm::ConstantFP::get(ctx.getLLVMType(new Type(TypeKind::Double)), fv.doubleVal);
        case FoldedValue::CHAR:
            return llvm::ConstantInt::get(ctx.getLLVMType(new Type(TypeKind::Char)), fv.charVal);
        default:
            return nullptr;
    }
}

llvm::Value* VarDeclAST::codegen(CodegenContext& ctx) {
    llvm::Type* llvmType = ctx.getLLVMType(type);
    
    if (ctx.isGlobalScope()) {
        llvm::Constant* initConstant = nullptr;
        if (isConstexpr && foldedValue) {
            initConstant = foldToConstant(ctx, *foldedValue);
        } else if (initExpr) {
            initConstant = llvm::dyn_cast_or_null<llvm::Constant>(initExpr->codegen(ctx));
        }
        
        llvm::GlobalVariable::LinkageTypes linkage = type->isConst 
            ? llvm::GlobalVariable::PrivateLinkage 
            : llvm::GlobalVariable::ExternalLinkage;
        llvm::GlobalVariable* global = new llvm::GlobalVariable(
            ctx.getModule(), llvmType, type->isConst, linkage, initConstant, name);
        ctx.declareVariable(name, global, type);
        return global;
    }
    
    llvm::AllocaInst* alloca = ctx.getBuilder().CreateAlloca(llvmType, nullptr, name);
    
    if (isConstexpr && foldedValue) {
        llvm::Value* constVal = foldToConstant(ctx, *foldedValue);
        if (constVal) {
            ctx.getBuilder().CreateStore(constVal, alloca);
        }
    } else if (initExpr) {
        llvm::Value* initVal = initExpr->codegen(ctx);
        if (initVal) {
            ctx.getBuilder().CreateStore(initVal, alloca);
        }
    }
    ctx.declareVariable(name, alloca, type);

    if (!sourceFile.empty()) {
        ctx.emitVariableDebug(alloca, name, type, sourceLine);
    }

    return alloca;
}

llvm::Value* FunctionDeclAST::codegen(CodegenContext& ctx) {
    llvm::Type* retType = ctx.getLLVMType(returnType);
    std::vector<llvm::Type*> paramTypes;
    for (auto& param : params) {
        paramTypes.push_back(ctx.getLLVMType(param->type));
    }

    // Use mangled name for LLVM IR
    std::vector<Type*> astParamTypes;
    for (auto& param : params) {
        astParamTypes.push_back(param->type);
    }
    std::string mangledName = mangleFunction(name, astParamTypes);

    llvm::FunctionType* funcType = llvm::FunctionType::get(retType, paramTypes, false);
    llvm::Function* function = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, mangledName, ctx.getModule());

    if (!sourceFile.empty()) {
        ctx.emitFunctionDebug(function, returnType, name, sourceLine);
    }

    llvm::BasicBlock* bb = llvm::BasicBlock::Create(ctx.getContext(), "entry", function);
    ctx.getBuilder().SetInsertPoint(bb);

    if (!sourceFile.empty()) {
        ctx.setDebugLocation(sourceLine);
    }

    ctx.pushScope();

    unsigned idx = 0;
    for (auto& param : params) {
        llvm::Argument* arg = function->getArg(idx);
        arg->setName(param->name);
        llvm::AllocaInst* alloca = ctx.getBuilder().CreateAlloca(
            ctx.getLLVMType(param->type), nullptr, param->name);
        ctx.getBuilder().CreateStore(arg, alloca);
        ctx.declareVariable(param->name, alloca, param->type);
        idx++;
    }

    if (body) {
        body->codegen(ctx);
    }

    // Add implicit return for void functions if no terminator exists
    if (returnType->kind == TypeKind::Void && !ctx.getBuilder().GetInsertBlock()->getTerminator()) {
        ctx.getBuilder().CreateRetVoid();
    }

    ctx.popScope();

    return function;
}

llvm::Value* DeclStmtAST::codegen(CodegenContext& ctx) {
    if (decl) {
        return decl->codegen(ctx);
    }
    return nullptr;
}

llvm::Value* TranslationUnitAST::codegen(CodegenContext& ctx) {
    llvm::Value* last = nullptr;
    for (auto& decl : declarations) {
        last = decl->codegen(ctx);
    }
    return last;
}

llvm::Value* ArrayDeclAST::codegen(CodegenContext& ctx) {
    llvm::Type* elemLLVMType = ctx.getLLVMType(elementType);
    if (!elemLLVMType) return nullptr;

    llvm::ArrayType* arrType = llvm::ArrayType::get(elemLLVMType, size);
    llvm::AllocaInst* alloca = ctx.getBuilder().CreateAlloca(arrType, nullptr, name);

    if (initExpr) {
        llvm::Value* initVal = initExpr->codegen(ctx);
        if (initVal) {
            ctx.getBuilder().CreateStore(initVal, alloca);
        }
    }

    ctx.declareVariable(name, alloca, elementType);
    return alloca;
}

llvm::Value* StructDeclAST::codegen(CodegenContext& ctx) {
    // Reuse existing struct type if one with this name already exists
    if (auto* existing = llvm::StructType::getTypeByName(ctx.getContext(), name)) {
        // Still need to generate methods if this is a class
        if (!methods.empty()) {
            for (auto& method : methods) {
                method->codegen(ctx);
            }
        }
        return nullptr;
    }

    bool isClass = !methods.empty() || !baseClass.empty();
    std::vector<llvm::Type*> fieldTypes;

    // For classes with inheritance, add base class struct as first field
    if (isClass && !baseClass.empty()) {
        if (auto* baseType = llvm::StructType::getTypeByName(ctx.getContext(), baseClass)) {
            fieldTypes.push_back(baseType);
        }
    }

    for (auto& field : fields) {
        fieldTypes.push_back(ctx.getLLVMType(field.second));
    }

    llvm::StructType* structType = llvm::StructType::create(ctx.getContext(), fieldTypes, name);

    // Generate methods as separate functions
    if (isClass) {
        for (auto& method : methods) {
            method->codegen(ctx);
        }
    }

    return nullptr;
}

llvm::Value* UnionDeclAST::codegen(CodegenContext& ctx) {
    // Reuse existing union type if one with this name already exists
    if (auto* existing = llvm::StructType::getTypeByName(ctx.getContext(), name)) {
        return nullptr;
    }
    std::vector<llvm::Type*> memberTypes;
    for (auto& member : members) {
        memberTypes.push_back(ctx.getLLVMType(member.second));
    }

    llvm::StructType* unionType = llvm::StructType::create(ctx.getContext(), memberTypes, name);
    return nullptr;
}

llvm::Value* EnumDeclAST::codegen(CodegenContext& ctx) {
    return nullptr;
}

llvm::Value* TypedefDeclAST::codegen(CodegenContext& ctx) {
    return nullptr;
}

llvm::Value* ForwardDeclAST::codegen(CodegenContext& ctx) {
    return nullptr;
}

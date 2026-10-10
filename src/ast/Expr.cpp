#include "Expr.h"
#include "Type.h"
#include "Symbol.h"
#include "codegen/CodegenContext.h"
#include "support/Log.h"
#include "Mangle.h"

// P1-06: builtin str/string lowering (defined at file bottom).
static llvm::Value* codegenBuiltinMethod(CodegenContext& ctx, MethodCallExprAST& node);
static llvm::Value* codegenStringMethod(CodegenContext& ctx, MethodCallExprAST& node);
static llvm::Value* codegenStrFromC(CodegenContext& ctx, CallExprAST& node);

// TYP-12: build the {ptr, len} view over a statically-sized array (zero-copy;
// the elements are never duplicated). Array operands already yield the
// address of their first element.
static Type* stripTypedefsT12(Type* t) {
    while (t && t->kind == TypeKind::Typedef)
        t = static_cast<TypedefType*>(t)->aliasedType;
    return t;
}

// TYP-12: strip typedefs, then report whether the value is an array being
// passed where a slice view is expected.
static bool isArrayToSliceArg(Type* argType, Type* paramType) {
    argType = stripTypedefsT12(argType);
    paramType = stripTypedefsT12(paramType);
    return argType && paramType && argType->kind == TypeKind::Array &&
           paramType->kind == TypeKind::Slice;
}

static llvm::Value* emitLoad(CodegenContext& ctx, llvm::Value* ptr, Type* astType = nullptr) {
    return ctx.loadValue(ptr, astType);
}

// P1-04 / CT-12: compile_time 常量节点直接出 llvm::Constant（零运行时指令）。
// 非常量节点返回 nullptr（走普通路径）；ctHandled 但无值（static_assert 等）
// 出被丢弃的 i32 0。必须在任何子表达式出码之前调用——compile_time 根标识符
// 未声明，子树出码会失败。
static llvm::Value* ctConstantOrNull(CodegenContext& ctx, ExprAST& node) {
    if (node.ctInt && node.type) {
        llvm::Type* ty = ctx.getLLVMType(node.type);
        if (auto* intTy = llvm::dyn_cast<llvm::IntegerType>(ty)) {
            return llvm::ConstantInt::get(intTy, static_cast<uint64_t>(*node.ctInt));
        }
    }
    if (node.ctFloat && node.type) {
        return llvm::ConstantFP::get(ctx.getLLVMType(node.type), *node.ctFloat);
    }
    if (node.ctHandled) {
        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx.getContext()), 0);
    }
    return nullptr;
}

// Evaluate an expression node as a value: lvalues are dereferenced, rvalues are
// used as-is. A pointer-typed rvalue (string literal, call result, function
// designator) is already a value and must not be loaded from.
static llvm::Value* emitRValue(CodegenContext& ctx, ExprAST& expr, llvm::Value* v) {
    if (!v) return nullptr;
    return expr.isLValue ? emitLoad(ctx, v, expr.type) : v;
}

// C default argument promotions for variadic calls: float -> double and integer
// types narrower than int -> int (sign- or zero-extended per signedness).
// `enum` uses its explicit underlying type. Other types are returned unchanged.
static llvm::Value* promoteVarArg(CodegenContext& ctx, llvm::Value* v, Type* t) {
    if (!v || !t) return v;
    while (t->kind == TypeKind::Typedef) {
        t = static_cast<TypedefType*>(t)->aliasedType;
    }
    if (!t) return v;
    llvm::LLVMContext& c = ctx.getContext();
    auto& builder = ctx.getBuilder();
    llvm::Type* i32 = llvm::Type::getInt32Ty(c);

    switch (t->kind) {
        case TypeKind::Bool:
            return v->getType()->isIntegerTy(1) ? builder.CreateZExt(v, i32, "promote") : v;
        case TypeKind::Char:
        case TypeKind::Int8:
        case TypeKind::Int16:
            return builder.CreateSExt(v, i32, "promote");
        case TypeKind::UInt8:
        case TypeKind::UInt16:
            return builder.CreateZExt(v, i32, "promote");
        case TypeKind::Enum: {
            auto* et = static_cast<EnumType*>(t);
            return et->underlyingType ? promoteVarArg(ctx, v, et->underlyingType) : v;
        }
        case TypeKind::Float32:
            return ctx.castValue(v, llvm::Type::getDoubleTy(c));
        // TYP-04 / C23 default argument promotions: _Float16 promotes to
        // double in variadic calls; __float128 is passed unchanged (the
        // x86-64 ABI has native quad support).
        case TypeKind::Float16:
            return ctx.castValue(v, llvm::Type::getDoubleTy(c));
        default:
            return v;
    }
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
            // TYP-04: float16 promotes losslessly; float128 truncates to
            // double precision for printing.
            if (ty->isFloatTy() || ty->isHalfTy())
                return builder.CreateFPExt(v, llvm::Type::getDoubleTy(c), "promo");
            if (ty->isFP128Ty())
                return builder.CreateFPTrunc(v, llvm::Type::getDoubleTy(c), "promo");
            return v;
        case PrintArgKind::CString:
        case PrintArgKind::Pointer:
        case PrintArgKind::ToString:
        default:
            return v;
    }
}

// P1-09 (FMT-08): render a placeholder value into a padded, NUL-terminated
// stack buffer for specs printf cannot express (centering or a custom fill
// character). Returns the buffer to pass through printf's "%s". When outLen
// is non-null it receives the padded byte count (max(rendered, width)).
static llvm::Value* emitPaddedValue(CodegenContext& ctx, const PrintSpec& spec,
                                    PrintArgKind kind, llvm::Value* v,
                                    llvm::Value** outLen = nullptr) {
    auto& builder = ctx.getBuilder();
    llvm::LLVMContext& c = ctx.getContext();
    auto* i8Ty = llvm::Type::getInt8Ty(c);
    auto* i32Ty = llvm::Type::getInt32Ty(c);
    auto* i64Ty = llvm::Type::getInt64Ty(c);

    // 1) Render the bare value (no width/fill/align) into a fixed buffer.
    //    Bounds: ints/binary <= 64 bytes; floats <= ~311 digits + precision.
    PrintSpec bare = spec;
    bare.align = 0;
    bare.fill = ' ';
    bare.width = 0;
    llvm::Value* srcPtr = nullptr;
    llvm::Value* len = nullptr;
    if (kind == PrintArgKind::Str) {
        // str/string values carry their own bytes; copy straight from the view.
        srcPtr = builder.CreateExtractValue(v, 0, "pad.ptr");
        len = builder.CreateExtractValue(v, 1, "pad.len");
    } else {
        llvm::Value* promoted = promotePrintArg(ctx, v, kind);
        if (!promoted) return nullptr;
        std::string bareConv, err;
        bool needsRender = false;
        if (!specToPrintfConversion(kind, bare, bareConv, needsRender, err)) {
            bareConv = "%s";
        }
        const int bufSize = 512 + (spec.precision > 0 ? spec.precision : 0);
        llvm::Value* buf1 = builder.CreateAlloca(
            i8Ty, llvm::ConstantInt::get(i32Ty, bufSize), "pad.buf");
        llvm::Value* fmt = builder.CreateGlobalString(bareConv, ".padfmt");
        llvm::FunctionCallee snprintfFn = ctx.getModule().getOrInsertFunction(
            "snprintf",
            llvm::FunctionType::get(
                i32Ty, {llvm::PointerType::get(c, 0), i64Ty,
                        llvm::PointerType::get(c, 0)},
                true));
        llvm::Value* ret = builder.CreateCall(
            snprintfFn.getFunctionType(), snprintfFn.getCallee(),
            {buf1, llvm::ConstantInt::get(i64Ty, bufSize), fmt, promoted});
        len = builder.CreateZExt(ret, i64Ty, "pad.len");
        // Clamp defensively: a negative snprintf return means encoding failure.
        len = builder.CreateSelect(
            builder.CreateICmpSLT(ret, llvm::ConstantInt::get(i32Ty, 0)),
            llvm::ConstantInt::get(i64Ty, 0), len, "pad.len.clamp");
        srcPtr = buf1;
    }

    // 2) Pad into a runtime-sized buffer: memset(fill) + memcpy at offset.
    llvm::Value* widthV =
        llvm::ConstantInt::get(i64Ty, static_cast<uint64_t>(spec.width));
    llvm::Value* total = builder.CreateSelect(
        builder.CreateICmpULT(len, widthV), widthV, len, "pad.total");
    llvm::Value* buf2 = builder.CreateAlloca(
        i8Ty, builder.CreateAdd(total, llvm::ConstantInt::get(i64Ty, 1)),
        "pad.out");
    builder.CreateMemSet(buf2, llvm::ConstantInt::get(i8Ty, spec.fill), total,
                         std::nullopt);
    llvm::Value* off = llvm::ConstantInt::get(i64Ty, 0);
    if (spec.align == '^') {
        off = builder.CreateLShr(builder.CreateSub(total, len),
                                 llvm::ConstantInt::get(i64Ty, 1), "pad.off");
    } else if (spec.align == '>') {
        off = builder.CreateSub(total, len, "pad.off");
    }
    llvm::Value* dst =
        builder.CreateGEP(i8Ty, buf2, off, "pad.dst");
    builder.CreateMemCpy(dst, std::nullopt, srcPtr, std::nullopt, len);
    builder.CreateStore(llvm::ConstantInt::get(i8Ty, 0),
                        builder.CreateGEP(i8Ty, buf2, total, "pad.nul"));
    if (outLen) *outLen = total;
    return buf2;
}

llvm::Value* NumberExprAST::codegen(CodegenContext& ctx) {
    // LEX-15: emit the integer constant at the literal's own width and
    // signedness (from sema), instead of always truncating to i32.
    llvm::Type* ty = type ? ctx.getLLVMType(type)
                          : llvm::Type::getInt32Ty(ctx.getContext());
    unsigned bits = ty->getIntegerBitWidth();

    bool isSigned = true;
    if (type) {
        switch (type->kind) {
            case TypeKind::UInt8:
            case TypeKind::UInt16:
            case TypeKind::UInt32:
            case TypeKind::UInt64:
            case TypeKind::UInt128:
            case TypeKind::USize:
                isSigned = false;
                break;
            default:
                break;
        }
    }
    return llvm::ConstantInt::get(
        ctx.getContext(), llvm::APInt(bits, static_cast<uint64_t>(value), isSigned));
}

llvm::Value* FloatExprAST::codegen(CodegenContext& ctx) {
    // LEX-15: pick the APFloat semantics from the literal's fixed-width type.
    // Build at double precision, then convert to the target semantics.
    llvm::APFloat ap(value);
    if (type) {
        const llvm::fltSemantics* sem = nullptr;
        switch (type->kind) {
            case TypeKind::Float16:  sem = &llvm::APFloat::IEEEhalf();   break;
            case TypeKind::Float32:  sem = &llvm::APFloat::IEEEsingle(); break;
            case TypeKind::Float64:  sem = &llvm::APFloat::IEEEdouble(); break;
            case TypeKind::Float128: sem = &llvm::APFloat::IEEEquad();   break;
            default: break;
        }
        if (sem && sem != &ap.getSemantics()) {
            bool losesInfo = false;
            ap.convert(*sem, llvm::APFloat::rmNearestTiesToEven, &losesInfo);
        }
    }
    return llvm::ConstantFP::get(ctx.getContext(), ap);
}

llvm::Value* CharExprAST::codegen(CodegenContext& ctx) {
    // P1-06: char is signed on x86-64 — a byte literal >= 0x80 must be
    // truncated to 8 bits, not sign-extended into the APInt (assert crash).
    return llvm::ConstantInt::get(ctx.getContext(),
        llvm::APInt(8, static_cast<unsigned char>(value)));
}

llvm::Value* StringExprAST::codegen(CodegenContext& ctx) {
    // P1-06 (FMT-03): literal is a `str` — {ptr, len} of a static global
    // (CreateGlobalString product); non-owning, globally alive (DEC-20).
    llvm::LLVMContext& c = ctx.getContext();
    auto* strTy = llvm::StructType::getTypeByName(c, "str");
    if (!strTy) {
        strTy = llvm::StructType::create(c,
            {llvm::PointerType::get(c, 0), llvm::Type::getInt64Ty(c)}, "str");
    }
    llvm::Value* ptr = ctx.getBuilder().CreateGlobalString(value, ".str");
    return llvm::ConstantStruct::get(strTy,
        {llvm::dyn_cast<llvm::Constant>(ptr),
         llvm::ConstantInt::get(llvm::Type::getInt64Ty(c), value.size())});
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
    if (auto* ct = ctConstantOrNull(ctx, *this)) return ct;
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

    // P1-06 (FMT-01): str == / != compare bytes — length first, then memcmp
    // over the (equal) length; never read past either view.
    {
        Type* lTy = left->type;
        Type* rTy = right->type;
        auto isStrT = [](Type* t) {
            while (t && t->kind == TypeKind::Typedef)
                t = static_cast<TypedefType*>(t)->aliasedType;
            return t && t->kind == TypeKind::Str;
        };
        if ((op == BinaryOp::Eq || op == BinaryOp::NotEq) &&
            isStrT(lTy) && isStrT(rTy) &&
            lhs->getType()->isStructTy() && rhs->getType()->isStructTy()) {
            llvm::LLVMContext& c = ctx.getContext();
            llvm::Value* lenL = builder.CreateExtractValue(lhs, 1, "strlen.l");
            llvm::Value* lenR = builder.CreateExtractValue(rhs, 1, "strlen.r");
            llvm::Value* lenEq = builder.CreateICmpEQ(lenL, lenR, "strlen.eq");
            // Lengths differ → Eq false / Ne true, and memcmp must NOT run
            // (select does not short-circuit: it would read past the shorter
            // view — final review I1). Branch instead.
            llvm::Function* fn = builder.GetInsertBlock()->getParent();
            llvm::BasicBlock* condBB = builder.GetInsertBlock();
            llvm::BasicBlock* cmpBB = llvm::BasicBlock::Create(c, "str.cmp", fn);
            llvm::BasicBlock* doneBB = llvm::BasicBlock::Create(c, "str.done", fn);
            builder.CreateCondBr(lenEq, cmpBB, doneBB);

            builder.SetInsertPoint(cmpBB);
            llvm::Value* ptrL = builder.CreateExtractValue(lhs, 0, "strptr.l");
            llvm::Value* ptrR = builder.CreateExtractValue(rhs, 0, "strptr.r");
            llvm::FunctionType* memcmpTy = llvm::FunctionType::get(
                llvm::Type::getInt32Ty(c),
                {llvm::PointerType::get(c, 0), llvm::PointerType::get(c, 0),
                 llvm::Type::getInt64Ty(c)},
                false);
            llvm::FunctionCallee memcmpFn =
                ctx.getModule().getOrInsertFunction("memcmp", memcmpTy);
            llvm::Value* bytesEq = builder.CreateICmpEQ(
                builder.CreateCall(memcmpFn, {ptrL, ptrR, lenL}, "memcmp"),
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(c), 0), "bytes.eq");
            builder.CreateBr(doneBB);

            builder.SetInsertPoint(doneBB);
            auto* eq = builder.CreatePHI(llvm::Type::getInt1Ty(c), 2, "str.eq");
            eq->addIncoming(llvm::ConstantInt::getFalse(c), condBB);
            eq->addIncoming(bytesEq, cmpBB);
            return op == BinaryOp::Eq ? static_cast<llvm::Value*>(eq)
                                      : builder.CreateNot(eq, "str.ne");
        }
    }

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

    // Coerce both operands to a common arithmetic type (TYP-22). The AST types
    // drive integer promotion, the usual arithmetic conversions and signedness;
    // the opcode selection (sdiv/udiv, icmp slt/ult, ashr/lshr) follows suit.
    Type* leftAst = left->type;
    Type* rightAst = right->type;
    const bool isShift = (op == BinaryOp::LShift || op == BinaryOp::RShift);
    Type* resultAstTy = nullptr;
    if (leftAst && rightAst && isArithmeticType(leftAst) && isArithmeticType(rightAst)) {
        // Shifts yield the promoted left operand; other operators the usual
        // arithmetic common type.
        resultAstTy = isShift ? promoteArithmeticType(leftAst)
                              : usualArithmeticType(leftAst, rightAst);
    }

    llvm::Type* lhsTy = lhs->getType();
    llvm::Type* rhsTy = rhs->getType();
    llvm::Type* opType = lhsTy;
    if (resultAstTy) {
        opType = ctx.getLLVMType(resultAstTy);
    } else if (lhsTy->isFloatingPointTy() || rhsTy->isFloatingPointTy()) {
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
    lhs = ctx.castValue(lhs, leftAst, opType);
    rhs = ctx.castValue(rhs, rightAst, opType);
    bool isFloat = opType->isFloatingPointTy();
    // Address comparisons and unsigned common types use unsigned predicates.
    bool unsignedOp = (leftPtr || rightPtr) ||
                      (resultAstTy && isUnsignedArithmeticType(resultAstTy));

    switch (op) {
        case BinaryOp::Add:    return isFloat ? builder.CreateFAdd(lhs, rhs, "addtmp")
                                              : builder.CreateAdd(lhs, rhs, "addtmp");
        case BinaryOp::Sub:    return isFloat ? builder.CreateFSub(lhs, rhs, "subtmp")
                                              : builder.CreateSub(lhs, rhs, "subtmp");
        case BinaryOp::Mul:    return isFloat ? builder.CreateFMul(lhs, rhs, "multmp")
                                              : builder.CreateMul(lhs, rhs, "multmp");
        case BinaryOp::Div:    return isFloat ? builder.CreateFDiv(lhs, rhs, "divtmp")
                                              : (unsignedOp ? builder.CreateUDiv(lhs, rhs, "divtmp")
                                                            : builder.CreateSDiv(lhs, rhs, "divtmp"));
        case BinaryOp::Mod:    return isFloat ? builder.CreateFRem(lhs, rhs, "modtmp")
                                              : (unsignedOp ? builder.CreateURem(lhs, rhs, "modtmp")
                                                            : builder.CreateSRem(lhs, rhs, "modtmp"));
        case BinaryOp::Eq:     return isFloat ? builder.CreateFCmpOEQ(lhs, rhs, "eqtmp")
                                              : builder.CreateICmpEQ(lhs, rhs, "eqtmp");
        case BinaryOp::NotEq:  return isFloat ? builder.CreateFCmpONE(lhs, rhs, "netmp")
                                              : builder.CreateICmpNE(lhs, rhs, "netmp");
        case BinaryOp::Lt:     return isFloat ? builder.CreateFCmpOLT(lhs, rhs, "lttmp")
                                              : (unsignedOp ? builder.CreateICmpULT(lhs, rhs, "lttmp")
                                                            : builder.CreateICmpSLT(lhs, rhs, "lttmp"));
        case BinaryOp::Gt:     return isFloat ? builder.CreateFCmpOGT(lhs, rhs, "gttmp")
                                              : (unsignedOp ? builder.CreateICmpUGT(lhs, rhs, "gttmp")
                                                            : builder.CreateICmpSGT(lhs, rhs, "gttmp"));
        case BinaryOp::Le:     return isFloat ? builder.CreateFCmpOLE(lhs, rhs, "letmp")
                                              : (unsignedOp ? builder.CreateICmpULE(lhs, rhs, "letmp")
                                                            : builder.CreateICmpSLE(lhs, rhs, "letmp"));
        case BinaryOp::Ge:     return isFloat ? builder.CreateFCmpOGE(lhs, rhs, "getmp")
                                              : (unsignedOp ? builder.CreateICmpUGE(lhs, rhs, "getmp")
                                                            : builder.CreateICmpSGE(lhs, rhs, "getmp"));
        case BinaryOp::And:    return builder.CreateAnd(lhs, rhs, "andtmp");
        case BinaryOp::Or:     return builder.CreateOr(lhs, rhs, "ortmp");
        case BinaryOp::BitAnd: return builder.CreateAnd(lhs, rhs, "bitandtmp");
        case BinaryOp::BitOr:  return builder.CreateOr(lhs, rhs, "bitortmp");
        case BinaryOp::BitXor: return builder.CreateXor(lhs, rhs, "bitxortmp");
        case BinaryOp::LShift: return builder.CreateShl(lhs, rhs, "lshifttmp");
        case BinaryOp::RShift: return unsignedOp ? builder.CreateLShr(lhs, rhs, "rshifttmp")
                                                 : builder.CreateAShr(lhs, rhs, "rshifttmp");
        default:
            LOGE("invalid binary operator");
            return nullptr;
    }
}

llvm::Value* UnaryExprAST::codegen(CodegenContext& ctx) {
    if (auto* ct = ctConstantOrNull(ctx, *this)) return ct;
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
    // P1-06 (FMT-04): builtin str_from_c — strlen + {ptr, len} assembly.
    if (isStrFromC) {
        return codegenStrFromC(ctx, *this);
    }
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
        // P1-06 (FMT-03): a str message is viewed as its char* bytes.
        if (args[0]->type && args[0]->type->kind == TypeKind::Str) {
            userMsg = ctx.castValue(userMsg, args[0]->type,
                                    llvm::PointerType::get(c, 0));
        } else if (!userMsg->getType()->isPointerTy()) {
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
                // P1-09 (FMT-08): specs printf cannot express (centering or a
                // custom fill) pre-render the value into a padded buffer.
                const bool renderSlot =
                    k - 1 < printSpecs.size() &&
                    (printSpecs[k - 1].align == '^' ||
                     (printSpecs[k - 1].fill != ' ' && printSpecs[k - 1].fill != '\0'));
                if (renderSlot) {
                    llvm::Value* padded = emitPaddedValue(
                        ctx, printSpecs[k - 1], printArgKinds[k - 1], v);
                    if (!padded) return nullptr;
                    callArgs.push_back(padded);
                    continue;
                }
                // P1-06 (FMT-03): str prints via `%.*s` — TWO printf
                // arguments ((int)len, ptr); push both and skip the loop's
                // single-value promote.
                if (printArgKinds[k - 1] == PrintArgKind::Str) {
                    llvm::Value* len = builder.CreateExtractValue(v, 1, "printfmt.len");
                    llvm::Value* ptr = builder.CreateExtractValue(v, 0, "printfmt.ptr");
                    callArgs.push_back(builder.CreateTrunc(
                        len, llvm::Type::getInt32Ty(c), "printfmt.len32"));
                    callArgs.push_back(ptr);
                    continue;
                }
                v = promotePrintArg(ctx, v, printArgKinds[k - 1]);
            }
            if (!v) return nullptr;
            callArgs.push_back(v);
        }
        return builder.CreateCall(printfTy, printfFn.getCallee(), callArgs);
    }

    // P1-09 (FMT-07/12): builtin `format(fmt, args...) -> string`. Literal
    // formats assemble straight-line (global segments + snprintf/padded slot
    // buffers, one malloc, no realloc); dynamic formats render each argument
    // with its default conversion into chunks and let smc.format.dyn scan.
    if (isFormat) {
        llvm::LLVMContext& c = ctx.getContext();
        auto& builder = ctx.getBuilder();
        auto* i8Ty = llvm::Type::getInt8Ty(c);
        auto* i32Ty = llvm::Type::getInt32Ty(c);
        auto* i64Ty = llvm::Type::getInt64Ty(c);
        auto* ptrTy = llvm::PointerType::get(c, 0);
        auto* strLLVM = ctx.getLLVMType(TypeContext::instance().getStringType());

        auto mallocFn = ctx.getModule().getOrInsertFunction(
            "malloc", llvm::FunctionType::get(ptrTy, {i64Ty}, false));
        auto memcpyFn = ctx.getModule().getOrInsertFunction(
            "memcpy",
            llvm::FunctionType::get(llvm::Type::getVoidTy(c),
                                    {ptrTy, ptrTy, i64Ty}, false));
        auto snprintfFn = ctx.getModule().getOrInsertFunction(
            "snprintf",
            llvm::FunctionType::get(i32Ty, {ptrTy, i64Ty, ptrTy}, true));

        // Render one scalar with `conv` into a stack buffer -> {buf, len}.
        auto renderScalar = [&](llvm::Value* v, PrintArgKind kind,
                                const std::string& conv,
                                int bufSize) -> std::pair<llvm::Value*, llvm::Value*> {
            llvm::Value* promoted = promotePrintArg(ctx, v, kind);
            llvm::Value* buf = builder.CreateAlloca(
                i8Ty, llvm::ConstantInt::get(i32Ty, bufSize), "fmt.buf");
            llvm::Value* cv = builder.CreateGlobalString(conv, ".fmt.cv");
            llvm::Value* ret = builder.CreateCall(
                snprintfFn.getFunctionType(), snprintfFn.getCallee(),
                {buf, llvm::ConstantInt::get(i64Ty, bufSize), cv, promoted});
            return {buf, builder.CreateSExt(ret, i64Ty, "fmt.len")};
        };
        auto defaultConvText = [](PrintArgKind kind) {
            switch (kind) {
                case PrintArgKind::Int32:   return "d";
                case PrintArgKind::Int64:   return "lld";
                case PrintArgKind::UInt32:  return "u";
                case PrintArgKind::UInt64:  return "llu";
                case PrintArgKind::Char:    return "c";
                case PrintArgKind::Float:   return "g";
                case PrintArgKind::Pointer: return "p";
                default:                    return "s";
            }
        };

        if (!formatLiteral.empty()) {
            std::vector<std::pair<llvm::Value*, llvm::Value*>> chunks;
            std::string lit;
            size_t argK = 1; // args[0] is the format string itself
            size_t slotIdx = 0;
            auto flushLit = [&]() {
                if (lit.empty()) return;
                llvm::Value* g = builder.CreateGlobalString(lit, ".fmt.seg");
                chunks.emplace_back(g, llvm::ConstantInt::get(i64Ty, lit.size()));
                lit.clear();
            };
            for (size_t i = 0; i < formatLiteral.size();) {
                const char ch = formatLiteral[i];
                if (ch == '{') {
                    if (i + 1 < formatLiteral.size() && formatLiteral[i + 1] == '{') {
                        lit += '{';
                        i += 2;
                        continue;
                    }
                    ++i; // consume '{'
                    if (i < formatLiteral.size() && formatLiteral[i] == ':') {
                        ++i;
                        while (i < formatLiteral.size() && formatLiteral[i] != '}') ++i;
                    }
                    while (i < formatLiteral.size() && formatLiteral[i] != '}') ++i;
                    ++i; // consume '}'
                    flushLit();
                    if (argK >= args.size() || slotIdx >= printArgKinds.size()) break;
                    llvm::Value* v = args[argK]->codegen(ctx);
                    if (!v) return nullptr;
                    if (args[argK]->isLValue) v = ctx.loadValue(v, args[argK]->type);
                    const PrintArgKind kind = printArgKinds[slotIdx];
                    const PrintSpec& spec = slotIdx < formatSpecs.size()
                                                ? formatSpecs[slotIdx]
                                                : PrintSpec{};
                    const bool renderSlot =
                        spec.align == '^' ||
                        (spec.fill != ' ' && spec.fill != '\0');
                    if (renderSlot) {
                        llvm::Value* len = nullptr;
                        llvm::Value* padded =
                            emitPaddedValue(ctx, spec, kind, v, &len);
                        if (!padded) return nullptr;
                        chunks.emplace_back(padded, len);
                    } else if (kind == PrintArgKind::Str) {
                        // Zero-copy byte fidelity (NUL-safe): the chunk IS
                        // the argument's bytes.
                        chunks.emplace_back(
                            builder.CreateExtractValue(v, 0, "fmt.sptr"),
                            builder.CreateExtractValue(v, 1, "fmt.slen"));
                    } else {
                        std::string conv, err;
                        bool needsRender = false;
                        if (!specToPrintfConversion(kind, spec, conv,
                                                    needsRender, err)) {
                            conv = "%s";
                        }
                        const int bufSize = 344 + spec.width +
                                            (spec.precision > 0 ? spec.precision : 0);
                        auto rendered = renderScalar(v, kind, conv, bufSize);
                        chunks.emplace_back(rendered.first, rendered.second);
                    }
                    ++argK;
                    ++slotIdx;
                } else if (ch == '}') {
                    if (i + 1 < formatLiteral.size() && formatLiteral[i + 1] == '}') {
                        lit += '}';
                        i += 2;
                        continue;
                    }
                    lit += '}';
                    ++i;
                } else {
                    lit += ch;
                    ++i;
                }
            }
            flushLit();

            llvm::Value* total = llvm::ConstantInt::get(i64Ty, 0);
            for (auto& chunk : chunks)
                total = builder.CreateAdd(total, chunk.second, "fmt.t");
            llvm::Value* cap = builder.CreateSelect(
                builder.CreateICmpUGT(total, llvm::ConstantInt::get(i64Ty, 64)),
                total, llvm::ConstantInt::get(i64Ty, 64), "fmt.cap");
            llvm::Value* start = builder.CreateCall(mallocFn, {cap}, "fmt.ptr");
            // NOTE: never chain on memcpy's return — LLVM lowers calls to the
            // memcpy libfunc to an intrinsic DAG node whose return value is
            // not guaranteed. Advance the destination by explicit offsets.
            llvm::Value* off = llvm::ConstantInt::get(i64Ty, 0);
            for (auto& chunk : chunks) {
                llvm::Value* dst =
                    builder.CreateGEP(i8Ty, start, off, "fmt.dst");
                builder.CreateCall(memcpyFn, {dst, chunk.first, chunk.second});
                off = builder.CreateAdd(off, chunk.second, "fmt.off");
            }
            llvm::Value* out = llvm::Constant::getNullValue(strLLVM);
            out = builder.CreateInsertValue(out, start, {0});
            out = builder.CreateInsertValue(out, total, {1});
            return builder.CreateInsertValue(out, cap, {2});
        }

        // Dynamic format string: pre-render every argument with its default
        // conversion, then let smc.format.dyn interleave (FMT-09 contract).
        llvm::Value* fmtV = args[0]->codegen(ctx);
        if (!fmtV) return nullptr;
        if (args[0]->isLValue) fmtV = ctx.loadValue(fmtV, args[0]->type);
        llvm::Value* fptr = builder.CreateExtractValue(fmtV, 0, "fmt.fptr");
        llvm::Value* flen = builder.CreateExtractValue(fmtV, 1, "fmt.flen");

        const size_t n = args.size() - 1;
        auto* arrLen = llvm::ConstantInt::get(i32Ty, n > 0 ? n : 1);
        auto* ptrArr = builder.CreateAlloca(ptrTy, arrLen, "fmt.ptrs");
        auto* lenArr = builder.CreateAlloca(i64Ty, arrLen, "fmt.lens");
        for (size_t k = 1; k < args.size(); ++k) {
            const PrintArgKind kind = k - 1 < printArgKinds.size()
                                          ? printArgKinds[k - 1]
                                          : PrintArgKind::Int32;
            llvm::Value* v = args[k]->codegen(ctx);
            if (!v) return nullptr;
            if (args[k]->isLValue) v = ctx.loadValue(v, args[k]->type);
            llvm::Value* cptr;
            llvm::Value* clen;
            if (kind == PrintArgKind::Str) {
                cptr = builder.CreateExtractValue(v, 0, "fmt.cptr");
                clen = builder.CreateExtractValue(v, 1, "fmt.clen");
            } else {
                auto rendered = renderScalar(
                    v, kind, std::string("%") + defaultConvText(kind), 64);
                cptr = rendered.first;
                clen = rendered.second;
            }
            builder.CreateStore(cptr, builder.CreateGEP(ptrTy, ptrArr,
                llvm::ConstantInt::get(i64Ty, k - 1), "fmt.pp"));
            builder.CreateStore(clen, builder.CreateGEP(i64Ty, lenArr,
                llvm::ConstantInt::get(i64Ty, k - 1), "fmt.lp"));
        }
        auto savedIP = builder.saveIP();
        llvm::Function* dynFn = ctx.getFormatDynFn();
        builder.restoreIP(savedIP);
        llvm::Value* res = builder.CreateCall(
            dynFn, {fptr, flen, ptrArr, lenArr,
                    llvm::ConstantInt::get(i64Ty, n)});
        llvm::Value* out = llvm::Constant::getNullValue(strLLVM);
        out = builder.CreateInsertValue(
            out, builder.CreateExtractValue(res, 0, "fmt.rptr"), {0});
        out = builder.CreateInsertValue(
            out, builder.CreateExtractValue(res, 1, "fmt.rlen"), {1});
        return builder.CreateInsertValue(
            out, builder.CreateExtractValue(res, 2, "fmt.rcap"), {2});
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
                argVal = ctx.castValue(argVal, args[i]->type,
                                       ctx.getLLVMType(resolvedParamTypes[i]));
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
        // P1-03 / GEN-05: 惰性前向声明——模板实例函数等定义可能在本调用点
        // 之后出码；按调用点已知签名建声明，定义侧复用同名 llvm::Function。
        llvm::Type* retLLVM = type ? ctx.getLLVMType(type) : nullptr;
        if (!retLLVM) {
            LOGE("unknown function: {}", callee);
            return nullptr;
        }
        std::vector<llvm::Type*> paramLLVM;
        for (auto* pt : argTypes) paramLLVM.push_back(ctx.getLLVMType(pt));
        auto* fnType = llvm::FunctionType::get(retLLVM, paramLLVM, false);
        calleeFn = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage,
                                          mangledName, ctx.getModule());
    }

    std::vector<llvm::Value*> argsV;
    for (size_t i = 0; i < args.size(); ++i) {
        llvm::Value* argVal = args[i]->codegen(ctx);
        if (!argVal) return nullptr;
        if (args[i]->isLValue) {
            argVal = emitLoad(ctx, argVal, args[i]->type);
        }
        if (i < resolvedParamTypes.size()) {
            if (isArrayToSliceArg(args[i]->type, resolvedParamTypes[i])) {
                // TYP-12: array -> slice view; no element copy.
                argVal = ctx.emitArrayToSliceDecay(
                    static_cast<ArrayType*>(stripTypedefsT12(args[i]->type)),
                    argVal);
            } else {
                argVal = ctx.castValue(argVal, args[i]->type,
                                       ctx.getLLVMType(resolvedParamTypes[i]));
            }
        } else if (calleeFn->isVarArg()) {
            // C default argument promotions for the variadic tail.
            argVal = promoteVarArg(ctx, argVal, args[i]->type);
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
    if (auto* ct = ctConstantOrNull(ctx, *this)) return ct;
    // P1-06: builtin str/string methods lower through their own path.
    if (builtinMethod != BuiltinMethod::None) {
        return codegenBuiltinMethod(ctx, *this);
    }
    llvm::Value* objVal = object->codegen(ctx);
    if (!objVal) return nullptr;

    // P1-03 / PAR-17: 对象是指针类型的左值变量（`D* other` 作 `other->area()`
    // 的对象）——先解引用取出对象指针；类实例变量（alloca 即对象地址）不变。
    {
        Type* objT = object->type;
        while (objT && objT->kind == TypeKind::Typedef)
            objT = static_cast<TypedefType*>(objT)->aliasedType;
        if (objT && objT->kind == TypeKind::Pointer && object->isLValue) {
            auto& builder = ctx.getBuilder();
            objVal = builder.CreateLoad(llvm::PointerType::get(ctx.getContext(), 0), objVal,
                                        "objderef");
        }
    }

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

    // Prefer the parameter types chosen by semantic analysis (incl. this) so
    // decayed arguments mangle against the definition site. Mirror of
    // CallExprAST::codegen.
    std::vector<Type*> argTypes;
    if (!resolvedParamTypes.empty()) {
        argTypes = resolvedParamTypes;
    } else {
        Type* thisType = new Type(TypeKind::Pointer, objType);
        argTypes.push_back(thisType);
        for (auto& arg : args) {
            argTypes.push_back(arg->type);
        }
    }

    std::string mangledName = mangleFunction(methodName, argTypes);

    llvm::Function* calleeFn = ctx.getModule().getFunction(mangledName);
    if (!calleeFn) {
        // P1-03 / INH-05: 惰性前向声明（基类实例方法体在派生类方法之后出码
        // 等场景）；定义侧复用同名 llvm::Function。
        if (resolvedParamTypes.empty() || !type) {
            LOGE("unknown method: {}", mangledName);
            return nullptr;
        }
        llvm::Type* retLLVM = ctx.getLLVMType(type);
        std::vector<llvm::Type*> paramLLVM;
        for (auto* pt : argTypes) paramLLVM.push_back(ctx.getLLVMType(pt));
        auto* fnType = llvm::FunctionType::get(retLLVM, paramLLVM, false);
        calleeFn = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage,
                                          mangledName, ctx.getModule());
    }

    std::vector<llvm::Value*> argsV;
    // Pass object pointer as the this argument
    argsV.push_back(objVal);
    for (size_t i = 0; i < args.size(); ++i) {
        llvm::Value* argVal = args[i]->codegen(ctx);
        if (!argVal) return nullptr;
        if (args[i]->isLValue) {
            argVal = emitLoad(ctx, argVal, args[i]->type);
        }
        // resolvedParamTypes[0] is this — explicit argument i maps to [i+1].
        if (i + 1 < resolvedParamTypes.size()) {
            if (isArrayToSliceArg(args[i]->type, resolvedParamTypes[i + 1])) {
                // TYP-12: array -> slice view; no element copy.
                argVal = ctx.emitArrayToSliceDecay(
                    static_cast<ArrayType*>(stripTypedefsT12(args[i]->type)),
                    argVal);
            } else {
                argVal = ctx.castValue(argVal, args[i]->type,
                                       ctx.getLLVMType(resolvedParamTypes[i + 1]));
            }
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

    // TYP-12: array RHS decays to a slice view when assigned to a slice LHS.
    {
        Type* lt = stripTypedefsT12(lhs->type);
        Type* rt = stripTypedefsT12(rhs->type);
        if (lt && rt && lt->kind == TypeKind::Slice && rt->kind == TypeKind::Array &&
            op == AssignOp::Assign) {
            result = ctx.emitArrayToSliceDecay(static_cast<ArrayType*>(rt), result);
        } else if (lhs->type) {
            result = ctx.castValue(result, rhs->type, ctx.getLLVMType(lhs->type));
        }
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
    if (auto* ct = ctConstantOrNull(ctx, *this)) return ct;
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

    // 根因修复：lvalue 分支 codegen 产生的是地址——直接喂 phi 会把地址当
    // 值（标量被截断、指针存变量地址、聚合为垃圾）。分支须先取值（数组
    // 除外：数组 lvalue 不按值装入，与条件路径同一处理）。取值/转换在分支
    // 块内完成——merge 块顶只允许 phi。
    Type* common = type;
    llvm::Type* commonLLVM = common ? ctx.getLLVMType(common) : nullptr;
    auto finishBranch = [&ctx, commonLLVM](ExprAST& branch, llvm::Value* val) -> llvm::Value* {
        if (branch.isLValue && branch.type && branch.type->kind != TypeKind::Array) {
            val = ctx.loadValue(val, branch.type);
        }
        if (val && branch.type) {
            if (llvm::Type* dest = commonLLVM) {
                if (val->getType() != dest) val = ctx.castValue(val, branch.type, dest);
            }
        }
        return val;
    };

    builder.SetInsertPoint(thenBB);
    llvm::Value* thenVal = then->codegen(ctx);
    if (!thenVal) return nullptr;
    thenVal = finishBranch(*then, thenVal);
    if (!thenVal) return nullptr;
    // 嵌套三元：内层表达式会切换插入点，phi 的前驱必须记实际终结块。
    llvm::BasicBlock* thenPred = builder.GetInsertBlock();
    builder.CreateBr(mergeBB);

    builder.SetInsertPoint(elseBB);
    llvm::Value* elseVal = elseExpr->codegen(ctx);
    if (!elseVal) return nullptr;
    elseVal = finishBranch(*elseExpr, elseVal);
    if (!elseVal) return nullptr;
    llvm::BasicBlock* elsePred = builder.GetInsertBlock();
    builder.CreateBr(mergeBB);

    builder.SetInsertPoint(mergeBB);
    llvm::PHINode* phi = builder.CreatePHI(thenVal->getType(), 2, "ternarytmp");
    phi->addIncoming(thenVal, thenPred);
    phi->addIncoming(elseVal, elsePred);
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
    } else if (arrType && arrType->kind == TypeKind::Slice) {
        // TYP-12: load the {ptr, len} view, then index through its pointer.
        auto* st = static_cast<SliceType*>(arrType);
        auto* sliceTy = ctx.getLLVMType(arrType);
        auto& builder = ctx.getBuilder();
        if (array->isLValue) arrVal = builder.CreateLoad(sliceTy, arrVal);
        if (st->elementType) elemTy = ctx.getLLVMType(st->elementType);
        arrVal = builder.CreateExtractValue(arrVal, {0}, "slice.ptr");
    } else if (arrType && arrType->kind == TypeKind::String) {
        // P1-06 (FMT-02): load the {ptr, len, cap} header, index its buffer.
        auto* strTy = ctx.getLLVMType(arrType);
        auto& builder = ctx.getBuilder();
        if (array->isLValue) arrVal = builder.CreateLoad(strTy, arrVal);
        elemTy = llvm::Type::getInt8Ty(ctx.getContext());
        arrVal = builder.CreateExtractValue(arrVal, {0}, "string.ptr");
    }

    auto& builder = ctx.getBuilder();
    return builder.CreateGEP(elemTy, arrVal, idxVal, "arrayidx");
}

// Emit a GEP for a class field, following the base-class chain if the member
// is inherited. The LLVM layout of a derived class is { %Base, ownFields... }.
static llvm::Value* emitClassFieldGEP(CodegenContext& ctx, Type* aggType,
                                      llvm::Value* objPtr, const std::string& memberName) {
    auto& builder = ctx.getBuilder();
    // INH-01: unified walk over ClassType and StructType inheritance chains —
    // the base sub-object occupies field slot 0 of every derived layout.
    // Depth-capped: a redefinition-shaped cycle must never hang the compiler
    // (评审 C1).
    Type* cur = aggType;
    llvm::Value* curPtr = objPtr;
    int walkDepth = 0;

    while (cur) {
        std::vector<FieldInfo>* fields = nullptr;
        std::string baseClassName;
        Type* baseType = nullptr;
        if (cur->kind == TypeKind::Class) {
            auto* classType = static_cast<ClassType*>(cur);
            fields = &classType->fields;
            baseClassName = classType->baseClass;
            baseType = classType->base;
        } else if (cur->kind == TypeKind::Struct) {
            auto* structType = static_cast<StructType*>(cur);
            fields = &structType->fields;
            baseClassName = structType->baseClass;
            baseType = structType->base;
        } else {
            break;
        }

        for (size_t i = 0; i < fields->size(); ++i) {
            if ((*fields)[i].name == memberName) {
                unsigned idx = static_cast<unsigned>(i);
                // Base sub-object occupies field index 0.
                if (!baseClassName.empty()) idx += 1;
                llvm::Type* curLLVM = ctx.getLLVMType(cur);
                if (!curLLVM) return nullptr;
                return builder.CreateStructGEP(curLLVM, curPtr, idx, "member");
            }
        }

        if (baseClassName.empty()) break;
        if (!baseType || (baseType->kind != TypeKind::Class &&
                          baseType->kind != TypeKind::Struct)) {
            break;
        }
        llvm::Type* curLLVM = ctx.getLLVMType(cur);
        if (!curLLVM) break;
        curPtr = builder.CreateStructGEP(curLLVM, curPtr, 0, "base");
        cur = baseType;
        if (++walkDepth > 64) break;
    }

    return nullptr;
}

llvm::Value* MemberAccessExprAST::codegen(CodegenContext& ctx) {
    if (auto* ct = ctConstantOrNull(ctx, *this)) return ct;
    // TYP-12: slice `.len` extracts the length field of the {ptr, len} view.
    {
        Type* st = object->type;
        while (st && st->kind == TypeKind::Typedef)
            st = static_cast<TypedefType*>(st)->aliasedType;
        if (st && st->kind == TypeKind::Slice && memberName == "len") {
            llvm::Value* sliceVal = object->codegen(ctx);
            if (!sliceVal) return nullptr;
            auto& builder = ctx.getBuilder();
            if (object->isLValue)
                sliceVal = builder.CreateLoad(ctx.getLLVMType(st), sliceVal);
            return builder.CreateExtractValue(sliceVal, {1}, "slice.len");
        }
    }

    // P1-02 (TYP-13/14): Optional/Result pseudo-fields. DS4 layout: flag
    // (i1) at index 0, value at 1, error at 2 (Result only). Lvalue access
    // yields the field address (writable); rvalue access extracts the field.
    {
        Type* st = object->type;
        while (st && st->kind == TypeKind::Typedef)
            st = static_cast<TypedefType*>(st)->aliasedType;
        if (st && (st->kind == TypeKind::Optional || st->kind == TypeKind::Result)) {
            long idx = -1;
            if (memberName == "valid" || memberName == "ok") idx = 0;
            else if (memberName == "value") idx = 1;
            else if (st->kind == TypeKind::Result && memberName == "error") idx = 2;
            if (idx < 0) return nullptr; // sema already diagnosed
            auto& builder = ctx.getBuilder();
            llvm::Value* agg = object->codegen(ctx);
            if (!agg) return nullptr;
            if (object->isLValue) {
                return builder.CreateStructGEP(ctx.getLLVMType(st), agg,
                                               (unsigned)idx, "optresmember");
            }
            // Rvalue object (call result etc.): the aggregate is already a
            // value — no load here (评审 I3: a spurious load on typedef'd
            // returns crashed the backend with "Do not know how to promote").
            return builder.CreateExtractValue(agg, {(unsigned)idx}, "optresmember");
        }
    }

    llvm::Value* objVal = object->codegen(ctx);
    if (!objVal) return nullptr;

    auto& builder = ctx.getBuilder();
    llvm::Type* objType = nullptr;
    unsigned fieldIndex = 0;

    // Typedefs to aggregates resolve to the underlying type for member access
    // (AGG-17): `typedef union U2 U2; U2 v; v.i` must behave like `U2 v`.
    auto stripTypedefsOf = [](Type* t) -> Type* {
        while (t && t->kind == TypeKind::Typedef) {
            t = static_cast<TypedefType*>(t)->aliasedType;
        }
        return t;
    };
    Type* valType = stripTypedefsOf(object->type);
    Type* valBase =
        (valType && valType->kind == TypeKind::Pointer) ? stripTypedefsOf(valType->base) : nullptr;

    // Union members all live at offset 0: GEP to field 0 gives the address of
    // the requested member regardless of which member it is.
    if (valType && valType->kind == TypeKind::Union) {
        llvm::Type* unionLLVM = ctx.getLLVMType(valType);
        if (!unionLLVM) return nullptr;
        return builder.CreateStructGEP(unionLLVM, objVal, 0, "unionmember");
    }
    if (valType && valType->kind == TypeKind::Pointer &&
        valBase && valBase->kind == TypeKind::Union) {
        if (object->isLValue) {
            objVal = builder.CreateLoad(llvm::PointerType::get(ctx.getContext(), 0), objVal, "deref");
        }
        llvm::Type* unionLLVM = ctx.getLLVMType(valBase);
        if (!unionLLVM) return nullptr;
        return builder.CreateStructGEP(unionLLVM, objVal, 0, "unionmember");
    }

    if (valType && valType->kind == TypeKind::Struct) {
        auto* structType = static_cast<StructType*>(valType);
        // INH-01: chain-aware GEP first (inherited fields live in the base
        // sub-object); falls back to the local scan below.
        if (auto* gep = emitClassFieldGEP(ctx, valType, objVal, memberName)) {
            return gep;
        }
        for (size_t i = 0; i < structType->fields.size(); ++i) {
            if (structType->fields[i].name == memberName) {
                fieldIndex = i;
                break;
            }
        }
        objType = ctx.getLLVMType(valType);
    } else if (valType && valType->kind == TypeKind::Class) {
        auto* classType = static_cast<ClassType*>(valType);
        if (auto* gep = emitClassFieldGEP(ctx, classType, objVal, memberName)) {
            return gep;
        }
        objType = ctx.getLLVMType(valType);
    } else if (valType && valType->kind == TypeKind::Pointer &&
               valBase && valBase->kind == TypeKind::Struct) {
        auto* structType = static_cast<StructType*>(valBase);
        // INH-01: chain-aware GEP first — inherited fields live in the base
        // sub-object behind the pointer.
        // Load the pointer unless the object already produced a pointer value
        // (a cast, a call result, `&x`, ...); only lvalue objects are addresses
        // of a variable that stores the pointer (MEM-10).
        if (object->isLValue) {
            objVal = builder.CreateLoad(llvm::PointerType::get(ctx.getContext(), 0), objVal, "deref");
        }
        if (auto* gep = emitClassFieldGEP(ctx, valBase, objVal, memberName)) {
            return gep;
        }
        for (size_t i = 0; i < structType->fields.size(); ++i) {
            if (structType->fields[i].name == memberName) {
                fieldIndex = i;
                break;
            }
        }
        objType = ctx.getLLVMType(valBase);
    } else if (valType && valType->kind == TypeKind::Pointer &&
               valBase && valBase->kind == TypeKind::Class) {
        auto* classType = static_cast<ClassType*>(valBase);
        // Load the pointer unless the object already produced a pointer value.
        if (object->isLValue) {
            objVal = builder.CreateLoad(llvm::PointerType::get(ctx.getContext(), 0), objVal, "deref");
        }
        if (auto* gep = emitClassFieldGEP(ctx, classType, objVal, memberName)) {
            return gep;
        }
        objType = ctx.getLLVMType(valBase);
    } else if (valType && valType->kind == TypeKind::Pointer) {
        objType = ctx.getLLVMType(valBase);
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

// ===== P1-06 (FMT-01/02/04): builtin str/string method lowering =====

// Materialize the receiver as an rvalue `str`/`string` struct value.
static llvm::Value* builtinObjectValue(CodegenContext& ctx, MethodCallExprAST& node) {
    llvm::Value* objVal = node.object->codegen(ctx);
    if (!objVal) return nullptr;
    if (node.object->isLValue) objVal = ctx.loadValue(objVal, node.object->type);
    return objVal;
}

static llvm::Value* codegenBuiltinMethod(CodegenContext& ctx, MethodCallExprAST& node) {
    auto& builder = ctx.getBuilder();
    llvm::LLVMContext& c = ctx.getContext();
    auto* i8Ty = llvm::Type::getInt8Ty(c);
    auto* i64Ty = llvm::Type::getInt64Ty(c);

    switch (node.builtinMethod) {
        case BuiltinMethod::StrLen: {
            llvm::Value* obj = builtinObjectValue(ctx, node);
            if (!obj) return nullptr;
            return builder.CreateExtractValue(obj, 1, "str.len");
        }
        case BuiltinMethod::StrCharCount: {
            // NOTE: getUtf8CharCountFn() may synthesize IR and re-point the
            // shared builder — save/restore the caller's insertion point.
            auto savedIP = builder.saveIP();
            llvm::Function* cntFn = ctx.getUtf8CharCountFn();
            builder.restoreIP(savedIP);
            llvm::Value* obj = builtinObjectValue(ctx, node);
            if (!obj) return nullptr;
            llvm::Value* ptr = builder.CreateExtractValue(obj, 0, "str.ptr");
            llvm::Value* len = builder.CreateExtractValue(obj, 1, "str.len");
            return builder.CreateCall(cntFn, {ptr, len}, "str.count");
        }
        case BuiltinMethod::StrCharAt: {
            llvm::Value* obj = builtinObjectValue(ctx, node);
            if (!obj) return nullptr;
            llvm::Value* ptr = builder.CreateExtractValue(obj, 0, "str.ptr");
            llvm::Value* idx = node.args[0]->codegen(ctx);
            if (!idx) return nullptr;
            if (node.args[0]->isLValue) idx = ctx.loadValue(idx, node.args[0]->type);
            idx = builder.CreateSExtOrTrunc(idx, i64Ty, "str.idx64");
            llvm::Value* bytePtr =
                builder.CreateGEP(i8Ty, ptr, idx, "str.bytep");
            return builder.CreateLoad(i8Ty, bytePtr, "str.byte");
        }
        case BuiltinMethod::StrCharLenAt: {
            // Same builder-hijack caveat as StrCharCount above.
            auto savedIP = builder.saveIP();
            llvm::Function* lenAtFn = ctx.getUtf8CharLenAtFn();
            builder.restoreIP(savedIP);
            llvm::Value* obj = builtinObjectValue(ctx, node);
            if (!obj) return nullptr;
            llvm::Value* ptr = builder.CreateExtractValue(obj, 0, "str.ptr");
            llvm::Value* idx = node.args[0]->codegen(ctx);
            if (!idx) return nullptr;
            if (node.args[0]->isLValue) idx = ctx.loadValue(idx, node.args[0]->type);
            idx = builder.CreateSExtOrTrunc(idx, i64Ty, "str.idx64");
            return builder.CreateCall(lenAtFn, {ptr, idx}, "str.width");
        }
        case BuiltinMethod::StrFind:
        case BuiltinMethod::StrRFind: {
            // P1-09 (STD-10): needle scan over the byte view. The needle
            // accepts str or string (both carry {ptr, len} prefix fields).
            auto savedIP = builder.saveIP();
            llvm::Function* findFn =
                ctx.getStrFindFn(node.builtinMethod == BuiltinMethod::StrRFind);
            builder.restoreIP(savedIP);
            llvm::Value* obj = builtinObjectValue(ctx, node);
            if (!obj) return nullptr;
            llvm::Value* ptr = builder.CreateExtractValue(obj, 0, "str.ptr");
            llvm::Value* len = builder.CreateExtractValue(obj, 1, "str.len");
            llvm::Value* nv = node.args[0]->codegen(ctx);
            if (!nv) return nullptr;
            if (node.args[0]->isLValue)
                nv = ctx.loadValue(nv, node.args[0]->type);
            Type* nArg = node.args[0]->type;
            while (nArg && nArg->kind == TypeKind::Typedef)
                nArg = static_cast<TypedefType*>(nArg)->aliasedType;
            if (nArg && nArg->kind == TypeKind::String) {
                // Project {ptr, len, cap} to the str view.
                llvm::Value* sv = llvm::Constant::getNullValue(
                    ctx.getLLVMType(TypeContext::instance().getStrType()));
                sv = builder.CreateInsertValue(
                    sv, builder.CreateExtractValue(nv, 0, "n.ptr"), {0});
                nv = builder.CreateInsertValue(
                    sv, builder.CreateExtractValue(nv, 1, "n.len"), {1});
            }
            llvm::Value* nptr = builder.CreateExtractValue(nv, 0, "n.sptr");
            llvm::Value* nlen = builder.CreateExtractValue(nv, 1, "n.slen");
            return builder.CreateCall(findFn, {ptr, len, nptr, nlen},
                                      node.builtinMethod == BuiltinMethod::StrFind
                                          ? "str.find"
                                          : "str.rfind");
        }
        case BuiltinMethod::StrSub: {
            // P1-09 (STD-10): zero-copy byte-range view. begin < 0 ||
            // end < begin || end > len aborts (str.sub: out of bounds).
            llvm::Value* obj = builtinObjectValue(ctx, node);
            if (!obj) return nullptr;
            llvm::Value* ptr = builder.CreateExtractValue(obj, 0, "str.ptr");
            llvm::Value* len = builder.CreateExtractValue(obj, 1, "str.len");
            auto index64 = [&](ExprAST& arg) -> llvm::Value* {
                llvm::Value* v = arg.codegen(ctx);
                if (!v) return nullptr;
                if (arg.isLValue) v = ctx.loadValue(v, arg.type);
                return builder.CreateSExtOrTrunc(v, i64Ty, "sub.idx64");
            };
            llvm::Value* begin = index64(*node.args[0]);
            if (!begin) return nullptr;
            llvm::Value* end = index64(*node.args[1]);
            if (!end) return nullptr;

            llvm::Function* fn = builder.GetInsertBlock()->getParent();
            llvm::BasicBlock* okBB = llvm::BasicBlock::Create(c, "sub.ok", fn);
            llvm::BasicBlock* badBB = llvm::BasicBlock::Create(c, "sub.bad", fn);
            auto* neg = builder.CreateICmpSLT(begin, llvm::ConstantInt::get(i64Ty, 0), "sub.neg");
            auto* inverted = builder.CreateICmpSLT(end, begin, "sub.inv");
            auto* over = builder.CreateICmpSGT(end, len, "sub.over");
            auto* bad = builder.CreateOr(neg, builder.CreateOr(inverted, over), "sub.oob");
            builder.CreateCondBr(bad, badBB, okBB);

            builder.SetInsertPoint(badBB);
            {
                std::string prefix = nodeSourcePrefix(node);
                std::string fmtStr = prefix.empty()
                    ? "panic: str.sub: out of bounds\n"
                    : prefix + ": panic: str.sub: out of bounds\n";
                llvm::Value* fmt = builder.CreateGlobalString(fmtStr, ".subfmt");
                emitDprintfAndAbort(ctx, fmt, {});
                builder.CreateUnreachable();
            }

            builder.SetInsertPoint(okBB);
            llvm::Value* subPtr = builder.CreateGEP(i8Ty, ptr, begin, "sub.ptr");
            llvm::Value* subLen = builder.CreateSub(end, begin, "sub.len");
            llvm::Value* v = llvm::Constant::getNullValue(
                ctx.getLLVMType(TypeContext::instance().getStrType()));
            v = builder.CreateInsertValue(v, subPtr, {0});
            return builder.CreateInsertValue(v, subLen, {1});
        }
        case BuiltinMethod::StringNew:
        case BuiltinMethod::StringDestroy:
        case BuiltinMethod::StringAppend:
        case BuiltinMethod::StringPush:
        case BuiltinMethod::StringLen:
        case BuiltinMethod::StringCapacity:
            return codegenStringMethod(ctx, node);
        case BuiltinMethod::None:
            break;
    }
    return nullptr;
}

// ===== P1-06 (FMT-02): string memory/mutation lowering =====
// Mutating methods take the receiver's ADDRESS: an lvalue object already
// codegens to its storage address; an rvalue (e.g. `string.new("ab")`) is
// materialized into a temporary alloca first so chains keep working.

// Receiver address for a string method call.
static llvm::Value* builtinObjectAddr(CodegenContext& ctx, MethodCallExprAST& node) {
    auto& builder = ctx.getBuilder();
    llvm::Value* objVal = node.object->codegen(ctx);
    if (!objVal) return nullptr;
    if (node.object->isLValue) return objVal; // already the storage address
    llvm::Type* strTy = objVal->getType();
    llvm::Value* tmp = builder.CreateAlloca(strTy, nullptr, "string.tmp");
    builder.CreateStore(objVal, tmp);
    return tmp;
}

// str-typed argument value for append/new (String -> str projected here).
static llvm::Value* builtinStrArg(CodegenContext& ctx, ExprAST* arg) {
    auto& builder = ctx.getBuilder();
    llvm::Value* v = arg->codegen(ctx);
    if (!v) return nullptr;
    if (arg->isLValue) v = ctx.loadValue(v, arg->type);
    Type* t = arg->type;
    while (t && t->kind == TypeKind::Typedef)
        t = static_cast<TypedefType*>(t)->aliasedType;
    if (t && t->kind == TypeKind::String) {
        llvm::Value* ptr = builder.CreateExtractValue(v, 0, "arg.ptr");
        llvm::Value* len = builder.CreateExtractValue(v, 1, "arg.len");
        llvm::Value* sv = llvm::Constant::getNullValue(
            ctx.getLLVMType(TypeContext::instance().getStrType()));
        sv = builder.CreateInsertValue(sv, ptr, {0});
        return builder.CreateInsertValue(sv, len, {1});
    }
    return v;
}

static llvm::FunctionCallee declareLibcFn(CodegenContext& ctx, const char* name,
                                          llvm::Type* ret, llvm::ArrayRef<llvm::Type*> params) {
    llvm::LLVMContext& c = ctx.getContext();
    return ctx.getModule().getOrInsertFunction(
        name, llvm::FunctionType::get(ret, params, false));
}

static llvm::Value* codegenStringMethod(CodegenContext& ctx, MethodCallExprAST& node) {
    auto& builder = ctx.getBuilder();
    llvm::LLVMContext& c = ctx.getContext();
    auto* i64Ty = llvm::Type::getInt64Ty(c);
    auto* i8Ty = llvm::Type::getInt8Ty(c);
    auto* ptrTy = llvm::PointerType::get(c, 0);
    auto* strLLVM = ctx.getLLVMType(TypeContext::instance().getStringType());

    switch (node.builtinMethod) {
        case BuiltinMethod::StringLen: {
            llvm::Value* addr = builtinObjectAddr(ctx, node);
            if (!addr) return nullptr;
            llvm::Value* v = builder.CreateLoad(strLLVM, addr, "string.v");
            return builder.CreateExtractValue(v, 1, "string.len");
        }
        case BuiltinMethod::StringCapacity: {
            llvm::Value* addr = builtinObjectAddr(ctx, node);
            if (!addr) return nullptr;
            llvm::Value* v = builder.CreateLoad(strLLVM, addr, "string.v");
            return builder.CreateExtractValue(v, 2, "string.cap");
        }
        case BuiltinMethod::StringNew: {
            // cap = max(len, 1); malloc(cap); memcpy(ptr, s.ptr, len).
            auto savedIP = builder.saveIP();
            auto mallocFn = declareLibcFn(ctx, "malloc", ptrTy, {i64Ty});
            auto memcpyFn = declareLibcFn(ctx, "memcpy", ptrTy, {ptrTy, ptrTy, i64Ty});
            builder.restoreIP(savedIP);
            llvm::Value* s = builtinStrArg(ctx, node.args[0].get());
            if (!s) return nullptr;
            llvm::Value* sptr = builder.CreateExtractValue(s, 0, "new.sptr");
            llvm::Value* slen = builder.CreateExtractValue(s, 1, "new.slen");
            llvm::Value* one = llvm::ConstantInt::get(i64Ty, 1);
            llvm::Value* cap = builder.CreateSelect(
                builder.CreateICmpUGT(slen, one, "new.gt1"), slen, one, "new.cap");
            llvm::Value* ptr = builder.CreateCall(mallocFn, {cap}, "new.ptr");
            builder.CreateCall(memcpyFn, {ptr, sptr, slen});
            llvm::Value* v = llvm::Constant::getNullValue(strLLVM);
            v = builder.CreateInsertValue(v, ptr, {0});
            v = builder.CreateInsertValue(v, slen, {1});
            return builder.CreateInsertValue(v, cap, {2});
        }
        case BuiltinMethod::StringDestroy: {
            auto savedIP = builder.saveIP();
            auto freeFn = declareLibcFn(ctx, "free", llvm::Type::getVoidTy(c), {ptrTy});
            builder.restoreIP(savedIP);
            llvm::Value* addr = builtinObjectAddr(ctx, node);
            if (!addr) return nullptr;
            llvm::Value* v = builder.CreateLoad(strLLVM, addr, "destroy.v");
            llvm::Value* ptr = builder.CreateExtractValue(v, 0, "destroy.ptr");
            builder.CreateCall(freeFn, {ptr});
            // Zero the header: a second destroy frees null (harmless) and
            // len()/capacity() observe the destruction.
            builder.CreateStore(llvm::Constant::getNullValue(strLLVM), addr);
            return llvm::Constant::getNullValue(llvm::Type::getInt32Ty(c));
        }
        case BuiltinMethod::StringAppend: {
            auto savedIP = builder.saveIP();
            auto reallocFn = declareLibcFn(ctx, "realloc", ptrTy, {ptrTy, i64Ty});
            auto memmoveFn = declareLibcFn(ctx, "memmove", ptrTy, {ptrTy, ptrTy, i64Ty});
            builder.restoreIP(savedIP);
            llvm::Value* addr = builtinObjectAddr(ctx, node);
            if (!addr) return nullptr;
            llvm::Value* s = builtinStrArg(ctx, node.args[0].get());
            if (!s) return nullptr;
            llvm::Value* v = builder.CreateLoad(strLLVM, addr, "append.v");
            llvm::Value* ptr = builder.CreateExtractValue(v, 0, "append.ptr");
            llvm::Value* len = builder.CreateExtractValue(v, 1, "append.len");
            llvm::Value* cap = builder.CreateExtractValue(v, 2, "append.cap");
            llvm::Value* sptr = builder.CreateExtractValue(s, 0, "append.sptr");
            llvm::Value* slen = builder.CreateExtractValue(s, 1, "append.slen");
            llvm::Value* need = builder.CreateAdd(len, slen, "append.need");

            llvm::Function* fn = builder.GetInsertBlock()->getParent();
            llvm::BasicBlock* growBB = llvm::BasicBlock::Create(c, "append.grow", fn);
            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(c, "append.merge", fn);
            llvm::Value* mustGrow = builder.CreateICmpUGT(need, cap, "append.grow?");
            llvm::BasicBlock* condBB = builder.GetInsertBlock();
            builder.CreateCondBr(mustGrow, growBB, mergeBB);

            builder.SetInsertPoint(growBB);
            llvm::Value* doubled = builder.CreateMul(cap, llvm::ConstantInt::get(i64Ty, 2), "append.dbl");
            llvm::Value* grown = builder.CreateSelect(
                builder.CreateICmpUGT(doubled, need, "append.dbl?"), doubled, need, "append.cap2");
            llvm::Value* rptr = builder.CreateCall(reallocFn, {ptr, grown}, "append.rptr");
            builder.CreateBr(mergeBB);

            builder.SetInsertPoint(mergeBB);
            llvm::PHINode* ptr2 = builder.CreatePHI(ptrTy, 2, "append.ptr2");
            llvm::PHINode* cap2 = builder.CreatePHI(i64Ty, 2, "append.cap2");
            ptr2->addIncoming(ptr, condBB);
            cap2->addIncoming(cap, condBB);
            ptr2->addIncoming(rptr, growBB);
            cap2->addIncoming(grown, growBB);

            llvm::Value* dst = builder.CreateGEP(i8Ty, ptr2, len, "append.dst");
            // 终审 I3: the source view may alias the receiver's own buffer
            // (`s.append(s)`, or any view into it). After a moving realloc
            // the old pointer is dangling — re-project a self-aliased source
            // onto the (content-preserving) realloc result, and use memmove
            // for the (now possible) overlap with dst.
            llvm::Value* capEnd = builder.CreateGEP(i8Ty, ptr, cap, "append.cepend");
            llvm::Value* selfAlias = builder.CreateAnd(
                builder.CreateICmpUGE(sptr, ptr, "append.alias1"),
                builder.CreateICmpULT(sptr, capEnd, "append.alias2"), "append.alias");
            llvm::Value* src = builder.CreateSelect(selfAlias, ptr2, sptr, "append.src");
            builder.CreateCall(memmoveFn, {dst, src, slen});
            llvm::Value* v2 = llvm::Constant::getNullValue(strLLVM);
            v2 = builder.CreateInsertValue(v2, ptr2, {0});
            v2 = builder.CreateInsertValue(v2, need, {1});
            v2 = builder.CreateInsertValue(v2, cap2, {2});
            builder.CreateStore(v2, addr);
            return llvm::Constant::getNullValue(llvm::Type::getInt32Ty(c));
        }
        case BuiltinMethod::StringPush: {
            auto savedIP = builder.saveIP();
            auto reallocFn = declareLibcFn(ctx, "realloc", ptrTy, {ptrTy, i64Ty});
            builder.restoreIP(savedIP);
            llvm::Value* addr = builtinObjectAddr(ctx, node);
            if (!addr) return nullptr;
            llvm::Value* ch = node.args[0]->codegen(ctx);
            if (!ch) return nullptr;
            if (node.args[0]->isLValue) ch = ctx.loadValue(ch, node.args[0]->type);
            ch = builder.CreateSExtOrTrunc(ch, i8Ty, "push.c8");

            llvm::Value* v = builder.CreateLoad(strLLVM, addr, "push.v");
            llvm::Value* ptr = builder.CreateExtractValue(v, 0, "push.ptr");
            llvm::Value* len = builder.CreateExtractValue(v, 1, "push.len");
            llvm::Value* cap = builder.CreateExtractValue(v, 2, "push.cap");
            llvm::Value* need = builder.CreateAdd(len, llvm::ConstantInt::get(i64Ty, 1), "push.need");

            llvm::Function* fn = builder.GetInsertBlock()->getParent();
            llvm::BasicBlock* growBB = llvm::BasicBlock::Create(c, "push.grow", fn);
            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(c, "push.merge", fn);
            llvm::Value* mustGrow = builder.CreateICmpUGT(need, cap, "push.grow?");
            llvm::BasicBlock* condBB = builder.GetInsertBlock();
            builder.CreateCondBr(mustGrow, growBB, mergeBB);

            builder.SetInsertPoint(growBB);
            llvm::Value* doubled = builder.CreateMul(cap, llvm::ConstantInt::get(i64Ty, 2), "push.dbl");
            llvm::Value* grown = builder.CreateSelect(
                builder.CreateICmpUGT(doubled, need, "push.dbl?"), doubled, need, "push.cap2");
            llvm::Value* rptr = builder.CreateCall(reallocFn, {ptr, grown}, "push.rptr");
            builder.CreateBr(mergeBB);

            builder.SetInsertPoint(mergeBB);
            llvm::PHINode* ptr2 = builder.CreatePHI(ptrTy, 2, "push.ptr2");
            llvm::PHINode* cap2 = builder.CreatePHI(i64Ty, 2, "push.cap2");
            ptr2->addIncoming(ptr, condBB);
            cap2->addIncoming(cap, condBB);
            ptr2->addIncoming(rptr, growBB);
            cap2->addIncoming(grown, growBB);

            llvm::Value* dst = builder.CreateGEP(i8Ty, ptr2, len, "push.dst");
            builder.CreateStore(ch, dst);
            llvm::Value* v2 = llvm::Constant::getNullValue(strLLVM);
            v2 = builder.CreateInsertValue(v2, ptr2, {0});
            v2 = builder.CreateInsertValue(v2, need, {1});
            v2 = builder.CreateInsertValue(v2, cap2, {2});
            builder.CreateStore(v2, addr);
            return llvm::Constant::getNullValue(llvm::Type::getInt32Ty(c));
        }
        default:
            return nullptr;
    }
}

// P1-06 (FMT-04): str_from_c — strlen over the buffer (or an identity view
// for a str argument), assembled into the canonical `str` struct.
static llvm::Value* codegenStrFromC(CodegenContext& ctx, CallExprAST& node) {
    auto& builder = ctx.getBuilder();
    llvm::LLVMContext& c = ctx.getContext();
    auto* i64Ty = llvm::Type::getInt64Ty(c);

    llvm::Value* arg = node.args[0]->codegen(ctx);
    if (!arg) return nullptr;
    if (node.args[0]->isLValue) arg = ctx.loadValue(arg, node.args[0]->type);

    Type* argTy = node.args[0]->type;
    while (argTy && argTy->kind == TypeKind::Typedef) {
        argTy = static_cast<TypedefType*>(argTy)->aliasedType;
    }
    if (argTy && argTy->kind == TypeKind::Str) {
        return arg; // identity view
    }

    // P1-06 (FMT-04): runtime buffers are validated (E2015's runtime
    // companion) — an invalid sequence panics, never silently mis-decodes.
    // Helper synthesis re-points the shared builder; save/restore around it.
    auto savedIP = builder.saveIP();
    auto* validateFn = ctx.getUtf8ValidateFn();
    auto* strlenTy = llvm::FunctionType::get(i64Ty, {llvm::PointerType::get(c, 0)}, false);
    llvm::FunctionCallee strlenFn =
        ctx.getModule().getOrInsertFunction("strlen", strlenTy);
    builder.restoreIP(savedIP);

    llvm::Value* len = builder.CreateCall(strlenFn, {arg}, "strfromc.len");
    llvm::Value* valid = builder.CreateCall(validateFn, {arg, len}, "strfromc.valid");

    llvm::Function* fn = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* okBB = llvm::BasicBlock::Create(c, "utf8.ok", fn);
    llvm::BasicBlock* badBB = llvm::BasicBlock::Create(c, "utf8.bad", fn);
    builder.CreateCondBr(valid, okBB, badBB);

    builder.SetInsertPoint(badBB);
    {
        std::string prefix = nodeSourcePrefix(node);
        std::string fmtStr = prefix.empty()
            ? "panic: invalid UTF-8 sequence\n"
            : prefix + ": panic: invalid UTF-8 sequence\n";
        llvm::Value* fmt = builder.CreateGlobalString(fmtStr, ".utf8fmt");
        emitDprintfAndAbort(ctx, fmt, {});
        builder.CreateUnreachable();
    }

    builder.SetInsertPoint(okBB);
    llvm::Value* v = llvm::Constant::getNullValue(ctx.getLLVMType(TypeContext::instance().getStrType()));
    v = builder.CreateInsertValue(v, arg, {0});
    return builder.CreateInsertValue(v, len, {1});
}

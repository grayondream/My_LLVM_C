#include "sema/CompileTimeEvaluator.h"

#include "ast/Expr.h"
#include "ast/LayoutBuilder.h"
#include "ast/Decl.h"
#include "ast/Type.h"
#include "sema/SemanticAnalyzer.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

#include <algorithm>
#include <unordered_set>

using ConstValue = CompileTimeEvaluator::ConstValue;

// P1-04 / CT-04: host 目标映射（spec §4）：triple → os/arch/cpu 查询值。
static std::string ctTargetOs() {
    std::string triple = llvm::sys::getDefaultTargetTriple();
    if (triple.find("linux") != std::string::npos) return "linux";
    if (triple.find("darwin") != std::string::npos
        || triple.find("apple") != std::string::npos) return "macos";
    if (triple.find("windows") != std::string::npos
        || triple.find("mingw") != std::string::npos) return "windows";
    return "unknown";
}

static std::string ctTargetArch() {
    llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
    return triple.getArchName().str();
}

CompileTimeEvaluator::CompileTimeEvaluator(SemanticAnalyzer& sema)
    : m_sema(sema) {
    // P1-04 / CT-06: 独立 LLVMContext/Module 承载布局查询（DataLayout 与
    // codegen 的 host 目标一致——默认布局即 64-bit LP64，与 x86_64/aarch64 匹配）。
    m_llvmCtx = std::make_unique<llvm::LLVMContext>();
    m_module = std::make_unique<llvm::Module>("compile_time_layout", *m_llvmCtx);
}

CompileTimeEvaluator::~CompileTimeEvaluator() = default;

std::optional<ConstValue> CompileTimeEvaluator::eval(ExprAST* expr, ASTNode& at) {
    if (!expr) {
        return std::nullopt;
    }
    if (m_depth >= kMaxDepth) {
        if (!m_depthReported) {
            m_sema.emitError("compile_time evaluation depth limit exceeded (64)", at);
            m_depthReported = true;
        }
        return std::nullopt;
    }

    struct DepthGuard {
        CompileTimeEvaluator& ev;
        ~DepthGuard() {
            if (--ev.m_depth == 0) {
                ev.m_depthReported = false;
            }
        }
    } guard{*this};
    ++m_depth;

    if (auto* num = dynamic_cast<NumberExprAST*>(expr)) {
        ConstValue cv;
        cv.type = ConstValue::INT;
        cv.intVal = num->value;
        return cv;
    }

    if (auto* flt = dynamic_cast<FloatExprAST*>(expr)) {
        ConstValue cv;
        cv.type = ConstValue::DOUBLE;
        cv.doubleVal = flt->value;
        return cv;
    }

    if (auto* chr = dynamic_cast<CharExprAST*>(expr)) {
        ConstValue cv;
        cv.type = ConstValue::CHAR;
        cv.charVal = chr->value;
        return cv;
    }

    if (auto* str = dynamic_cast<StringExprAST*>(expr)) {
        ConstValue cv;
        cv.type = ConstValue::STR;
        cv.strVal = str->value;
        return cv;
    }

    if (auto* unary = dynamic_cast<UnaryExprAST*>(expr)) {
        auto operand = eval(unary->operand.get(), at);
        if (!operand) {
            return std::nullopt;
        }
        if (operand->type == ConstValue::INT) {
            ConstValue cv;
            cv.type = ConstValue::INT;
            switch (unary->op) {
                case UnaryOp::Plus:   cv.intVal = operand->intVal; break;
                case UnaryOp::Minus:  cv.intVal = -operand->intVal; break;
                case UnaryOp::Not:    cv.intVal = !operand->intVal; break;
                case UnaryOp::BitNot: cv.intVal = ~operand->intVal; break;
                default: return std::nullopt;
            }
            return cv;
        }
        if (operand->type == ConstValue::DOUBLE) {
            ConstValue cv;
            cv.type = ConstValue::DOUBLE;
            switch (unary->op) {
                case UnaryOp::Plus:  cv.doubleVal = operand->doubleVal; break;
                case UnaryOp::Minus: cv.doubleVal = -operand->doubleVal; break;
                default: return std::nullopt;
            }
            return cv;
        }
        return std::nullopt;
    }

    if (auto* bin = dynamic_cast<BinaryExprAST*>(expr)) {
        auto left = eval(bin->left.get(), at);
        if (!left) {
            return std::nullopt;
        }
        auto right = eval(bin->right.get(), at);
        if (!right) {
            return std::nullopt;
        }
        return evalBinary(*left, *right, static_cast<int>(bin->op));
    }

    if (auto* ternary = dynamic_cast<TernaryExprAST*>(expr)) {
        auto cond = eval(ternary->cond.get(), at);
        if (!cond || cond->type != ConstValue::INT) {
            return std::nullopt;
        }
        return cond->intVal != 0 ? eval(ternary->then.get(), at)
                                 : eval(ternary->elseExpr.get(), at);
    }

    // constexpr 变量（DEC-05：共享 constexprValues）；constexpr 函数调用
    // 委托既有解释内核（evalConstexprCall/evalConstexprStmt，含 ConstEnv）。
    if (auto* var = dynamic_cast<VariableExprAST*>(expr)) {
        const auto& values = m_sema.getConstexprValues();
        auto it = values.find(var->name);
        if (it != values.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    if (auto* call = dynamic_cast<CallExprAST*>(expr)) {
        // P1-04 评审 I1: 实参由本求值器折叠（可能含 CT 根），再以已折叠
        // 实参解释 constexpr 函数体——避免与 evaluateConstexpr 的整树
        // CT 委托互递归。
        std::vector<ConstValue> argValues;
        for (auto& a : call->args) {
            auto v = eval(a.get(), at);
            if (!v) return std::nullopt;
            argValues.push_back(*v);
        }
        return m_sema.evalConstexprCallCT(*call, argValues);
    }

    // P1-04 / CT-04/05: compile_time 成员链值（target.os/arch/cpu、
    // build.debug/optimize/version）。未知成员诊断在此发出（spec §3 钉死）。
    if (auto* ma = dynamic_cast<MemberAccessExprAST*>(expr)) {
        if (m_sema.isCompileTimeRoot(ma->object.get())) {
            return evalCTMemberChain(*ma, at);
        }
        return std::nullopt;
    }

    // P1-04 / CT-01: compile_time 成员调用（static_assert/size_of 族/if）
    // —— CT-02（T5）接管 if/static_assert；此处实现布局查询（CT-06）。
    if (auto* mc = dynamic_cast<MethodCallExprAST*>(expr)) {
        if (m_sema.isCompileTimeRoot(mc->object.get())) {
            static const std::unordered_set<std::string> kKnownMembers = {
                "static_assert", "if", "size_of", "align_of", "offset_of"};
            if (!kKnownMembers.count(mc->methodName)) {
                m_sema.emitError("unknown compile_time member '" + mc->methodName + "'", at);
                return std::nullopt;
            }
            if (mc->methodName == "size_of" || mc->methodName == "align_of"
                || mc->methodName == "offset_of") {
                return evalLayoutQuery(*mc, at);
            }
            // static_assert / if：CT-02/CT-03 后续任务接管，此处静默。
            return std::nullopt;
        }
        return std::nullopt;
    }

    return std::nullopt; // 普通非常量节点：静默失败（spec §2 钉死）
}

std::optional<ConstValue>
CompileTimeEvaluator::evalBinary(ConstValue& left, ConstValue& right, int op) {
    const BinaryOp binOp = static_cast<BinaryOp>(op);

    if (left.type == ConstValue::STR && right.type == ConstValue::STR) {
        ConstValue cv;
        switch (binOp) {
            case BinaryOp::Add:
                cv.type = ConstValue::STR;
                cv.strVal = left.strVal + right.strVal;
                return cv;
            case BinaryOp::Eq:
                cv.type = ConstValue::INT;
                cv.intVal = left.strVal == right.strVal;
                return cv;
            case BinaryOp::NotEq:
                cv.type = ConstValue::INT;
                cv.intVal = left.strVal != right.strVal;
                return cv;
            default:
                return std::nullopt;
        }
    }

    if (left.type == ConstValue::INT && right.type == ConstValue::INT) {
        ConstValue cv;
        cv.type = ConstValue::INT;
        switch (binOp) {
            case BinaryOp::Add: cv.intVal = left.intVal + right.intVal; break;
            case BinaryOp::Sub: cv.intVal = left.intVal - right.intVal; break;
            case BinaryOp::Mul: cv.intVal = left.intVal * right.intVal; break;
            case BinaryOp::Div:
                if (right.intVal == 0) return std::nullopt;
                cv.intVal = left.intVal / right.intVal;
                break;
            case BinaryOp::Mod:
                if (right.intVal == 0) return std::nullopt;
                cv.intVal = left.intVal % right.intVal;
                break;
            case BinaryOp::Eq:     cv.intVal = left.intVal == right.intVal; break;
            case BinaryOp::NotEq:  cv.intVal = left.intVal != right.intVal; break;
            case BinaryOp::Lt:     cv.intVal = left.intVal < right.intVal; break;
            case BinaryOp::Gt:     cv.intVal = left.intVal > right.intVal; break;
            case BinaryOp::Le:     cv.intVal = left.intVal <= right.intVal; break;
            case BinaryOp::Ge:     cv.intVal = left.intVal >= right.intVal; break;
            case BinaryOp::And:    cv.intVal = left.intVal && right.intVal; break;
            case BinaryOp::Or:     cv.intVal = left.intVal || right.intVal; break;
            case BinaryOp::BitAnd: cv.intVal = left.intVal & right.intVal; break;
            case BinaryOp::BitOr:  cv.intVal = left.intVal | right.intVal; break;
            case BinaryOp::BitXor: cv.intVal = left.intVal ^ right.intVal; break;
            case BinaryOp::LShift: cv.intVal = left.intVal << right.intVal; break;
            case BinaryOp::RShift: cv.intVal = left.intVal >> right.intVal; break;
            default: return std::nullopt;
        }
        return cv;
    }

    if (left.type == ConstValue::DOUBLE && right.type == ConstValue::DOUBLE) {
        ConstValue cv;
        cv.type = ConstValue::DOUBLE;
        switch (binOp) {
            case BinaryOp::Add: cv.doubleVal = left.doubleVal + right.doubleVal; break;
            case BinaryOp::Sub: cv.doubleVal = left.doubleVal - right.doubleVal; break;
            case BinaryOp::Mul: cv.doubleVal = left.doubleVal * right.doubleVal; break;
            case BinaryOp::Div:
                if (right.doubleVal == 0.0) return std::nullopt;
                cv.doubleVal = left.doubleVal / right.doubleVal;
                break;
            case BinaryOp::Eq:    cv.intVal = left.doubleVal == right.doubleVal; cv.type = ConstValue::INT; break;
            case BinaryOp::NotEq: cv.intVal = left.doubleVal != right.doubleVal; cv.type = ConstValue::INT; break;
            case BinaryOp::Lt:    cv.intVal = left.doubleVal < right.doubleVal; cv.type = ConstValue::INT; break;
            case BinaryOp::Gt:    cv.intVal = left.doubleVal > right.doubleVal; cv.type = ConstValue::INT; break;
            case BinaryOp::Le:    cv.intVal = left.doubleVal <= right.doubleVal; cv.type = ConstValue::INT; break;
            case BinaryOp::Ge:    cv.intVal = left.doubleVal >= right.doubleVal; cv.type = ConstValue::INT; break;
            default: return std::nullopt;
        }
        return cv;
    }

    return std::nullopt;
}

std::optional<ConstValue>
CompileTimeEvaluator::evalCTMemberChain(MemberAccessExprAST& node, ASTNode& at) {
    // 根链成员名序列：compile_time.target.os → [target, os]。
    std::vector<std::string> members;
    {
        std::vector<MemberAccessExprAST*> chain;
        ExprAST* cur = &node;
        while (auto* ma = dynamic_cast<MemberAccessExprAST*>(cur)) {
            chain.push_back(ma);
            cur = ma->object.get();
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            members.push_back((*it)->memberName);
        }
    }

    auto unknown = [&](const std::string& name) {
        m_sema.emitError("unknown compile_time member '" + name + "'", at);
        return std::optional<ConstValue>{};
    };

    if (members[0] == "target") {
        if (members.size() == 1) return unknown("target"); // 裸命名空间
        if (members.size() > 2) return unknown(members[2]);
        ConstValue cv;
        cv.type = ConstValue::STR;
        const std::string& m = members[1];
        if (m == "os") {
            cv.strVal = ctTargetOs();
        } else if (m == "arch") {
            cv.strVal = ctTargetArch();
        } else if (m == "cpu") {
            cv.strVal = llvm::sys::getHostCPUName().str();
        } else {
            return unknown(m);
        }
        return cv;
    }

    if (members[0] == "build") {
        if (members.size() == 1) return unknown("build");
        if (members.size() > 2) return unknown(members[2]);
        const std::string& m = members[1];
        ConstValue cv;
        if (m == "debug") {
            cv.type = ConstValue::INT;
            cv.intVal = m_sema.m_ctBuildDebug ? 1 : 0;
            return cv;
        }
        if (m == "optimize") {
            cv.type = ConstValue::STR;
            cv.strVal = m_sema.m_ctBuildOptimize;
            return cv;
        }
        if (m == "version") {
            cv.type = ConstValue::STR;
            cv.strVal = "1.0.0";
            return cv;
        }
        return unknown(m);
    }

    return unknown(members[0]);
}

// P1-04 / CT-06: Type* → llvm::Type（布局规则与 CodegenContext::getLLVMType
// 一致——基类子对象占槽 0；union 以最大对齐成员 + 填充布局）。
llvm::Type* CompileTimeEvaluator::toLLVMType(Type* t) {
    if (!t) return nullptr;
    while (t && t->kind == TypeKind::Typedef) {
        t = static_cast<TypedefType*>(t)->aliasedType;
    }
    if (!t) return nullptr;
    auto cached = m_typeCache.find(t);
    if (cached != m_typeCache.end()) return cached->second;

    llvm::LLVMContext& ctx = *m_llvmCtx;
    llvm::Type* result = nullptr;
    switch (t->kind) {
        case TypeKind::Char:    result = llvm::Type::getInt8Ty(ctx); break;
        case TypeKind::Bool:    result = llvm::Type::getInt1Ty(ctx); break;
        case TypeKind::Int8:    result = llvm::Type::getInt8Ty(ctx); break;
        case TypeKind::Int16:   result = llvm::Type::getInt16Ty(ctx); break;
        case TypeKind::Int32:   result = llvm::Type::getInt32Ty(ctx); break;
        case TypeKind::Int64:   result = llvm::Type::getInt64Ty(ctx); break;
        case TypeKind::Int128:  result = llvm::Type::getInt128Ty(ctx); break;
        case TypeKind::UInt8:   result = llvm::Type::getInt8Ty(ctx); break;
        case TypeKind::UInt16:  result = llvm::Type::getInt16Ty(ctx); break;
        case TypeKind::UInt32:  result = llvm::Type::getInt32Ty(ctx); break;
        case TypeKind::UInt64:  result = llvm::Type::getInt64Ty(ctx); break;
        case TypeKind::UInt128: result = llvm::Type::getInt128Ty(ctx); break;
        case TypeKind::ISize:   result = llvm::Type::getInt64Ty(ctx); break;
        case TypeKind::USize:   result = llvm::Type::getInt64Ty(ctx); break;
        case TypeKind::Float32: result = llvm::Type::getFloatTy(ctx); break;
        case TypeKind::Float64: result = llvm::Type::getDoubleTy(ctx); break;
        case TypeKind::Float16: result = llvm::Type::getHalfTy(ctx); break;
        case TypeKind::Float128:result = llvm::Type::getFP128Ty(ctx); break;
        case TypeKind::Pointer: result = llvm::PointerType::get(ctx, 0); break;
        case TypeKind::Enum: {
            auto* et = static_cast<EnumType*>(t);
            result = et->underlyingType ? toLLVMType(et->underlyingType)
                                        : llvm::Type::getInt32Ty(ctx);
            break;
        }
        case TypeKind::Struct:
        case TypeKind::Class: {
            // P1-05 / ANN: 布局单源化（LayoutBuilder；基类槽 0、packed/align）。
            auto* agg = static_cast<StructType*>(t);
            llvm::StructType* llvmSt = llvm::StructType::getTypeByName(ctx, agg->name);
            if (!llvmSt) {
                llvmSt = llvm::StructType::create(ctx, agg->name);
            }
            m_typeCache[t] = llvmSt;
            if (!llvmSt->isOpaque()) {
                result = llvmSt;
                break;
            }
            auto LR = LayoutBuilder::buildAggregate(t, ctx, m_module->getDataLayout(),
                [this](Type* tt) { return toLLVMType(tt); },
                [this](const std::string& n) -> Type* { return m_sema.resolveTypeByName(n); });
            llvmSt->setBody(LR.fieldTypes(), agg->isPacked);
            result = llvmSt;
            break;
        }
        case TypeKind::Union: {
            // P1-05 / ANN: 布局单源化（LayoutBuilder）。
            auto* ut = static_cast<UnionType*>(t);
            llvm::StructType* st = llvm::StructType::getTypeByName(ctx, ut->name);
            if (!st) {
                st = llvm::StructType::create(ctx, ut->name);
            }
            m_typeCache[t] = st;
            if (!st->isOpaque()) {
                result = st;
                break;
            }
            auto LR = LayoutBuilder::buildUnion(ut, ctx, m_module->getDataLayout(),
                [this](Type* tt) { return toLLVMType(tt); });
            st->setBody(LR.fieldTypes());
            result = st;
            break;
        }
        default:
            return nullptr; // Optional/Slice 等未支持布局——诊断路径
    }
    m_typeCache[t] = result;
    return result;
}

// P1-04 / CT-06: size_of(T)/align_of(T)/offset_of(T, "f") —— 结果 usize。
std::optional<ConstValue>
CompileTimeEvaluator::evalLayoutQuery(MethodCallExprAST& node, ASTNode& at) {
    // 类型实参：SizeofExprAST 包装（内建标量关键字路径）或单个标识符
    // （自定义类型按名解析——CT-08 切除后的最小落地，spec §1）。
    Type* queriedType = nullptr;
    std::string queriedName;
    if (!node.args.empty()) {
        if (auto* sized = dynamic_cast<SizeofExprAST*>(node.args[0].get())) {
            queriedType = sized->sizeofType;
        } else if (auto* name = dynamic_cast<VariableExprAST*>(node.args[0].get())) {
            queriedName = name->name;
            queriedType = m_sema.resolveTypeByName(name->name);
        }
    }
    if (!queriedType) {
        m_sema.emitError("unknown type '" + (queriedName.empty()
                             ? std::string("(unnamed)") : queriedName)
                             + "' in compile_time expression", at);
        return std::nullopt;
    }

    const llvm::DataLayout& dl = m_module->getDataLayout();
    ConstValue cv;
    cv.type = ConstValue::INT;

    if (node.methodName == "offset_of") {
        if (node.args.size() != 2) {
            m_sema.emitError("compile_time.offset_of requires (type, field)", at);
            return std::nullopt;
        }
        auto* field = dynamic_cast<StringExprAST*>(node.args[1].get());
        if (!field) {
            m_sema.emitError("compile_time.offset_of field must be a string literal", at);
            return std::nullopt;
        }
        llvm::Type* lt = toLLVMType(queriedType);
        auto* structTy = lt ? llvm::dyn_cast<llvm::StructType>(lt) : nullptr;
        if (!structTy || structTy->isOpaque()) {
            m_sema.emitError("unknown type '" + queriedName + "' in compile_time expression", at);
            return std::nullopt;
        }
        // P1-05 / ANN: 字段偏移来自 LayoutBuilder（与 IR 一致；基类槽 0）。
        auto LR = LayoutBuilder::buildAggregate(queriedType, *m_llvmCtx,
            m_module->getDataLayout(),
            [this](Type* tt) { return toLLVMType(tt); },
            [this](const std::string& n) -> Type* { return m_sema.resolveTypeByName(n); });
        std::vector<FieldInfo> fields;
        bool hasBase = false;
        if (queriedType->kind == TypeKind::Struct) {
            auto* st = static_cast<StructType*>(queriedType);
            fields = st->fields;
            hasBase = !st->baseClass.empty();
        } else if (queriedType->kind == TypeKind::Class) {
            auto* ct = static_cast<ClassType*>(queriedType);
            fields = ct->fields;
            hasBase = !ct->baseClass.empty();
        }
        for (size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].name == field->value) {
                cv.intVal = static_cast<long long>(LR.fields[i + (hasBase ? 1 : 0)].offset);
                return cv;
            }
        }
        for (size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].name == field->value) {
                cv.intVal = static_cast<long long>(
                    dl.getStructLayout(structTy)->getElementOffset(i + (hasBase ? 1 : 0)));
                return cv;
            }
        }
        m_sema.emitError("unknown field '" + field->value
                         + "' in compile_time.offset_of expression", at);
        return std::nullopt;
    }

    llvm::Type* lt = toLLVMType(queriedType);
    if (!lt) {
        m_sema.emitError("unknown type in compile_time." + node.methodName + " expression", at);
        return std::nullopt;
    }
    if (node.methodName == "size_of") {
        cv.intVal = static_cast<long long>(dl.getTypeAllocSize(lt));
    } else { // align_of
        cv.intVal = static_cast<long long>(dl.getABITypeAlign(lt).value());
    }
    return cv;
}

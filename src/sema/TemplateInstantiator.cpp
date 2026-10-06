// P1-03 / GEN-05: TemplateInstantiator 实现。覆盖全部 AST 节点族——
// 漏一个节点 = 含该节点的模板体实例化时静默丢失，新增节点须同步维护。
#include "sema/TemplateInstantiator.h"

#include "sema/TemplateRegistry.h"
#include "support/LiteralKind.h"

void TemplateInstantiator::copyLoc(ASTNode* dst, const ASTNode& src) {
    dst->sourceFile = src.sourceFile;
    dst->sourceLine = src.sourceLine;
    dst->sourceColumn = src.sourceColumn;
}

Type* TemplateInstantiator::rewrite(Type* t) {
    if (!t) return nullptr;
    switch (t->kind) {
        case TypeKind::TypeVar: {
            auto* tv = static_cast<TypeVarType*>(t);
            auto it = m_typeArgs.find(tv->name);
            // 缺失实参属内部错误（sema 定义处检查应拦下）；原样返回避免崩溃。
            // 实参可能内嵌 TypeInstance 占位（Box<Box<i32>>），须递归解析。
            if (it == m_typeArgs.end()) return t;
            return rewrite(it->second);
        }
        case TypeKind::Pointer: {
            Type* base = rewrite(t->base);
            if (base == t->base) return t;
            auto* p = new Type(TypeKind::Pointer, base);
            p->isConst = t->isConst;
            p->isVolatile = t->isVolatile;
            return p;
        }
        case TypeKind::Array: {
            auto* arr = static_cast<ArrayType*>(t);
            Type* elem = rewrite(arr->elementType);
            int size = arr->size;
            std::string sizeParam = arr->sizeParam;
            if (!sizeParam.empty()) {
                auto it = m_valueArgs.find(sizeParam);
                if (it != m_valueArgs.end()) {
                    size = static_cast<int>(it->second);
                    sizeParam.clear();
                }
            }
            if (elem == arr->elementType && sizeParam == arr->sizeParam) return t;
            auto* cloned = new ArrayType(elem, size);
            cloned->sizeParam = sizeParam;
            cloned->isConst = t->isConst;
            cloned->isVolatile = t->isVolatile;
            return cloned;
        }
        case TypeKind::Slice: {
            auto* s = static_cast<SliceType*>(t);
            Type* elem = rewrite(s->elementType);
            if (elem == s->elementType) return t;
            return TypeContext::instance().getSliceType(elem);
        }
        case TypeKind::Optional: {
            auto* o = static_cast<OptionalType*>(t);
            Type* elem = rewrite(o->elementType);
            if (elem == o->elementType) return t;
            return TypeContext::instance().getOptionalType(elem);
        }
        case TypeKind::Result: {
            auto* r = static_cast<ResultType*>(t);
            Type* ok = rewrite(r->successType);
            Type* err = rewrite(r->errorType);
            if (ok == r->successType && err == r->errorType) return t;
            return TypeContext::instance().getResultType(ok, err);
        }
        case TypeKind::Typedef: {
            auto* td = static_cast<TypedefType*>(t);
            Type* aliased = rewrite(td->aliasedType);
            if (aliased == td->aliasedType) return t;
            return new TypedefType(td->name, aliased);
        }
        case TypeKind::TypeInstance: {
            // 嵌套/自引用使用点：交 Registry 解析（缓存/递归检测/深度上限）。
            // 解析产物是按实例名注册的 StructType 占位，sema visit 实例 decl
            // 时按 INH-01 的 reuse-registration 模式补全字段。
            return TemplateRegistry::instance().resolveInstance(
                static_cast<TypeInstanceType*>(t), m_typeArgs, m_valueArgs);
        }
        default:
            return t;
    }
}

// ---------- 表达式 ----------

std::unique_ptr<ExprAST> TemplateInstantiator::cloneExpr(const ExprAST* e) {
    if (!e) return nullptr;

    std::unique_ptr<ExprAST> out;
    if (auto* n = dynamic_cast<const NumberExprAST*>(e)) {
        out = std::make_unique<NumberExprAST>(n->value, n->literalKind);
    } else if (auto* f = dynamic_cast<const FloatExprAST*>(e)) {
        out = std::make_unique<FloatExprAST>(f->value, f->literalKind);
    } else if (auto* c = dynamic_cast<const CharExprAST*>(e)) {
        out = std::make_unique<CharExprAST>(c->value);
    } else if (auto* s = dynamic_cast<const StringExprAST*>(e)) {
        out = std::make_unique<StringExprAST>(s->value);
    } else if (auto* v = dynamic_cast<const VariableExprAST*>(e)) {
        auto cloned = std::make_unique<VariableExprAST>(v->name);
        cloned->isFunctionRef = v->isFunctionRef;
        cloned->resolvedFunctionName = v->resolvedFunctionName;
        cloned->isEnumConstant = v->isEnumConstant;
        cloned->enumValue = v->enumValue;
        out = std::move(cloned);
    } else if (auto* b = dynamic_cast<const BinaryExprAST*>(e)) {
        out = std::make_unique<BinaryExprAST>(b->op, cloneExpr(b->left.get()),
                                              cloneExpr(b->right.get()));
        static_cast<BinaryExprAST*>(out.get())->mangledCallee = b->mangledCallee;
    } else if (auto* u = dynamic_cast<const UnaryExprAST*>(e)) {
        out = std::make_unique<UnaryExprAST>(u->op, cloneExpr(u->operand.get()));
    } else if (auto* call = dynamic_cast<const CallExprAST*>(e)) {
        std::vector<std::unique_ptr<ExprAST>> args;
        for (auto& a : call->args) args.push_back(cloneExpr(a.get()));
        auto cloned = std::make_unique<CallExprAST>(call->callee, std::move(args));
        cloned->isIndirect = call->isIndirect;
        cloned->isPrint = call->isPrint;
        cloned->printNewline = call->printNewline;
        cloned->printCFormat = call->printCFormat;
        cloned->printArgKinds = call->printArgKinds;
        cloned->isAssert = call->isAssert;
        cloned->isPanic = call->isPanic;
        for (auto* pt : call->resolvedParamTypes)
            cloned->resolvedParamTypes.push_back(rewrite(pt));
        out = std::move(cloned);
    } else if (auto* as = dynamic_cast<const AssignmentExprAST*>(e)) {
        out = std::make_unique<AssignmentExprAST>(as->op, cloneExpr(as->lhs.get()),
                                                  cloneExpr(as->rhs.get()));
    } else if (auto* te = dynamic_cast<const TernaryExprAST*>(e)) {
        out = std::make_unique<TernaryExprAST>(cloneExpr(te->cond.get()),
                                               cloneExpr(te->then.get()),
                                               cloneExpr(te->elseExpr.get()));
    } else if (auto* cast = dynamic_cast<const CastExprAST*>(e)) {
        out = std::make_unique<CastExprAST>(rewrite(cast->castType),
                                            cloneExpr(cast->expr.get()), cast->castKind);
    } else if (auto* comma = dynamic_cast<const CommaExprAST*>(e)) {
        out = std::make_unique<CommaExprAST>(cloneExpr(comma->left.get()),
                                             cloneExpr(comma->right.get()));
    } else if (auto* inc = dynamic_cast<const PostfixIncDecExprAST*>(e)) {
        out = std::make_unique<PostfixIncDecExprAST>(cloneExpr(inc->operand.get()),
                                                     inc->isIncrement);
    } else if (auto* aa = dynamic_cast<const ArrayAccessExprAST*>(e)) {
        out = std::make_unique<ArrayAccessExprAST>(cloneExpr(aa->array.get()),
                                                   cloneExpr(aa->index.get()));
    } else if (auto* ma = dynamic_cast<const MemberAccessExprAST*>(e)) {
        out = std::make_unique<MemberAccessExprAST>(ma->accessKind,
                                                    cloneExpr(ma->object.get()),
                                                    ma->memberName);
    } else if (auto* mc = dynamic_cast<const MethodCallExprAST*>(e)) {
        std::vector<std::unique_ptr<ExprAST>> args;
        for (auto& a : mc->args) args.push_back(cloneExpr(a.get()));
        auto cloned = std::make_unique<MethodCallExprAST>(cloneExpr(mc->object.get()),
                                                          mc->methodName, std::move(args));
        for (auto* pt : mc->resolvedParamTypes)
            cloned->resolvedParamTypes.push_back(rewrite(pt));
        out = std::move(cloned);
    } else if (auto* sz = dynamic_cast<const SizeofExprAST*>(e)) {
        out = std::make_unique<SizeofExprAST>(rewrite(sz->sizeofType),
                                              cloneExpr(sz->expr.get()));
    } else if (auto* il = dynamic_cast<const InitializerListExprAST*>(e)) {
        std::vector<std::unique_ptr<ExprAST>> inits;
        for (auto& i : il->initializers) inits.push_back(cloneExpr(i.get()));
        out = std::make_unique<InitializerListExprAST>(std::move(inits));
    } else {
        // 未知节点：返回空——调用方判空并在 sema 报内部错误。
        return nullptr;
    }

    out->type = e->type ? rewrite(e->type) : nullptr;
    out->isLValue = e->isLValue;
    copyLoc(out.get(), *e);
    return out;
}

// ---------- 语句 ----------

std::unique_ptr<StmtAST> TemplateInstantiator::cloneStmt(const StmtAST* s) {
    if (!s) return nullptr;

    std::unique_ptr<StmtAST> out;
    if (auto* es = dynamic_cast<const ExprStmtAST*>(s)) {
        out = std::make_unique<ExprStmtAST>(cloneExpr(es->expr.get()));
    } else if (auto* cs = dynamic_cast<const CompoundStmtAST*>(s)) {
        std::vector<std::unique_ptr<StmtAST>> stmts;
        for (auto& st : cs->stmts) stmts.push_back(cloneStmt(st.get()));
        out = std::make_unique<CompoundStmtAST>(std::move(stmts));
    } else if (auto* rs = dynamic_cast<const ReturnStmtAST*>(s)) {
        out = std::make_unique<ReturnStmtAST>(cloneExpr(rs->value.get()));
    } else if (auto* is = dynamic_cast<const IfStmtAST*>(s)) {
        out = std::make_unique<IfStmtAST>(cloneExpr(is->cond.get()),
                                          cloneStmt(is->thenStmt.get()),
                                          cloneStmt(is->elseStmt.get()));
    } else if (auto* ws = dynamic_cast<const WhileStmtAST*>(s)) {
        out = std::make_unique<WhileStmtAST>(cloneExpr(ws->cond.get()),
                                             cloneStmt(ws->body.get()));
    } else if (auto* dw = dynamic_cast<const DoWhileStmtAST*>(s)) {
        out = std::make_unique<DoWhileStmtAST>(cloneExpr(dw->cond.get()),
                                               cloneStmt(dw->body.get()));
    } else if (auto* fs = dynamic_cast<const ForStmtAST*>(s)) {
        out = std::make_unique<ForStmtAST>(cloneStmt(fs->init.get()),
                                           cloneExpr(fs->cond.get()),
                                           cloneExpr(fs->inc.get()),
                                           cloneStmt(fs->body.get()));
    } else if (auto* sw = dynamic_cast<const SwitchStmtAST*>(s)) {
        std::vector<std::unique_ptr<StmtAST>> cases;
        std::vector<std::unique_ptr<ExprAST>> labels;
        for (auto& c : sw->cases) cases.push_back(cloneStmt(c.get()));
        for (auto& l : sw->caseLabels) labels.push_back(cloneExpr(l.get()));
        out = std::make_unique<SwitchStmtAST>(cloneExpr(sw->cond.get()), std::move(cases));
        static_cast<SwitchStmtAST*>(out.get())->caseLabels = std::move(labels);
    } else if (dynamic_cast<const BreakStmtAST*>(s)) {
        out = std::make_unique<BreakStmtAST>();
    } else if (dynamic_cast<const ContinueStmtAST*>(s)) {
        out = std::make_unique<ContinueStmtAST>();
    } else if (dynamic_cast<const NullStmtAST*>(s)) {
        out = std::make_unique<NullStmtAST>();
    } else if (auto* df = dynamic_cast<const DeferStmtAST*>(s)) {
        out = std::make_unique<DeferStmtAST>(cloneExpr(df->callExpr.get()));
    } else if (auto* ds = dynamic_cast<const DeclStmtAST*>(s)) {
        out = std::make_unique<DeclStmtAST>(cloneDeclInternal(*ds->decl));
    } else {
        return nullptr;
    }

    copyLoc(out.get(), *s);
    return out;
}

// ---------- 声明 ----------

std::unique_ptr<DeclAST> TemplateInstantiator::cloneDeclInternal(const DeclAST& decl) {
    if (auto* fn = dynamic_cast<const FunctionDeclAST*>(&decl)) {
        std::vector<std::unique_ptr<ParamDeclAST>> params;
        for (auto& p : fn->params)
            params.push_back(std::make_unique<ParamDeclAST>(p->name, rewrite(p->type)));
        std::unique_ptr<CompoundStmtAST> body(
            dynamic_cast<CompoundStmtAST*>(cloneStmt(fn->body.get()).release()));
        auto* cloned = new FunctionDeclAST(fn->name, rewrite(fn->returnType), params,
                                           body, fn->isConstexpr, fn->isVarArg);
        cloned->isStatic = fn->isStatic;
        copyLoc(cloned, *fn);
        return std::unique_ptr<DeclAST>(cloned);
    }
    if (auto* st = dynamic_cast<const StructDeclAST*>(&decl)) {
        std::vector<std::pair<std::string, Type*>> fields;
        for (auto& f : st->fields) fields.push_back({f.first, rewrite(f.second)});
        auto* cloned = new StructDeclAST(st->name, std::move(fields));
        for (auto& m : st->methods) {
            auto* mc = dynamic_cast<FunctionDeclAST*>(cloneDeclInternal(*m).release());
            cloned->methods.push_back(std::unique_ptr<FunctionDeclAST>(mc));
        }
        for (auto& sm : st->staticMembers) {
            auto* vc = dynamic_cast<VarDeclAST*>(cloneDeclInternal(*sm).release());
            cloned->staticMembers.push_back(std::unique_ptr<VarDeclAST>(vc));
        }
        for (auto& nt : st->nestedTypes)
            cloned->nestedTypes.push_back(cloneDeclInternal(*nt));
        cloned->bareName = st->bareName;
        cloned->baseClass = st->baseClass;
        cloned->memberAccess = st->memberAccess;
        cloned->isForwardDecl = st->isForwardDecl;
        cloned->isClassDecl = st->isClassDecl;
        copyLoc(cloned, *st);
        return std::unique_ptr<DeclAST>(cloned);
    }
    if (auto* vd = dynamic_cast<const VarDeclAST*>(&decl)) {
        auto* cloned = new VarDeclAST(vd->name, rewrite(vd->type),
                                      cloneExpr(vd->initExpr.get()), vd->isConstexpr);
        copyLoc(cloned, *vd);
        return std::unique_ptr<DeclAST>(cloned);
    }
    if (auto* ad = dynamic_cast<const ArrayDeclAST*>(&decl)) {
        auto* cloned = new ArrayDeclAST(ad->name, rewrite(ad->elementType), ad->size,
                                        cloneExpr(ad->initExpr.get()));
        copyLoc(cloned, *ad);
        return std::unique_ptr<DeclAST>(cloned);
    }
    if (auto* mv = dynamic_cast<const MultiVarDeclAST*>(&decl)) {
        std::vector<std::unique_ptr<DeclAST>> decls;
        for (auto& d : mv->decls) decls.push_back(cloneDeclInternal(*d));
        auto* cloned = new MultiVarDeclAST(std::move(decls));
        copyLoc(cloned, *mv);
        return std::unique_ptr<DeclAST>(cloned);
    }
    if (auto* ud = dynamic_cast<const UsingDeclAST*>(&decl)) {
        auto* cloned = new UsingDeclAST(ud->name, rewrite(ud->aliasedType));
        copyLoc(cloned, *ud);
        return std::unique_ptr<DeclAST>(cloned);
    }
    if (auto* td = dynamic_cast<const TypedefDeclAST*>(&decl)) {
        auto* cloned = new TypedefDeclAST(td->name, rewrite(td->aliasedType));
        copyLoc(cloned, *td);
        return std::unique_ptr<DeclAST>(cloned);
    }
    // 评审 I4：嵌套 union/enum/type 声明此前被静默丢弃。
    if (auto* un = dynamic_cast<const UnionDeclAST*>(&decl)) {
        std::vector<std::pair<std::string, Type*>> members;
        for (auto& m : un->members) members.push_back({m.first, rewrite(m.second)});
        auto* cloned = new UnionDeclAST(un->name, std::move(members));
        cloned->bareName = un->bareName;
        cloned->isForwardDecl = un->isForwardDecl;
        for (auto& nt : un->nestedTypes) cloned->nestedTypes.push_back(cloneDeclInternal(*nt));
        copyLoc(cloned, *un);
        return std::unique_ptr<DeclAST>(cloned);
    }
    if (auto* ed = dynamic_cast<const EnumDeclAST*>(&decl)) {
        auto* cloned = new EnumDeclAST(ed->name, ed->values, rewrite(ed->underlyingType));
        cloned->bareName = ed->bareName;
        cloned->isForwardDecl = ed->isForwardDecl;
        copyLoc(cloned, *ed);
        return std::unique_ptr<DeclAST>(cloned);
    }
    if (auto* ty = dynamic_cast<const TypeDeclAST*>(&decl)) {
        auto* cloned = new TypeDeclAST(ty->name, rewrite(ty->aliasedType));
        copyLoc(cloned, *ty);
        return std::unique_ptr<DeclAST>(cloned);
    }
    if (auto* ns = dynamic_cast<const NamespaceDeclAST*>(&decl)) {
        std::vector<std::unique_ptr<DeclAST>> decls;
        for (auto& d : ns->declarations) decls.push_back(cloneDeclInternal(*d));
        auto* cloned = new NamespaceDeclAST(ns->name, std::move(decls));
        copyLoc(cloned, *ns);
        return std::unique_ptr<DeclAST>(cloned);
    }
    return nullptr;
}

std::unique_ptr<DeclAST> TemplateInstantiator::cloneDecl(const DeclAST& decl) {
    return cloneDeclInternal(decl);
}

#pragma once

// P1-05 / ANN-01: 注解值类型（spec 2026-10-07-annotations §2）。
// `[[name]]` / `[[name(arg, ...)]]`；实参按注解名解释（align→Expr 折叠、
// deprecated→String、repr→Ident "C"）。Annotation 非节点——值语义直挂
// DeclAST/ParamDeclAST/FieldInfo。

#include <memory>
#include <string>
#include <vector>

#include "Expr.h"

class Type;

struct AnnotationArg {
    enum class Kind { Expr, Type, Ident, String };
    Kind kind;
    std::unique_ptr<ExprAST> expr; // Kind::Expr
    Type* type{};                  // Kind::Type
    std::string text;              // Kind::Ident / Kind::String
    int line{0};
    int column{0};
};

struct Annotation {
    std::string name;
    std::vector<AnnotationArg> args;
    int line{0};
    int column{0};
};

// P1-05 / ANN-01 / T2: 聚合字段统一载体（StructDeclAST::fields /
// UnionType::members），字段级注解挂这里。
struct FieldInfo {
    std::string name;
    Type* type{};
    std::vector<Annotation> annotations;
};

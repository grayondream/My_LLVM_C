#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <vector>

class ASTNode {
public:
    virtual ~ASTNode() = default;
};

//表达式
class ExprAST : public ASTNode {
public:
    
};

//语句
class StmtAST : public ASTNode {
public:
    
};

//声明
class DeclAST : public ASTNode {
public:
    
};

class IntegerExprAST : public ExprAST {
public:
    IntegerExprAST(int value) : m_value(value) {}

public:
    int m_value;
};

class VariableExprAST : public ExprAST {
public:
    VariableExprAST(const std::string& name) : m_name(name) {}

public:
    std::string m_name;
};

class BinaryExprAST : public ExprAST {
public:
public:
    std::string op;
    std::unique_ptr<ExprAST> left;
    std::unique_ptr<ExprAST> right;    
};

class UnaryExprAST : public ExprAST {
public:
public:
    std::string op;
    std::unique_ptr<ExprAST> arg;
};

class CallExprAST : public ExprAST {
public:
public:
    std::string callee;
    std::vector<std::unique_ptr<ExprAST>> args;
};  

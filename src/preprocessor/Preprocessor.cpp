#include "preprocessor/Preprocessor.h"
#include <algorithm>
#include <fstream>
#include <sstream>

Preprocessor::Preprocessor() = default;

void Preprocessor::addIncludePath(const std::string& path) {
    includePaths.push_back(path);
}

static bool isPreprocessorDirective(const std::string& name) {
    return name == "define" || name == "undef" || name == "ifdef" ||
           name == "ifndef" || name == "elif" || name == "else" || name == "endif";
}

std::vector<Token> Preprocessor::preprocess(Lexer& lexer) {
    std::vector<Token> output;
    std::vector<Token> tokens = lexer.tokenize();
    
    bool active = true;
    std::vector<bool> conditionStack;
    size_t i = 0;
    
    while (i < tokens.size()) {
        Token& tok = tokens[i];
        
        if (tok.type == TokenType::TOKEN_HASH) {
            size_t directiveLine = tok.line;
            i++;
            if (i >= tokens.size()) break;
            
            Token& directive = tokens[i];
            if (directive.type != TokenType::TOKEN_IDENTIFIER || !isPreprocessorDirective(directive.lexeme)) {
                continue;
            }
            
            std::string dir = directive.lexeme;
            
            if (dir == "define") {
                i++;
                if (!active) {
                    while (i < tokens.size() && tokens[i].line == directiveLine) i++;
                    continue;
                }
                
                if (i < tokens.size() && tokens[i].type == TokenType::TOKEN_IDENTIFIER) {
                    Macro m;
                    m.name = tokens[i].lexeme;
                    i++;
                    
                    if (i < tokens.size() && tokens[i].type == TokenType::TOKEN_LPAREN) {
                        i++;
                        while (i < tokens.size() && tokens[i].type != TokenType::TOKEN_RPAREN) {
                            if (tokens[i].type == TokenType::TOKEN_IDENTIFIER) {
                                m.params.push_back(tokens[i].lexeme);
                            }
                            i++;
                        }
                        if (i < tokens.size()) i++;
                    }
                    
                    while (i < tokens.size() && tokens[i].line == directiveLine) {
                        m.body.push_back(tokens[i]);
                        i++;
                    }
                    
                    macros.define(m);
                }
            } else if (dir == "undef") {
                i++;
                if (!active) {
                    while (i < tokens.size() && tokens[i].line == directiveLine) i++;
                    continue;
                }
                
                if (i < tokens.size() && tokens[i].type == TokenType::TOKEN_IDENTIFIER) {
                    macros.undefine(tokens[i].lexeme);
                    i++;
                }
            } else if (dir == "ifdef") {
                i++;
                if (i < tokens.size() && tokens[i].type == TokenType::TOKEN_IDENTIFIER) {
                    conditionStack.push_back(active);
                    active = active && macros.isDefined(tokens[i].lexeme);
                    i++;
                }
            } else if (dir == "ifndef") {
                i++;
                if (i < tokens.size() && tokens[i].type == TokenType::TOKEN_IDENTIFIER) {
                    conditionStack.push_back(active);
                    active = active && !macros.isDefined(tokens[i].lexeme);
                    i++;
                }
            } else if (dir == "elif") {
                i++;
                if (!conditionStack.empty()) {
                    bool parentActive = conditionStack.back();
                    if (parentActive && !active) {
                        if (i < tokens.size() && tokens[i].type == TokenType::TOKEN_IDENTIFIER) {
                            active = macros.isDefined(tokens[i].lexeme);
                            i++;
                        }
                    } else {
                        active = false;
                        while (i < tokens.size() && tokens[i].line == directiveLine) i++;
                    }
                }
            } else if (dir == "else") {
                i++;
                if (!conditionStack.empty()) {
                    bool parentActive = conditionStack.back();
                    active = parentActive && !active;
                }
            } else if (dir == "endif") {
                i++;
                if (!conditionStack.empty()) {
                    active = conditionStack.back();
                    conditionStack.pop_back();
                }
            } else {
                i++;
            }
            
            continue;
        }
        
        if (active) {
            if (tok.type == TokenType::TOKEN_IDENTIFIER && macros.isDefined(tok.lexeme)) {
                const Macro* m = macros.lookup(tok.lexeme);
                if (m && m->params.empty()) {
                    for (const auto& bodyTok : m->body) {
                        output.push_back(bodyTok);
                    }
                } else {
                    output.push_back(tok);
                }
            } else {
                output.push_back(tok);
            }
        }
        
        i++;
    }
    
    return output;
}

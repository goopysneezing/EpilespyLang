#pragma once
#include <string>
#include <vector>
#include <memory>
#include "value.hpp"

#ifdef TRUE
#undef TRUE
#endif
#ifdef FALSE
#undef FALSE
#endif

// Redirect TokenType to EpTokenType to avoid collisions with Windows SDK
#define TokenType EpTokenType

// Token types for EpilespyLang
enum class TokenType {
    // Single-character tokens
    LEFT_PAREN, RIGHT_PAREN, LEFT_BRACE, RIGHT_BRACE,
    LEFT_BRACKET, RIGHT_BRACKET,
    COMMA, DOT, MINUS, PLUS, SLASH, STAR, PERCENT, COLON, SEMICOLON,
    
    // One or two character tokens
    BANG, BANG_EQUAL,
    EQUAL, EQUAL_EQUAL,
    GREATER, GREATER_EQUAL,
    LESS, LESS_EQUAL,
    
    // Literals
    IDENTIFIER, STRING, NUMBER,
    
    // Keywords
    AND, OR, NOT,
    IF, ELSE, WHILE, FOR, SWITCH, CASE, DEFAULT,
    TRUE, FALSE, NULL_VAL,
    QUANTUM, QBREAK, QKILLOTHERS,
    
    // Special
    NEWLINE, EOF_VAL
};

inline std::string tokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::LEFT_PAREN: return "(";
        case TokenType::RIGHT_PAREN: return ")";
        case TokenType::LEFT_BRACE: return "{";
        case TokenType::RIGHT_BRACE: return "}";
        case TokenType::LEFT_BRACKET: return "[";
        case TokenType::RIGHT_BRACKET: return "]";
        case TokenType::COMMA: return ",";
        case TokenType::DOT: return ".";
        case TokenType::MINUS: return "-";
        case TokenType::PLUS: return "+";
        case TokenType::SLASH: return "/";
        case TokenType::STAR: return "*";
        case TokenType::PERCENT: return "%";
        case TokenType::COLON: return ":";
        case TokenType::SEMICOLON: return ";";
        case TokenType::BANG: return "!";
        case TokenType::BANG_EQUAL: return "!=";
        case TokenType::EQUAL: return "=";
        case TokenType::EQUAL_EQUAL: return "==";
        case TokenType::GREATER: return ">";
        case TokenType::GREATER_EQUAL: return ">=";
        case TokenType::LESS: return "<";
        case TokenType::LESS_EQUAL: return "<=";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::STRING: return "STRING";
        case TokenType::NUMBER: return "NUMBER";
        case TokenType::AND: return "and";
        case TokenType::OR: return "or";
        case TokenType::NOT: return "not";
        case TokenType::IF: return "if";
        case TokenType::ELSE: return "else";
        case TokenType::WHILE: return "while";
        case TokenType::FOR: return "for";
        case TokenType::SWITCH: return "switch";
        case TokenType::CASE: return "case";
        case TokenType::DEFAULT: return "default";
        case TokenType::TRUE: return "true";
        case TokenType::FALSE: return "false";
        case TokenType::NULL_VAL: return "null";
        case TokenType::QUANTUM: return "quantum";
        case TokenType::QBREAK: return "qbreak";
        case TokenType::QKILLOTHERS: return "qkillothers";
        case TokenType::NEWLINE: return "NEWLINE";
        case TokenType::EOF_VAL: return "EOF";
        default: return "UNKNOWN";
    }
}

struct Token {
    TokenType type;
    std::string lexeme;
    Value literal;
    int line;
};

// Forward declaration of expressions
class LiteralExpr;
class VariableExpr;
class AssignmentExpr;
class BinaryExpr;
class UnaryExpr;
class CallExpr;
class ArrayExpr;
class IndexExpr;
class IndexAssignmentExpr;
class MethodCallExpr;
class PropertyAccessExpr;

class ExprVisitor {
public:
    virtual ~ExprVisitor() = default;
    virtual Value visitLiteralExpr(LiteralExpr* expr) = 0;
    virtual Value visitVariableExpr(VariableExpr* expr) = 0;
    virtual Value visitAssignmentExpr(AssignmentExpr* expr) = 0;
    virtual Value visitBinaryExpr(BinaryExpr* expr) = 0;
    virtual Value visitUnaryExpr(UnaryExpr* expr) = 0;
    virtual Value visitCallExpr(CallExpr* expr) = 0;
    virtual Value visitArrayExpr(ArrayExpr* expr) = 0;
    virtual Value visitIndexExpr(IndexExpr* expr) = 0;
    virtual Value visitIndexAssignmentExpr(IndexAssignmentExpr* expr) = 0;
    virtual Value visitMethodCallExpr(MethodCallExpr* expr) = 0;
    virtual Value visitPropertyAccessExpr(PropertyAccessExpr* expr) = 0;
};

class Expr {
public:
    virtual ~Expr() = default;
    virtual Value accept(ExprVisitor* visitor) = 0;
};

using ExprPtr = std::shared_ptr<Expr>;

class LiteralExpr : public Expr {
public:
    Value value;
    LiteralExpr(Value value) : value(value) {}
    Value accept(ExprVisitor* visitor) override { return visitor->visitLiteralExpr(this); }
};

class VariableExpr : public Expr {
public:
    Token name;
    VariableExpr(Token name) : name(name) {}
    Value accept(ExprVisitor* visitor) override { return visitor->visitVariableExpr(this); }
};

class AssignmentExpr : public Expr {
public:
    Token name;
    ExprPtr value;
    AssignmentExpr(Token name, ExprPtr value) : name(name), value(value) {}
    Value accept(ExprVisitor* visitor) override { return visitor->visitAssignmentExpr(this); }
};

class BinaryExpr : public Expr {
public:
    ExprPtr left;
    Token op;
    ExprPtr right;
    BinaryExpr(ExprPtr left, Token op, ExprPtr right) : left(left), op(op), right(right) {}
    Value accept(ExprVisitor* visitor) override { return visitor->visitBinaryExpr(this); }
};

class UnaryExpr : public Expr {
public:
    Token op;
    ExprPtr right;
    UnaryExpr(Token op, ExprPtr right) : op(op), right(right) {}
    Value accept(ExprVisitor* visitor) override { return visitor->visitUnaryExpr(this); }
};

class CallExpr : public Expr {
public:
    Token callee; // For error reporting
    std::vector<ExprPtr> arguments;
    CallExpr(Token callee, std::vector<ExprPtr> arguments) : callee(callee), arguments(arguments) {}
    Value accept(ExprVisitor* visitor) override { return visitor->visitCallExpr(this); }
};

class ArrayExpr : public Expr {
public:
    Token bracket; // Opening bracket for line reference
    std::vector<ExprPtr> elements;
    ArrayExpr(Token bracket, std::vector<ExprPtr> elements) : bracket(bracket), elements(elements) {}
    Value accept(ExprVisitor* visitor) override { return visitor->visitArrayExpr(this); }
};

class IndexExpr : public Expr {
public:
    ExprPtr array;
    Token bracket; // Opening bracket for line reference
    ExprPtr index;
    IndexExpr(ExprPtr array, Token bracket, ExprPtr index) : array(array), bracket(bracket), index(index) {}
    Value accept(ExprVisitor* visitor) override { return visitor->visitIndexExpr(this); }
};

class IndexAssignmentExpr : public Expr {
public:
    ExprPtr array;
    Token bracket; // For line reference
    ExprPtr index;
    ExprPtr value;
    IndexAssignmentExpr(ExprPtr array, Token bracket, ExprPtr index, ExprPtr value)
        : array(array), bracket(bracket), index(index), value(value) {}
    Value accept(ExprVisitor* visitor) override { return visitor->visitIndexAssignmentExpr(this); }
};

class MethodCallExpr : public Expr {
public:
    ExprPtr object;
    Token method;
    std::vector<ExprPtr> arguments;
    MethodCallExpr(ExprPtr object, Token method, std::vector<ExprPtr> arguments)
        : object(object), method(method), arguments(arguments) {}
    Value accept(ExprVisitor* visitor) override { return visitor->visitMethodCallExpr(this); }
};

class PropertyAccessExpr : public Expr {
public:
    ExprPtr object;
    Token property;
    PropertyAccessExpr(ExprPtr object, Token property)
        : object(object), property(property) {}
    Value accept(ExprVisitor* visitor) override { return visitor->visitPropertyAccessExpr(this); }
};

// Forward declaration of statements
class ExpressionStmt;
class BlockStmt;
class IfStmt;
class WhileStmt;
class ForStmt;
class SwitchStmt;
class QuantumStmt;
class QbreakStmt;
class QkillothersStmt;

class StmtVisitor {
public:
    virtual ~StmtVisitor() = default;
    virtual void visitExpressionStmt(ExpressionStmt* stmt) = 0;
    virtual void visitBlockStmt(BlockStmt* stmt) = 0;
    virtual void visitIfStmt(IfStmt* stmt) = 0;
    virtual void visitWhileStmt(WhileStmt* stmt) = 0;
    virtual void visitForStmt(ForStmt* stmt) = 0;
    virtual void visitSwitchStmt(SwitchStmt* stmt) = 0;
    virtual void visitQuantumStmt(QuantumStmt* stmt) = 0;
    virtual void visitQbreakStmt(QbreakStmt* stmt) = 0;
    virtual void visitQkillothersStmt(QkillothersStmt* stmt) = 0;
};

class Stmt {
public:
    int line = 0;
    virtual ~Stmt() = default;
    virtual void accept(StmtVisitor* visitor) = 0;
};

using StmtPtr = std::shared_ptr<Stmt>;

class ExpressionStmt : public Stmt {
public:
    ExprPtr expression;
    ExpressionStmt(ExprPtr expression) : expression(expression) {}
    void accept(StmtVisitor* visitor) override { visitor->visitExpressionStmt(this); }
};

class BlockStmt : public Stmt {
public:
    std::vector<StmtPtr> statements;
    BlockStmt(std::vector<StmtPtr> statements) : statements(statements) {}
    void accept(StmtVisitor* visitor) override { visitor->visitBlockStmt(this); }
};

class IfStmt : public Stmt {
public:
    ExprPtr condition;
    StmtPtr thenBranch;
    StmtPtr elseBranch; // can be nullptr
    IfStmt(ExprPtr condition, StmtPtr thenBranch, StmtPtr elseBranch)
        : condition(condition), thenBranch(thenBranch), elseBranch(elseBranch) {}
    void accept(StmtVisitor* visitor) override { visitor->visitIfStmt(this); }
};

class WhileStmt : public Stmt {
public:
    ExprPtr condition;
    StmtPtr body;
    WhileStmt(ExprPtr condition, StmtPtr body) : condition(condition), body(body) {}
    void accept(StmtVisitor* visitor) override { visitor->visitWhileStmt(this); }
};

class ForStmt : public Stmt {
public:
    StmtPtr init; // initialization (can be expression statement or nullptr)
    ExprPtr condition; // condition (can be nullptr)
    ExprPtr increment; // increment (can be nullptr)
    StmtPtr body;
    ForStmt(StmtPtr init, ExprPtr condition, ExprPtr increment, StmtPtr body)
        : init(init), condition(condition), increment(increment), body(body) {}
    void accept(StmtVisitor* visitor) override { visitor->visitForStmt(this); }
};

struct SwitchCase {
    ExprPtr value; // case value expression
    StmtPtr body;  // case body statement
};

class SwitchStmt : public Stmt {
public:
    ExprPtr expression;
    std::vector<SwitchCase> cases;
    StmtPtr defaultBranch; // can be nullptr
    Token switchToken; // for error reporting/line number
    SwitchStmt(Token switchToken, ExprPtr expression, std::vector<SwitchCase> cases, StmtPtr defaultBranch)
        : switchToken(switchToken), expression(expression), cases(cases), defaultBranch(defaultBranch) {}
    void accept(StmtVisitor* visitor) override { visitor->visitSwitchStmt(this); }
};

class QuantumStmt : public Stmt {
public:
    Token name;
    ExprPtr choices;
    std::vector<StmtPtr> body;
    QuantumStmt(Token name, ExprPtr choices, std::vector<StmtPtr> body)
        : name(name), choices(choices), body(body) {}
    void accept(StmtVisitor* visitor) override { visitor->visitQuantumStmt(this); }
};

class QbreakStmt : public Stmt {
public:
    Token token;
    QbreakStmt(Token token) : token(token) {}
    void accept(StmtVisitor* visitor) override { visitor->visitQbreakStmt(this); }
};

class QkillothersStmt : public Stmt {
public:
    Token token;
    QkillothersStmt(Token token) : token(token) {}
    void accept(StmtVisitor* visitor) override { visitor->visitQkillothersStmt(this); }
};

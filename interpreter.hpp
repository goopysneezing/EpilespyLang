#pragma once
#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <cmath>
#include "ast.hpp"
#include "value.hpp"
#include "environment.hpp"

class Interpreter : public ExprVisitor, public StmtVisitor {
private:
    std::shared_ptr<Environment> environment = std::make_shared<Environment>();

    void checkNumberOperands(Token op, const Value& left, const Value& right) {
        if (left.isNumber() && right.isNumber()) return;
        throw RuntimeError(op.line, "Operands must be numbers.");
    }

public:
    Interpreter() = default;

    std::shared_ptr<Environment> getGlobals() const {
        return environment;
    }

    Value evaluate(ExprPtr expr) {
        return expr->accept(this);
    }

    void execute(StmtPtr stmt) {
        stmt->accept(this);
    }

    void executeBlock(const std::vector<StmtPtr>& statements, std::shared_ptr<Environment> env) {
        std::shared_ptr<Environment> previous = this->environment;
        try {
            this->environment = env;
            for (auto& statement : statements) {
                execute(statement);
            }
            this->environment = previous;
        } catch (...) {
            this->environment = previous;
            throw;
        }
    }

    void interpret(const std::vector<StmtPtr>& statements) {
        for (auto& stmt : statements) {
            execute(stmt);
        }
    }

    // ExprVisitor implementation
    Value visitLiteralExpr(LiteralExpr* expr) override {
        return expr->value;
    }

    Value visitVariableExpr(VariableExpr* expr) override {
        bool found = false;
        Value val = environment->get(expr->name.lexeme, found);
        if (!found) {
            throw RuntimeError(expr->name.line, "Undefined variable '" + expr->name.lexeme + "'.");
        }
        return val;
    }

    Value visitAssignmentExpr(AssignmentExpr* expr) override {
        Value val = evaluate(expr->value);
        bool updated = environment->assign(expr->name.lexeme, val);
        if (!updated) {
            environment->define(expr->name.lexeme, val);
        }
        return val;
    }

    Value visitBinaryExpr(BinaryExpr* expr) override {
        Value left = evaluate(expr->left);

        if (expr->op.type == TokenType::AND) {
            if (!left.isTruthy()) return left;
            return evaluate(expr->right);
        }
        if (expr->op.type == TokenType::OR) {
            if (left.isTruthy()) return left;
            return evaluate(expr->right);
        }

        Value right = evaluate(expr->right);

        switch (expr->op.type) {
            case TokenType::PLUS:
                if (left.isNumber() && right.isNumber()) {
                    return left.asNumber() + right.asNumber();
                }
                if (left.isString() || right.isString()) {
                    return left.toString() + right.toString();
                }
                throw RuntimeError(expr->op.line, "Operands must be numbers or strings for '+'.");
                
            case TokenType::MINUS:
                checkNumberOperands(expr->op, left, right);
                return left.asNumber() - right.asNumber();
                
            case TokenType::STAR:
                checkNumberOperands(expr->op, left, right);
                return left.asNumber() * right.asNumber();
                
            case TokenType::SLASH:
                checkNumberOperands(expr->op, left, right);
                if (right.asNumber() == 0.0) {
                    throw RuntimeError(expr->op.line, "Division by zero.");
                }
                return left.asNumber() / right.asNumber();
                
            case TokenType::PERCENT:
                checkNumberOperands(expr->op, left, right);
                if (right.asNumber() == 0.0) {
                    throw RuntimeError(expr->op.line, "Modulo by zero.");
                }
                return std::fmod(left.asNumber(), right.asNumber());

            case TokenType::GREATER:
                checkNumberOperands(expr->op, left, right);
                return left.asNumber() > right.asNumber();
                
            case TokenType::GREATER_EQUAL:
                checkNumberOperands(expr->op, left, right);
                return left.asNumber() >= right.asNumber();
                
            case TokenType::LESS:
                checkNumberOperands(expr->op, left, right);
                return left.asNumber() < right.asNumber();
                
            case TokenType::LESS_EQUAL:
                checkNumberOperands(expr->op, left, right);
                return left.asNumber() <= right.asNumber();

            case TokenType::EQUAL_EQUAL:
                return valuesEqual(left, right);
                
            case TokenType::BANG_EQUAL:
                return !valuesEqual(left, right);

            default:
                break;
        }

        throw RuntimeError(expr->op.line, "Unknown binary operator.");
    }

    Value visitUnaryExpr(UnaryExpr* expr) override {
        Value right = evaluate(expr->right);
        switch (expr->op.type) {
            case TokenType::MINUS:
                if (!right.isNumber()) {
                    throw RuntimeError(expr->op.line, "Operand must be a number for unary '-'.");
                }
                return -right.asNumber();
            case TokenType::NOT:
            case TokenType::BANG:
                return !right.isTruthy();
            default:
                break;
        }
        throw RuntimeError(expr->op.line, "Unknown unary operator.");
    }

    Value visitCallExpr(CallExpr* expr) override {
        std::string calleeName = expr->callee.lexeme;
        std::vector<Value> args;
        for (auto& argExpr : expr->arguments) {
            args.push_back(evaluate(argExpr));
        }

        if (calleeName == "print") {
            for (size_t i = 0; i < args.size(); ++i) {
                if (i > 0) std::cout << " ";
                std::cout << args[i].toString();
            }
            std::cout << "\n";
            return Value();
        }
        
        if (calleeName == "input") {
            if (args.size() > 1) {
                throw RuntimeError(expr->callee.line, "input() expects at most 1 argument.");
            }
            if (args.size() == 1) {
                std::cout << args[0].toString();
                std::cout.flush();
            }
            std::string inputStr;
            if (std::getline(std::cin, inputStr)) {
                return Value(inputStr);
            }
            return Value("");
        }
        
        if (calleeName == "len") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "len() expects exactly 1 argument.");
            }
            Value val = args[0];
            if (val.isString()) {
                return Value(static_cast<double>(val.asString().length()));
            }
            if (val.isArray()) {
                return Value(static_cast<double>(val.asArray()->size()));
            }
            throw RuntimeError(expr->callee.line, "len() argument must be string or array.");
        }

        if (calleeName == "num") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "num() expects exactly 1 argument.");
            }
            Value val = args[0];
            if (val.isNumber()) return val;
            if (val.isBool()) return val.asBool() ? 1.0 : 0.0;
            if (val.isString()) {
                try {
                    return Value(std::stod(val.asString()));
                } catch (...) {
                    return Value(0.0);
                }
            }
            return Value(0.0);
        }

        if (calleeName == "str") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "str() expects exactly 1 argument.");
            }
            return Value(args[0].toString());
        }

        if (calleeName == "push") {
            if (args.size() != 2) {
                throw RuntimeError(expr->callee.line, "push() expects exactly 2 arguments (array, value).");
            }
            Value arrVal = args[0];
            if (!arrVal.isArray()) {
                throw RuntimeError(expr->callee.line, "First argument of push() must be an array.");
            }
            arrVal.asArray()->push_back(args[1]);
            return arrVal;
        }

        if (calleeName == "pop") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "pop() expects exactly 1 argument (array).");
            }
            Value arrVal = args[0];
            if (!arrVal.isArray()) {
                throw RuntimeError(expr->callee.line, "Argument of pop() must be an array.");
            }
            auto arr = arrVal.asArray();
            if (arr->empty()) {
                throw RuntimeError(expr->callee.line, "pop() from empty array.");
            }
            Value popped = arr->back();
            arr->pop_back();
            return popped;
        }

        if (calleeName == "type") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "type() expects exactly 1 argument.");
            }
            return Value(args[0].typeString());
        }

        throw RuntimeError(expr->callee.line, "Undefined function '" + calleeName + "'.");
    }

    Value visitArrayExpr(ArrayExpr* expr) override {
        auto elements = std::make_shared<std::vector<Value>>();
        for (auto& elemExpr : expr->elements) {
            elements->push_back(evaluate(elemExpr));
        }
        return Value(elements);
    }

    Value visitIndexExpr(IndexExpr* expr) override {
        Value target = evaluate(expr->array);
        Value indexVal = evaluate(expr->index);

        if (!indexVal.isNumber()) {
            throw RuntimeError(expr->bracket.line, "Index must be a number.");
        }
        double dIdx = indexVal.asNumber();
        if (std::floor(dIdx) != dIdx) {
            throw RuntimeError(expr->bracket.line, "Index must be an integer.");
        }
        int idx = static_cast<int>(dIdx);

        if (target.isArray()) {
            auto arr = target.asArray();
            if (idx < 0 || idx >= static_cast<int>(arr->size())) {
                throw RuntimeError(expr->bracket.line, "Array index " + std::to_string(idx) + " out of bounds (size " + std::to_string(arr->size()) + ").");
            }
            return (*arr)[idx];
        }
        if (target.isString()) {
            const std::string& str = target.asString();
            if (idx < 0 || idx >= static_cast<int>(str.length())) {
                throw RuntimeError(expr->bracket.line, "String index " + std::to_string(idx) + " out of bounds (length " + std::to_string(str.length()) + ").");
            }
            return Value(std::string(1, str[idx]));
        }

        throw RuntimeError(expr->bracket.line, "Only arrays and strings can be indexed.");
    }

    Value visitIndexAssignmentExpr(IndexAssignmentExpr* expr) override {
        Value target = evaluate(expr->array);
        Value indexVal = evaluate(expr->index);
        Value newVal = evaluate(expr->value);

        if (!target.isArray()) {
            throw RuntimeError(expr->bracket.line, "Only arrays support index assignment.");
        }

        if (!indexVal.isNumber()) {
            throw RuntimeError(expr->bracket.line, "Index must be a number.");
        }
        double dIdx = indexVal.asNumber();
        if (std::floor(dIdx) != dIdx) {
            throw RuntimeError(expr->bracket.line, "Index must be an integer.");
        }
        int idx = static_cast<int>(dIdx);

        auto arr = target.asArray();
        if (idx < 0 || idx >= static_cast<int>(arr->size())) {
            throw RuntimeError(expr->bracket.line, "Array index " + std::to_string(idx) + " out of bounds (size " + std::to_string(arr->size()) + ").");
        }

        (*arr)[idx] = newVal;
        return newVal;
    }

    // StmtVisitor implementation
    void visitExpressionStmt(ExpressionStmt* stmt) override {
        evaluate(stmt->expression);
    }

    void visitBlockStmt(BlockStmt* stmt) override {
        executeBlock(stmt->statements, std::make_shared<Environment>(environment));
    }

    void visitIfStmt(IfStmt* stmt) override {
        if (evaluate(stmt->condition).isTruthy()) {
            execute(stmt->thenBranch);
        } else if (stmt->elseBranch != nullptr) {
            execute(stmt->elseBranch);
        }
    }

    void visitWhileStmt(WhileStmt* stmt) override {
        while (evaluate(stmt->condition).isTruthy()) {
            execute(stmt->body);
        }
    }

    void visitForStmt(ForStmt* stmt) override {
        std::shared_ptr<Environment> previousEnv = this->environment;
        std::shared_ptr<Environment> loopEnv = std::make_shared<Environment>(environment);
        
        try {
            this->environment = loopEnv;
            if (stmt->init != nullptr) {
                execute(stmt->init);
            }
            
            while (true) {
                if (stmt->condition != nullptr) {
                    if (!evaluate(stmt->condition).isTruthy()) break;
                }
                
                execute(stmt->body);
                
                if (stmt->increment != nullptr) {
                    evaluate(stmt->increment);
                }
            }
            this->environment = previousEnv;
        } catch (...) {
            this->environment = previousEnv;
            throw;
        }
    }

    void visitSwitchStmt(SwitchStmt* stmt) override {
        Value switchVal = evaluate(stmt->expression);
        bool matched = false;

        for (auto& caseClause : stmt->cases) {
            Value caseVal = evaluate(caseClause.value);
            if (valuesEqual(switchVal, caseVal)) {
                execute(caseClause.body);
                matched = true;
                break;
            }
        }

        if (!matched && stmt->defaultBranch != nullptr) {
            execute(stmt->defaultBranch);
        }
    }
};

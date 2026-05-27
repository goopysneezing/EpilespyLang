#pragma once
#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <cmath>
#include <random>
#include <algorithm>
#include <thread>
#include <chrono>
#include "ast.hpp"
#include "value.hpp"
#include "environment.hpp"
#include <wininet.h>
#include <fstream>
#include <sstream>

inline std::string fetchURL(const std::string& url) {
    HINTERNET hInternet = InternetOpenA("EpilespyLangUserAgent", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) {
        throw std::runtime_error("Failed to initialize internet connection.");
    }

    HINTERNET hConnect = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        throw std::runtime_error("Failed to open URL: " + url);
    }

    std::string result = "";
    char buffer[4096];
    DWORD bytesRead = 0;
    while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        result.append(buffer, bytesRead);
    }

    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return result;
}

inline std::string fetchLocalFile(const std::string& path) {
    std::string cleanPath = path;
    if (cleanPath.rfind("file://", 0) == 0) {
        cleanPath = cleanPath.substr(7);
        // On Windows, file:///C:/path might have a leading slash
        if (cleanPath.length() >= 3 && cleanPath[0] == '/' && cleanPath[2] == ':') {
            cleanPath = cleanPath.substr(1);
        }
    }
    std::ifstream file(cleanPath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open local file: " + cleanPath);
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

class Interpreter : public ExprVisitor, public StmtVisitor {
private:
    std::shared_ptr<Environment> environment = std::make_shared<Environment>();
    bool epilepsyState = false;
    std::mt19937 rng{static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count())};

    void checkNumberOperands(Token op, const Value& left, const Value& right) {
        if (left.isNumber() && right.isNumber()) return;
        throw RuntimeError(op.line, "Operands must be numbers.");
    }

public:
    Interpreter() {
        environment->define("pi", Value(3.14159265358979323846));
        environment->define("e", Value(2.71828182845904523536));
        environment->define("tau", Value(6.28318530717958647692));
        environment->define("sqrt2", Value(1.41421356237309504880));
        environment->define("ln2", Value(0.693147180559945309417));
        environment->define("ln10", Value(2.30258509299404568402));
    }

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

        if (calleeName == "window") {
            if (args.size() != 3) {
                throw RuntimeError(expr->callee.line, "window() expects exactly 3 arguments (title, width, height).");
            }
            if (!args[0].isString() || !args[1].isNumber() || !args[2].isNumber()) {
                throw RuntimeError(expr->callee.line, "window() arguments must be (string, number, number).");
            }
            std::string title = args[0].asString();
            int w = static_cast<int>(args[1].asNumber());
            int h = static_cast<int>(args[2].asNumber());
            
            auto win = std::make_shared<WindowInstance>(title, w, h);
            return Value(win);
        }

        if (calleeName == "gameWindow3D") {
            if (args.size() != 3) {
                throw RuntimeError(expr->callee.line, "gameWindow3D() expects exactly 3 arguments (title, width, height).");
            }
            if (!args[0].isString() || !args[1].isNumber() || !args[2].isNumber()) {
                throw RuntimeError(expr->callee.line, "gameWindow3D() arguments must be (string, number, number).");
            }
            std::string title = args[0].asString();
            int w = static_cast<int>(args[1].asNumber());
            int h = static_cast<int>(args[2].asNumber());
            
            auto win = std::make_shared<WindowInstance>(title, w, h);
            win->is3D = true;
            win->camX = 0; win->camY = 2; win->camZ = -5;
            win->camPitch = 0; win->camYaw = 0;
            return Value(win);
        }

        if (calleeName == "gameWindow2D") {
            if (args.size() != 3) {
                throw RuntimeError(expr->callee.line, "gameWindow2D() expects exactly 3 arguments (title, width, height).");
            }
            if (!args[0].isString() || !args[1].isNumber() || !args[2].isNumber()) {
                throw RuntimeError(expr->callee.line, "gameWindow2D() arguments must be (string, number, number).");
            }
            std::string title = args[0].asString();
            int w = static_cast<int>(args[1].asNumber());
            int h = static_cast<int>(args[2].asNumber());
            
            auto win = std::make_shared<WindowInstance>(title, w, h);
            win->is2D = true;
            return Value(win);
        }

        if (calleeName == "sleep") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "sleep() expects exactly 1 argument (milliseconds).");
            }
            if (!args[0].isNumber()) {
                throw RuntimeError(expr->callee.line, "sleep() argument must be a number.");
            }
            double ms = args[0].asNumber();
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(ms)));
            return Value();
        }

        // Mathematical Built-in Functions
        auto checkNumArgs = [&](const std::string& name, size_t expectedCount) {
            if (args.size() != expectedCount) {
                throw RuntimeError(expr->callee.line, name + "() expects exactly " + std::to_string(expectedCount) + " argument(s).");
            }
            for (size_t i = 0; i < expectedCount; ++i) {
                if (!args[i].isNumber()) {
                    throw RuntimeError(expr->callee.line, "Argument " + std::to_string(i + 1) + " of " + name + "() must be a number.");
                }
            }
        };

        if (calleeName == "sin") {
            checkNumArgs("sin", 1);
            return Value(std::sin(args[0].asNumber()));
        }
        if (calleeName == "cos") {
            checkNumArgs("cos", 1);
            return Value(std::cos(args[0].asNumber()));
        }
        if (calleeName == "tan") {
            checkNumArgs("tan", 1);
            return Value(std::tan(args[0].asNumber()));
        }
        if (calleeName == "asin") {
            checkNumArgs("asin", 1);
            double val = args[0].asNumber();
            if (val < -1.0 || val > 1.0) {
                throw RuntimeError(expr->callee.line, "asin() argument must be between -1.0 and 1.0.");
            }
            return Value(std::asin(val));
        }
        if (calleeName == "acos") {
            checkNumArgs("acos", 1);
            double val = args[0].asNumber();
            if (val < -1.0 || val > 1.0) {
                throw RuntimeError(expr->callee.line, "acos() argument must be between -1.0 and 1.0.");
            }
            return Value(std::acos(val));
        }
        if (calleeName == "atan") {
            checkNumArgs("atan", 1);
            return Value(std::atan(args[0].asNumber()));
        }
        if (calleeName == "atan2") {
            checkNumArgs("atan2", 2);
            return Value(std::atan2(args[0].asNumber(), args[1].asNumber()));
        }
        if (calleeName == "sinh") {
            checkNumArgs("sinh", 1);
            return Value(std::sinh(args[0].asNumber()));
        }
        if (calleeName == "cosh") {
            checkNumArgs("cosh", 1);
            return Value(std::cosh(args[0].asNumber()));
        }
        if (calleeName == "tanh") {
            checkNumArgs("tanh", 1);
            return Value(std::tanh(args[0].asNumber()));
        }
        if (calleeName == "sqrt") {
            checkNumArgs("sqrt", 1);
            double val = args[0].asNumber();
            if (val < 0.0) {
                throw RuntimeError(expr->callee.line, "sqrt() argument must be non-negative.");
            }
            return Value(std::sqrt(val));
        }
        if (calleeName == "cbrt") {
            checkNumArgs("cbrt", 1);
            return Value(std::cbrt(args[0].asNumber()));
        }
        if (calleeName == "pow") {
            checkNumArgs("pow", 2);
            return Value(std::pow(args[0].asNumber(), args[1].asNumber()));
        }
        if (calleeName == "exp") {
            checkNumArgs("exp", 1);
            return Value(std::exp(args[0].asNumber()));
        }
        if (calleeName == "log") {
            checkNumArgs("log", 1);
            double val = args[0].asNumber();
            if (val <= 0.0) {
                throw RuntimeError(expr->callee.line, "log() argument must be positive.");
            }
            return Value(std::log(val));
        }
        if (calleeName == "log10") {
            checkNumArgs("log10", 1);
            double val = args[0].asNumber();
            if (val <= 0.0) {
                throw RuntimeError(expr->callee.line, "log10() argument must be positive.");
            }
            return Value(std::log10(val));
        }
        if (calleeName == "log2") {
            checkNumArgs("log2", 1);
            double val = args[0].asNumber();
            if (val <= 0.0) {
                throw RuntimeError(expr->callee.line, "log2() argument must be positive.");
            }
            return Value(std::log2(val));
        }
        if (calleeName == "abs") {
            checkNumArgs("abs", 1);
            return Value(std::abs(args[0].asNumber()));
        }
        if (calleeName == "ceil") {
            checkNumArgs("ceil", 1);
            return Value(std::ceil(args[0].asNumber()));
        }
        if (calleeName == "floor") {
            checkNumArgs("floor", 1);
            return Value(std::floor(args[0].asNumber()));
        }
        if (calleeName == "round") {
            checkNumArgs("round", 1);
            return Value(std::round(args[0].asNumber()));
        }
        if (calleeName == "min") {
            checkNumArgs("min", 2);
            return Value(std::min(args[0].asNumber(), args[1].asNumber()));
        }
        if (calleeName == "max") {
            checkNumArgs("max", 2);
            return Value(std::max(args[0].asNumber(), args[1].asNumber()));
        }
        if (calleeName == "deg2rad") {
            checkNumArgs("deg2rad", 1);
            return Value(args[0].asNumber() * 3.14159265358979323846 / 180.0);
        }
        if (calleeName == "rad2deg") {
            checkNumArgs("rad2deg", 1);
            return Value(args[0].asNumber() * 180.0 / 3.14159265358979323846);
        }
        if (calleeName == "asinh") {
            checkNumArgs("asinh", 1);
            return Value(std::asinh(args[0].asNumber()));
        }
        if (calleeName == "acosh") {
            checkNumArgs("acosh", 1);
            double val = args[0].asNumber();
            if (val < 1.0) {
                throw RuntimeError(expr->callee.line, "acosh() argument must be greater than or equal to 1.0.");
            }
            return Value(std::acosh(val));
        }
        if (calleeName == "atanh") {
            checkNumArgs("atanh", 1);
            double val = args[0].asNumber();
            if (val <= -1.0 || val >= 1.0) {
                throw RuntimeError(expr->callee.line, "atanh() argument must be strictly between -1.0 and 1.0.");
            }
            return Value(std::atanh(val));
        }
        if (calleeName == "hypot") {
            if (args.size() != 2 && args.size() != 3) {
                throw RuntimeError(expr->callee.line, "hypot() expects exactly 2 or 3 arguments.");
            }
            for (size_t i = 0; i < args.size(); ++i) {
                if (!args[i].isNumber()) {
                    throw RuntimeError(expr->callee.line, "Argument " + std::to_string(i + 1) + " of hypot() must be a number.");
                }
            }
            if (args.size() == 2) {
                return Value(std::hypot(args[0].asNumber(), args[1].asNumber()));
            } else {
                return Value(std::hypot(args[0].asNumber(), args[1].asNumber(), args[2].asNumber()));
            }
        }
        if (calleeName == "ln") {
            checkNumArgs("ln", 1);
            double val = args[0].asNumber();
            if (val <= 0.0) {
                throw RuntimeError(expr->callee.line, "ln() argument must be positive.");
            }
            return Value(std::log(val));
        }
        if (calleeName == "log1p") {
            checkNumArgs("log1p", 1);
            double val = args[0].asNumber();
            if (val <= -1.0) {
                throw RuntimeError(expr->callee.line, "log1p() argument must be greater than -1.0.");
            }
            return Value(std::log1p(val));
        }
        if (calleeName == "expm1") {
            checkNumArgs("expm1", 1);
            return Value(std::expm1(args[0].asNumber()));
        }
        if (calleeName == "copysign") {
            checkNumArgs("copysign", 2);
            return Value(std::copysign(args[0].asNumber(), args[1].asNumber()));
        }
        if (calleeName == "sign" || calleeName == "sgn") {
            checkNumArgs(calleeName, 1);
            double val = args[0].asNumber();
            if (val > 0.0) return Value(1.0);
            if (val < 0.0) return Value(-1.0);
            return Value(0.0);
        }
        if (calleeName == "trunc") {
            checkNumArgs("trunc", 1);
            return Value(std::trunc(args[0].asNumber()));
        }
        if (calleeName == "erf") {
            checkNumArgs("erf", 1);
            return Value(std::erf(args[0].asNumber()));
        }
        if (calleeName == "erfc") {
            checkNumArgs("erfc", 1);
            return Value(std::erfc(args[0].asNumber()));
        }
        if (calleeName == "tgamma") {
            checkNumArgs("tgamma", 1);
            return Value(std::tgamma(args[0].asNumber()));
        }
        if (calleeName == "lgamma") {
            checkNumArgs("lgamma", 1);
            return Value(std::lgamma(args[0].asNumber()));
        }
        if (calleeName == "isnan") {
            checkNumArgs("isnan", 1);
            return Value(std::isnan(args[0].asNumber()) ? 1.0 : 0.0);
        }
        if (calleeName == "isinf") {
            checkNumArgs("isinf", 1);
            return Value(std::isinf(args[0].asNumber()) ? 1.0 : 0.0);
        }
        if (calleeName == "isfinite") {
            checkNumArgs("isfinite", 1);
            return Value(std::isfinite(args[0].asNumber()) ? 1.0 : 0.0);
        }
        if (calleeName == "clamp") {
            checkNumArgs("clamp", 3);
            double val = args[0].asNumber();
            double minVal = args[1].asNumber();
            double maxVal = args[2].asNumber();
            if (minVal > maxVal) {
                throw RuntimeError(expr->callee.line, "clamp() min must be less than or equal to max.");
            }
            return Value(std::clamp(val, minVal, maxVal));
        }
        if (calleeName == "lerp") {
            checkNumArgs("lerp", 3);
            double startVal = args[0].asNumber();
            double endVal = args[1].asNumber();
            double t = args[2].asNumber();
            return Value(startVal + t * (endVal - startVal));
        }
        if (calleeName == "rand" || calleeName == "random") {
            if (args.size() != 0) {
                throw RuntimeError(expr->callee.line, calleeName + "() expects exactly 0 arguments.");
            }
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            return Value(dist(rng));
        }
        if (calleeName == "randint") {
            checkNumArgs("randint", 2);
            double dMin = args[0].asNumber();
            double dMax = args[1].asNumber();
            if (std::floor(dMin) != dMin || std::floor(dMax) != dMax) {
                throw RuntimeError(expr->callee.line, "randint() arguments must be integers.");
            }
            int minVal = static_cast<int>(dMin);
            int maxVal = static_cast<int>(dMax);
            if (minVal > maxVal) {
                throw RuntimeError(expr->callee.line, "randint() min must be less than or equal to max.");
            }
            std::uniform_int_distribution<int> dist(minVal, maxVal);
            return Value(static_cast<double>(dist(rng)));
        }
        if (calleeName == "srand" || calleeName == "seedRand") {
            checkNumArgs(calleeName, 1);
            rng.seed(static_cast<unsigned int>(args[0].asNumber()));
            return Value();
        }
        if (calleeName == "fract") {
            checkNumArgs("fract", 1);
            double val = args[0].asNumber();
            return Value(val - std::floor(val));
        }
        if (calleeName == "epilepsy") {
            if (args.size() != 0) {
                throw RuntimeError(expr->callee.line, "epilepsy() expects exactly 0 arguments.");
            }
            epilepsyState = !epilepsyState;
            return Value(epilepsyState ? 1.0 : 0.0);
        }
        if (calleeName == "fetch") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "fetch() expects exactly 1 argument (URI).");
            }
            if (!args[0].isString()) {
                throw RuntimeError(expr->callee.line, "fetch() argument must be a string.");
            }
            std::string uri = args[0].asString();
            try {
                if (uri.rfind("http://", 0) == 0 || uri.rfind("https://", 0) == 0) {
                    return Value(fetchURL(uri));
                } else {
                    return Value(fetchLocalFile(uri));
                }
            } catch (const std::exception& e) {
                throw RuntimeError(expr->callee.line, e.what());
            }
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

    Value visitMethodCallExpr(MethodCallExpr* expr) override {
        Value obj = evaluate(expr->object);
        if (!obj.isWindow()) {
            throw RuntimeError(expr->method.line, "Only window objects support method calls.");
        }
        auto win = obj.asWindow();
        std::string methodName = expr->method.lexeme;
        
        std::vector<Value> args;
        for (auto& argExpr : expr->arguments) {
            args.push_back(evaluate(argExpr));
        }

        if (methodName == "clear") {
            if (args.size() != 1) throw RuntimeError(expr->method.line, "clear() expects exactly 1 argument (color).");
            if (!args[0].isString()) throw RuntimeError(expr->method.line, "clear() argument must be a string color.");
            win->clear(args[0].asString());
            return obj;
        }
        
        if (methodName == "rect") {
            if (args.size() != 5) throw RuntimeError(expr->method.line, "rect() expects exactly 5 arguments (x, y, w, h, color).");
            if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber() || !args[3].isNumber() || !args[4].isString()) {
                throw RuntimeError(expr->method.line, "rect() expects (number, number, number, number, string).");
            }
            win->rect(static_cast<int>(args[0].asNumber()), static_cast<int>(args[1].asNumber()),
                      static_cast<int>(args[2].asNumber()), static_cast<int>(args[3].asNumber()),
                      args[4].asString());
            return obj;
        }

        if (methodName == "rectEmpty") {
            if (args.size() != 6) throw RuntimeError(expr->method.line, "rectEmpty() expects exactly 6 arguments (x, y, w, h, color, thickness).");
            if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber() || !args[3].isNumber() || !args[4].isString() || !args[5].isNumber()) {
                throw RuntimeError(expr->method.line, "rectEmpty() expects (number, number, number, number, string, number).");
            }
            win->rectEmpty(static_cast<int>(args[0].asNumber()), static_cast<int>(args[1].asNumber()),
                           static_cast<int>(args[2].asNumber()), static_cast<int>(args[3].asNumber()),
                           args[4].asString(), static_cast<int>(args[5].asNumber()));
            return obj;
        }

        if (methodName == "circle") {
            if (args.size() != 4) throw RuntimeError(expr->method.line, "circle() expects exactly 4 arguments (x, y, r, color).");
            if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber() || !args[3].isString()) {
                throw RuntimeError(expr->method.line, "circle() expects (number, number, number, string).");
            }
            win->circle(static_cast<int>(args[0].asNumber()), static_cast<int>(args[1].asNumber()),
                        static_cast<int>(args[2].asNumber()), args[3].asString());
            return obj;
        }

        if (methodName == "circleEmpty") {
            if (args.size() != 5) throw RuntimeError(expr->method.line, "circleEmpty() expects exactly 5 arguments (x, y, r, color, thickness).");
            if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber() || !args[3].isString() || !args[4].isNumber()) {
                throw RuntimeError(expr->method.line, "circleEmpty() expects (number, number, number, string, number).");
            }
            win->circleEmpty(static_cast<int>(args[0].asNumber()), static_cast<int>(args[1].asNumber()),
                             static_cast<int>(args[2].asNumber()), args[3].asString(), static_cast<int>(args[4].asNumber()));
            return obj;
        }

        if (methodName == "ellipse") {
            if (args.size() != 5) throw RuntimeError(expr->method.line, "ellipse() expects exactly 5 arguments (x, y, rx, ry, color).");
            if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber() || !args[3].isNumber() || !args[4].isString()) {
                throw RuntimeError(expr->method.line, "ellipse() expects (number, number, number, number, string).");
            }
            win->ellipse(static_cast<int>(args[0].asNumber()), static_cast<int>(args[1].asNumber()),
                         static_cast<int>(args[2].asNumber()), static_cast<int>(args[3].asNumber()),
                         args[4].asString());
            return obj;
        }

        if (methodName == "line") {
            if (args.size() != 6) throw RuntimeError(expr->method.line, "line() expects exactly 6 arguments (x1, y1, x2, y2, color, thickness).");
            if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber() || !args[3].isNumber() || !args[4].isString() || !args[5].isNumber()) {
                throw RuntimeError(expr->method.line, "line() expects (number, number, number, number, string, number).");
            }
            win->line(static_cast<int>(args[0].asNumber()), static_cast<int>(args[1].asNumber()),
                      static_cast<int>(args[2].asNumber()), static_cast<int>(args[3].asNumber()),
                      args[4].asString(), static_cast<int>(args[5].asNumber()));
            return obj;
        }

        if (methodName == "text") {
            if (args.size() != 5) throw RuntimeError(expr->method.line, "text() expects exactly 5 arguments (x, y, text, color, size).");
            if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isString() || !args[3].isString() || !args[4].isNumber()) {
                throw RuntimeError(expr->method.line, "text() expects (number, number, string, string, number).");
            }
            win->text(static_cast<int>(args[0].asNumber()), static_cast<int>(args[1].asNumber()),
                      args[2].asString(), args[3].asString(), static_cast<int>(args[4].asNumber()));
            return obj;
        }

        if (methodName == "pixel") {
            if (args.size() != 3) throw RuntimeError(expr->method.line, "pixel() expects exactly 3 arguments (x, y, color).");
            if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isString()) {
                throw RuntimeError(expr->method.line, "pixel() expects (number, number, string).");
            }
            win->pixel(static_cast<int>(args[0].asNumber()), static_cast<int>(args[1].asNumber()), args[2].asString());
            return obj;
        }

        if (methodName == "isOpen") {
            if (args.size() != 0) throw RuntimeError(expr->method.line, "isOpen() expects 0 arguments.");
            return win->isOpen();
        }

        if (methodName == "getKey") {
            if (args.size() != 1) throw RuntimeError(expr->method.line, "getKey() expects exactly 1 argument (keyName).");
            if (!args[0].isString()) throw RuntimeError(expr->method.line, "getKey() argument must be a string.");
            return win->getKey(args[0].asString());
        }

        if (methodName == "getMouseX") {
            if (args.size() != 0) throw RuntimeError(expr->method.line, "getMouseX() expects 0 arguments.");
            return win->getMouseX();
        }

        if (methodName == "getMouseY") {
            if (args.size() != 0) throw RuntimeError(expr->method.line, "getMouseY() expects 0 arguments.");
            return win->getMouseY();
        }

        if (methodName == "getMouseLeft") {
            if (args.size() != 0) throw RuntimeError(expr->method.line, "getMouseLeft() expects 0 arguments.");
            return win->getMouseLeft();
        }

        if (methodName == "close") {
            if (args.size() != 0) throw RuntimeError(expr->method.line, "close() expects 0 arguments.");
            win->close();
            return obj;
        }

        // 3D/2D Game Engine Method Bindings
        if (methodName == "addCube") {
            if (args.size() != 5) throw RuntimeError(expr->method.line, "addCube() expects exactly 5 arguments (x, y, z, size, color).");
            if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber() || !args[3].isNumber() || !args[4].isString()) {
                throw RuntimeError(expr->method.line, "addCube() expects (number, number, number, number, string).");
            }
            win->addCube(args[0].asNumber(), args[1].asNumber(), args[2].asNumber(), args[3].asNumber(), args[4].asString());
            return obj;
        }

        if (methodName == "addGrid") {
            if (args.size() != 5) throw RuntimeError(expr->method.line, "addGrid() expects exactly 5 arguments (x, z, size, spacing, color).");
            if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber() || !args[3].isNumber() || !args[4].isString()) {
                throw RuntimeError(expr->method.line, "addGrid() expects (number, number, number, number, string).");
            }
            win->addGrid(args[0].asNumber(), args[1].asNumber(), args[2].asNumber(), args[3].asNumber(), args[4].asString());
            return obj;
        }

        if (methodName == "setCamera") {
            if (args.size() != 5) throw RuntimeError(expr->method.line, "setCamera() expects exactly 5 arguments (x, y, z, pitch, yaw).");
            if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber() || !args[3].isNumber() || !args[4].isNumber()) {
                throw RuntimeError(expr->method.line, "setCamera() expects (number, number, number, number, number).");
            }
            win->setCamera(args[0].asNumber(), args[1].asNumber(), args[2].asNumber(), args[3].asNumber(), args[4].asNumber());
            return obj;
        }

        if (methodName == "moveCamera") {
            if (args.size() != 3) throw RuntimeError(expr->method.line, "moveCamera() expects exactly 3 arguments (forward, right, up).");
            if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber()) {
                throw RuntimeError(expr->method.line, "moveCamera() expects (number, number, number).");
            }
            win->moveCamera(args[0].asNumber(), args[1].asNumber(), args[2].asNumber());
            return obj;
        }

        if (methodName == "rotateCamera") {
            if (args.size() != 2) throw RuntimeError(expr->method.line, "rotateCamera() expects exactly 2 arguments (dpitch, dyaw).");
            if (!args[0].isNumber() || !args[1].isNumber()) {
                throw RuntimeError(expr->method.line, "rotateCamera() expects (number, number).");
            }
            win->rotateCamera(args[0].asNumber(), args[1].asNumber());
            return obj;
        }

        if (methodName == "clear3D") {
            if (args.size() != 0) throw RuntimeError(expr->method.line, "clear3D() expects 0 arguments.");
            win->clear3D();
            return obj;
        }

        if (methodName == "render3D") {
            if (args.size() != 0) throw RuntimeError(expr->method.line, "render3D() expects 0 arguments.");
            win->render3D();
            return obj;
        }

        if (methodName == "addRect2D") {
            if (args.size() != 5) throw RuntimeError(expr->method.line, "addRect2D() expects exactly 5 arguments (x, y, w, h, color).");
            if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber() || !args[3].isNumber() || !args[4].isString()) {
                throw RuntimeError(expr->method.line, "addRect2D() expects (number, number, number, number, string).");
            }
            win->addRect2D(args[0].asNumber(), args[1].asNumber(), args[2].asNumber(), args[3].asNumber(), args[4].asString());
            return obj;
        }

        if (methodName == "addCircle2D") {
            if (args.size() != 4) throw RuntimeError(expr->method.line, "addCircle2D() expects exactly 4 arguments (x, y, r, color).");
            if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber() || !args[3].isString()) {
                throw RuntimeError(expr->method.line, "addCircle2D() expects (number, number, number, string).");
            }
            win->addCircle2D(args[0].asNumber(), args[1].asNumber(), args[2].asNumber(), args[3].asString());
            return obj;
        }

        if (methodName == "setCamera2D") {
            if (args.size() != 3) throw RuntimeError(expr->method.line, "setCamera2D() expects exactly 3 arguments (cx, cy, zoom).");
            if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber()) {
                throw RuntimeError(expr->method.line, "setCamera2D() expects (number, number, number).");
            }
            win->setCamera2D(args[0].asNumber(), args[1].asNumber(), args[2].asNumber());
            return obj;
        }

        if (methodName == "moveCamera2D") {
            if (args.size() != 2) throw RuntimeError(expr->method.line, "moveCamera2D() expects exactly 2 arguments (dx, dy).");
            if (!args[0].isNumber() || !args[1].isNumber()) {
                throw RuntimeError(expr->method.line, "moveCamera2D() expects (number, number).");
            }
            win->moveCamera2D(args[0].asNumber(), args[1].asNumber());
            return obj;
        }

        if (methodName == "clear2D") {
            if (args.size() != 0) throw RuntimeError(expr->method.line, "clear2D() expects 0 arguments.");
            win->clear2D();
            return obj;
        }

        if (methodName == "render2D") {
            if (args.size() != 0) throw RuntimeError(expr->method.line, "render2D() expects 0 arguments.");
            win->render2D();
            return obj;
        }

        throw RuntimeError(expr->method.line, "Unknown method '" + methodName + "' on window object.");
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

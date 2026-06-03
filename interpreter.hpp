#pragma once
#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <cmath>
#include <random>
#include <algorithm>
#include <cctype>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <shellapi.h>
#include <shlobj.h>
#include "ast.hpp"
#include "value.hpp"
#include "environment.hpp"
#include "database.hpp"

inline std::mutex& getStdoutMutex() {
    static std::mutex mtx;
    return mtx;
}

inline std::mutex& getRegistryMutex() {
    static std::mutex mtx;
    return mtx;
}
inline std::vector<class Interpreter*>& getActiveInterpreters() {
    static std::vector<class Interpreter*> registry;
    return registry;
}

class AdminHelperManager {
private:
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    HANDLE hProcess = NULL;
    std::string pipeName;

public:
    AdminHelperManager() {
        pipeName = "\\\\.\\pipe\\EpilepsyAdmin_" + std::to_string(GetCurrentProcessId());
    }

    ~AdminHelperManager() {
        cleanup();
    }

    void cleanup() {
        if (hPipe != INVALID_HANDLE_VALUE) {
            DWORD cmdLen = 4;
            DWORD bytesWritten = 0;
            WriteFile(hPipe, &cmdLen, sizeof(cmdLen), &bytesWritten, NULL);
            WriteFile(hPipe, "exit", 4, &bytesWritten, NULL);
            
            CloseHandle(hPipe);
            hPipe = INVALID_HANDLE_VALUE;
        }
        if (hProcess != NULL) {
            CloseHandle(hProcess);
            hProcess = NULL;
        }
    }

    bool ensureStarted(int line) {
        if (hPipe != INVALID_HANDLE_VALUE) {
            return true;
        }

        hPipe = CreateNamedPipeA(
            pipeName.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            1024,
            1024,
            0,
            NULL
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            throw RuntimeError(line, "Failed to create named pipe for admin helper. (Windows Error " + std::to_string(GetLastError()) + ")");
        }

        char szPath[MAX_PATH];
        GetModuleFileNameA(NULL, szPath, MAX_PATH);

        std::string params = "--admin-helper:" + pipeName;

        SHELLEXECUTEINFOA sei;
        ZeroMemory(&sei, sizeof(sei));
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = "runas";
        sei.lpFile = szPath;
        sei.lpParameters = params.c_str();
        sei.nShow = SW_HIDE;

        if (!ShellExecuteExA(&sei)) {
            CloseHandle(hPipe);
            hPipe = INVALID_HANDLE_VALUE;
            throw RuntimeError(line, "Elevation request denied (Windows Error " + std::to_string(GetLastError()) + ")");
        }

        hProcess = sei.hProcess;

        if (!ConnectNamedPipe(hPipe, NULL)) {
            if (GetLastError() != ERROR_PIPE_CONNECTED) {
                cleanup();
                throw RuntimeError(line, "Failed to connect to admin helper.");
            }
        }

        return true;
    }

    DWORD executeCommand(const std::string& cmd, int line) {
        ensureStarted(line);

        DWORD cmdLen = static_cast<DWORD>(cmd.length());
        DWORD bytesWritten = 0;
        
        if (!WriteFile(hPipe, &cmdLen, sizeof(cmdLen), &bytesWritten, NULL)) {
            cleanup();
            throw RuntimeError(line, "Failed to communicate with admin helper.");
        }

        if (!WriteFile(hPipe, cmd.c_str(), cmdLen, &bytesWritten, NULL)) {
            cleanup();
            throw RuntimeError(line, "Failed to communicate with admin helper.");
        }

        DWORD exitCode = 0;
        DWORD bytesRead = 0;
        if (!ReadFile(hPipe, &exitCode, sizeof(exitCode), &bytesRead, NULL)) {
            cleanup();
            throw RuntimeError(line, "Failed to read response from admin helper.");
        }

        return exitCode;
    }
};

inline AdminHelperManager& getAdminHelperManager() {
    static AdminHelperManager manager;
    return manager;
}

struct RegistryGuard {
    class Interpreter* interp;
    RegistryGuard(class Interpreter* i) : interp(i) {
        std::lock_guard<std::mutex> lock(getRegistryMutex());
        getActiveInterpreters().push_back(interp);
    }
    ~RegistryGuard() {
        std::lock_guard<std::mutex> lock(getRegistryMutex());
        auto& list = getActiveInterpreters();
        list.erase(std::remove(list.begin(), list.end(), interp), list.end());
    }
};
#include <wininet.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <limits>
#include <wincrypt.h>
#include <iomanip>

#pragma comment(lib, "advapi32.lib")

inline Complex toComplex(const Value& val) {
    if (val.isComplex()) return val.asComplex();
    if (val.isNumber()) return Complex(val.asNumber(), 0.0);
    throw std::runtime_error("Cannot convert type '" + val.typeString() + "' to complex number.");
}

inline Complex operator+(const Complex& lhs, const Complex& rhs) {
    return Complex(lhs.real + rhs.real, lhs.imag + rhs.imag);
}
inline Complex operator-(const Complex& lhs, const Complex& rhs) {
    return Complex(lhs.real - rhs.real, lhs.imag - rhs.imag);
}
inline Complex operator*(const Complex& lhs, const Complex& rhs) {
    return Complex(lhs.real * rhs.real - lhs.imag * rhs.imag,
                   lhs.real * rhs.imag + lhs.imag * rhs.real);
}
inline Complex operator/(const Complex& lhs, const Complex& rhs) {
    double denom = rhs.real * rhs.real + rhs.imag * rhs.imag;
    if (denom == 0.0) {
        throw std::runtime_error("Division by zero in complex numbers.");
    }
    return Complex((lhs.real * rhs.real + lhs.imag * rhs.imag) / denom,
                   (lhs.imag * rhs.real - lhs.real * rhs.imag) / denom);
}
inline Complex operator-(const Complex& c) {
    return Complex(-c.real, -c.imag);
}

inline std::string cleanFilePath(const std::string& path) {
    std::string clean = path;
    if (clean.rfind("file://", 0) == 0) {
        clean = clean.substr(7);
        // On Windows, file:///C:/path might have a leading slash
        if (clean.length() >= 3 && clean[0] == '/' && clean[2] == ':') {
            clean = clean.substr(1);
        }
    }
    return clean;
}

inline std::string readTextFile(const std::string& path) {
    std::string clean = cleanFilePath(path);
    std::ifstream file(clean);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file for reading: " + clean);
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

inline void writeTextFile(const std::string& path, const std::string& content) {
    std::string clean = cleanFilePath(path);
    std::ofstream file(clean, std::ios::trunc);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file for writing: " + clean);
    }
    file << content;
}

inline void appendTextFile(const std::string& path, const std::string& content) {
    std::string clean = cleanFilePath(path);
    std::ofstream file(clean, std::ios::app);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file for appending: " + clean);
    }
    file << content;
}

inline bool fileOrDirExists(const std::string& path) {
    return std::filesystem::exists(cleanFilePath(path));
}

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

inline std::string computeHash(const std::string& data, ALG_ID algId) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    std::string result = "";

    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return "";
    }

    if (!CryptCreateHash(hProv, algId, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }

    if (!CryptHashData(hHash, reinterpret_cast<const BYTE*>(data.c_str()), static_cast<DWORD>(data.length()), 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    DWORD cbHashSize = 0;
    DWORD dwCount = sizeof(DWORD);
    if (CryptGetHashParam(hHash, HP_HASHSIZE, reinterpret_cast<BYTE*>(&cbHashSize), &dwCount, 0)) {
        std::vector<BYTE> rgbHash(cbHashSize);
        if (CryptGetHashParam(hHash, HP_HASHVAL, rgbHash.data(), &cbHashSize, 0)) {
            std::ostringstream oss;
            for (DWORD i = 0; i < cbHashSize; i++) {
                oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(rgbHash[i]);
            }
            result = oss.str();
        }
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return result;
}

class Interpreter : public ExprVisitor, public StmtVisitor {
private:
    std::shared_ptr<Environment> environment = std::make_shared<Environment>();
    bool epilepsyState = false;
    std::mt19937 rng{static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count())};
public:
    std::atomic<bool> killed{false};
private:

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
        environment->define("i", Value(Complex(0.0, 1.0)));
        environment->define("infinity", Value(std::numeric_limits<double>::infinity()));
        environment->define("inf", Value(std::numeric_limits<double>::infinity()));
        environment->define("nan", Value(std::numeric_limits<double>::quiet_NaN()));
        environment->define("phi", Value(1.61803398874989484820));
        environment->define("gold", Value(1.61803398874989484820));
        environment->define("gamma", Value(0.57721566490153286060));
        environment->define("catalan", Value(0.91596559417721901505));
        environment->define("apery", Value(1.20205690315959428540));
    }

    std::shared_ptr<Environment> getGlobals() const {
        return environment;
    }

    std::shared_ptr<Environment> getEnvironment() const {
        return environment;
    }

    void setEnvironment(std::shared_ptr<Environment> env) {
        environment = env;
    }

    bool getEpilepsyState() const {
        return epilepsyState;
    }

    void setEpilepsyState(bool state) {
        epilepsyState = state;
    }

    Value evaluate(ExprPtr expr) {
        return expr->accept(this);
    }

    void execute(StmtPtr stmt) {
        if (killed) {
            throw QuantumKilledException();
        }
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

    std::string currentFilePath = "";

    bool isMatchingLabel(StmtPtr stmt, const std::string& labelName) {
        auto exprStmt = std::dynamic_pointer_cast<ExpressionStmt>(stmt);
        if (!exprStmt) return false;
        
        auto callExpr = std::dynamic_pointer_cast<CallExpr>(exprStmt->expression);
        if (!callExpr) return false;
        
        if (callExpr->callee.lexeme != "label") return false;
        if (callExpr->arguments.size() != 1) return false;
        
        auto litExpr = std::dynamic_pointer_cast<LiteralExpr>(callExpr->arguments[0]);
        if (!litExpr) return false;
        
        return litExpr->value.isString() && litExpr->value.asString() == labelName;
    }

    void interpret(const std::vector<StmtPtr>& statements, const std::string& filePath = "") {
        currentFilePath = filePath;
        std::vector<StmtPtr> activeStatements = statements;
        size_t ip = 0;

        while (ip < activeStatements.size()) {
            try {
                execute(activeStatements[ip]);
                ip++;
            } catch (const GotoException& g) {
                std::string targetFile = g.file;
                if (targetFile.empty()) {
                    targetFile = currentFilePath;
                }

                if (targetFile != currentFilePath) {
                    std::string clean = cleanFilePath(targetFile);
                    if (!fileOrDirExists(clean)) {
                        throw RuntimeError(activeStatements[ip]->line, "Goto target file not found: " + targetFile);
                    }
                    std::string source = readTextFile(clean);
                    Lexer lexer(source);
                    auto tokens = lexer.scanTokens();
                    Parser parser(tokens);
                    activeStatements = parser.parse();
                    currentFilePath = targetFile;
                }

                int targetLine = -1;
                try {
                    targetLine = std::stoi(g.target);
                } catch (...) {
                    // Target is label name
                }

                size_t foundIdx = activeStatements.size();
                if (targetLine != -1) {
                    for (size_t i = 0; i < activeStatements.size(); ++i) {
                        if (activeStatements[i]->line >= targetLine) {
                            foundIdx = i;
                            break;
                        }
                    }
                } else {
                    for (size_t i = 0; i < activeStatements.size(); ++i) {
                        if (isMatchingLabel(activeStatements[i], g.target)) {
                            foundIdx = i;
                            break;
                        }
                    }
                }

                if (foundIdx < activeStatements.size()) {
                    ip = foundIdx;
                } else {
                    throw RuntimeError(0, "Goto target not found: " + g.target + " in " + targetFile);
                }
            }
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
                if (left.isComplex() || right.isComplex()) {
                    try {
                        return Value(toComplex(left) + toComplex(right));
                    } catch (const std::exception& e) {
                        throw RuntimeError(expr->op.line, e.what());
                    }
                }
                if (left.isNumber() && right.isNumber()) {
                    return left.asNumber() + right.asNumber();
                }
                if (left.isString() || right.isString()) {
                    return left.toString() + right.toString();
                }
                throw RuntimeError(expr->op.line, "Operands must be numbers, complex, or strings for '+'.");
                
            case TokenType::MINUS:
                if (left.isComplex() || right.isComplex()) {
                    try {
                        return Value(toComplex(left) - toComplex(right));
                    } catch (const std::exception& e) {
                        throw RuntimeError(expr->op.line, e.what());
                    }
                }
                checkNumberOperands(expr->op, left, right);
                return left.asNumber() - right.asNumber();
                
            case TokenType::STAR:
                if (left.isComplex() || right.isComplex()) {
                    try {
                        return Value(toComplex(left) * toComplex(right));
                    } catch (const std::exception& e) {
                        throw RuntimeError(expr->op.line, e.what());
                    }
                }
                checkNumberOperands(expr->op, left, right);
                return left.asNumber() * right.asNumber();
                
            case TokenType::SLASH:
                if (left.isComplex() || right.isComplex()) {
                    try {
                        return Value(toComplex(left) / toComplex(right));
                    } catch (const std::exception& e) {
                        throw RuntimeError(expr->op.line, e.what());
                    }
                }
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
                if (right.isComplex()) {
                    return Value(-right.asComplex());
                }
                if (!right.isNumber()) {
                    throw RuntimeError(expr->op.line, "Operand must be a number or complex for unary '-'.");
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
            std::lock_guard<std::mutex> lock(getStdoutMutex());
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
            if (val.isTable()) {
                return Value(static_cast<double>(val.asTable()->rows.size()));
            }
            throw RuntimeError(expr->callee.line, "len() argument must be string, array, or table.");
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

        if (calleeName == "database") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "database() expects exactly 1 argument (path).");
            }
            if (!args[0].isString()) {
                throw RuntimeError(expr->callee.line, "database() argument must be a string file path.");
            }
            try {
                auto db = std::make_shared<DatabaseInstance>(args[0].asString());
                return Value(db);
            } catch (const std::exception& e) {
                throw RuntimeError(expr->callee.line, std::string("Failed to initialize database: ") + e.what());
            }
        }

        if (calleeName == "solve") {
            if (args.size() < 2 || args.size() > 3) {
                throw RuntimeError(expr->callee.line, "solve() expects 2 or 3 arguments: (equations_array, variables_array, [initial_guesses_array]).");
            }
            if (!args[0].isArray()) {
                throw RuntimeError(expr->callee.line, "First argument to solve() must be an array of equation strings.");
            }
            if (!args[1].isArray()) {
                throw RuntimeError(expr->callee.line, "Second argument to solve() must be an array of variable names (strings).");
            }

            auto equationsArray = args[0].asArray();
            auto variablesArray = args[1].asArray();

            if (equationsArray->size() != variablesArray->size()) {
                throw RuntimeError(expr->callee.line, "Number of equations (" + std::to_string(equationsArray->size()) + 
                                   ") must match the number of variables (" + std::to_string(variablesArray->size()) + ").");
            }

            size_t n = equationsArray->size();
            if (n == 0) {
                throw RuntimeError(expr->callee.line, "System of equations cannot be empty.");
            }

            std::vector<std::string> varNames;
            for (size_t i = 0; i < n; ++i) {
                Value varVal = (*variablesArray)[i];
                if (!varVal.isString()) {
                    throw RuntimeError(expr->callee.line, "Variable name at index " + std::to_string(i) + " must be a string.");
                }
                std::string varName = varVal.asString();
                if (std::find(varNames.begin(), varNames.end(), varName) != varNames.end()) {
                    throw RuntimeError(expr->callee.line, "Variable name '" + varName + "' is duplicated.");
                }
                varNames.push_back(varName);
            }

            std::vector<double> x(n, 1.0);
            if (args.size() == 3) {
                if (!args[2].isArray()) {
                    throw RuntimeError(expr->callee.line, "Third argument to solve() must be an array of initial guesses (numbers).");
                }
                auto guessesArray = args[2].asArray();
                if (guessesArray->size() != n) {
                    throw RuntimeError(expr->callee.line, "Initial guesses array size (" + std::to_string(guessesArray->size()) + 
                                       ") must match the number of variables (" + std::to_string(n) + ").");
                }
                for (size_t i = 0; i < n; ++i) {
                    Value guessVal = (*guessesArray)[i];
                    if (!guessVal.isNumber()) {
                        throw RuntimeError(expr->callee.line, "Initial guess at index " + std::to_string(i) + " must be a number.");
                    }
                    x[i] = guessVal.asNumber();
                }
            }

            std::vector<ExprPtr> eqExprs;
            for (size_t i = 0; i < n; ++i) {
                Value eqVal = (*equationsArray)[i];
                if (!eqVal.isString()) {
                    throw RuntimeError(expr->callee.line, "Equation at index " + std::to_string(i) + " must be a string.");
                }
                std::string eqStr = eqVal.asString();
                try {
                    Lexer eqLexer(eqStr);
                    auto eqTokens = eqLexer.scanTokens();
                    Parser eqParser(eqTokens);
                    auto eqStatements = eqParser.parse();
                    if (eqStatements.empty()) {
                        throw std::runtime_error("Equation is empty.");
                    }
                    auto exprStmt = std::dynamic_pointer_cast<ExpressionStmt>(eqStatements[0]);
                    if (!exprStmt) {
                        throw std::runtime_error("Equation must be an expression.");
                    }
                    ExprPtr eqExpr = exprStmt->expression;

                    // If it is a binary comparison (==), transform to LHS - RHS
                    if (auto binExpr = std::dynamic_pointer_cast<BinaryExpr>(eqExpr)) {
                        if (binExpr->op.type == TokenType::EQUAL_EQUAL) {
                            Token opMinus = binExpr->op;
                            opMinus.type = TokenType::MINUS;
                            opMinus.lexeme = "-";
                            eqExpr = std::make_shared<BinaryExpr>(binExpr->left, opMinus, binExpr->right);
                        }
                    }
                    eqExprs.push_back(eqExpr);
                } catch (const ParseError& pe) {
                    throw RuntimeError(expr->callee.line, "Equation " + std::to_string(i + 1) + " syntax error: " + pe.what());
                } catch (const std::exception& ex) {
                    throw RuntimeError(expr->callee.line, "Equation " + std::to_string(i + 1) + " parsing error: " + ex.what());
                }
            }

            const int maxIterations = 100;
            const double tolerance = 1e-11;
            const double epsilon = 1e-7;
            bool converged = false;

            for (int iter = 0; iter < maxIterations; ++iter) {
                // 1. Evaluate F(X)
                std::vector<double> F(n, 0.0);
                auto tempEnv = std::make_shared<Environment>(this->environment);
                for (size_t j = 0; j < n; ++j) {
                    tempEnv->define(varNames[j], Value(x[j]));
                }

                auto prevEnv = this->environment;
                try {
                    this->environment = tempEnv;
                    for (size_t i = 0; i < n; ++i) {
                        Value val = evaluate(eqExprs[i]);
                        if (!val.isNumber()) {
                            throw RuntimeError(expr->callee.line, "Equation " + std::to_string(i + 1) + " did not evaluate to a number.");
                        }
                        F[i] = val.asNumber();
                    }
                    this->environment = prevEnv;
                } catch (...) {
                    this->environment = prevEnv;
                    throw;
                }

                // Check convergence
                double maxF = 0.0;
                for (double f_val : F) {
                    maxF = (std::max)(maxF, std::abs(f_val));
                }
                if (maxF < tolerance) {
                    converged = true;
                    break;
                }

                // 2. Compute Jacobian matrix J
                std::vector<std::vector<double>> J(n, std::vector<double>(n, 0.0));
                for (size_t j = 0; j < n; ++j) {
                    std::vector<double> x_perturbed = x;
                    x_perturbed[j] += epsilon;

                    auto pertEnv = std::make_shared<Environment>(prevEnv);
                    for (size_t k = 0; k < n; ++k) {
                        pertEnv->define(varNames[k], Value(x_perturbed[k]));
                    }

                    try {
                        this->environment = pertEnv;
                        for (size_t i = 0; i < n; ++i) {
                            Value val = evaluate(eqExprs[i]);
                            if (!val.isNumber()) {
                                throw RuntimeError(expr->callee.line, "Equation " + std::to_string(i + 1) + " did not evaluate to a number during Jacobian calculation.");
                            }
                            J[i][j] = (val.asNumber() - F[i]) / epsilon;
                        }
                        this->environment = prevEnv;
                    } catch (...) {
                        this->environment = prevEnv;
                        throw;
                    }
                }

                // 3. Solve J * delta_x = -F using Gaussian Elimination with partial pivoting
                std::vector<double> rhs(n);
                for (size_t i = 0; i < n; ++i) rhs[i] = -F[i];

                for (size_t k = 0; k < n; ++k) {
                    // Pivot selection
                    size_t pivot_row = k;
                    double max_val = std::abs(J[k][k]);
                    for (size_t i = k + 1; i < n; ++i) {
                        if (std::abs(J[i][k]) > max_val) {
                            max_val = std::abs(J[i][k]);
                            pivot_row = i;
                        }
                    }

                    if (max_val < 1e-12) {
                        throw RuntimeError(expr->callee.line, "System of equations is singular or underdetermined (no unique solution exists).");
                    }

                    if (pivot_row != k) {
                        std::swap(J[k], J[pivot_row]);
                        std::swap(rhs[k], rhs[pivot_row]);
                    }

                    // Elimination
                    for (size_t i = k + 1; i < n; ++i) {
                        double factor = J[i][k] / J[k][k];
                        for (size_t c = k; c < n; ++c) {
                            J[i][c] -= factor * J[k][c];
                        }
                        rhs[i] -= factor * rhs[k];
                    }
                }

                // Back substitution
                std::vector<double> delta_x(n, 0.0);
                for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
                    double val = rhs[i];
                    for (size_t c = i + 1; c < n; ++c) {
                        val -= J[i][c] * delta_x[c];
                    }
                    delta_x[i] = val / J[i][i];
                }

                // Update X
                for (size_t i = 0; i < n; ++i) {
                    x[i] += delta_x[i];
                    if (std::isnan(x[i]) || std::isinf(x[i])) {
                        throw RuntimeError(expr->callee.line, "Solver encountered divergent or invalid value (NaN/Inf). Try providing better initial guesses.");
                    }
                }
            }

            if (!converged) {
                throw RuntimeError(expr->callee.line, "Solver failed to converge after " + std::to_string(maxIterations) + " iterations.");
            }

            auto results = std::make_shared<std::vector<Value>>();
            for (double solVal : x) {
                results->push_back(Value(solVal));
            }
            return Value(results);
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
            win->playerX = 0; win->playerY = 2; win->playerZ = -5;
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

        if (calleeName == "larp") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "larp() expects exactly 1 argument (executable path).");
            }
            if (!args[0].isString()) {
                throw RuntimeError(expr->callee.line, "larp() argument must be a string.");
            }
            std::string path = args[0].asString();

            std::vector<char> cmdBuf(path.begin(), path.end());
            cmdBuf.push_back('\0');

            std::cout.flush();

            STARTUPINFOA si;
            PROCESS_INFORMATION pi;
            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(si);
            ZeroMemory(&pi, sizeof(pi));

            if (!CreateProcessA(
                NULL,
                cmdBuf.data(),
                NULL,
                NULL,
                false,
                0,
                NULL,
                NULL,
                &si,
                &pi
            )) {
                throw RuntimeError(expr->callee.line, "larp() failed to start application: " + path + " (Windows Error " + std::to_string(GetLastError()) + ")");
            }

            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            return Value();
        }

        if (calleeName == "sudo") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "sudo() expects exactly 1 argument (command to run).");
            }
            if (!args[0].isString()) {
                throw RuntimeError(expr->callee.line, "sudo() argument must be a string.");
            }
            std::string cmd = args[0].asString();

            DWORD exitCode = getAdminHelperManager().executeCommand(cmd, expr->callee.line);
            return Value(static_cast<double>(exitCode));
        }

        if (calleeName == "is_admin") {
            if (args.size() != 0) {
                throw RuntimeError(expr->callee.line, "is_admin() expects 0 arguments.");
            }
            return Value(static_cast<bool>(IsUserAnAdmin()));
        }

        if (calleeName == "goto") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "goto() expects exactly 1 argument (target).");
            }
            std::string targetStr = "";
            if (args[0].isNumber()) {
                targetStr = std::to_string(static_cast<int>(args[0].asNumber()));
            } else if (args[0].isString()) {
                targetStr = args[0].asString();
            } else {
                throw RuntimeError(expr->callee.line, "goto() argument must be a string or number.");
            }

            std::string file = "";
            std::string target = targetStr;

            size_t colonPos = targetStr.rfind(':');
            if (colonPos != std::string::npos && colonPos > 1) {
                if (targetStr[colonPos - 1] != '\\' && targetStr[colonPos - 1] != '/') {
                    file = targetStr.substr(0, colonPos);
                    target = targetStr.substr(colonPos + 1);
                }
            }

            throw GotoException(file, target);
        }

        if (calleeName == "label") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "label() expects exactly 1 argument (label name).");
            }
            if (!args[0].isString()) {
                throw RuntimeError(expr->callee.line, "label() argument must be a string.");
            }
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
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "sqrt() expects exactly 1 argument.");
            }
            Value val = args[0];
            if (val.isComplex()) {
                Complex c = val.asComplex();
                double r = std::hypot(c.real, c.imag);
                double theta = std::atan2(c.imag, c.real);
                return Value(Complex(std::sqrt(r) * std::cos(theta / 2.0),
                                     std::sqrt(r) * std::sin(theta / 2.0)));
            }
            if (val.isNumber()) {
                double d = val.asNumber();
                if (d >= 0.0) {
                    return Value(std::sqrt(d));
                } else {
                    return Value(Complex(0.0, std::sqrt(-d)));
                }
            }
            throw RuntimeError(expr->callee.line, "sqrt() argument must be a number or complex.");
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
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "abs() expects exactly 1 argument.");
            }
            Value val = args[0];
            if (val.isComplex()) {
                Complex c = val.asComplex();
                return Value(std::hypot(c.real, c.imag));
            }
            if (val.isNumber()) {
                return Value(std::abs(val.asNumber()));
            }
            throw RuntimeError(expr->callee.line, "abs() argument must be a number or complex.");
        }
        if (calleeName == "real") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "real() expects exactly 1 argument.");
            }
            Value val = args[0];
            if (val.isComplex()) return Value(val.asComplex().real);
            if (val.isNumber()) return Value(val.asNumber());
            throw RuntimeError(expr->callee.line, "real() argument must be a number or complex.");
        }
        if (calleeName == "imag") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "imag() expects exactly 1 argument.");
            }
            Value val = args[0];
            if (val.isComplex()) return Value(val.asComplex().imag);
            if (val.isNumber()) return Value(0.0);
            throw RuntimeError(expr->callee.line, "imag() argument must be a number or complex.");
        }
        if (calleeName == "conj") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "conj() expects exactly 1 argument.");
            }
            Value val = args[0];
            if (val.isComplex()) return Value(Complex(val.asComplex().real, -val.asComplex().imag));
            if (val.isNumber()) return Value(val.asNumber());
            throw RuntimeError(expr->callee.line, "conj() argument must be a number or complex.");
        }
        if (calleeName == "arg") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "arg() expects exactly 1 argument.");
            }
            Value val = args[0];
            if (val.isComplex()) return Value(std::atan2(val.asComplex().imag, val.asComplex().real));
            if (val.isNumber()) return Value(val.asNumber() >= 0.0 ? 0.0 : 3.14159265358979323846);
            throw RuntimeError(expr->callee.line, "arg() argument must be a number or complex.");
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
        if (calleeName == "form") {
            if (args.size() != 3) {
                throw RuntimeError(expr->callee.line, "form() expects exactly 3 arguments (title, width, height).");
            }
            if (!args[0].isString() || !args[1].isNumber() || !args[2].isNumber()) {
                throw RuntimeError(expr->callee.line, "form() arguments must be (string, number, number).");
            }
            std::string title = args[0].asString();
            int w = static_cast<int>(args[1].asNumber());
            int h = static_cast<int>(args[2].asNumber());
            
            auto frm = std::make_shared<FormInstance>(title, w, h);
            return Value(frm);
        }
        if (calleeName == "readFile") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "readFile() expects exactly 1 argument (path).");
            }
            if (!args[0].isString()) {
                throw RuntimeError(expr->callee.line, "readFile() argument must be a string.");
            }
            try {
                return Value(readTextFile(args[0].asString()));
            } catch (const std::exception& e) {
                throw RuntimeError(expr->callee.line, e.what());
            }
        }
        if (calleeName == "writeFile") {
            if (args.size() != 2) {
                throw RuntimeError(expr->callee.line, "writeFile() expects exactly 2 arguments (path, content).");
            }
            if (!args[0].isString()) {
                throw RuntimeError(expr->callee.line, "writeFile() path argument must be a string.");
            }
            try {
                writeTextFile(args[0].asString(), args[1].toString());
                return Value();
            } catch (const std::exception& e) {
                throw RuntimeError(expr->callee.line, e.what());
            }
        }
        if (calleeName == "appendFile") {
            if (args.size() != 2) {
                throw RuntimeError(expr->callee.line, "appendFile() expects exactly 2 arguments (path, content).");
            }
            if (!args[0].isString()) {
                throw RuntimeError(expr->callee.line, "appendFile() path argument must be a string.");
            }
            try {
                appendTextFile(args[0].asString(), args[1].toString());
                return Value();
            } catch (const std::exception& e) {
                throw RuntimeError(expr->callee.line, e.what());
            }
        }
        if (calleeName == "exists") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "exists() expects exactly 1 argument (path).");
            }
            if (!args[0].isString()) {
                throw RuntimeError(expr->callee.line, "exists() argument must be a string.");
            }
            try {
                return Value(fileOrDirExists(args[0].asString()));
            } catch (const std::exception& e) {
                throw RuntimeError(expr->callee.line, e.what());
            }
        }
        if (calleeName == "substring") {
            if (args.size() != 2 && args.size() != 3) {
                throw RuntimeError(expr->callee.line, "substring() expects 2 or 3 arguments.");
            }
            if (!args[0].isString() || !args[1].isNumber()) {
                throw RuntimeError(expr->callee.line, "substring() arguments must be (string, number, [number]).");
            }
            const std::string& str = args[0].asString();
            double dStart = args[1].asNumber();
            if (std::floor(dStart) != dStart) {
                throw RuntimeError(expr->callee.line, "substring() start index must be an integer.");
            }
            int start = static_cast<int>(dStart);
            if (start < 0 || start > static_cast<int>(str.length())) {
                throw RuntimeError(expr->callee.line, "substring() start index out of bounds.");
            }
            if (args.size() == 3) {
                if (!args[2].isNumber()) {
                    throw RuntimeError(expr->callee.line, "substring() length must be a number.");
                }
                double dLen = args[2].asNumber();
                if (std::floor(dLen) != dLen) {
                    throw RuntimeError(expr->callee.line, "substring() length must be an integer.");
                }
                int len = static_cast<int>(dLen);
                if (len < 0) {
                    throw RuntimeError(expr->callee.line, "substring() length cannot be negative.");
                }
                int actualLen = std::min(len, static_cast<int>(str.length()) - start);
                return Value(str.substr(start, actualLen));
            } else {
                return Value(str.substr(start));
            }
        }
        if (calleeName == "split") {
            if (args.size() != 2) {
                throw RuntimeError(expr->callee.line, "split() expects exactly 2 arguments.");
            }
            if (!args[0].isString() || !args[1].isString()) {
                throw RuntimeError(expr->callee.line, "split() arguments must be (string, string).");
            }
            const std::string& str = args[0].asString();
            const std::string& delim = args[1].asString();
            auto result = std::make_shared<std::vector<Value>>();
            
            if (delim.empty()) {
                for (char c : str) {
                    result->push_back(Value(std::string(1, c)));
                }
            } else {
                size_t startPos = 0;
                size_t endPos = str.find(delim);
                while (endPos != std::string::npos) {
                    result->push_back(Value(str.substr(startPos, endPos - startPos)));
                    startPos = endPos + delim.length();
                    endPos = str.find(delim, startPos);
                }
                result->push_back(Value(str.substr(startPos)));
            }
            return Value(result);
        }
        if (calleeName == "trim") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "trim() expects exactly 1 argument.");
            }
            if (!args[0].isString()) {
                throw RuntimeError(expr->callee.line, "trim() argument must be a string.");
            }
            std::string str = args[0].asString();
            str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](unsigned char ch) {
                return !std::isspace(ch);
            }));
            str.erase(std::find_if(str.rbegin(), str.rend(), [](unsigned char ch) {
                return !std::isspace(ch);
            }).base(), str.end());
            return Value(str);
        }
        if (calleeName == "lower") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "lower() expects exactly 1 argument.");
            }
            if (!args[0].isString()) {
                throw RuntimeError(expr->callee.line, "lower() argument must be a string.");
            }
            std::string str = args[0].asString();
            std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c){ return std::tolower(c); });
            return Value(str);
        }
        if (calleeName == "upper") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "upper() expects exactly 1 argument.");
            }
            if (!args[0].isString()) {
                throw RuntimeError(expr->callee.line, "upper() argument must be a string.");
            }
            std::string str = args[0].asString();
            std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c){ return std::toupper(c); });
            return Value(str);
        }
        if (calleeName == "replace") {
            if (args.size() != 3) {
                throw RuntimeError(expr->callee.line, "replace() expects exactly 3 arguments.");
            }
            if (!args[0].isString() || !args[1].isString() || !args[2].isString()) {
                throw RuntimeError(expr->callee.line, "replace() arguments must be (string, string, string).");
            }
            std::string str = args[0].asString();
            const std::string& oldStr = args[1].asString();
            const std::string& newStr = args[2].asString();
            if (oldStr.empty()) return Value(str);
            size_t pos = 0;
            while ((pos = str.find(oldStr, pos)) != std::string::npos) {
                str.replace(pos, oldStr.length(), newStr);
                pos += newStr.length();
            }
            return Value(str);
        }
        if (calleeName == "indexOf") {
            if (args.size() != 2) {
                throw RuntimeError(expr->callee.line, "indexOf() expects exactly 2 arguments.");
            }
            Value target = args[0];
            Value searchVal = args[1];
            if (target.isString()) {
                if (!searchVal.isString()) {
                    throw RuntimeError(expr->callee.line, "indexOf() second argument must be a string if first argument is a string.");
                }
                size_t idx = target.asString().find(searchVal.asString());
                if (idx == std::string::npos) return Value(-1.0);
                return Value(static_cast<double>(idx));
            } else if (target.isArray()) {
                auto arr = target.asArray();
                for (size_t i = 0; i < arr->size(); ++i) {
                    if (valuesEqual((*arr)[i], searchVal)) {
                        return Value(static_cast<double>(i));
                    }
                }
                return Value(-1.0);
            } else {
                throw RuntimeError(expr->callee.line, "indexOf() first argument must be a string or an array.");
            }
        }
        if (calleeName == "contains") {
            if (args.size() != 2) {
                throw RuntimeError(expr->callee.line, "contains() expects exactly 2 arguments.");
            }
            Value target = args[0];
            Value searchVal = args[1];
            if (target.isString()) {
                if (!searchVal.isString()) {
                    throw RuntimeError(expr->callee.line, "contains() second argument must be a string if first argument is a string.");
                }
                size_t idx = target.asString().find(searchVal.asString());
                return Value(idx != std::string::npos);
            } else if (target.isArray()) {
                auto arr = target.asArray();
                for (size_t i = 0; i < arr->size(); ++i) {
                    if (valuesEqual((*arr)[i], searchVal)) {
                        return Value(true);
                    }
                }
                return Value(false);
            } else {
                throw RuntimeError(expr->callee.line, "contains() first argument must be a string or an array.");
            }
        }
        if (calleeName == "join") {
            if (args.size() != 2) {
                throw RuntimeError(expr->callee.line, "join() expects exactly 2 arguments.");
            }
            if (!args[0].isArray() || !args[1].isString()) {
                throw RuntimeError(expr->callee.line, "join() arguments must be (array, string).");
            }
            auto arr = args[0].asArray();
            const std::string& delim = args[1].asString();
            std::string result = "";
            for (size_t i = 0; i < arr->size(); ++i) {
                if (i > 0) result += delim;
                result += (*arr)[i].toString();
            }
            return Value(result);
        }
        if (calleeName == "slice") {
            if (args.size() != 2 && args.size() != 3) {
                throw RuntimeError(expr->callee.line, "slice() expects 2 or 3 arguments.");
            }
            Value target = args[0];
            if (!args[1].isNumber()) {
                throw RuntimeError(expr->callee.line, "slice() start index must be a number.");
            }
            double dStart = args[1].asNumber();
            if (std::floor(dStart) != dStart) {
                throw RuntimeError(expr->callee.line, "slice() start index must be an integer.");
            }
            int start = static_cast<int>(dStart);
            
            int length = 0;
            if (target.isString()) length = static_cast<int>(target.asString().length());
            else if (target.isArray()) length = static_cast<int>(target.asArray()->size());
            else {
                throw RuntimeError(expr->callee.line, "slice() first argument must be a string or an array.");
            }

            if (start < 0) start = std::max(0, length + start);
            else start = std::min(length, start);

            int end = length;
            if (args.size() == 3) {
                if (!args[2].isNumber()) {
                    throw RuntimeError(expr->callee.line, "slice() end index must be a number.");
                }
                double dEnd = args[2].asNumber();
                if (std::floor(dEnd) != dEnd) {
                    throw RuntimeError(expr->callee.line, "slice() end index must be an integer.");
                }
                end = static_cast<int>(dEnd);
                if (end < 0) end = std::max(0, length + end);
                else end = std::min(length, end);
            }

            if (end < start) end = start;

            if (target.isString()) {
                return Value(target.asString().substr(start, end - start));
            } else {
                auto srcArr = target.asArray();
                auto result = std::make_shared<std::vector<Value>>();
                for (int i = start; i < end; ++i) {
                    result->push_back((*srcArr)[i]);
                }
                return Value(result);
            }
        }
        if (calleeName == "range") {
            if (args.size() < 1 || args.size() > 3) {
                throw RuntimeError(expr->callee.line, "range() expects 1, 2, or 3 arguments.");
            }
            for (size_t i = 0; i < args.size(); ++i) {
                if (!args[i].isNumber()) {
                    throw RuntimeError(expr->callee.line, "range() arguments must be numbers.");
                }
            }
            double start = 0;
            double end = 0;
            double step = 1;
            if (args.size() == 1) {
                end = args[0].asNumber();
            } else if (args.size() == 2) {
                start = args[0].asNumber();
                end = args[1].asNumber();
            } else {
                start = args[0].asNumber();
                end = args[1].asNumber();
                step = args[2].asNumber();
            }

            if (step == 0.0) {
                throw RuntimeError(expr->callee.line, "range() step cannot be zero.");
            }

            auto result = std::make_shared<std::vector<Value>>();
            if (step > 0.0) {
                for (double val = start; val < end; val += step) {
                    if (result->size() > 100000) {
                        throw RuntimeError(expr->callee.line, "range() output exceeds maximum array size limit (100,000).");
                    }
                    result->push_back(Value(val));
                }
            } else {
                for (double val = start; val > end; val += step) {
                    if (result->size() > 100000) {
                        throw RuntimeError(expr->callee.line, "range() output exceeds maximum array size limit (100,000).");
                    }
                    result->push_back(Value(val));
                }
            }
            return Value(result);
        }
        if (calleeName == "time") {
            if (args.size() != 0) {
                throw RuntimeError(expr->callee.line, "time() expects exactly 0 arguments.");
            }
            auto now = std::chrono::system_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            return Value(static_cast<double>(ms));
        }

        if (calleeName == "sound") {
            if (args.size() != 1) {
                throw RuntimeError(expr->callee.line, "sound() expects exactly 1 argument (filePath).");
            }
            if (!args[0].isString()) {
                throw RuntimeError(expr->callee.line, "sound() argument must be a string.");
            }
            auto snd = std::make_shared<SoundInstance>(args[0].asString());
            return Value(snd);
        }
        if (calleeName == "beep") {
            double freq = 1000.0;
            double dur = 200.0;
            if (args.size() > 2) {
                throw RuntimeError(expr->callee.line, "beep() expects at most 2 arguments (frequency, duration).");
            }
            if (args.size() >= 1) {
                if (!args[0].isNumber()) throw RuntimeError(expr->callee.line, "beep() frequency must be a number.");
                freq = args[0].asNumber();
            }
            if (args.size() == 2) {
                if (!args[1].isNumber()) throw RuntimeError(expr->callee.line, "beep() duration must be a number.");
                dur = args[1].asNumber();
            }
            std::thread([freq, dur]() {
                ::Beep(static_cast<DWORD>(freq), static_cast<DWORD>(dur));
            }).detach();
            return Value();
        }

        if (calleeName == "hash") {
            if (args.size() < 1 || args.size() > 2) {
                throw RuntimeError(expr->callee.line, "hash() expects 1 or 2 arguments: (data, [algorithm]).");
            }
            if (!args[0].isString()) {
                throw RuntimeError(expr->callee.line, "hash() first argument must be a string.");
            }
            std::string algorithm = "sha256";
            if (args.size() == 2) {
                if (!args[1].isString()) {
                    throw RuntimeError(expr->callee.line, "hash() second argument (algorithm) must be a string.");
                }
                algorithm = args[1].asString();
                std::transform(algorithm.begin(), algorithm.end(), algorithm.begin(), [](unsigned char c) {
                    return std::tolower(c);
                });
            }

            ALG_ID algId = CALG_SHA_256;
            if (algorithm == "md5") {
                algId = CALG_MD5;
            } else if (algorithm == "sha1" || algorithm == "sha-1") {
                algId = CALG_SHA1;
            } else if (algorithm == "sha256" || algorithm == "sha-256") {
                algId = CALG_SHA_256;
            } else if (algorithm == "sha384" || algorithm == "sha-384") {
                algId = CALG_SHA_384;
            } else if (algorithm == "sha512" || algorithm == "sha-512") {
                algId = CALG_SHA_512;
            } else {
                throw RuntimeError(expr->callee.line, "Unsupported hashing algorithm: '" + algorithm + "'. Supported: md5, sha1, sha256, sha384, sha512.");
            }

            std::string hashResult = computeHash(args[0].asString(), algId);
            if (hashResult.empty()) {
                throw RuntimeError(expr->callee.line, "Failed to compute hash.");
            }
            return Value(hashResult);
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
        if (target.isTable()) {
            auto tbl = target.asTable();
            if (idx < 0 || idx >= static_cast<int>(tbl->rows.size())) {
                throw RuntimeError(expr->bracket.line, "Table row index " + std::to_string(idx) + " out of bounds (size " + std::to_string(tbl->rows.size()) + ").");
            }
            return Value(tbl->rows[idx]);
        }

        throw RuntimeError(expr->bracket.line, "Only arrays, strings, and tables can be indexed.");
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
        std::string methodName = expr->method.lexeme;
        
        std::vector<Value> args;
        for (auto& argExpr : expr->arguments) {
            args.push_back(evaluate(argExpr));
        }

        if (obj.isWindow()) {
            auto win = obj.asWindow();
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

            if (methodName == "addBox") {
                if (args.size() != 7) throw RuntimeError(expr->method.line, "addBox() expects exactly 7 arguments (x, y, z, sx, sy, sz, color).");
                if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber() || !args[3].isNumber() || !args[4].isNumber() || !args[5].isNumber() || !args[6].isString()) {
                    throw RuntimeError(expr->method.line, "addBox() expects (number, number, number, number, number, number, string).");
                }
                win->addBox(args[0].asNumber(), args[1].asNumber(), args[2].asNumber(), args[3].asNumber(), args[4].asNumber(), args[5].asNumber(), args[6].asString());
                return obj;
            }

            if (methodName == "addTree") {
                if (args.size() != 5) throw RuntimeError(expr->method.line, "addTree() expects exactly 5 arguments (x, y, z, trunkHeight, foliageSize).");
                if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber() || !args[3].isNumber() || !args[4].isNumber()) {
                    throw RuntimeError(expr->method.line, "addTree() expects (number, number, number, number, number).");
                }
                win->addTree(args[0].asNumber(), args[1].asNumber(), args[2].asNumber(), args[3].asNumber(), args[4].asNumber());
                return obj;
            }

            if (methodName == "addModel") {
                if (args.size() != 11) throw RuntimeError(expr->method.line, "addModel() expects exactly 11 arguments (x, y, z, sx, sy, sz, rx, ry, rz, modelPath, color).");
                if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber() || !args[3].isNumber() || !args[4].isNumber() || !args[5].isNumber() || !args[6].isNumber() || !args[7].isNumber() || !args[8].isNumber() || !args[9].isString() || !args[10].isString()) {
                    throw RuntimeError(expr->method.line, "addModel() expects (number, number, number, number, number, number, number, number, number, string, string).");
                }
                win->addModel(args[0].asNumber(), args[1].asNumber(), args[2].asNumber(), args[3].asNumber(), args[4].asNumber(), args[5].asNumber(), args[6].asNumber(), args[7].asNumber(), args[8].asNumber(), args[9].asString(), args[10].asString());
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

            if (methodName == "setGravity") {
                if (args.size() != 1) throw RuntimeError(expr->method.line, "setGravity() expects exactly 1 argument (gravityValue).");
                if (!args[0].isNumber()) throw RuntimeError(expr->method.line, "setGravity() argument must be a number.");
                win->setGravity(args[0].asNumber());
                return obj;
            }

            if (methodName == "enableGravity") {
                if (args.size() != 1) throw RuntimeError(expr->method.line, "enableGravity() expects exactly 1 argument (boolean).");
                win->enableGravity(args[0].isTruthy());
                return obj;
            }

            if (methodName == "setCameraMode") {
                if (args.size() != 1) throw RuntimeError(expr->method.line, "setCameraMode() expects exactly 1 argument (string).");
                if (!args[0].isString()) throw RuntimeError(expr->method.line, "setCameraMode() argument must be a string (\"first\", \"second\", \"third\").");
                win->setCameraMode(args[0].asString());
                return obj;
            }

            if (methodName == "setSecondPersonCamera") {
                if (args.size() != 3) throw RuntimeError(expr->method.line, "setSecondPersonCamera() expects exactly 3 arguments (x, y, z).");
                if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber()) {
                    throw RuntimeError(expr->method.line, "setSecondPersonCamera() arguments must be (number, number, number).");
                }
                win->setSecondPersonCamera(args[0].asNumber(), args[1].asNumber(), args[2].asNumber());
                return obj;
            }

            if (methodName == "jump") {
                if (args.size() != 1) throw RuntimeError(expr->method.line, "jump() expects exactly 1 argument (force).");
                if (!args[0].isNumber()) throw RuntimeError(expr->method.line, "jump() argument must be a number.");
                win->jump(args[0].asNumber());
                return obj;
            }

            if (methodName == "getPlayerX") {
                if (args.size() != 0) throw RuntimeError(expr->method.line, "getPlayerX() expects 0 arguments.");
                return Value(win->getPlayerX());
            }

            if (methodName == "getPlayerY") {
                if (args.size() != 0) throw RuntimeError(expr->method.line, "getPlayerY() expects 0 arguments.");
                return Value(win->getPlayerY());
            }

            if (methodName == "getPlayerZ") {
                if (args.size() != 0) throw RuntimeError(expr->method.line, "getPlayerZ() expects 0 arguments.");
                return Value(win->getPlayerZ());
            }

            if (methodName == "isGrounded") {
                if (args.size() != 0) throw RuntimeError(expr->method.line, "isGrounded() expects 0 arguments.");
                return Value(win->getIsGrounded());
            }

            throw RuntimeError(expr->method.line, "Unknown method '" + methodName + "' on window object.");
        } else if (obj.isSound()) {
            auto snd = obj.asSound();
            if (methodName == "play") {
                bool loop = false;
                if (args.size() > 1) throw RuntimeError(expr->method.line, "play() expects at most 1 argument (loop).");
                if (args.size() == 1) {
                    if (!args[0].isBool()) throw RuntimeError(expr->method.line, "play() argument must be a boolean.");
                    loop = args[0].asBool();
                }
                snd->play(loop);
                return obj;
            }
            if (methodName == "pause") {
                if (args.size() != 0) throw RuntimeError(expr->method.line, "pause() expects 0 arguments.");
                snd->pause();
                return obj;
            }
            if (methodName == "resume") {
                if (args.size() != 0) throw RuntimeError(expr->method.line, "resume() expects 0 arguments.");
                snd->resume();
                return obj;
            }
            if (methodName == "stop") {
                if (args.size() != 0) throw RuntimeError(expr->method.line, "stop() expects 0 arguments.");
                snd->stop();
                return obj;
            }
            if (methodName == "volume") {
                if (args.size() != 1) throw RuntimeError(expr->method.line, "volume() expects exactly 1 argument (0-100).");
                if (!args[0].isNumber()) throw RuntimeError(expr->method.line, "volume() argument must be a number.");
                snd->setVolume(args[0].asNumber());
                return obj;
            }
            if (methodName == "close") {
                if (args.size() != 0) throw RuntimeError(expr->method.line, "close() expects 0 arguments.");
                snd->close();
                return obj;
            }

            throw RuntimeError(expr->method.line, "Unknown method '" + methodName + "' on sound object.");
        } else if (obj.isForm()) {
            auto frm = obj.asForm();
            if (methodName == "addButton") {
                if (args.size() != 6) throw RuntimeError(expr->method.line, "addButton() expects exactly 6 arguments (name, text, x, y, width, height).");
                if (!args[0].isString() || !args[1].isString() || !args[2].isNumber() || !args[3].isNumber() || !args[4].isNumber() || !args[5].isNumber()) {
                    throw RuntimeError(expr->method.line, "addButton() arguments must be (string, string, number, number, number, number).");
                }
                frm->addButton(args[0].asString(), args[1].asString(),
                               static_cast<int>(args[2].asNumber()), static_cast<int>(args[3].asNumber()),
                               static_cast<int>(args[4].asNumber()), static_cast<int>(args[5].asNumber()));
                return obj;
            }
            if (methodName == "addLabel") {
                if (args.size() != 6) throw RuntimeError(expr->method.line, "addLabel() expects exactly 6 arguments (name, text, x, y, width, height).");
                if (!args[0].isString() || !args[1].isString() || !args[2].isNumber() || !args[3].isNumber() || !args[4].isNumber() || !args[5].isNumber()) {
                    throw RuntimeError(expr->method.line, "addLabel() arguments must be (string, string, number, number, number, number).");
                }
                frm->addLabel(args[0].asString(), args[1].asString(),
                              static_cast<int>(args[2].asNumber()), static_cast<int>(args[3].asNumber()),
                              static_cast<int>(args[4].asNumber()), static_cast<int>(args[5].asNumber()));
                return obj;
            }
            if (methodName == "addTextBox") {
                if (args.size() != 6) throw RuntimeError(expr->method.line, "addTextBox() expects exactly 6 arguments (name, text, x, y, width, height).");
                if (!args[0].isString() || !args[1].isString() || !args[2].isNumber() || !args[3].isNumber() || !args[4].isNumber() || !args[5].isNumber()) {
                    throw RuntimeError(expr->method.line, "addTextBox() arguments must be (string, string, number, number, number, number).");
                }
                frm->addTextBox(args[0].asString(), args[1].asString(),
                                static_cast<int>(args[2].asNumber()), static_cast<int>(args[3].asNumber()),
                                static_cast<int>(args[4].asNumber()), static_cast<int>(args[5].asNumber()));
                return obj;
            }
            if (methodName == "addCheckBox") {
                if (args.size() != 6) throw RuntimeError(expr->method.line, "addCheckBox() expects exactly 6 arguments (name, text, x, y, width, height).");
                if (!args[0].isString() || !args[1].isString() || !args[2].isNumber() || !args[3].isNumber() || !args[4].isNumber() || !args[5].isNumber()) {
                    throw RuntimeError(expr->method.line, "addCheckBox() arguments must be (string, string, number, number, number, number).");
                }
                frm->addCheckBox(args[0].asString(), args[1].asString(),
                                 static_cast<int>(args[2].asNumber()), static_cast<int>(args[3].asNumber()),
                                 static_cast<int>(args[4].asNumber()), static_cast<int>(args[5].asNumber()));
                return obj;
            }
            if (methodName == "clicked") {
                if (args.size() != 1) throw RuntimeError(expr->method.line, "clicked() expects exactly 1 argument (name).");
                if (!args[0].isString()) throw RuntimeError(expr->method.line, "clicked() argument must be a string name.");
                return Value(frm->clicked(args[0].asString()));
            }
            if (methodName == "get") {
                if (args.size() != 1) throw RuntimeError(expr->method.line, "get() expects exactly 1 argument (name).");
                if (!args[0].isString()) throw RuntimeError(expr->method.line, "get() argument must be a string name.");
                return Value(frm->get(args[0].asString()));
            }
            if (methodName == "set") {
                if (args.size() != 2) throw RuntimeError(expr->method.line, "set() expects exactly 2 arguments (name, value).");
                if (!args[0].isString()) throw RuntimeError(expr->method.line, "set() first argument must be a string name.");
                frm->set(args[0].asString(), args[1].toString());
                return obj;
            }
            if (methodName == "isOpen") {
                if (args.size() != 0) throw RuntimeError(expr->method.line, "isOpen() expects 0 arguments.");
                return Value(frm->isOpen());
            }
            if (methodName == "close") {
                if (args.size() != 0) throw RuntimeError(expr->method.line, "close() expects 0 arguments.");
                frm->close();
                return obj;
            }
            throw RuntimeError(expr->method.line, "Unknown method '" + methodName + "' on form object.");
        } else if (obj.isDatabase()) {
            auto db = obj.asDatabase();
            if (methodName == "query") {
                if (args.size() != 1) {
                    throw RuntimeError(expr->method.line, "query() expects exactly 1 argument (SQL query).");
                }
                if (!args[0].isString()) {
                    throw RuntimeError(expr->method.line, "query() argument must be a string containing SQL.");
                }
                try {
                    return db->query(args[0].asString());
                } catch (const std::exception& e) {
                    throw RuntimeError(expr->method.line, e.what());
                }
            }
            throw RuntimeError(expr->method.line, "Unknown method '" + methodName + "' on database object.");
        }

        throw RuntimeError(expr->method.line, "Only window, sound, form, and database objects support method calls (attempted method call '" + methodName + "' on type '" + obj.typeString() + "').");
    }

    Value visitPropertyAccessExpr(PropertyAccessExpr* expr) override {
        Value obj = evaluate(expr->object);
        std::string propName = expr->property.lexeme;

        if (obj.isEntry()) {
            auto entry = obj.asEntry();
            bool found = false;
            for (size_t i = 0; i < entry->columns.size(); ++i) {
                if (entry->columns[i] == propName) {
                    found = true;
                    return entry->values[i];
                }
            }
            if (!found) {
                std::string cols = "";
                for (size_t i = 0; i < entry->columns.size(); ++i) {
                    if (i > 0) cols += ", ";
                    cols += "'" + entry->columns[i] + "'";
                }
                throw RuntimeError(expr->property.line, "Column '" + propName + "' does not exist in the database entry. Available columns: [" + cols + "].");
            }
        }

        throw RuntimeError(expr->property.line, "Only database entry objects support property access (attempted property access '" + propName + "' on type '" + obj.typeString() + "').");
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

    void visitQuantumStmt(QuantumStmt* stmt) override {
        Value choicesVal = evaluate(stmt->choices);
        if (!choicesVal.isArray()) {
            throw RuntimeError(stmt->name.line, "Quantum choices must be an array.");
        }
        auto choicesArr = choicesVal.asArray();
        
        std::vector<std::thread> threads;
        for (const auto& choice : *choicesArr) {
            auto clonedEnv = this->environment->clone();
            clonedEnv->define(stmt->name.lexeme, choice);
            
            threads.push_back(std::thread([this, clonedEnv, stmt]() {
                try {
                    Interpreter subInterpreter;
                    subInterpreter.setEnvironment(clonedEnv);
                    subInterpreter.setEpilepsyState(this->getEpilepsyState());
                    
                    RegistryGuard guard(&subInterpreter);
                    
                    for (auto& s : stmt->body) {
                        subInterpreter.execute(s);
                    }
                } catch (const QuantumKilledException&) {
                    // Silently terminate when killed by other thread
                } catch (const QuantumBreakException&) {
                    // Silently terminate on qbreak
                } catch (const RuntimeError&) {
                    // Silently terminate on crashes
                } catch (const std::exception&) {
                    // Silently terminate on other std exceptions
                } catch (...) {
                    // Silently terminate on everything else
                }
            }));
        }
        
        for (auto& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }
    }

    void visitQbreakStmt(QbreakStmt* stmt) override {
        throw QuantumBreakException();
    }

    void visitQkillothersStmt(QkillothersStmt* stmt) override {
        std::lock_guard<std::mutex> lock(getRegistryMutex());
        for (auto* interp : getActiveInterpreters()) {
            if (interp != this) {
                interp->killed = true;
            }
        }
    }
};

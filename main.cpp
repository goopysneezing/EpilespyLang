#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <map>
#include <stdexcept>
#include <memory>

using namespace std;

// --- Lexer ---
enum class TokenType {
    LET, PRINT, IDENTIFIER, NUMBER, EQUALS, 
    PLUS, MINUS, MULTIPLY, DIVIDE, SEMICOLON, END_OF_FILE
};

struct Token {
    TokenType type;
    string value;
};

class Lexer {
    string source;
    size_t pos = 0;

public:
    Lexer(const string& source) : source(source) {}

    vector<Token> tokenize() {
        vector<Token> tokens;
        while (pos < source.length()) {
            char current = source[pos];

            if (isspace(current)) {
                pos++;
                continue;
            }

            if (isalpha(current)) {
                string id = "";
                while (pos < source.length() && isalnum(source[pos])) {
                    id += source[pos];
                    pos++;
                }
                if (id == "let") tokens.push_back({TokenType::LET, id});
                else if (id == "print") tokens.push_back({TokenType::PRINT, id});
                else tokens.push_back({TokenType::IDENTIFIER, id});
                continue;
            }

            if (isdigit(current)) {
                string num = "";
                while (pos < source.length() && isdigit(source[pos])) {
                    num += source[pos];
                    pos++;
                }
                tokens.push_back({TokenType::NUMBER, num});
                continue;
            }

            switch (current) {
                case '=': tokens.push_back({TokenType::EQUALS, "="}); break;
                case '+': tokens.push_back({TokenType::PLUS, "+"}); break;
                case '-': tokens.push_back({TokenType::MINUS, "-"}); break;
                case '*': tokens.push_back({TokenType::MULTIPLY, "*"}); break;
                case '/': tokens.push_back({TokenType::DIVIDE, "/"}); break;
                case ';': tokens.push_back({TokenType::SEMICOLON, ";"}); break;
                default: throw runtime_error("Unexpected character: " + string(1, current));
            }
            pos++;
        }
        tokens.push_back({TokenType::END_OF_FILE, ""});
        return tokens;
    }
};

// --- AST Nodes ---
struct ASTNode {
    virtual ~ASTNode() = default;
};

struct ExprNode : public ASTNode {};

struct NumberNode : public ExprNode {
    int value;
    NumberNode(int value) : value(value) {}
};

struct IdentifierNode : public ExprNode {
    string name;
    IdentifierNode(const string& name) : name(name) {}
};

struct BinaryOpNode : public ExprNode {
    unique_ptr<ExprNode> left;
    TokenType op;
    unique_ptr<ExprNode> right;
    BinaryOpNode(unique_ptr<ExprNode> left, TokenType op, unique_ptr<ExprNode> right)
        : left(move(left)), op(op), right(move(right)) {}
};

struct StmtNode : public ASTNode {};

struct LetStmtNode : public StmtNode {
    string name;
    unique_ptr<ExprNode> expr;
    LetStmtNode(const string& name, unique_ptr<ExprNode> expr)
        : name(name), expr(move(expr)) {}
};

struct PrintStmtNode : public StmtNode {
    unique_ptr<ExprNode> expr;
    PrintStmtNode(unique_ptr<ExprNode> expr) : expr(move(expr)) {}
};

// --- Parser ---
class Parser {
    vector<Token> tokens;
    size_t pos = 0;

    Token peek() { return tokens[pos]; }
    Token consume() { return tokens[pos++]; }
    Token expect(TokenType type) {
        if (peek().type == type) return consume();
        throw runtime_error("Unexpected token");
    }

public:
    Parser(const vector<Token>& tokens) : tokens(tokens) {}

    vector<unique_ptr<StmtNode>> parse() {
        vector<unique_ptr<StmtNode>> statements;
        while (peek().type != TokenType::END_OF_FILE) {
            statements.push_back(parseStatement());
        }
        return statements;
    }

private:
    unique_ptr<StmtNode> parseStatement() {
        if (peek().type == TokenType::LET) {
            consume(); // let
            string name = expect(TokenType::IDENTIFIER).value;
            expect(TokenType::EQUALS);
            auto expr = parseExpression();
            expect(TokenType::SEMICOLON);
            return make_unique<LetStmtNode>(name, move(expr));
        } else if (peek().type == TokenType::PRINT) {
            consume(); // print
            auto expr = parseExpression();
            expect(TokenType::SEMICOLON);
            return make_unique<PrintStmtNode>(move(expr));
        }
        throw runtime_error("Unknown statement");
    }

    unique_ptr<ExprNode> parseExpression() {
        return parseTerm();
    }

    unique_ptr<ExprNode> parseTerm() {
        auto left = parseFactor();
        while (peek().type == TokenType::PLUS || peek().type == TokenType::MINUS) {
            TokenType op = consume().type;
            auto right = parseFactor();
            left = make_unique<BinaryOpNode>(move(left), op, move(right));
        }
        return left;
    }

    unique_ptr<ExprNode> parseFactor() {
        auto left = parsePrimary();
        while (peek().type == TokenType::MULTIPLY || peek().type == TokenType::DIVIDE) {
            TokenType op = consume().type;
            auto right = parsePrimary();
            left = make_unique<BinaryOpNode>(move(left), op, move(right));
        }
        return left;
    }

    unique_ptr<ExprNode> parsePrimary() {
        if (peek().type == TokenType::NUMBER) {
            return make_unique<NumberNode>(stoi(consume().value));
        } else if (peek().type == TokenType::IDENTIFIER) {
            return make_unique<IdentifierNode>(consume().value);
        }
        throw runtime_error("Expected number or identifier");
    }
};

// --- Interpreter ---
class Interpreter {
    map<string, int> env;

public:
    void execute(const vector<unique_ptr<StmtNode>>& stmts) {
        for (const auto& stmt : stmts) {
            executeStmt(stmt.get());
        }
    }

private:
    void executeStmt(StmtNode* stmt) {
        if (auto letStmt = dynamic_cast<LetStmtNode*>(stmt)) {
            env[letStmt->name] = evaluate(letStmt->expr.get());
        } else if (auto printStmt = dynamic_cast<PrintStmtNode*>(stmt)) {
            cout << evaluate(printStmt->expr.get()) << endl;
        }
    }

    int evaluate(ExprNode* expr) {
        if (auto num = dynamic_cast<NumberNode*>(expr)) {
            return num->value;
        } else if (auto id = dynamic_cast<IdentifierNode*>(expr)) {
            if (env.count(id->name)) return env[id->name];
            throw runtime_error("Undefined variable: " + id->name);
        } else if (auto binOp = dynamic_cast<BinaryOpNode*>(expr)) {
            int left = evaluate(binOp->left.get());
            int right = evaluate(binOp->right.get());
            switch (binOp->op) {
                case TokenType::PLUS: return left + right;
                case TokenType::MINUS: return left - right;
                case TokenType::MULTIPLY: return left * right;
                case TokenType::DIVIDE: 
                    if (right == 0) throw runtime_error("Division by zero");
                    return left / right;
                default: throw runtime_error("Unknown operator");
            }
        }
        throw runtime_error("Unknown expression");
    }
};

int main() {
    string sourceCode = 
        "let a = 10;\n"
        "let b = 20;\n"
        "let result = a + b * 2;\n"
        "print result;\n";

    cout << "Executing EpilespyLang Code:\n" << sourceCode << "\n";
    cout << "Output:\n";

    try {
        Lexer lexer(sourceCode);
        vector<Token> tokens = lexer.tokenize();

        Parser parser(tokens);
        auto ast = parser.parse();

        Interpreter interpreter;
        interpreter.execute(ast);
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
    }

    return 0;
}

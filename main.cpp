#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <map>
#include <stdexcept>
#include <memory>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace std;

// --- Lexer ---
enum class TokenType {
    LET, PRINT, HELP, AUTO, IF, ELSE, WHILE, FOR,
    IDENTIFIER, NUMBER, EQUALS, 
    PLUS, MINUS, MULTIPLY, DIVIDE, 
    EQ_EQ, NOT_EQ, LESS, LESS_EQ, GREATER, GREATER_EQ,
    AND, OR, NOT,
    LPAREN, RPAREN, LBRACE, RBRACE,
    SEMICOLON, END_OF_FILE
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
                string lowerId = id;
                for (auto &c : lowerId) c = tolower(c);

                if (lowerId == "let") tokens.push_back({TokenType::LET, id});
                else if (lowerId == "print") tokens.push_back({TokenType::PRINT, id});
                else if (lowerId == "help") tokens.push_back({TokenType::HELP, id});
                else if (lowerId == "auto") tokens.push_back({TokenType::AUTO, id});
                else if (lowerId == "if") tokens.push_back({TokenType::IF, id});
                else if (lowerId == "else") tokens.push_back({TokenType::ELSE, id});
                else if (lowerId == "while") tokens.push_back({TokenType::WHILE, id});
                else if (lowerId == "for") tokens.push_back({TokenType::FOR, id});
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

            if (current == '=') {
                if (pos + 1 < source.length() && source[pos+1] == '=') { tokens.push_back({TokenType::EQ_EQ, "=="}); pos++; }
                else tokens.push_back({TokenType::EQUALS, "="});
            } else if (current == '!') {
                if (pos + 1 < source.length() && source[pos+1] == '=') { tokens.push_back({TokenType::NOT_EQ, "!="}); pos++; }
                else tokens.push_back({TokenType::NOT, "!"});
            } else if (current == '<') {
                if (pos + 1 < source.length() && source[pos+1] == '=') { tokens.push_back({TokenType::LESS_EQ, "<="}); pos++; }
                else tokens.push_back({TokenType::LESS, "<"});
            } else if (current == '>') {
                if (pos + 1 < source.length() && source[pos+1] == '=') { tokens.push_back({TokenType::GREATER_EQ, ">="}); pos++; }
                else tokens.push_back({TokenType::GREATER, ">"});
            } else if (current == '&' && pos + 1 < source.length() && source[pos+1] == '&') {
                tokens.push_back({TokenType::AND, "&&"}); pos++;
            } else if (current == '|' && pos + 1 < source.length() && source[pos+1] == '|') {
                tokens.push_back({TokenType::OR, "||"}); pos++;
            } else {
                switch (current) {
                    case '+': tokens.push_back({TokenType::PLUS, "+"}); break;
                    case '-': tokens.push_back({TokenType::MINUS, "-"}); break;
                    case '*': tokens.push_back({TokenType::MULTIPLY, "*"}); break;
                    case '/': tokens.push_back({TokenType::DIVIDE, "/"}); break;
                    case '(': tokens.push_back({TokenType::LPAREN, "("}); break;
                    case ')': tokens.push_back({TokenType::RPAREN, ")"}); break;
                    case '{': tokens.push_back({TokenType::LBRACE, "{"}); break;
                    case '}': tokens.push_back({TokenType::RBRACE, "}"}); break;
                    case ';': tokens.push_back({TokenType::SEMICOLON, ";"}); break;
                    default: throw runtime_error("Unexpected character: " + string(1, current));
                }
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

struct UnaryOpNode : public ExprNode {
    TokenType op;
    unique_ptr<ExprNode> expr;
    UnaryOpNode(TokenType op, unique_ptr<ExprNode> expr) : op(op), expr(move(expr)) {}
};

struct StmtNode : public ASTNode {};

struct LetStmtNode : public StmtNode {
    string name;
    unique_ptr<ExprNode> expr;
    LetStmtNode(const string& name, unique_ptr<ExprNode> expr) : name(name), expr(move(expr)) {}
};

struct AssignStmtNode : public StmtNode {
    string name;
    unique_ptr<ExprNode> expr;
    AssignStmtNode(const string& name, unique_ptr<ExprNode> expr) : name(name), expr(move(expr)) {}
};

struct PrintStmtNode : public StmtNode {
    unique_ptr<ExprNode> expr;
    PrintStmtNode(unique_ptr<ExprNode> expr) : expr(move(expr)) {}
};

struct HelpStmtNode : public StmtNode {};

struct BlockStmtNode : public StmtNode {
    vector<unique_ptr<StmtNode>> statements;
};

struct IfStmtNode : public StmtNode {
    unique_ptr<ExprNode> condition;
    unique_ptr<StmtNode> thenBranch;
    unique_ptr<StmtNode> elseBranch;
    IfStmtNode(unique_ptr<ExprNode> c, unique_ptr<StmtNode> t, unique_ptr<StmtNode> e)
        : condition(move(c)), thenBranch(move(t)), elseBranch(move(e)) {}
};

struct WhileStmtNode : public StmtNode {
    unique_ptr<ExprNode> condition;
    unique_ptr<StmtNode> body;
    WhileStmtNode(unique_ptr<ExprNode> c, unique_ptr<StmtNode> b) : condition(move(c)), body(move(b)) {}
};

struct ForStmtNode : public StmtNode {
    unique_ptr<StmtNode> init;
    unique_ptr<ExprNode> condition;
    unique_ptr<StmtNode> increment;
    unique_ptr<StmtNode> body;
    ForStmtNode(unique_ptr<StmtNode> init, unique_ptr<ExprNode> cond, unique_ptr<StmtNode> inc, unique_ptr<StmtNode> body)
        : init(move(init)), condition(move(cond)), increment(move(inc)), body(move(body)) {}
};

// --- Helper ---
int levenshtein(const string& s1, const string& s2) {
    size_t m = s1.size();
    size_t n = s2.size();
    vector<vector<int>> dp(m + 1, vector<int>(n + 1));
    for (size_t i = 0; i <= m; i++) dp[i][0] = i;
    for (size_t j = 0; j <= n; j++) dp[0][j] = j;
    for (size_t i = 1; i <= m; i++) {
        for (size_t j = 1; j <= n; j++) {
            if (tolower(s1[i - 1]) == tolower(s2[j - 1])) {
                dp[i][j] = dp[i - 1][j - 1];
            } else {
                dp[i][j] = 1 + min({dp[i - 1][j], dp[i][j - 1], dp[i - 1][j - 1]});
            }
        }
    }
    return dp[m][n];
}

// --- Parser ---
class Parser {
    vector<Token> tokens;
    size_t pos = 0;

    Token peek() { return tokens[pos]; }
    Token consume() { return tokens[pos++]; }
    Token expect(TokenType type) {
        if (peek().type == type) return consume();
        throw runtime_error("Unexpected token. Expected type ID: " + to_string((int)type) + " but got " + to_string((int)peek().type) + " ('" + peek().value + "')");
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
    unique_ptr<StmtNode> parseBlock() {
        auto block = make_unique<BlockStmtNode>();
        expect(TokenType::LBRACE);
        while (peek().type != TokenType::RBRACE && peek().type != TokenType::END_OF_FILE) {
            block->statements.push_back(parseStatement());
        }
        expect(TokenType::RBRACE);
        return block;
    }

    unique_ptr<StmtNode> parseStatement() {
        TokenType type = peek().type;
        bool isAssign = (type == TokenType::IDENTIFIER && pos + 1 < tokens.size() && tokens[pos+1].type == TokenType::EQUALS);

        if (type == TokenType::IDENTIFIER && !isAssign) {
            string val = peek().value;
            if (levenshtein(val, "let") <= 2) type = TokenType::LET;
            else if (levenshtein(val, "print") <= 2) type = TokenType::PRINT;
            else if (levenshtein(val, "help") <= 2) type = TokenType::HELP;
            else if (levenshtein(val, "auto") <= 2) type = TokenType::AUTO;
            else if (levenshtein(val, "if") <= 2) type = TokenType::IF;
            else if (levenshtein(val, "while") <= 2) type = TokenType::WHILE;
            else if (levenshtein(val, "for") <= 2) type = TokenType::FOR;
        }

        if (type == TokenType::AUTO || peek().type == TokenType::AUTO) {
            if (pos + 2 < tokens.size() && tokens[pos+1].type == TokenType::IDENTIFIER && tokens[pos+2].type == TokenType::EQUALS) {
                type = TokenType::LET;
            } else if (pos + 1 < tokens.size() && tokens[pos+1].type == TokenType::SEMICOLON) {
                type = TokenType::HELP;
            } else if (pos + 1 < tokens.size() && tokens[pos+1].type == TokenType::LPAREN) {
                type = TokenType::IF;
            } else {
                type = TokenType::PRINT;
            }
        }

        if (type == TokenType::LBRACE || peek().type == TokenType::LBRACE) {
            return parseBlock();
        }

        if (isAssign) {
            string name = consume().value; 
            consume(); // =
            auto expr = parseExpression();
            expect(TokenType::SEMICOLON);
            return make_unique<AssignStmtNode>(name, move(expr));
        }

        if (type == TokenType::LET || peek().type == TokenType::LET) {
            consume();
            string name = expect(TokenType::IDENTIFIER).value;
            expect(TokenType::EQUALS);
            auto expr = parseExpression();
            expect(TokenType::SEMICOLON);
            return make_unique<LetStmtNode>(name, move(expr));
        } else if (type == TokenType::PRINT || peek().type == TokenType::PRINT) {
            consume();
            auto expr = parseExpression();
            expect(TokenType::SEMICOLON);
            return make_unique<PrintStmtNode>(move(expr));
        } else if (type == TokenType::HELP || peek().type == TokenType::HELP) {
            consume();
            expect(TokenType::SEMICOLON);
            return make_unique<HelpStmtNode>();
        } else if (type == TokenType::IF || peek().type == TokenType::IF) {
            consume();
            expect(TokenType::LPAREN);
            auto cond = parseExpression();
            expect(TokenType::RPAREN);
            auto thenBranch = parseStatement();
            unique_ptr<StmtNode> elseBranch = nullptr;
            
            TokenType nextType = peek().type;
            if (nextType == TokenType::ELSE || (nextType == TokenType::IDENTIFIER && levenshtein(peek().value, "else") <= 2)) {
                consume();
                elseBranch = parseStatement();
            }
            return make_unique<IfStmtNode>(move(cond), move(thenBranch), move(elseBranch));
        } else if (type == TokenType::WHILE || peek().type == TokenType::WHILE) {
            consume();
            expect(TokenType::LPAREN);
            auto cond = parseExpression();
            expect(TokenType::RPAREN);
            auto body = parseStatement();
            return make_unique<WhileStmtNode>(move(cond), move(body));
        } else if (type == TokenType::FOR || peek().type == TokenType::FOR) {
            consume();
            expect(TokenType::LPAREN);
            unique_ptr<StmtNode> init = nullptr;
            if (peek().type != TokenType::SEMICOLON) {
                init = parseStatement(); // expects semicolon
            } else {
                consume();
            }
            
            unique_ptr<ExprNode> cond = nullptr;
            if (peek().type != TokenType::SEMICOLON) {
                cond = parseExpression();
            }
            expect(TokenType::SEMICOLON);
            
            unique_ptr<StmtNode> inc = nullptr;
            if (peek().type != TokenType::RPAREN) {
                TokenType incType = peek().type;
                if (incType == TokenType::LET || (incType == TokenType::IDENTIFIER && levenshtein(peek().value, "let") <= 2)) {
                    consume();
                    string name = expect(TokenType::IDENTIFIER).value;
                    expect(TokenType::EQUALS);
                    auto expr = parseExpression();
                    inc = make_unique<LetStmtNode>(name, move(expr));
                } else if (peek().type == TokenType::IDENTIFIER && pos + 1 < tokens.size() && tokens[pos+1].type == TokenType::EQUALS) {
                    string name = consume().value;
                    consume(); // =
                    auto expr = parseExpression();
                    inc = make_unique<AssignStmtNode>(name, move(expr));
                }
            }
            expect(TokenType::RPAREN);
            auto body = parseStatement();
            return make_unique<ForStmtNode>(move(init), move(cond), move(inc), move(body));
        }
        throw runtime_error("Unknown statement starting with token: " + peek().value);
    }

    unique_ptr<ExprNode> parseExpression() { return parseLogicalOr(); }

    unique_ptr<ExprNode> parseLogicalOr() {
        auto left = parseLogicalAnd();
        while (peek().type == TokenType::OR) {
            TokenType op = consume().type;
            auto right = parseLogicalAnd();
            left = make_unique<BinaryOpNode>(move(left), op, move(right));
        }
        return left;
    }

    unique_ptr<ExprNode> parseLogicalAnd() {
        auto left = parseEquality();
        while (peek().type == TokenType::AND) {
            TokenType op = consume().type;
            auto right = parseEquality();
            left = make_unique<BinaryOpNode>(move(left), op, move(right));
        }
        return left;
    }

    unique_ptr<ExprNode> parseEquality() {
        auto left = parseComparison();
        while (peek().type == TokenType::EQ_EQ || peek().type == TokenType::NOT_EQ) {
            TokenType op = consume().type;
            auto right = parseComparison();
            left = make_unique<BinaryOpNode>(move(left), op, move(right));
        }
        return left;
    }

    unique_ptr<ExprNode> parseComparison() {
        auto left = parseTerm();
        while (peek().type == TokenType::LESS || peek().type == TokenType::LESS_EQ ||
               peek().type == TokenType::GREATER || peek().type == TokenType::GREATER_EQ) {
            TokenType op = consume().type;
            auto right = parseTerm();
            left = make_unique<BinaryOpNode>(move(left), op, move(right));
        }
        return left;
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
        auto left = parseUnary();
        while (peek().type == TokenType::MULTIPLY || peek().type == TokenType::DIVIDE) {
            TokenType op = consume().type;
            auto right = parseUnary();
            left = make_unique<BinaryOpNode>(move(left), op, move(right));
        }
        return left;
    }

    unique_ptr<ExprNode> parseUnary() {
        if (peek().type == TokenType::NOT || peek().type == TokenType::MINUS) {
            TokenType op = consume().type;
            auto right = parseUnary();
            return make_unique<UnaryOpNode>(op, move(right));
        }
        return parsePrimary();
    }

    unique_ptr<ExprNode> parsePrimary() {
        if (peek().type == TokenType::NUMBER) {
            return make_unique<NumberNode>(stoi(consume().value));
        } else if (peek().type == TokenType::IDENTIFIER) {
            return make_unique<IdentifierNode>(consume().value);
        } else if (peek().type == TokenType::LPAREN) {
            consume();
            auto expr = parseExpression();
            expect(TokenType::RPAREN);
            return expr;
        }
        throw runtime_error("Expected number, identifier, or '(' but got '" + peek().value + "'");
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
        } else if (auto assignStmt = dynamic_cast<AssignStmtNode*>(stmt)) {
            if (!env.count(assignStmt->name)) {
                // If it's a very forgiving language, we might just let them assign undeclared vars.
                // But for now, let's keep it somewhat structured.
                env[assignStmt->name] = evaluate(assignStmt->expr.get()); 
            } else {
                env[assignStmt->name] = evaluate(assignStmt->expr.get());
            }
        } else if (auto printStmt = dynamic_cast<PrintStmtNode*>(stmt)) {
            cout << evaluate(printStmt->expr.get()) << endl;
        } else if (auto blockStmt = dynamic_cast<BlockStmtNode*>(stmt)) {
            for (const auto& s : blockStmt->statements) {
                executeStmt(s.get());
            }
        } else if (auto ifStmt = dynamic_cast<IfStmtNode*>(stmt)) {
            if (evaluate(ifStmt->condition.get()) != 0) {
                executeStmt(ifStmt->thenBranch.get());
            } else if (ifStmt->elseBranch) {
                executeStmt(ifStmt->elseBranch.get());
            }
        } else if (auto whileStmt = dynamic_cast<WhileStmtNode*>(stmt)) {
            while (evaluate(whileStmt->condition.get()) != 0) {
                executeStmt(whileStmt->body.get());
            }
        } else if (auto forStmt = dynamic_cast<ForStmtNode*>(stmt)) {
            if (forStmt->init) executeStmt(forStmt->init.get());
            while (true) {
                if (forStmt->condition && evaluate(forStmt->condition.get()) == 0) {
                    break;
                }
                executeStmt(forStmt->body.get());
                if (forStmt->increment) executeStmt(forStmt->increment.get());
            }
        } else if (dynamic_cast<HelpStmtNode*>(stmt)) {
            cout << "EpilespyLang Help:\n"
                 << "  let <var> = <expr>;   - Assign a value to a variable\n"
                 << "  <var> = <expr>;       - Reassign a variable\n"
                 << "  print <expr>;         - Print the value of an expression\n"
                 << "  if (cond) { ... }     - If statement (supports else)\n"
                 << "  while (cond) { ... }  - While loop\n"
                 << "  for (i=0; i<5; i=i+1) - For loop\n"
                 << "  help;                 - Show this help message\n"
                 << "  auto ...              - Automatically guess command\n"
                 << "Note: Commands are typo-tolerant (up to 2 mistakes)!\n";
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
            // Short-circuiting for logical operators
            if (binOp->op == TokenType::AND) {
                if (left == 0) return 0;
                return evaluate(binOp->right.get()) != 0 ? 1 : 0;
            }
            if (binOp->op == TokenType::OR) {
                if (left != 0) return 1;
                return evaluate(binOp->right.get()) != 0 ? 1 : 0;
            }

            int right = evaluate(binOp->right.get());
            switch (binOp->op) {
                case TokenType::PLUS: return left + right;
                case TokenType::MINUS: return left - right;
                case TokenType::MULTIPLY: return left * right;
                case TokenType::DIVIDE: 
                    if (right == 0) throw runtime_error("Division by zero");
                    return left / right;
                case TokenType::EQ_EQ: return left == right ? 1 : 0;
                case TokenType::NOT_EQ: return left != right ? 1 : 0;
                case TokenType::LESS: return left < right ? 1 : 0;
                case TokenType::LESS_EQ: return left <= right ? 1 : 0;
                case TokenType::GREATER: return left > right ? 1 : 0;
                case TokenType::GREATER_EQ: return left >= right ? 1 : 0;
                default: throw runtime_error("Unknown binary operator");
            }
        } else if (auto unOp = dynamic_cast<UnaryOpNode*>(expr)) {
            int right = evaluate(unOp->expr.get());
            if (unOp->op == TokenType::MINUS) return -right;
            if (unOp->op == TokenType::NOT) return right == 0 ? 1 : 0;
            throw runtime_error("Unknown unary operator");
        }
        throw runtime_error("Unknown expression");
    }
};

void run(const string& sourceCode, Interpreter& interpreter) {
    try {
        Lexer lexer(sourceCode);
        vector<Token> tokens = lexer.tokenize();
        if (tokens.empty() || tokens.front().type == TokenType::END_OF_FILE) return;

        Parser parser(tokens);
        auto ast = parser.parse();

        interpreter.execute(ast);
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
    }
}

void runFile(const string& path) {
    ifstream file(path);
    if (!file.is_open()) {
        cerr << "Could not open file: " << path << endl;
        return;
    }
    stringstream buffer;
    buffer << file.rdbuf();
    Interpreter interpreter;
    run(buffer.str(), interpreter);
}

void runPrompt() {
    Interpreter interpreter;
    string line;
    cout << "EpilespyLang REPL\nType 'exit' to quit.\n";
    while (true) {
        cout << "> ";
        if (!getline(cin, line)) break;
        if (line == "exit") break;
        if (line.empty()) continue;
        run(line, interpreter);
    }
}

int main(int argc, char* argv[]) {
    if (argc > 2) {
        cout << "Usage: epilespylang [script]" << endl;
        return 1;
    } else if (argc == 2) {
        runFile(argv[1]);
    } else {
        runPrompt();
    }
    return 0;
}

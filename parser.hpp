#pragma once
#include <vector>
#include <memory>
#include <string>
#include <stdexcept>
#include <iostream>
#include "ast.hpp"

class ParseError : public std::runtime_error {
public:
    Token token;
    ParseError(Token token, const std::string& message)
        : std::runtime_error(message), token(token) {}
};

class Parser {
private:
    std::vector<Token> tokens;
    int current = 0;

    bool isAtEnd() const {
        return peek().type == TokenType::EOF_VAL;
    }

    Token peek() const {
        return tokens[current];
    }

    Token previous() const {
        return tokens[current - 1];
    }

    Token advance() {
        if (!isAtEnd()) current++;
        return previous();
    }

    bool check(TokenType type) const {
        if (isAtEnd()) return false;
        return peek().type == type;
    }

    bool match(TokenType type) {
        if (check(type)) {
            advance();
            return true;
        }
        return false;
    }

    Token consume(TokenType type, const std::string& message) {
        if (check(type)) return advance();
        throw error(peek(), message);
    }

    ParseError error(Token token, const std::string& message) {
        if (token.type == TokenType::EOF_VAL) {
            return ParseError(token, "[Line " + std::to_string(token.line) + "] Error at end of file: " + message);
        } else {
            return ParseError(token, "[Line " + std::to_string(token.line) + "] Error at '" + token.lexeme + "': " + message);
        }
    }

    void skipNewlines() {
        while (match(TokenType::NEWLINE));
    }

    void consumeStatementTerminator() {
        if (match(TokenType::NEWLINE)) {
            skipNewlines();
            return;
        }
        if (match(TokenType::SEMICOLON)) {
            while (match(TokenType::NEWLINE) || match(TokenType::SEMICOLON));
            return;
        }
        if (check(TokenType::RIGHT_BRACE) || check(TokenType::EOF_VAL)) {
            return;
        }
        // Auto-fix missing statement terminator
        std::cout << "[Auto-Fix] Added missing semicolon\n";
    }

    // Expressions
    ExprPtr expression() {
        return assignment();
    }

    ExprPtr assignment() {
        ExprPtr expr = logicalOr();

        if (match(TokenType::EQUAL)) {
            Token equals = previous();
            ExprPtr value = assignment();

            if (auto varExpr = std::dynamic_pointer_cast<VariableExpr>(expr)) {
                return std::make_shared<AssignmentExpr>(varExpr->name, value);
            } else if (auto indexExpr = std::dynamic_pointer_cast<IndexExpr>(expr)) {
                return std::make_shared<IndexAssignmentExpr>(indexExpr->array, indexExpr->bracket, indexExpr->index, value);
            }

            throw error(equals, "Invalid assignment target.");
        }

        return expr;
    }

    ExprPtr logicalOr() {
        ExprPtr expr = logicalAnd();

        while (match(TokenType::OR)) {
            Token op = previous();
            skipNewlines();
            ExprPtr right = logicalAnd();
            expr = std::make_shared<BinaryExpr>(expr, op, right);
        }

        return expr;
    }

    ExprPtr logicalAnd() {
        ExprPtr expr = equality();

        while (match(TokenType::AND)) {
            Token op = previous();
            skipNewlines();
            ExprPtr right = equality();
            expr = std::make_shared<BinaryExpr>(expr, op, right);
        }

        return expr;
    }

    ExprPtr equality() {
        ExprPtr expr = comparison();

        while (match(TokenType::EQUAL_EQUAL) || match(TokenType::BANG_EQUAL)) {
            Token op = previous();
            skipNewlines();
            ExprPtr right = comparison();
            expr = std::make_shared<BinaryExpr>(expr, op, right);
        }

        return expr;
    }

    ExprPtr comparison() {
        ExprPtr expr = term();

        while (match(TokenType::LESS) || match(TokenType::LESS_EQUAL) ||
               match(TokenType::GREATER) || match(TokenType::GREATER_EQUAL)) {
            Token op = previous();
            skipNewlines();
            ExprPtr right = term();
            expr = std::make_shared<BinaryExpr>(expr, op, right);
        }

        return expr;
    }

    ExprPtr term() {
        ExprPtr expr = factor();

        while (match(TokenType::PLUS) || match(TokenType::MINUS)) {
            Token op = previous();
            skipNewlines();
            ExprPtr right = factor();
            expr = std::make_shared<BinaryExpr>(expr, op, right);
        }

        return expr;
    }

    ExprPtr factor() {
        ExprPtr expr = unary();

        while (match(TokenType::STAR) || match(TokenType::SLASH) || match(TokenType::PERCENT)) {
            Token op = previous();
            skipNewlines();
            ExprPtr right = unary();
            expr = std::make_shared<BinaryExpr>(expr, op, right);
        }

        return expr;
    }

    ExprPtr unary() {
        if (match(TokenType::NOT) || match(TokenType::BANG) || match(TokenType::MINUS)) {
            Token op = previous();
            ExprPtr right = unary();
            return std::make_shared<UnaryExpr>(op, right);
        }

        return call();
    }

    ExprPtr call() {
        ExprPtr expr = primary();

        while (true) {
            if (match(TokenType::LEFT_PAREN)) {
                expr = finishCall(expr);
            } else if (match(TokenType::LEFT_BRACKET)) {
                Token bracket = previous();
                ExprPtr index = expression();
                consume(TokenType::RIGHT_BRACKET, "Expect ']' after index.");
                expr = std::make_shared<IndexExpr>(expr, bracket, index);
            } else if (match(TokenType::DOT)) {
                Token name = consume(TokenType::IDENTIFIER, "Expect method name after '.'.");
                consume(TokenType::LEFT_PAREN, "Expect '(' after method name.");
                std::vector<ExprPtr> arguments;
                if (!check(TokenType::RIGHT_PAREN)) {
                    do {
                        skipNewlines();
                        arguments.push_back(expression());
                        skipNewlines();
                    } while (match(TokenType::COMMA));
                }
                consume(TokenType::RIGHT_PAREN, "Expect ')' after method arguments.");
                expr = std::make_shared<MethodCallExpr>(expr, name, arguments);
            } else {
                break;
            }
        }

        return expr;
    }

    ExprPtr finishCall(ExprPtr callee) {
        Token calleeToken;
        if (auto varExpr = std::dynamic_pointer_cast<VariableExpr>(callee)) {
            calleeToken = varExpr->name;
        } else {
            throw error(peek(), "Only identifiers can be called as functions.");
        }

        std::vector<ExprPtr> arguments;
        if (!check(TokenType::RIGHT_PAREN)) {
            do {
                skipNewlines();
                arguments.push_back(expression());
                skipNewlines();
            } while (match(TokenType::COMMA));
        }

        consume(TokenType::RIGHT_PAREN, "Expect ')' after arguments.");
        return std::make_shared<CallExpr>(calleeToken, arguments);
    }

    ExprPtr primary() {
        if (match(TokenType::FALSE)) return std::make_shared<LiteralExpr>(false);
        if (match(TokenType::TRUE)) return std::make_shared<LiteralExpr>(true);
        if (match(TokenType::NULL_VAL)) return std::make_shared<LiteralExpr>(Nil{});

        if (match(TokenType::NUMBER) || match(TokenType::STRING)) {
            return std::make_shared<LiteralExpr>(previous().literal);
        }

        if (match(TokenType::IDENTIFIER)) {
            return std::make_shared<VariableExpr>(previous());
        }

        if (match(TokenType::LEFT_PAREN)) {
            skipNewlines();
            ExprPtr expr = expression();
            skipNewlines();
            consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
            return expr;
        }

        if (match(TokenType::LEFT_BRACKET)) {
            Token bracket = previous();
            std::vector<ExprPtr> elements;
            if (!check(TokenType::RIGHT_BRACKET)) {
                do {
                    skipNewlines();
                    elements.push_back(expression());
                    skipNewlines();
                } while (match(TokenType::COMMA));
            }
            consume(TokenType::RIGHT_BRACKET, "Expect ']' after array literal.");
            return std::make_shared<ArrayExpr>(bracket, elements);
        }

        throw error(peek(), "Expect expression.");
    }

    // Statements
    StmtPtr statement() {
        skipNewlines();

        if (match(TokenType::LEFT_BRACE)) return block();
        if (match(TokenType::IF)) return ifStatement();
        if (match(TokenType::WHILE)) return whileStatement();
        if (match(TokenType::FOR)) return forStatement();
        if (match(TokenType::SWITCH)) return switchStatement();

        return expressionStatement();
    }

    StmtPtr block() {
        std::vector<StmtPtr> statements;
        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            skipNewlines();
            if (check(TokenType::RIGHT_BRACE)) break;
            statements.push_back(statement());
        }
        consume(TokenType::RIGHT_BRACE, "Expect '}' after block.");
        return std::make_shared<BlockStmt>(statements);
    }

    StmtPtr ifStatement() {
        bool hasLparen = match(TokenType::LEFT_PAREN);
        ExprPtr condition = expression();
        bool hasRparen = match(TokenType::RIGHT_PAREN);

        if (!hasLparen && !hasRparen) {
            std::cout << "[Auto-Fix] Added missing '(' and ')' for 'if' condition\n";
        } else if (!hasLparen) {
            std::cout << "[Auto-Fix] Added missing '(' for 'if' condition\n";
        } else if (!hasRparen) {
            std::cout << "[Auto-Fix] Added missing ')' for 'if' condition\n";
        }

        skipNewlines();
        StmtPtr thenBranch = statement();
        StmtPtr elseBranch = nullptr;

        // Peak ahead to see if the next non-newline is else
        int checkpoint = current;
        skipNewlines();
        if (match(TokenType::ELSE)) {
            skipNewlines();
            elseBranch = statement();
        } else {
            current = checkpoint; // backtrack
        }

        return std::make_shared<IfStmt>(condition, thenBranch, elseBranch);
    }

    StmtPtr whileStatement() {
        bool hasLparen = match(TokenType::LEFT_PAREN);
        ExprPtr condition = expression();
        bool hasRparen = match(TokenType::RIGHT_PAREN);

        if (!hasLparen && !hasRparen) {
            std::cout << "[Auto-Fix] Added missing '(' and ')' for 'while' condition\n";
        } else if (!hasLparen) {
            std::cout << "[Auto-Fix] Added missing '(' for 'while' condition\n";
        } else if (!hasRparen) {
            std::cout << "[Auto-Fix] Added missing ')' for 'while' condition\n";
        }

        skipNewlines();
        StmtPtr body = statement();
        return std::make_shared<WhileStmt>(condition, body);
    }

    StmtPtr forStatement() {
        bool hasLparen = match(TokenType::LEFT_PAREN);

        StmtPtr init = nullptr;
        if (!match(TokenType::SEMICOLON)) {
            ExprPtr initExpr = expression();
            consume(TokenType::SEMICOLON, "Expect ';' after for loop initializer.");
            init = std::make_shared<ExpressionStmt>(initExpr);
        }

        ExprPtr condition = nullptr;
        if (!match(TokenType::SEMICOLON)) {
            condition = expression();
            consume(TokenType::SEMICOLON, "Expect ';' after for loop condition.");
        }

        ExprPtr increment = nullptr;
        if (!check(TokenType::RIGHT_PAREN) && !check(TokenType::LEFT_BRACE) && !isAtEnd()) {
            increment = expression();
        }
        
        bool hasRparen = match(TokenType::RIGHT_PAREN);

        if (!hasLparen && !hasRparen) {
            std::cout << "[Auto-Fix] Added missing '(' and ')' for 'for' loop header\n";
        } else if (!hasLparen) {
            std::cout << "[Auto-Fix] Added missing '(' for 'for' loop header\n";
        } else if (!hasRparen) {
            std::cout << "[Auto-Fix] Added missing ')' for 'for' loop header\n";
        }

        skipNewlines();
        StmtPtr body = statement();

        return std::make_shared<ForStmt>(init, condition, increment, body);
    }

    StmtPtr switchStatement() {
        Token switchToken = previous();
        consume(TokenType::LEFT_PAREN, "Expect '(' after 'switch'.");
        ExprPtr expr = expression();
        consume(TokenType::RIGHT_PAREN, "Expect ')' after switch value.");

        skipNewlines();
        consume(TokenType::LEFT_BRACE, "Expect '{' before switch body.");

        std::vector<SwitchCase> cases;
        StmtPtr defaultBranch = nullptr;

        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            skipNewlines();
            if (check(TokenType::RIGHT_BRACE)) break;

            if (match(TokenType::CASE)) {
                ExprPtr caseVal = expression();
                consume(TokenType::COLON, "Expect ':' after case value.");
                skipNewlines();
                StmtPtr caseBody = statement();
                cases.push_back({caseVal, caseBody});
            } else if (match(TokenType::DEFAULT)) {
                consume(TokenType::COLON, "Expect ':' after default.");
                skipNewlines();
                StmtPtr defaultBody = statement();
                defaultBranch = defaultBody;
            } else {
                throw error(peek(), "Expect 'case' or 'default' inside switch.");
            }
        }

        consume(TokenType::RIGHT_BRACE, "Expect '}' after switch body.");
        return std::make_shared<SwitchStmt>(switchToken, expr, cases, defaultBranch);
    }

    StmtPtr expressionStatement() {
        ExprPtr expr = expression();
        consumeStatementTerminator();
        return std::make_shared<ExpressionStmt>(expr);
    }

public:
    Parser(const std::vector<Token>& tokens) : tokens(tokens) {}

    std::vector<StmtPtr> parse() {
        std::vector<StmtPtr> statements;
        skipNewlines();
        while (!isAtEnd()) {
            statements.push_back(statement());
            skipNewlines();
        }
        return statements;
    }
};

#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include "ast.hpp"

class Lexer {
private:
    std::string source;
    std::vector<Token> tokens;
    int start = 0;
    int current = 0;
    int line = 1;

    static const std::unordered_map<std::string, TokenType> keywords;

    bool isAtEnd() const {
        return current >= source.length();
    }

    char advance() {
        return source[current++];
    }

    char peek() const {
        if (isAtEnd()) return '\0';
        return source[current];
    }

    char peekNext() const {
        if (current + 1 >= source.length()) return '\0';
        return source[current + 1];
    }

    bool matchChar(char expected) {
        if (isAtEnd()) return false;
        if (source[current] != expected) return false;
        current++;
        return true;
    }

    void addToken(TokenType type) {
        addToken(type, Value());
    }

    void addToken(TokenType type, Value literal) {
        std::string text = source.substr(start, current - start);
        // Avoid duplicate consecutive newlines
        if (type == TokenType::NEWLINE) {
            if (!tokens.empty() && tokens.back().type == TokenType::NEWLINE) {
                return;
            }
        }
        tokens.push_back({type, text, literal, line});
    }

    bool isDigit(char c) const {
        return c >= '0' && c <= '9';
    }

    bool isAlpha(char c) const {
        return (c >= 'a' && c <= 'z') ||
               (c >= 'A' && c <= 'Z') ||
                c == '_';
    }

    bool isAlphaNumeric(char c) const {
        return isAlpha(c) || isDigit(c);
    }

    void scanToken() {
        char c = advance();
        switch (c) {
            case '(': addToken(TokenType::LEFT_PAREN); break;
            case ')': addToken(TokenType::RIGHT_PAREN); break;
            case '{': addToken(TokenType::LEFT_BRACE); break;
            case '}': addToken(TokenType::RIGHT_BRACE); break;
            case '[': addToken(TokenType::LEFT_BRACKET); break;
            case ']': addToken(TokenType::RIGHT_BRACKET); break;
            case ',': addToken(TokenType::COMMA); break;
            case ':': addToken(TokenType::COLON); break;
            case ';': addToken(TokenType::SEMICOLON); break;
            case '-': addToken(TokenType::MINUS); break;
            case '+': addToken(TokenType::PLUS); break;
            case '*': addToken(TokenType::STAR); break;
            case '%': addToken(TokenType::PERCENT); break;
            
            case '!':
                addToken(matchChar('=') ? TokenType::BANG_EQUAL : TokenType::BANG);
                break;
            case '=':
                addToken(matchChar('=') ? TokenType::EQUAL_EQUAL : TokenType::EQUAL);
                break;
            case '<':
                addToken(matchChar('=') ? TokenType::LESS_EQUAL : TokenType::LESS);
                break;
            case '>':
                addToken(matchChar('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER);
                break;
            
            case '&':
                if (matchChar('&')) {
                    addToken(TokenType::AND);
                } else {
                    throw std::runtime_error("Unexpected character '&' at line " + std::to_string(line) + ". Did you mean '&&'?");
                }
                break;
            case '|':
                if (matchChar('|')) {
                    addToken(TokenType::OR);
                } else {
                    throw std::runtime_error("Unexpected character '|' at line " + std::to_string(line) + ". Did you mean '||'?");
                }
                break;

            case '/':
                if (matchChar('/')) {
                    // Line comment
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else {
                    addToken(TokenType::SLASH);
                }
                break;

            case '#':
                // Line comment
                while (peek() != '\n' && !isAtEnd()) advance();
                break;

            case ' ':
            case '\r':
            case '\t':
                // Ignore whitespace
                break;

            case '\n':
                addToken(TokenType::NEWLINE);
                line++;
                break;

            case '"': scanString(); break;

            default:
                if (isDigit(c)) {
                    scanNumber();
                } else if (isAlpha(c)) {
                    scanIdentifier();
                } else {
                    throw std::runtime_error("Unexpected character '" + std::string(1, c) + "' at line " + std::to_string(line));
                }
                break;
        }
    }

    void scanString() {
        std::string value = "";
        while (peek() != '"' && !isAtEnd()) {
            if (peek() == '\n') line++;
            if (peek() == '\\') {
                advance(); // consume backslash
                if (isAtEnd()) {
                    throw std::runtime_error("Unterminated escape sequence at line " + std::to_string(line));
                }
                char c = advance();
                switch (c) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case 'r': value += '\r'; break;
                    case '\"': value += '\"'; break;
                    case '\\': value += '\\'; break;
                    default: value += c; break;
                }
            } else {
                value += advance();
            }
        }

        if (isAtEnd()) {
            throw std::runtime_error("Unterminated string at line " + std::to_string(line));
        }

        advance(); // Closing quote
        addToken(TokenType::STRING, Value(value));
    }

    void scanNumber() {
        while (isDigit(peek())) advance();

        // Fractional part
        if (peek() == '.' && isDigit(peekNext())) {
            advance(); // Consume '.'
            while (isDigit(peek())) advance();
        }

        std::string lex = source.substr(start, current - start);
        double val = std::stod(lex);
        addToken(TokenType::NUMBER, Value(val));
    }

    void scanIdentifier() {
        while (isAlphaNumeric(peek())) advance();

        std::string text = source.substr(start, current - start);
        TokenType type = TokenType::IDENTIFIER;
        auto it = keywords.find(text);
        if (it != keywords.end()) {
            type = it->second;
        }
        
        // Handle boolean/null literals directly
        if (type == TokenType::TRUE) {
            addToken(type, Value(true));
        } else if (type == TokenType::FALSE) {
            addToken(type, Value(false));
        } else if (type == TokenType::NULL_VAL) {
            addToken(type, Value(Nil{}));
        } else {
            addToken(type);
        }
    }

public:
    Lexer(const std::string& source) : source(source) {}

    std::vector<Token> scanTokens() {
        while (!isAtEnd()) {
            start = current;
            scanToken();
        }

        // If the last token was a newline, let's remove it and place EOF
        if (!tokens.empty() && tokens.back().type == TokenType::NEWLINE) {
            tokens.pop_back();
        }

        tokens.push_back({TokenType::EOF_VAL, "", Value(), line});
        return tokens;
    }
};

// Define the static keywords map
inline const std::unordered_map<std::string, TokenType> Lexer::keywords = {
    {"if", TokenType::IF},
    {"else", TokenType::ELSE},
    {"while", TokenType::WHILE},
    {"for", TokenType::FOR},
    {"switch", TokenType::SWITCH},
    {"case", TokenType::CASE},
    {"default", TokenType::DEFAULT},
    {"true", TokenType::TRUE},
    {"false", TokenType::FALSE},
    {"null", TokenType::NULL_VAL},
    {"and", TokenType::AND},
    {"or", TokenType::OR},
    {"not", TokenType::NOT}
};

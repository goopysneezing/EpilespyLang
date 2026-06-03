#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <algorithm>
#include <utility>
#include <iostream>
#include "ast.hpp"

// Helper functions for spelling and casing auto-correction
inline int getEditDistance(const std::string& s1, const std::string& s2) {
    int m = static_cast<int>(s1.length());
    int n = static_cast<int>(s2.length());
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));

    for (int i = 0; i <= m; i++) dp[i][0] = i;
    for (int j = 0; j <= n; j++) dp[0][j] = j;

    for (int i = 1; i <= m; i++) {
        for (int j = 1; j <= n; j++) {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            dp[i][j] = (std::min)({dp[i - 1][j] + 1,      // Deletion
                                  dp[i][j - 1] + 1,      // Insertion
                                  dp[i - 1][j - 1] + cost}); // Substitution

            // Check for transposition (Damerau-Levenshtein)
            if (i > 1 && j > 1 && s1[i - 1] == s2[j - 2] && s1[i - 2] == s2[j - 1]) {
                dp[i][j] = (std::min)(dp[i][j], dp[i - 2][j - 2] + 1);
            }
        }
    }
    return dp[m][n];
}

inline std::string getCorrectedKeyword(const std::string& original_id) {
    std::string id = original_id;
    for (char &c : id) c = tolower(c);

    // 1. Case-insensitive exact matches
    if (id == "if") return "if";
    if (id == "else") return "else";
    if (id == "while") return "while";
    if (id == "for") return "for";
    if (id == "switch") return "switch";
    if (id == "case") return "case";
    if (id == "default") return "default";
    if (id == "true") return "true";
    if (id == "false") return "false";
    if (id == "null") return "null";
    if (id == "and") return "and";
    if (id == "or") return "or";
    if (id == "not") return "not";
    if (id == "quantum") return "quantum";
    if (id == "qbreak") return "qbreak";
    if (id == "qkillothers") return "qkillothers";

    // 2. Strict matching rules for short keywords to prevent false positives
    if (id == "iff" || id == "iif" || id == "fi") return "if";
    if (id == "fro" || id == "fo" || id == "forr" || id == "fpr") return "for";
    if (id == "an" || id == "nd") return "and";
    if (id == "ot" || id == "nt") return "not";

    // 3. Edit distance for longer keywords (length >= 4)
    std::vector<std::pair<std::string, std::string>> longerKeywords = {
        {"else", "else"},
        {"while", "while"},
        {"switch", "switch"},
        {"case", "case"},
        {"default", "default"},
        {"true", "true"},
        {"false", "false"},
        {"null", "null"},
        {"quantum", "quantum"},
        {"qbreak", "qbreak"},
        {"qkillothers", "qkillothers"}
    };

    // Specific typo rules for true/false to be even more robust
    if (id == "ture") return "true";
    if (id == "flase") return "false";

    for (const auto& kw : longerKeywords) {
        if (getEditDistance(id, kw.first) <= 1) {
            return kw.second;
        }
    }

    return "";
}


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

    void addToken(TokenType type, const std::string& customText = "") {
        addToken(type, Value(), customText);
    }

    void addToken(TokenType type, Value literal, const std::string& customText = "") {
        std::string text = customText.empty() ? source.substr(start, current - start) : customText;
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
            case '.': addToken(TokenType::DOT); break;
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
        std::string corrected = getCorrectedKeyword(text);
        std::string final_text = text;
        if (!corrected.empty()) {
            final_text = corrected;
            if (corrected != text) {
                std::cout << "[Auto-Fix] Corrected spelling/casing: '" << text << "' -> '" << corrected << "'\n";
            }
        }

        TokenType type = TokenType::IDENTIFIER;
        auto it = keywords.find(final_text);
        if (it != keywords.end()) {
            type = it->second;
        }
        
        // Handle boolean/null literals directly
        if (type == TokenType::TRUE) {
            addToken(type, Value(true), final_text);
        } else if (type == TokenType::FALSE) {
            addToken(type, Value(false), final_text);
        } else if (type == TokenType::NULL_VAL) {
            addToken(type, Value(Nil{}), final_text);
        } else {
            addToken(type, final_text);
        }
    }

public:
    Lexer(const std::string& source) : source(source) {}

    std::vector<Token> scanTokens() {
        while (!isAtEnd()) {
            start = current;
            scanToken();
        }

        // Ensure there is a NEWLINE token before EOF so the end of code acts as a newline
        if (tokens.empty() || tokens.back().type != TokenType::NEWLINE) {
            tokens.push_back({TokenType::NEWLINE, "\n", Value(), line});
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
    {"not", TokenType::NOT},
    {"quantum", TokenType::QUANTUM},
    {"qbreak", TokenType::QBREAK},
    {"qkillothers", TokenType::QKILLOTHERS}
};

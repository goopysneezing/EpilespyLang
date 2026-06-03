#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <exception>
#include <cstdlib>
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include "value.hpp"
#include "ast.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "environment.hpp"
#include "interpreter.hpp"

// Utility to check if braces/parens are balanced for multi-line block parsing in REPL
bool isBalanced(const std::string& code) {
    int braces = 0;
    int parens = 0;
    int brackets = 0;
    bool inString = false;
    bool escape = false;

    for (size_t i = 0; i < code.length(); ++i) {
        char c = code[i];
        if (escape) {
            escape = false;
            continue;
        }
        if (inString) {
            if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
            continue;
        }

        // Handle single-line comments
        if (c == '#' || (c == '/' && i + 1 < code.length() && code[i + 1] == '/')) {
            while (i < code.length() && code[i] != '\n') {
                i++;
            }
            continue;
        }

        if (c == '{') braces++;
        else if (c == '}') braces--;
        else if (c == '(') parens++;
        else if (c == ')') parens--;
        else if (c == '[') brackets++;
        else if (c == ']') brackets--;
    }

    return braces <= 0 && parens <= 0 && brackets <= 0 && !inString;
}

void runREPL() {
    std::cout << "=================================================\n";
    std::cout << "  E P I L E P S Y L A N G   C M D   M O D E      \n";
    std::cout << "  Interactive interpreter for EpilepsyLang.      \n";
    std::cout << "  Type your code. Press Enter to execute.        \n";
    std::cout << "  Type \"exit\" to exit, \"help\" for info.          \n";
    std::cout << "=================================================\n\n";

    Interpreter interpreter;
    std::string codeBuffer = "";

    while (true) {
        if (codeBuffer.empty()) {
            std::cout << "epilepsy > ";
        } else {
            std::cout << "...      ";
        }
        std::cout.flush();

        std::string line;
        if (!std::getline(std::cin, line)) {
            break;
        }

        if (codeBuffer.empty() && (line == "exit" || line == "quit")) {
            break;
        }
        if (codeBuffer.empty() && line == "help") {
            std::cout << "Commands and Help:\n";
            std::cout << "  exit / quit : Close CMD mode\n";
            std::cout << "  help        : Show this message\n";
            std::cout << "  Variables   : x = 10\n";
            std::cout << "  Arrays      : arr = [1, 2, 3]\n";
            std::cout << "  Built-ins   : print(expr), input(prompt), len(expr), num(expr), str(expr)\n";
            std::cout << "  Blocks      : {} for scope. Use if, while, for, switch.\n\n";
            continue;
        }

        codeBuffer += line + "\n";

        if (isBalanced(codeBuffer)) {
            try {
                Lexer lexer(codeBuffer);
                auto tokens = lexer.scanTokens();
                Parser parser(tokens);
                auto statements = parser.parse();
                interpreter.interpret(statements, "repl");
            } catch (const ParseError& e) {
                std::cerr << "Syntax Error: " << e.what() << "\n";
            } catch (const QuantumBreakException& e) {
                std::cerr << "Runtime Error: qbreak statement executed outside of a quantum branch.\n";
            } catch (const RuntimeError& e) {
                std::cerr << "Runtime Error (Line " << e.line << "): " << e.what() << "\n";
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << "\n";
            }
            codeBuffer = ""; // Reset buffer
        }
    }
}

void runFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file '" << path << "'\n";
        std::exit(1);
    }

    std::string source((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    try {
        Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        Parser parser(tokens);
        auto statements = parser.parse();
        Interpreter interpreter;
        interpreter.interpret(statements, path);
    } catch (const ParseError& e) {
        std::cerr << "Syntax Error: " << e.what() << "\n";
        std::exit(1);
    } catch (const QuantumBreakException& e) {
        std::cerr << "Runtime Error: qbreak statement executed outside of a quantum branch.\n";
        std::exit(1);
    } catch (const RuntimeError& e) {
        std::cerr << "Runtime Error (Line " << e.line << "): " << e.what() << "\n";
        std::exit(1);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        std::exit(1);
    }
}

std::string getExeDirectory() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::filesystem::path p(buffer);
    return p.parent_path().string();
}

void runAdminHelper(const std::string& pipeName) {
    HANDLE hPipe = CreateFileA(
        pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE) {
        return;
    }

    while (true) {
        DWORD bytesRead = 0;
        DWORD cmdLen = 0;
        if (!ReadFile(hPipe, &cmdLen, sizeof(cmdLen), &bytesRead, NULL) || bytesRead == 0) {
            break;
        }

        std::vector<char> buf(cmdLen + 1, 0);
        if (!ReadFile(hPipe, buf.data(), cmdLen, &bytesRead, NULL) || bytesRead == 0) {
            break;
        }
        std::string cmd(buf.data(), bytesRead);

        if (cmd == "exit") {
            break;
        }

        std::vector<char> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back('\0');

        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        DWORD exitCode = 0xFFFFFFFF;
        if (CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, false, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            exitCode = GetLastError();
        }

        DWORD bytesWritten = 0;
        WriteFile(hPipe, &exitCode, sizeof(exitCode), &bytesWritten, NULL);
    }

    CloseHandle(hPipe);
}

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cout << "Usage: EpilespyLang [script.ep] or --repl\n";
        return 1;
    } else if (argc == 2) {
        std::string arg = argv[1];
        if (arg == "--repl") {
            runREPL();
        } else if (arg.rfind("--admin-helper:", 0) == 0) {
            std::string pipeName = arg.substr(15);
            runAdminHelper(pipeName);
            return 0;
        } else {
            runFile(argv[1]);
        }
    } else {
        std::cout << "Starting EpilespyLang IDE Server...\n";
        std::string exeDir = getExeDirectory();
        std::string cwd = std::filesystem::current_path().string();
        
        // Resolve Node.js path (check local installation first, otherwise fallback to system node)
        std::string nodePath = "node";
        std::string localNodePath = exeDir + "\\node-local\\node-v20.13.1-win-x64\\node.exe";
        if (std::filesystem::exists(localNodePath)) {
            nodePath = "\"" + localNodePath + "\"";
        } else {
            std::cout << "Using system Node.js. Make sure Node.js is installed!\n";
        }

        std::string command = "\"" + nodePath + " \"" + exeDir + "\\ide.js\" --parent-pid " + std::to_string(GetCurrentProcessId()) + " \"" + cwd + "\"\"";
        std::system(command.c_str());
    }
    return 0;
}

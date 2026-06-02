#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include "value.hpp"

class EntryInstance {
public:
    std::vector<std::string> columns;
    std::vector<Value> values;

    Value get(const std::string& name) const {
        for (size_t i = 0; i < columns.size(); ++i) {
            if (columns[i] == name) {
                return values[i];
            }
        }
        return Value(); // Nil
    }
};

class TableInstance {
public:
    std::string name;
    std::vector<std::string> columns;
    std::vector<std::shared_ptr<EntryInstance>> rows;
};

class DatabaseInstance {
public:
    std::string filepath;
    std::map<std::string, std::shared_ptr<TableInstance>> tables;

    DatabaseInstance(const std::string& path) : filepath(path) {
        load();
    }

    void load() {
        std::ifstream file(filepath);
        if (!file.is_open()) return;

        std::string line;
        std::shared_ptr<TableInstance> currentTable = nullptr;

        while (std::getline(file, line)) {
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            if (line.empty() || line[0] == '#') continue;

            if (line.rfind("[TABLE] ", 0) == 0) {
                std::string tableName = line.substr(8);
                tableName.erase(0, tableName.find_first_not_of(" \t\r\n"));
                tableName.erase(tableName.find_last_not_of(" \t\r\n") + 1);
                
                currentTable = std::make_shared<TableInstance>();
                currentTable->name = tableName;
                tables[tableName] = currentTable;
            } else if (line.rfind("[COLUMNS] ", 0) == 0 && currentTable) {
                std::string colsStr = line.substr(10);
                currentTable->columns = splitCSV(colsStr);
            } else if (line.rfind("[ROW] ", 0) == 0 && currentTable) {
                std::string rowStr = line.substr(6);
                auto rowVals = parseCSV(rowStr);
                
                auto entry = std::make_shared<EntryInstance>();
                entry->columns = currentTable->columns;
                entry->values = rowVals;
                
                // Align or pad values if they don't match column size
                entry->values.resize(currentTable->columns.size(), Value(Nil{}));
                
                currentTable->rows.push_back(entry);
            }
        }
    }

    void save() {
        std::ofstream file(filepath);
        if (!file.is_open()) return;

        for (auto const& [name, table] : tables) {
            file << "[TABLE] " << name << "\n";
            file << "[COLUMNS] ";
            for (size_t i = 0; i < table->columns.size(); ++i) {
                if (i > 0) file << ",";
                file << table->columns[i];
            }
            file << "\n";
            for (auto const& row : table->rows) {
                file << "[ROW] ";
                for (size_t i = 0; i < row->values.size(); ++i) {
                    if (i > 0) file << ",";
                    file << serializeValue(row->values[i]);
                }
                file << "\n";
            }
            file << "\n";
        }
    }

    Value query(const std::string& sql) {
        std::string cleanSql = sql;
        cleanSql.erase(0, cleanSql.find_first_not_of(" \t\r\n"));
        cleanSql.erase(cleanSql.find_last_not_of(" \t\r\n") + 1);

        std::string upperSql = cleanSql;
        std::transform(upperSql.begin(), upperSql.end(), upperSql.begin(), ::toupper);

        if (upperSql.rfind("CREATE TABLE ", 0) == 0) {
            executeCreate(cleanSql);
            save();
            return Value(true);
        } else if (upperSql.rfind("INSERT INTO ", 0) == 0) {
            executeInsert(cleanSql);
            save();
            return Value(true);
        } else if (upperSql.rfind("SELECT ", 0) == 0) {
            return executeSelect(cleanSql);
        } else if (upperSql.rfind("UPDATE ", 0) == 0) {
            executeUpdate(cleanSql);
            save();
            return Value(true);
        } else if (upperSql.rfind("DELETE FROM ", 0) == 0) {
            executeDelete(cleanSql);
            save();
            return Value(true);
        }

        throw std::runtime_error("SQL Execution Error: Unsupported or unrecognized query command starting with '" + 
                                 cleanSql.substr(0, std::min(cleanSql.length(), size_t(15))) + "...'");
    }

private:
    struct WhereCondition {
        std::string column;
        std::string op;
        Value value;
        bool hasCondition = false;
    };

    std::vector<std::string> splitCSV(const std::string& str) {
        std::vector<std::string> result;
        std::string current = "";
        for (char c : str) {
            if (c == ',') {
                result.push_back(current);
                current = "";
            } else {
                current += c;
            }
        }
        if (!current.empty() || str.back() == ',') {
            result.push_back(current);
        }
        return result;
    }

    std::vector<Value> parseCSV(const std::string& str) {
        std::vector<Value> result;
        bool inString = false;
        char quoteChar = '\0';
        std::string current = "";
        for (size_t i = 0; i < str.length(); ++i) {
            char c = str[i];
            if (inString) {
                if (c == quoteChar) {
                    inString = false;
                } else if (c == '\\' && i + 1 < str.length()) {
                    current += str[++i];
                } else {
                    current += c;
                }
            } else {
                if (c == '"' || c == '\'') {
                    inString = true;
                    quoteChar = c;
                } else if (c == ',') {
                    result.push_back(parseSingleValue(current));
                    current = "";
                } else {
                    current += c;
                }
            }
        }
        result.push_back(parseSingleValue(current));
        return result;
    }

    Value parseSingleValue(const std::string& valStr) {
        std::string trimmed = valStr;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
        trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

        if (trimmed == "null" || trimmed.empty()) return Value(Nil{});
        if (trimmed == "true") return Value(true);
        if (trimmed == "false") return Value(false);

        if (trimmed.length() >= 2 && 
            ((trimmed.front() == '"' && trimmed.back() == '"') || 
             (trimmed.front() == '\'' && trimmed.back() == '\''))) {
            return Value(trimmed.substr(1, trimmed.length() - 2));
        }

        try {
            size_t idx;
            double d = std::stod(trimmed, &idx);
            if (idx == trimmed.length()) return Value(d);
        } catch (...) {}

        return Value(trimmed);
    }

    std::string serializeValue(const Value& val) {
        if (val.isNil()) return "null";
        if (val.isBool()) return val.asBool() ? "true" : "false";
        if (val.isNumber()) return val.toString();
        if (val.isString()) {
            std::string escaped = "";
            for (char c : val.asString()) {
                if (c == '"' || c == '\\') escaped += '\\';
                escaped += c;
            }
            return "\"" + escaped + "\"";
        }
        return "null";
    }

    void executeCreate(const std::string& sql) {
        size_t openParen = sql.find('(');
        size_t closeParen = sql.rfind(')');
        if (openParen == std::string::npos || closeParen == std::string::npos || closeParen < openParen) {
            throw std::runtime_error("SQL Syntax Error in CREATE TABLE: Missing column definition parentheses.");
        }

        std::string tableName = sql.substr(12, openParen - 12);
        tableName.erase(0, tableName.find_first_not_of(" \t\r\n"));
        tableName.erase(tableName.find_last_not_of(" \t\r\n") + 1);

        if (tableName.empty()) {
            throw std::runtime_error("SQL Syntax Error in CREATE TABLE: Table name cannot be empty.");
        }

        std::string colsStr = sql.substr(openParen + 1, closeParen - openParen - 1);
        auto rawCols = splitCSV(colsStr);
        std::vector<std::string> columns;
        for (auto& col : rawCols) {
            col.erase(0, col.find_first_not_of(" \t\r\n"));
            col.erase(col.find_last_not_of(" \t\r\n") + 1);
            if (!col.empty()) columns.push_back(col);
        }

        if (columns.empty()) {
            throw std::runtime_error("SQL Syntax Error in CREATE TABLE: Table must define at least one column.");
        }

        auto table = std::make_shared<TableInstance>();
        table->name = tableName;
        table->columns = columns;
        tables[tableName] = table;
    }

    void executeInsert(const std::string& sql) {
        std::string upperSql = sql;
        std::transform(upperSql.begin(), upperSql.end(), upperSql.begin(), ::toupper);

        size_t valuesPos = upperSql.find("VALUES");
        if (valuesPos == std::string::npos) {
            throw std::runtime_error("SQL Syntax Error in INSERT INTO: Missing expected VALUES keyword.");
        }

        std::string targetPart = sql.substr(11, valuesPos - 11);
        targetPart.erase(0, targetPart.find_first_not_of(" \t\r\n"));
        targetPart.erase(targetPart.find_last_not_of(" \t\r\n") + 1);

        std::string tableName = "";
        std::vector<std::string> insertCols;

        size_t openParenTarget = targetPart.find('(');
        if (openParenTarget != std::string::npos) {
            tableName = targetPart.substr(0, openParenTarget);
            tableName.erase(0, tableName.find_first_not_of(" \t\r\n"));
            tableName.erase(tableName.find_last_not_of(" \t\r\n") + 1);

            size_t closeParenTarget = targetPart.rfind(')');
            if (closeParenTarget == std::string::npos || closeParenTarget < openParenTarget) {
                throw std::runtime_error("SQL Syntax Error in INSERT INTO: Mismatched target columns parentheses.");
            }
            std::string colsStr = targetPart.substr(openParenTarget + 1, closeParenTarget - openParenTarget - 1);
            auto rawCols = splitCSV(colsStr);
            for (auto& col : rawCols) {
                col.erase(0, col.find_first_not_of(" \t\r\n"));
                col.erase(col.find_last_not_of(" \t\r\n") + 1);
                insertCols.push_back(col);
            }
        } else {
            tableName = targetPart;
        }

        if (tableName.empty()) {
            throw std::runtime_error("SQL Syntax Error in INSERT INTO: Table name is missing.");
        }

        if (tables.find(tableName) == tables.end()) {
            throw std::runtime_error("SQL Execution Error in INSERT INTO: Table '" + tableName + "' does not exist.");
        }
        auto table = tables[tableName];

        std::string valuesPart = sql.substr(valuesPos + 6);
        size_t openParenVals = valuesPart.find('(');
        size_t closeParenVals = valuesPart.rfind(')');
        if (openParenVals == std::string::npos || closeParenVals == std::string::npos || closeParenVals < openParenVals) {
            throw std::runtime_error("SQL Syntax Error in INSERT INTO: Values must be wrapped in parentheses, e.g. VALUES (...)");
        }

        std::string valsStr = valuesPart.substr(openParenVals + 1, closeParenVals - openParenVals - 1);
        auto rawVals = parseCSV(valsStr);

        auto entry = std::make_shared<EntryInstance>();
        entry->columns = table->columns;
        entry->values.resize(table->columns.size(), Value(Nil{}));

        if (insertCols.empty()) {
            if (rawVals.size() != table->columns.size()) {
                throw std::runtime_error("SQL Execution Error in INSERT: Column count mismatch. Table has " +
                                         std::to_string(table->columns.size()) + " columns, but " +
                                         std::to_string(rawVals.size()) + " values were provided.");
            }
            entry->values = rawVals;
        } else {
            for (size_t i = 0; i < insertCols.size(); ++i) {
                std::string colName = insertCols[i];
                auto it = std::find(table->columns.begin(), table->columns.end(), colName);
                if (it == table->columns.end()) {
                    throw std::runtime_error("SQL Execution Error in INSERT: Target column '" + colName + 
                                             "' does not exist in table '" + tableName + "'.");
                }
                size_t colIdx = std::distance(table->columns.begin(), it);
                if (i < rawVals.size()) {
                    entry->values[colIdx] = rawVals[i];
                }
            }
        }

        table->rows.push_back(entry);
    }

    Value executeSelect(const std::string& sql) {
        std::string upperSql = sql;
        std::transform(upperSql.begin(), upperSql.end(), upperSql.begin(), ::toupper);

        size_t fromPos = upperSql.find("FROM");
        if (fromPos == std::string::npos) {
            throw std::runtime_error("SQL Syntax Error in SELECT: Missing FROM keyword.");
        }

        std::string colsPart = sql.substr(7, fromPos - 7);
        colsPart.erase(0, colsPart.find_first_not_of(" \t\r\n"));
        colsPart.erase(colsPart.find_last_not_of(" \t\r\n") + 1);

        if (colsPart.empty()) {
            throw std::runtime_error("SQL Syntax Error in SELECT: Expecting columns or '*' before FROM.");
        }

        std::vector<std::string> selectCols;
        if (colsPart != "*") {
            auto rawCols = splitCSV(colsPart);
            for (auto& col : rawCols) {
                col.erase(0, col.find_first_not_of(" \t\r\n"));
                col.erase(col.find_last_not_of(" \t\r\n") + 1);
                selectCols.push_back(col);
            }
        }

        size_t wherePos = upperSql.find("WHERE");
        std::string tablePart = "";
        if (wherePos != std::string::npos) {
            tablePart = sql.substr(fromPos + 4, wherePos - (fromPos + 4));
        } else {
            tablePart = sql.substr(fromPos + 4);
        }
        tablePart.erase(0, tablePart.find_first_not_of(" \t\r\n"));
        tablePart.erase(tablePart.find_last_not_of(" \t\r\n") + 1);

        if (tablePart.empty()) {
            throw std::runtime_error("SQL Syntax Error in SELECT: Table name is missing after FROM.");
        }

        if (tables.find(tablePart) == tables.end()) {
            throw std::runtime_error("SQL Execution Error in SELECT: Table '" + tablePart + "' does not exist.");
        }
        auto srcTable = tables[tablePart];

        // Check if all requested columns exist
        for (auto const& col : selectCols) {
            if (std::find(srcTable->columns.begin(), srcTable->columns.end(), col) == srcTable->columns.end()) {
                throw std::runtime_error("SQL Execution Error in SELECT: Column '" + col + 
                                         "' does not exist in table '" + tablePart + "'.");
            }
        }

        WhereCondition cond = parseWhere(sql, wherePos);
        if (cond.hasCondition) {
            if (std::find(srcTable->columns.begin(), srcTable->columns.end(), cond.column) == srcTable->columns.end()) {
                throw std::runtime_error("SQL Execution Error in SELECT WHERE: Column '" + cond.column + 
                                         "' does not exist in table '" + tablePart + "'.");
            }
        }

        auto resultTable = std::make_shared<TableInstance>();
        resultTable->name = "result";
        resultTable->columns = selectCols.empty() ? srcTable->columns : selectCols;

        for (auto const& srcRow : srcTable->rows) {
            if (evalCondition(cond, srcRow)) {
                auto newRow = std::make_shared<EntryInstance>();
                newRow->columns = resultTable->columns;
                for (auto const& colName : resultTable->columns) {
                    newRow->values.push_back(srcRow->get(colName));
                }
                resultTable->rows.push_back(newRow);
            }
        }

        return Value(resultTable);
    }

    void executeUpdate(const std::string& sql) {
        std::string upperSql = sql;
        std::transform(upperSql.begin(), upperSql.end(), upperSql.begin(), ::toupper);

        size_t setPos = upperSql.find("SET");
        if (setPos == std::string::npos) {
            throw std::runtime_error("SQL Syntax Error in UPDATE: Missing SET keyword.");
        }

        std::string tableName = sql.substr(7, setPos - 7);
        tableName.erase(0, tableName.find_first_not_of(" \t\r\n"));
        tableName.erase(tableName.find_last_not_of(" \t\r\n") + 1);

        if (tables.find(tableName) == tables.end()) {
            throw std::runtime_error("SQL Execution Error in UPDATE: Table '" + tableName + "' does not exist.");
        }
        auto table = tables[tableName];

        size_t wherePos = upperSql.find("WHERE");
        std::string setPart = "";
        if (wherePos != std::string::npos) {
            setPart = sql.substr(setPos + 3, wherePos - (setPos + 3));
        } else {
            setPart = sql.substr(setPos + 3);
        }

        auto rawSets = splitCSV(setPart);
        std::map<std::string, Value> updates;
        for (auto& setItem : rawSets) {
            size_t eqPos = setItem.find('=');
            if (eqPos == std::string::npos) {
                throw std::runtime_error("SQL Syntax Error in UPDATE SET: Missing '=' assignment operator.");
            }
            std::string colName = setItem.substr(0, eqPos);
            colName.erase(0, colName.find_first_not_of(" \t\r\n"));
            colName.erase(colName.find_last_not_of(" \t\r\n") + 1);

            if (std::find(table->columns.begin(), table->columns.end(), colName) == table->columns.end()) {
                throw std::runtime_error("SQL Execution Error in UPDATE SET: Column '" + colName + 
                                         "' does not exist in table '" + tableName + "'.");
            }

            std::string valStr = setItem.substr(eqPos + 1);
            valStr.erase(0, valStr.find_first_not_of(" \t\r\n"));
            valStr.erase(valStr.find_last_not_of(" \t\r\n") + 1);

            updates[colName] = parseSingleValue(valStr);
        }

        if (updates.empty()) {
            throw std::runtime_error("SQL Syntax Error in UPDATE SET: Expecting at least one column assignment.");
        }

        WhereCondition cond = parseWhere(sql, wherePos);
        if (cond.hasCondition) {
            if (std::find(table->columns.begin(), table->columns.end(), cond.column) == table->columns.end()) {
                throw std::runtime_error("SQL Execution Error in UPDATE WHERE: Column '" + cond.column + 
                                         "' does not exist in table '" + tableName + "'.");
            }
        }

        for (auto& row : table->rows) {
            if (evalCondition(cond, row)) {
                for (auto const& [colName, newValue] : updates) {
                    auto it = std::find(table->columns.begin(), table->columns.end(), colName);
                    if (it != table->columns.end()) {
                        size_t colIdx = std::distance(table->columns.begin(), it);
                        row->values[colIdx] = newValue;
                    }
                }
            }
        }
    }

    void executeDelete(const std::string& sql) {
        std::string upperSql = sql;
        std::transform(upperSql.begin(), upperSql.end(), upperSql.begin(), ::toupper);

        size_t fromPos = upperSql.find("FROM");
        if (fromPos == std::string::npos) {
            throw std::runtime_error("SQL Syntax Error in DELETE FROM: Missing FROM keyword.");
        }

        size_t wherePos = upperSql.find("WHERE");
        std::string tableName = "";
        if (wherePos != std::string::npos) {
            tableName = sql.substr(fromPos + 4, wherePos - (fromPos + 4));
        } else {
            tableName = sql.substr(fromPos + 4);
        }
        tableName.erase(0, tableName.find_first_not_of(" \t\r\n"));
        tableName.erase(tableName.find_last_not_of(" \t\r\n") + 1);

        if (tables.find(tableName) == tables.end()) {
            throw std::runtime_error("SQL Execution Error in DELETE: Table '" + tableName + "' does not exist.");
        }
        auto table = tables[tableName];

        WhereCondition cond = parseWhere(sql, wherePos);
        if (cond.hasCondition) {
            if (std::find(table->columns.begin(), table->columns.end(), cond.column) == table->columns.end()) {
                throw std::runtime_error("SQL Execution Error in DELETE WHERE: Column '" + cond.column + 
                                         "' does not exist in table '" + tableName + "'.");
            }
        }

        std::vector<std::shared_ptr<EntryInstance>> remainingRows;
        for (auto const& row : table->rows) {
            if (!evalCondition(cond, row)) {
                remainingRows.push_back(row);
            }
        }
        table->rows = remainingRows;
    }

    WhereCondition parseWhere(const std::string& sql, size_t wherePos) {
        WhereCondition cond;
        if (wherePos == std::string::npos) return cond;

        std::string whereClause = sql.substr(wherePos + 5);
        whereClause.erase(0, whereClause.find_first_not_of(" \t\r\n"));
        whereClause.erase(whereClause.find_last_not_of(" \t\r\n") + 1);

        std::vector<std::string> ops = {"==", "!=", "<=", ">=", "<", ">", "="};
        std::string foundOp = "";
        size_t opPos = std::string::npos;

        for (auto const& op : ops) {
            size_t pos = whereClause.find(op);
            if (pos != std::string::npos) {
                foundOp = op;
                opPos = pos;
                break;
            }
        }

        if (opPos == std::string::npos) {
            throw std::runtime_error("SQL Syntax Error in WHERE clause: Missing or unsupported operator (supported: =, ==, !=, <, <=, >, >=).");
        }

        std::string colName = whereClause.substr(0, opPos);
        colName.erase(0, colName.find_first_not_of(" \t\r\n"));
        colName.erase(colName.find_last_not_of(" \t\r\n") + 1);

        if (colName.empty()) {
            throw std::runtime_error("SQL Syntax Error in WHERE clause: Left-hand column name cannot be empty.");
        }

        std::string valStr = whereClause.substr(opPos + foundOp.length());
        valStr.erase(0, valStr.find_first_not_of(" \t\r\n"));
        valStr.erase(valStr.find_last_not_of(" \t\r\n") + 1);

        if (valStr.empty()) {
            throw std::runtime_error("SQL Syntax Error in WHERE clause: Right-hand value cannot be empty.");
        }

        cond.column = colName;
        cond.op = (foundOp == "=") ? "==" : foundOp;
        cond.value = parseSingleValue(valStr);
        cond.hasCondition = true;
        return cond;
    }

    bool evalCondition(const WhereCondition& cond, const std::shared_ptr<EntryInstance>& entry) {
        if (!cond.hasCondition) return true;
        Value colVal = entry->get(cond.column);
        Value matchVal = cond.value;

        if (cond.op == "==") {
            return valuesEqual(colVal, matchVal);
        } else if (cond.op == "!=") {
            return !valuesEqual(colVal, matchVal);
        }

        if (!colVal.isNumber() || !matchVal.isNumber()) {
            return false;
        }

        double cv = colVal.asNumber();
        double mv = matchVal.asNumber();

        if (cond.op == "<") return cv < mv;
        if (cond.op == "<=") return cv <= mv;
        if (cond.op == ">") return cv > mv;
        if (cond.op == ">=") return cv >= mv;

        return false;
    }
};

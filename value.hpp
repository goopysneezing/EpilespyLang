#pragma once
#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <stdexcept>
#include "window.hpp"
#include "sound.hpp"

struct Complex {
    double real;
    double imag;
    Complex(double r = 0.0, double i = 0.0) : real(r), imag(i) {}
    bool operator==(const Complex& other) const {
        return real == other.real && imag == other.imag;
    }
};

// Forward declarations
struct Value;
class DatabaseInstance;
class TableInstance;
class EntryInstance;

using ValueArray = std::shared_ptr<std::vector<Value>>;
using WindowPtr = std::shared_ptr<WindowInstance>;
using SoundPtr = std::shared_ptr<SoundInstance>;
class FormInstance;
using FormPtr = std::shared_ptr<FormInstance>;
using DatabasePtr = std::shared_ptr<DatabaseInstance>;
using TablePtr = std::shared_ptr<TableInstance>;
using EntryPtr = std::shared_ptr<EntryInstance>;

class RuntimeError : public std::runtime_error {
public:
    int line;
    RuntimeError(int line, const std::string& message)
        : std::runtime_error(message), line(line) {}
};

class QuantumBreakException : public std::exception {
public:
    const char* what() const noexcept override {
        return "Quantum Break";
    }
};

class QuantumKilledException : public std::exception {
public:
    const char* what() const noexcept override {
        return "Quantum Killed";
    }
};

struct Nil {
    bool operator==(const Nil&) const { return true; }
    bool operator!=(const Nil&) const { return false; }
};

class Value {
public:
    std::variant<Nil, bool, double, std::string, ValueArray, WindowPtr, SoundPtr, FormPtr, DatabasePtr, TablePtr, EntryPtr, Complex> asVariant;

    Value() : asVariant(Nil{}) {}
    Value(Nil) : asVariant(Nil{}) {}
    Value(bool b) : asVariant(b) {}
    Value(double d) : asVariant(d) {}
    Value(const std::string& s) : asVariant(s) {}
    Value(const char* s) : asVariant(std::string(s)) {}
    Value(ValueArray arr) : asVariant(arr) {}
    Value(WindowPtr win) : asVariant(win) {}
    Value(SoundPtr snd) : asVariant(snd) {}
    Value(FormPtr frm) : asVariant(frm) {}
    Value(DatabasePtr db) : asVariant(db) {}
    Value(TablePtr tbl) : asVariant(tbl) {}
    Value(EntryPtr entry) : asVariant(entry) {}
    Value(Complex c) : asVariant(c) {}

    bool isNil() const { return std::holds_alternative<Nil>(asVariant); }
    bool isBool() const { return std::holds_alternative<bool>(asVariant); }
    bool isNumber() const { return std::holds_alternative<double>(asVariant); }
    bool isString() const { return std::holds_alternative<std::string>(asVariant); }
    bool isArray() const { return std::holds_alternative<ValueArray>(asVariant); }
    bool isWindow() const { return std::holds_alternative<WindowPtr>(asVariant); }
    bool isSound() const { return std::holds_alternative<SoundPtr>(asVariant); }
    bool isForm() const { return std::holds_alternative<FormPtr>(asVariant); }
    bool isDatabase() const { return std::holds_alternative<DatabasePtr>(asVariant); }
    bool isTable() const { return std::holds_alternative<TablePtr>(asVariant); }
    bool isEntry() const { return std::holds_alternative<EntryPtr>(asVariant); }
    bool isComplex() const { return std::holds_alternative<Complex>(asVariant); }

    bool asBool() const { return std::get<bool>(asVariant); }
    double asNumber() const { return std::get<double>(asVariant); }
    const std::string& asString() const { return std::get<std::string>(asVariant); }
    ValueArray asArray() const { return std::get<ValueArray>(asVariant); }
    WindowPtr asWindow() const { return std::get<WindowPtr>(asVariant); }
    SoundPtr asSound() const { return std::get<SoundPtr>(asVariant); }
    FormPtr asForm() const { return std::get<FormPtr>(asVariant); }
    DatabasePtr asDatabase() const { return std::get<DatabasePtr>(asVariant); }
    TablePtr asTable() const { return std::get<TablePtr>(asVariant); }
    EntryPtr asEntry() const { return std::get<EntryPtr>(asVariant); }
    Complex asComplex() const { return std::get<Complex>(asVariant); }

    bool isTruthy() const {
        if (isNil()) return false;
        if (isBool()) return asBool();
        if (isNumber()) return asNumber() != 0.0;
        if (isString()) return !asString().empty();
        if (isArray()) return !asArray()->empty();
        if (isWindow()) return true;
        if (isSound()) return true;
        if (isForm()) return true;
        if (isDatabase()) return true;
        if (isTable()) return true;
        if (isEntry()) return true;
        if (isComplex()) return asComplex().real != 0.0 || asComplex().imag != 0.0;
        return true;
    }

    std::string toString() const {
        if (isNil()) return "null";
        if (isBool()) return asBool() ? "true" : "false";
        if (isNumber()) {
            double d = asNumber();
            if (std::isnan(d)) return "nan";
            if (std::isinf(d)) return "infinity";
            if (std::floor(d) == d) {
                return std::to_string(static_cast<long long>(d));
            }
            std::ostringstream ss;
            ss << std::setprecision(14) << d;
            return ss.str();
        }
        if (isString()) return asString();
        if (isWindow()) return "<window>";
        if (isSound()) return "<sound>";
        if (isForm()) return "<form>";
        if (isDatabase()) return "<database>";
        if (isTable()) return "<table>";
        if (isEntry()) return "<entry>";
        if (isComplex()) {
            Complex c = asComplex();
            if (std::isnan(c.real) || std::isnan(c.imag)) return "nan";
            if (std::isinf(c.real) || std::isinf(c.imag)) return "infinity";
            if (c.imag == 0.0) {
                double d = c.real;
                if (std::floor(d) == d) {
                    return std::to_string(static_cast<long long>(d));
                }
                std::ostringstream ss;
                ss << std::setprecision(14) << d;
                return ss.str();
            }
            std::ostringstream ss;
            ss << std::setprecision(14);
            if (c.real != 0.0) {
                ss << c.real;
                if (c.imag > 0.0) {
                    ss << " + ";
                    if (c.imag != 1.0) ss << c.imag;
                    ss << "i";
                } else {
                    ss << " - ";
                    if (c.imag != -1.0) ss << -c.imag;
                    ss << "i";
                }
            } else {
                if (c.imag == 1.0) ss << "i";
                else if (c.imag == -1.0) ss << "-i";
                else ss << c.imag << "i";
            }
            return ss.str();
        }
        if (isArray()) {
            std::string res = "[";
            auto arr = asArray();
            for (size_t i = 0; i < arr->size(); ++i) {
                if (i > 0) res += ", ";
                if ((*arr)[i].isString()) {
                    res += "\"" + (*arr)[i].asString() + "\"";
                } else {
                    res += (*arr)[i].toString();
                }
            }
            res += "]";
            return res;
        }
        return "unknown";
    }

    std::string typeString() const {
        if (isNil()) return "nil";
        if (isBool()) return "boolean";
        if (isNumber()) return "number";
        if (isString()) return "string";
        if (isArray()) return "array";
        if (isWindow()) return "window";
        if (isSound()) return "sound";
        if (isForm()) return "form";
        if (isDatabase()) return "database";
        if (isTable()) return "table";
        if (isEntry()) return "entry";
        if (isComplex()) return "complex";
        return "unknown";
    }

    Value clone() const {
        if (isArray()) {
            auto oldArr = asArray();
            auto newArr = std::make_shared<std::vector<Value>>();
            newArr->reserve(oldArr->size());
            for (const auto& item : *oldArr) {
                newArr->push_back(item.clone());
            }
            return Value(newArr);
        }
        return *this;
    }
};

inline bool valuesEqual(const Value& a, const Value& b) {
    if (a.asVariant.index() != b.asVariant.index()) return false;
    if (a.isNil()) return true;
    if (a.isBool()) return a.asBool() == b.asBool();
    if (a.isNumber()) return a.asNumber() == b.asNumber();
    if (a.isString()) return a.asString() == b.asString();
    if (a.isWindow()) return a.asWindow() == b.asWindow();
    if (a.isSound()) return a.asSound() == b.asSound();
    if (a.isForm()) return a.asForm() == b.asForm();
    if (a.isDatabase()) return a.asDatabase() == b.asDatabase();
    if (a.isTable()) return a.asTable() == b.asTable();
    if (a.isEntry()) return a.asEntry() == b.asEntry();
    if (a.isComplex()) return a.asComplex() == b.asComplex();
    if (a.isArray()) {
        auto arrA = a.asArray();
        auto arrB = b.asArray();
        if (arrA->size() != arrB->size()) return false;
        for (size_t i = 0; i < arrA->size(); ++i) {
            if (!valuesEqual((*arrA)[i], (*arrB)[i])) return false;
        }
        return true;
    }
    return false;
}

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

// Forward declarations
struct Value;
using ValueArray = std::shared_ptr<std::vector<Value>>;
using WindowPtr = std::shared_ptr<WindowInstance>;
using SoundPtr = std::shared_ptr<SoundInstance>;

class RuntimeError : public std::runtime_error {
public:
    int line;
    RuntimeError(int line, const std::string& message)
        : std::runtime_error(message), line(line) {}
};

struct Nil {
    bool operator==(const Nil&) const { return true; }
    bool operator!=(const Nil&) const { return false; }
};

class Value {
public:
    std::variant<Nil, bool, double, std::string, ValueArray, WindowPtr, SoundPtr> asVariant;

    Value() : asVariant(Nil{}) {}
    Value(Nil) : asVariant(Nil{}) {}
    Value(bool b) : asVariant(b) {}
    Value(double d) : asVariant(d) {}
    Value(const std::string& s) : asVariant(s) {}
    Value(const char* s) : asVariant(std::string(s)) {}
    Value(ValueArray arr) : asVariant(arr) {}
    Value(WindowPtr win) : asVariant(win) {}
    Value(SoundPtr snd) : asVariant(snd) {}

    bool isNil() const { return std::holds_alternative<Nil>(asVariant); }
    bool isBool() const { return std::holds_alternative<bool>(asVariant); }
    bool isNumber() const { return std::holds_alternative<double>(asVariant); }
    bool isString() const { return std::holds_alternative<std::string>(asVariant); }
    bool isArray() const { return std::holds_alternative<ValueArray>(asVariant); }
    bool isWindow() const { return std::holds_alternative<WindowPtr>(asVariant); }
    bool isSound() const { return std::holds_alternative<SoundPtr>(asVariant); }

    bool asBool() const { return std::get<bool>(asVariant); }
    double asNumber() const { return std::get<double>(asVariant); }
    const std::string& asString() const { return std::get<std::string>(asVariant); }
    ValueArray asArray() const { return std::get<ValueArray>(asVariant); }
    WindowPtr asWindow() const { return std::get<WindowPtr>(asVariant); }
    SoundPtr asSound() const { return std::get<SoundPtr>(asVariant); }

    bool isTruthy() const {
        if (isNil()) return false;
        if (isBool()) return asBool();
        if (isNumber()) return asNumber() != 0.0;
        if (isString()) return !asString().empty();
        if (isArray()) return !asArray()->empty();
        if (isWindow()) return true;
        if (isSound()) return true;
        return true;
    }

    std::string toString() const {
        if (isNil()) return "null";
        if (isBool()) return asBool() ? "true" : "false";
        if (isNumber()) {
            double d = asNumber();
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
        return "unknown";
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

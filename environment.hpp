#pragma once
#include <unordered_map>
#include <string>
#include <memory>
#include "value.hpp"

class Environment : public std::enable_shared_from_this<Environment> {
private:
    std::unordered_map<std::string, Value> values;
    std::shared_ptr<Environment> enclosing;

public:
    Environment() : enclosing(nullptr) {}
    Environment(std::shared_ptr<Environment> enclosing) : enclosing(enclosing) {}

    std::shared_ptr<Environment> getEnclosing() const {
        return enclosing;
    }

    void define(const std::string& name, Value value) {
        values[name] = value;
    }

    bool assign(const std::string& name, Value value) {
        if (values.find(name) != values.end()) {
            values[name] = value;
            return true;
        }

        if (enclosing != nullptr) {
            return enclosing->assign(name, value);
        }

        return false;
    }

    Value get(const std::string& name, bool& found) {
        auto it = values.find(name);
        if (it != values.end()) {
            found = true;
            return it->second;
        }

        if (enclosing != nullptr) {
            return enclosing->get(name, found);
        }

        found = false;
        return Value();
    }
};

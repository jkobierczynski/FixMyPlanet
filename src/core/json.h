// SPDX-License-Identifier: GPL-3.0-or-later
// A small JSON value with a parser and a writer; enough for session files.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace fixmyplanet {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(bool b) : type_(Type::Bool), bool_(b) {}
    Json(double n) : type_(Type::Number), num_(n) {}
    Json(int n) : type_(Type::Number), num_(n) {}
    Json(const char* s) : type_(Type::String), str_(s) {}
    Json(const std::string& s) : type_(Type::String), str_(s) {}

    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    double number(double fallback = 0.0) const { return type_ == Type::Number ? num_ : fallback; }
    bool boolean(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
    std::string string(const std::string& fallback = "") const { return type_ == Type::String ? str_ : fallback; }

    // Arrays.
    const std::vector<Json>& items() const { return arr_; }
    void push(const Json& v) { arr_.push_back(v); }

    // Objects (insertion order is kept for writing).
    bool has(const std::string& key) const;
    const Json& get(const std::string& key) const;   // Null value if missing
    void set(const std::string& key, const Json& v);
    const std::vector<std::string>& keys() const { return keys_; }

    std::string dump(int indent = 2) const;

    // Returns false and sets `error` (with a character offset) on malformed input.
    static bool parse(const std::string& text, Json& out, std::string& error);

private:
    void write(std::string& out, int indent, int level) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    std::vector<Json> arr_;
    std::vector<std::string> keys_;
    std::map<std::string, Json> obj_;
};

}  // namespace fixmyplanet

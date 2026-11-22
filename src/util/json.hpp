// WeBjammin — minimal JSON value, parser and writer.
//
// Deliberately small and dependency-free: the Cardputer Zero has no
// json library and pulling one in for config/chart files would cost more RAM
// than the whole game. Supports objects, arrays, strings, numbers, bools and
// null. Object key order is preserved on write.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace wj {

class Json;
using JsonArray = std::vector<Json>;
using JsonObject = std::vector<std::pair<std::string, Json>>;

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() : type_(Type::Null) {}
    Json(std::nullptr_t) : type_(Type::Null) {}
    Json(bool v) : type_(Type::Bool), bool_(v) {}
    Json(double v) : type_(Type::Number), num_(v) {}
    Json(int v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(long long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(unsigned long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(unsigned v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(const char* v) : type_(Type::String), str_(v ? v : "") {}
    Json(std::string v) : type_(Type::String), str_(std::move(v)) {}
    Json(JsonArray v) : type_(Type::Array), arr_(std::move(v)) {}
    Json(JsonObject v) : type_(Type::Object), obj_(std::move(v)) {}

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    bool as_bool(bool def = false) const { return type_ == Type::Bool ? bool_ : def; }
    double as_double(double def = 0.0) const { return type_ == Type::Number ? num_ : def; }
    int as_int(int def = 0) const {
        return type_ == Type::Number ? static_cast<int>(num_) : def;
    }
    long long as_int64(long long def = 0) const {
        return type_ == Type::Number ? static_cast<long long>(num_) : def;
    }
    const std::string& as_string() const { return str_; }
    std::string as_string(const std::string& def) const {
        return type_ == Type::String ? str_ : def;
    }
    const JsonArray& as_array() const { return arr_; }
    JsonArray& as_array() { return arr_; }
    const JsonObject& as_object() const { return obj_; }
    JsonObject& as_object() { return obj_; }

    // Object access: returns a null Json when the key is missing.
    const Json& operator[](const std::string& key) const {
        for (const auto& kv : obj_)
            if (kv.first == key) return kv.second;
        return null_value();
    }
    Json& operator[](const std::string& key) {
        if (type_ != Type::Object) {
            // Indexing a null or scalar promotes it to an object.
            type_ = Type::Object;
            arr_.clear();
            str_.clear();
            num_ = 0.0;
            bool_ = false;
        }
        for (auto& kv : obj_)
            if (kv.first == key) return kv.second;
        obj_.emplace_back(key, Json());
        return obj_.back().second;
    }
    bool has(const std::string& key) const {
        for (const auto& kv : obj_)
            if (kv.first == key) return true;
        return false;
    }
    size_t size() const {
        if (type_ == Type::Array) return arr_.size();
        if (type_ == Type::Object) return obj_.size();
        return 0;
    }
    const Json& at(size_t i) const {
        return type_ == Type::Array && i < arr_.size() ? arr_[i] : null_value();
    }

    // Convenience typed getters with defaults.
    double get_number(const std::string& key, double def) const {
        return (*this)[key].is_number() ? (*this)[key].as_double() : def;
    }
    int get_int(const std::string& key, int def) const {
        return (*this)[key].is_number() ? (*this)[key].as_int() : def;
    }
    long long get_int64(const std::string& key, long long def) const {
        return (*this)[key].is_number() ? (*this)[key].as_int64() : def;
    }
    bool get_bool(const std::string& key, bool def) const {
        return (*this)[key].is_bool() ? (*this)[key].as_bool() : def;
    }
    std::string get_string(const std::string& key, const std::string& def) const {
        return (*this)[key].is_string() ? (*this)[key].as_string() : def;
    }

    std::string dump(int indent = 2) const;

    // Returns false and fills `error` when the text is not valid JSON.
    static bool parse(const std::string& text, Json& out, std::string& error);

private:
    static const Json& null_value();

    Type type_;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    JsonArray arr_;
    JsonObject obj_;
};

} // namespace wj

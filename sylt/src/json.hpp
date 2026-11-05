#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace sylt {
class JsonValue {
public:
    enum class Kind { Null, Bool, Number, Text, Array, Object };
    Kind K = Kind::Null;
    bool B = false;
    double N = 0;
    std::string S;
    std::vector<JsonValue> A;
    std::map<std::string, JsonValue> O;
    static JsonValue MakeNull() { return JsonValue(); }
    static JsonValue MakeBool(bool v) {
        JsonValue j;
        j.K = Kind::Bool;
        j.B = v;
        return j;
    }
    static JsonValue MakeNum(double v) {
        JsonValue j;
        j.K = Kind::Number;
        j.N = v;
        return j;
    }
    static JsonValue MakeInt(int64_t v) {
        JsonValue j;
        j.K = Kind::Number;
        j.N = (double)v;
        return j;
    }
    static JsonValue MakeText(const std::string& v) {
        JsonValue j;
        j.K = Kind::Text;
        j.S = v;
        return j;
    }
    static JsonValue MakeArr() {
        JsonValue j;
        j.K = Kind::Array;
        return j;
    }
    static JsonValue MakeObj() {
        JsonValue j;
        j.K = Kind::Object;
        return j;
    }
    bool IsNull() const { return K == Kind::Null; }
    bool IsBool() const { return K == Kind::Bool; }
    bool IsNum() const { return K == Kind::Number; }
    bool IsText() const { return K == Kind::Text; }
    bool IsArr() const { return K == Kind::Array; }
    bool IsObj() const { return K == Kind::Object; }
    bool AsBool() const { return B; }
    double AsNum() const { return N; }
    int64_t AsInt() const { return (int64_t)N; }
    const std::string& AsText() const { return S; }
    const JsonValue* At(const std::string& key) const {
        if (K != Kind::Object) return nullptr;
        auto it = O.find(key);
        if (it == O.end()) return nullptr;
        return &it->second;
    }
    const JsonValue* At(size_t idx) const {
        if (K != Kind::Array || idx >= A.size()) return nullptr;
        return &A[idx];
    }
    void Set(const std::string& key, JsonValue v) { O[key] = std::move(v); }
    void Push(JsonValue v) { A.push_back(std::move(v)); }
    bool ParseFrom(const std::string& src, std::string& err);
    std::string Dump() const;
};
bool JsonParse(const std::string& src, JsonValue& out, std::string& err);
std::string JsonDump(const JsonValue& v);
}

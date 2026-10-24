#pragma once

#include <string>
#include <vector>
#include <utility>
#include <cstdint>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace luna::util {

enum class JsonType {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object
};

class JsonValue {
public:
    JsonType type = JsonType::Null;
    bool boolVal = false;
    double numVal = 0.0;
    int64_t intVal = 0;
    std::string strVal;
    std::vector<JsonValue> arrVal;
    std::vector<std::pair<std::string, JsonValue>> objVal;

    JsonValue() = default;
    JsonValue(JsonType t) : type(t) {}
    JsonValue(bool b) : type(JsonType::Bool), boolVal(b) {}
    JsonValue(double n) : type(JsonType::Number), numVal(n), intVal(static_cast<int64_t>(n)) {}
    JsonValue(int n) : type(JsonType::Number), numVal(n), intVal(n) {}
    JsonValue(const std::string& s) : type(JsonType::String), strVal(s) {}
    JsonValue(const char* s) : type(JsonType::String), strVal(s ? s : "") {}

    bool isNull() const { return type == JsonType::Null; }
    bool isBool() const { return type == JsonType::Bool; }
    bool isNumber() const { return type == JsonType::Number; }
    bool isString() const { return type == JsonType::String; }
    bool isArray() const { return type == JsonType::Array; }
    bool isObject() const { return type == JsonType::Object; }

    const std::string& asString(const std::string& def = "") const {
        return (type == JsonType::String) ? strVal : def;
    }

    int asInt(int def = 0) const {
        if (type == JsonType::Number) return static_cast<int>(intVal);
        if (type == JsonType::String && !strVal.empty()) {
            return std::atoi(strVal.c_str());
        }
        return def;
    }

    int64_t asInt64(int64_t def = 0) const {
        if (type == JsonType::Number) return intVal;
        if (type == JsonType::String && !strVal.empty()) {
            return std::strtoll(strVal.c_str(), nullptr, 10);
        }
        return def;
    }

    bool asBool(bool def = false) const {
        if (type == JsonType::Bool) return boolVal;
        if (type == JsonType::String) {
            return (strVal == "true" || strVal == "1");
        }
        return def;
    }

    size_t size() const {
        if (type == JsonType::Array) return arrVal.size();
        if (type == JsonType::Object) return objVal.size();
        if (type == JsonType::String) return strVal.size();
        return 0;
    }

    bool contains(const std::string& key) const {
        if (type != JsonType::Object) return false;
        for (const auto& kv : objVal) {
            if (kv.first == key) return true;
        }
        return false;
    }

    const JsonValue& operator[](const std::string& key) const {
        static const JsonValue kNullValue;
        if (type != JsonType::Object) return kNullValue;
        for (const auto& kv : objVal) {
            if (kv.first == key) return kv.second;
        }
        return kNullValue;
    }

    const JsonValue& operator[](size_t index) const {
        static const JsonValue kNullValue;
        if (type != JsonType::Array || index >= arrVal.size()) return kNullValue;
        return arrVal[index];
    }

    static JsonValue parse(const std::string& text) {
        const char* p = text.c_str();
        const char* end = p + text.size();
        skipWhitespace(p, end);
        if (p >= end) return JsonValue();
        return parseValue(p, end);
    }

    // Helper to unescape string with multiple passes if needed
    static std::string unescape(const std::string& input) {
        std::string out;
        out.reserve(input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            if (input[i] == '\\' && i + 1 < input.size()) {
                char next = input[i + 1];
                if (next == '"') { out += '"'; i++; }
                else if (next == '\\') { out += '\\'; i++; }
                else if (next == '/') { out += '/'; i++; }
                else if (next == 'b') { out += '\b'; i++; }
                else if (next == 'f') { out += '\f'; i++; }
                else if (next == 'n') { out += '\n'; i++; }
                else if (next == 'r') { out += '\r'; i++; }
                else if (next == 't') { out += '\t'; i++; }
                else { out += next; i++; }
            } else {
                out += input[i];
            }
        }
        return out;
    }

private:
    static void skipWhitespace(const char*& p, const char* end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
            p++;
        }
    }

    static std::string parseString(const char*& p, const char* end) {
        if (p >= end || *p != '"') return "";
        p++; // skip opening quote
        std::string s;
        while (p < end) {
            char c = *p++;
            if (c == '"') {
                return s;
            }
            if (c == '\\' && p < end) {
                char esc = *p++;
                switch (esc) {
                    case '"': s += '"'; break;
                    case '\\': s += '\\'; break;
                    case '/': s += '/'; break;
                    case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case 'n': s += '\n'; break;
                    case 'r': s += '\r'; break;
                    case 't': s += '\t'; break;
                    case 'u': {
                        // 4 hex digits
                        if (p + 4 <= end) {
                            uint32_t cp = 0;
                            for (int i = 0; i < 4; ++i) {
                                char h = *p++;
                                cp <<= 4;
                                if (h >= '0' && h <= '9') cp |= (h - '0');
                                else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                                else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                            }
                            // UTF-8 encode
                            if (cp <= 0x7F) {
                                s += static_cast<char>(cp);
                            } else if (cp <= 0x7FF) {
                                s += static_cast<char>(0xC0 | ((cp >> 6) & 0x1F));
                                s += static_cast<char>(0x80 | (cp & 0x3F));
                            } else {
                                s += static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
                                s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                                s += static_cast<char>(0x80 | (cp & 0x3F));
                            }
                        }
                        break;
                    }
                    default:
                        s += esc;
                        break;
                }
            } else {
                s += c;
            }
        }
        return s;
    }

    static JsonValue parseValue(const char*& p, const char* end) {
        skipWhitespace(p, end);
        if (p >= end) return JsonValue();

        if (*p == '{') return parseObject(p, end);
        if (*p == '[') return parseArray(p, end);
        if (*p == '"') {
            JsonValue v(JsonType::String);
            v.strVal = parseString(p, end);
            return v;
        }
        if (*p == 't' || *p == 'f') return parseBool(p, end);
        if (*p == 'n') return parseNull(p, end);
        if (*p == '-' || (*p >= '0' && *p <= '9')) return parseNumber(p, end);

        p++; // skip unexpected char
        return JsonValue();
    }

    static JsonValue parseObject(const char*& p, const char* end) {
        JsonValue val(JsonType::Object);
        p++; // skip '{'
        skipWhitespace(p, end);

        if (p < end && *p == '}') {
            p++;
            return val;
        }

        while (p < end) {
            skipWhitespace(p, end);
            if (p >= end || *p != '"') break;

            std::string key = parseString(p, end);
            skipWhitespace(p, end);

            if (p >= end || *p != ':') break;
            p++; // skip ':'

            JsonValue child = parseValue(p, end);
            val.objVal.emplace_back(std::move(key), std::move(child));

            skipWhitespace(p, end);
            if (p < end && *p == ',') {
                p++;
                continue;
            }
            if (p < end && *p == '}') {
                p++;
                break;
            }
            break;
        }
        return val;
    }

    static JsonValue parseArray(const char*& p, const char* end) {
        JsonValue val(JsonType::Array);
        p++; // skip '['
        skipWhitespace(p, end);

        if (p < end && *p == ']') {
            p++;
            return val;
        }

        while (p < end) {
            JsonValue item = parseValue(p, end);
            val.arrVal.push_back(std::move(item));

            skipWhitespace(p, end);
            if (p < end && *p == ',') {
                p++;
                continue;
            }
            if (p < end && *p == ']') {
                p++;
                break;
            }
            break;
        }
        return val;
    }

    static JsonValue parseNumber(const char*& p, const char* end) {
        const char* start = p;
        if (*p == '-') p++;
        while (p < end && (*p >= '0' && *p <= '9')) p++;
        if (p < end && *p == '.') {
            p++;
            while (p < end && (*p >= '0' && *p <= '9')) p++;
        }
        if (p < end && (*p == 'e' || *p == 'E')) {
            p++;
            if (p < end && (*p == '+' || *p == '-')) p++;
            while (p < end && (*p >= '0' && *p <= '9')) p++;
        }

        std::string numStr(start, p - start);
        JsonValue v(JsonType::Number);
        v.numVal = std::strtod(numStr.c_str(), nullptr);
        v.intVal = std::strtoll(numStr.c_str(), nullptr, 10);
        return v;
    }

    static JsonValue parseBool(const char*& p, const char* end) {
        if (p + 4 <= end && std::strncmp(p, "true", 4) == 0) {
            p += 4;
            return JsonValue(true);
        }
        if (p + 5 <= end && std::strncmp(p, "false", 5) == 0) {
            p += 5;
            return JsonValue(false);
        }
        p++;
        return JsonValue();
    }

    static JsonValue parseNull(const char*& p, const char* end) {
        if (p + 4 <= end && std::strncmp(p, "null", 4) == 0) {
            p += 4;
            return JsonValue(JsonType::Null);
        }
        p++;
        return JsonValue();
    }
};

} // namespace luna::util

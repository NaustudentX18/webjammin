#include "util/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace wj {
namespace {

struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string err;

    explicit Parser(const std::string& text) : s(text) {}

    void skip_ws() {
        while (i < s.size()) {
            char c = s[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++i;
            } else {
                break;
            }
        }
    }

    bool fail(const std::string& msg) {
        if (err.empty())
            err = msg + " at offset " + std::to_string(i);
        return false;
    }

    bool parse_value(Json& out) {
        skip_ws();
        if (i >= s.size()) return fail("unexpected end of input");
        char c = s[i];
        switch (c) {
            case '{': return parse_object(out);
            case '[': return parse_array(out);
            case '"': {
                std::string str;
                if (!parse_string(str)) return false;
                out = Json(str);
                return true;
            }
            case 't':
                if (s.compare(i, 4, "true") == 0) {
                    i += 4;
                    out = Json(true);
                    return true;
                }
                return fail("invalid literal");
            case 'f':
                if (s.compare(i, 5, "false") == 0) {
                    i += 5;
                    out = Json(false);
                    return true;
                }
                return fail("invalid literal");
            case 'n':
                if (s.compare(i, 4, "null") == 0) {
                    i += 4;
                    out = Json();
                    return true;
                }
                return fail("invalid literal");
            default: return parse_number(out);
        }
    }

    bool parse_number(Json& out) {
        size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        bool any = false;
        while (i < s.size() && isdigit(static_cast<unsigned char>(s[i]))) {
            ++i;
            any = true;
        }
        if (i < s.size() && s[i] == '.') {
            ++i;
            while (i < s.size() && isdigit(static_cast<unsigned char>(s[i]))) {
                ++i;
                any = true;
            }
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            ++i;
            if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
            while (i < s.size() && isdigit(static_cast<unsigned char>(s[i]))) ++i;
        }
        if (!any) return fail("invalid number");
        out = Json(std::strtod(s.substr(start, i - start).c_str(), nullptr));
        return true;
    }

    bool parse_string(std::string& out) {
        if (i >= s.size() || s[i] != '"') return fail("expected string");
        ++i;
        out.clear();
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (i >= s.size()) return fail("unterminated escape");
            char e = s[i++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (i + 4 > s.size()) return fail("bad \\u escape");
                    unsigned cp = static_cast<unsigned>(
                        std::strtoul(s.substr(i, 4).c_str(), nullptr, 16));
                    i += 4;
                    // Only the BMP subset we need (ASCII + Latin-1) is encoded.
                    if (cp < 0x80) {
                        out.push_back(static_cast<char>(cp));
                    } else if (cp < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: return fail("unknown escape");
            }
        }
        return fail("unterminated string");
    }

    bool parse_array(Json& out) {
        ++i; // '['
        JsonArray arr;
        skip_ws();
        if (i < s.size() && s[i] == ']') {
            ++i;
            out = Json(std::move(arr));
            return true;
        }
        while (true) {
            Json v;
            if (!parse_value(v)) return false;
            arr.push_back(std::move(v));
            skip_ws();
            if (i >= s.size()) return fail("unterminated array");
            if (s[i] == ',') {
                ++i;
                continue;
            }
            if (s[i] == ']') {
                ++i;
                out = Json(std::move(arr));
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    bool parse_object(Json& out) {
        ++i; // '{'
        JsonObject obj;
        skip_ws();
        if (i < s.size() && s[i] == '}') {
            ++i;
            out = Json(std::move(obj));
            return true;
        }
        while (true) {
            skip_ws();
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (i >= s.size() || s[i] != ':') return fail("expected ':'");
            ++i;
            Json v;
            if (!parse_value(v)) return false;
            obj.emplace_back(std::move(key), std::move(v));
            skip_ws();
            if (i >= s.size()) return fail("unterminated object");
            if (s[i] == ',') {
                ++i;
                continue;
            }
            if (s[i] == '}') {
                ++i;
                out = Json(std::move(obj));
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }
};

void dump_value(const Json& j, std::string& out, int indent, int depth) {
    const std::string pad(indent > 0 ? (depth + 1) * indent : 0, ' ');
    const std::string pad_end(indent > 0 ? depth * indent : 0, ' ');
    const char* nl = indent > 0 ? "\n" : "";
    switch (j.type()) {
        case Json::Type::Null: out += "null"; break;
        case Json::Type::Bool: out += j.as_bool() ? "true" : "false"; break;
        case Json::Type::Number: {
            double v = j.as_double();
            char buf[40];
            if (v == static_cast<long long>(v))
                std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v));
            else
                std::snprintf(buf, sizeof buf, "%.6g", v);
            out += buf;
            break;
        }
        case Json::Type::String: {
            out += '"';
            for (char c : j.as_string()) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (static_cast<unsigned char>(c) < 0x20) {
                            char buf[8];
                            std::snprintf(buf, sizeof buf, "\\u%04x", c);
                            out += buf;
                        } else {
                            out += c;
                        }
                }
            }
            out += '"';
            break;
        }
        case Json::Type::Array: {
            if (j.as_array().empty()) {
                out += "[]";
                break;
            }
            out += '[';
            out += nl;
            bool first = true;
            for (const auto& e : j.as_array()) {
                if (!first) {
                    out += ',';
                    out += nl;
                }
                first = false;
                out += pad;
                dump_value(e, out, indent, depth + 1);
            }
            out += nl;
            out += pad_end;
            out += ']';
            break;
        }
        case Json::Type::Object: {
            if (j.as_object().empty()) {
                out += "{}";
                break;
            }
            out += '{';
            out += nl;
            bool first = true;
            for (const auto& kv : j.as_object()) {
                if (!first) {
                    out += ',';
                    out += nl;
                }
                first = false;
                out += pad;
                out += '"';
                out += kv.first;
                out += "\":";
                if (indent > 0) out += ' ';
                dump_value(kv.second, out, indent, depth + 1);
            }
            out += nl;
            out += pad_end;
            out += '}';
            break;
        }
    }
}

} // namespace

const Json& Json::null_value() {
    static const Json n;
    return n;
}

std::string Json::dump(int indent) const {
    std::string out;
    dump_value(*this, out, indent, 0);
    return out;
}

bool Json::parse(const std::string& text, Json& out, std::string& error) {
    Parser p(text);
    if (!p.parse_value(out)) {
        error = p.err;
        return false;
    }
    p.skip_ws();
    if (p.i != text.size()) {
        error = "trailing data at offset " + std::to_string(p.i);
        return false;
    }
    error.clear();
    return true;
}

} // namespace wj

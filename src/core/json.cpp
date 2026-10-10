// SPDX-License-Identifier: GPL-3.0-or-later
#include "json.h"

#include <cmath>
#include <cstdio>

#include "strutil.h"

namespace fixmyplanet {

bool Json::has(const std::string& key) const { return obj_.find(key) != obj_.end(); }

const Json& Json::get(const std::string& key) const {
    static const Json null;
    auto it = obj_.find(key);
    return it == obj_.end() ? null : it->second;
}

void Json::set(const std::string& key, const Json& v) {
    if (obj_.find(key) == obj_.end()) keys_.push_back(key);
    obj_[key] = v;
}

static void writeString(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char ch : s) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", ch);
                    out += buf;
                } else {
                    out += static_cast<char>(ch);
                }
        }
    }
    out += '"';
}

void Json::write(std::string& out, int indent, int level) const {
    auto newline = [&](int lv) {
        if (indent > 0) {
            out += '\n';
            out.append(static_cast<size_t>(indent) * lv, ' ');
        }
    };
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Number: {
            if (!std::isfinite(num_)) {
                out += "null";
            } else if (num_ == std::floor(num_) && std::fabs(num_) < 1e15) {
                out += formatFixed(num_, 0);
            } else {
                out += formatDouble(num_, 12);
            }
            break;
        }
        case Type::String: writeString(out, str_); break;
        case Type::Array:
            if (arr_.empty()) { out += "[]"; break; }
            out += '[';
            for (size_t i = 0; i < arr_.size(); ++i) {
                newline(level + 1);
                arr_[i].write(out, indent, level + 1);
                if (i + 1 < arr_.size()) out += ',';
            }
            newline(level);
            out += ']';
            break;
        case Type::Object:
            if (keys_.empty()) { out += "{}"; break; }
            out += '{';
            for (size_t i = 0; i < keys_.size(); ++i) {
                newline(level + 1);
                writeString(out, keys_[i]);
                out += indent > 0 ? ": " : ":";
                obj_.at(keys_[i]).write(out, indent, level + 1);
                if (i + 1 < keys_.size()) out += ',';
            }
            newline(level);
            out += '}';
            break;
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    write(out, indent, 0);
    if (indent > 0) out += '\n';
    return out;
}

namespace {

struct Parser {
    const std::string& s;
    size_t p = 0;
    std::string err;
    int depth = 0;

    void ws() {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) ++p;
    }
    bool fail(const std::string& m) {
        if (err.empty()) err = m + " at offset " + std::to_string(p);
        return false;
    }
    static void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& v) {
        if (p + 4 > s.size()) return fail("bad \\u escape");
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s[p++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }
    bool str(std::string& out) {
        if (p >= s.size() || s[p] != '"') return fail("expected string");
        ++p;
        while (p < s.size()) {
            char c = s[p++];
            if (c == '"') return true;
            if (c == '\\') {
                if (p >= s.size()) break;
                char e = s[p++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        unsigned cp = 0;
                        if (!hex4(cp)) return false;
                        if (cp >= 0xD800 && cp < 0xDC00 && p + 1 < s.size() && s[p] == '\\' && s[p + 1] == 'u') {
                            p += 2;
                            unsigned lo = 0;
                            if (!hex4(lo)) return false;
                            if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        utf8(out, cp);
                        break;
                    }
                    default: return fail("bad escape");
                }
            } else {
                out += c;
            }
        }
        return fail("unterminated string");
    }
    bool value(Json& out) {
        if (++depth > 64) return fail("nesting too deep");
        ws();
        if (p >= s.size()) return fail("unexpected end");
        char c = s[p];
        bool ok = true;
        if (c == '{') {
            ++p;
            out = Json::object();
            ws();
            if (p < s.size() && s[p] == '}') ++p;
            else
                for (;;) {
                    ws();
                    std::string key;
                    if (!str(key)) { ok = false; break; }
                    ws();
                    if (p >= s.size() || s[p] != ':') { ok = fail("expected ':'"); break; }
                    ++p;
                    Json v;
                    if (!value(v)) { ok = false; break; }
                    out.set(key, v);
                    ws();
                    if (p < s.size() && s[p] == ',') { ++p; continue; }
                    if (p < s.size() && s[p] == '}') { ++p; break; }
                    ok = fail("expected ',' or '}'");
                    break;
                }
        } else if (c == '[') {
            ++p;
            out = Json::array();
            ws();
            if (p < s.size() && s[p] == ']') ++p;
            else
                for (;;) {
                    Json v;
                    if (!value(v)) { ok = false; break; }
                    out.push(v);
                    ws();
                    if (p < s.size() && s[p] == ',') { ++p; continue; }
                    if (p < s.size() && s[p] == ']') { ++p; break; }
                    ok = fail("expected ',' or ']'");
                    break;
                }
        } else if (c == '"') {
            std::string v;
            ok = str(v);
            if (ok) out = Json(v);
        } else if (s.compare(p, 4, "true") == 0) {
            p += 4;
            out = Json(true);
        } else if (s.compare(p, 5, "false") == 0) {
            p += 5;
            out = Json(false);
        } else if (s.compare(p, 4, "null") == 0) {
            p += 4;
            out = Json();
        } else {
            size_t start = p;
            if (p < s.size() && (s[p] == '-' || s[p] == '+')) ++p;
            while (p < s.size() && ((s[p] >= '0' && s[p] <= '9') || s[p] == '.' || s[p] == 'e' || s[p] == 'E' ||
                                    s[p] == '-' || s[p] == '+'))
                ++p;
            if (p == start) ok = fail("unexpected character");
            else {
                double d;
                if (!parseDouble(s.substr(start, p - start), d)) ok = fail("bad number");
                else out = Json(d);
            }
        }
        --depth;
        return ok;
    }
};

}  // namespace

bool Json::parse(const std::string& text, Json& out, std::string& error) {
    Parser ps{text, 0, std::string(), 0};
    // Skip a UTF-8 byte order mark.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        ps.p = 3;
    if (!ps.value(out)) {
        error = ps.err;
        return false;
    }
    ps.ws();
    if (ps.p != text.size()) {
        error = "trailing characters at offset " + std::to_string(ps.p);
        return false;
    }
    return true;
}

}  // namespace fixmyplanet

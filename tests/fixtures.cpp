#include "fixtures.h"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace tb::test {

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : s_(text) {}

    Json value() {
        space();
        if (i_ >= s_.size()) fail("unexpected end");
        char c = s_[i_];
        Json v;
        if (c == '{') {
            v.type_ = Json::Type::object;
            ++i_;
            space();
            if (peek() == '}') {
                ++i_;
                return v;
            }
            while (true) {
                space();
                std::string key = string();
                space();
                expect(':');
                v.members_.emplace_back(key, value());
                space();
                if (peek() == ',') {
                    ++i_;
                    continue;
                }
                expect('}');
                return v;
            }
        }
        if (c == '[') {
            v.type_ = Json::Type::array;
            ++i_;
            space();
            if (peek() == ']') {
                ++i_;
                return v;
            }
            while (true) {
                v.items_.push_back(value());
                space();
                if (peek() == ',') {
                    ++i_;
                    continue;
                }
                expect(']');
                return v;
            }
        }
        if (c == '"') {
            v.type_ = Json::Type::string;
            v.text_ = string();
            return v;
        }
        if (s_.compare(i_, 4, "true") == 0) {
            i_ += 4;
            v.type_ = Json::Type::boolean;
            v.bool_ = true;
            return v;
        }
        if (s_.compare(i_, 5, "false") == 0) {
            i_ += 5;
            v.type_ = Json::Type::boolean;
            return v;
        }
        if (s_.compare(i_, 4, "null") == 0) {
            i_ += 4;
            return v;
        }
        size_t start = i_;
        while (i_ < s_.size() && (std::isdigit(uint8_t(s_[i_])) || std::strchr("+-.eE", s_[i_]))) ++i_;
        if (i_ == start) fail("unexpected character");
        v.type_ = Json::Type::number;
        v.text_ = s_.substr(start, i_ - start);
        return v;
    }

    void end() {
        space();
        if (i_ != s_.size()) fail("trailing data");
    }

private:
    [[noreturn]] void fail(const char* what) { throw std::runtime_error(std::string("json: ") + what + " at " + std::to_string(i_)); }
    char peek() const { return i_ < s_.size() ? s_[i_] : '\0'; }
    void space() {
        while (i_ < s_.size() && std::isspace(uint8_t(s_[i_]))) ++i_;
    }
    void expect(char c) {
        if (peek() != c) fail("unexpected character");
        ++i_;
    }

    std::string string() {
        expect('"');
        std::string out;
        while (true) {
            if (i_ >= s_.size()) fail("unterminated string");
            char c = s_[i_++];
            if (c == '"') return out;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            char e = s_[i_++];
            switch (e) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'u': {
                unsigned cp = unsigned(std::stoul(s_.substr(i_, 4), nullptr, 16));
                i_ += 4;
                if (cp >= 0xd800 && cp < 0xdc00 && s_.compare(i_, 2, "\\u") == 0) {
                    unsigned low = unsigned(std::stoul(s_.substr(i_ + 2, 4), nullptr, 16));
                    i_ += 6;
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                }
                if (cp < 0x80) {
                    out.push_back(char(cp));
                } else if (cp < 0x800) {
                    out.push_back(char(0xc0 | cp >> 6));
                    out.push_back(char(0x80 | (cp & 0x3f)));
                } else if (cp < 0x10000) {
                    out.push_back(char(0xe0 | cp >> 12));
                    out.push_back(char(0x80 | ((cp >> 6) & 0x3f)));
                    out.push_back(char(0x80 | (cp & 0x3f)));
                } else {
                    out.push_back(char(0xf0 | cp >> 18));
                    out.push_back(char(0x80 | ((cp >> 12) & 0x3f)));
                    out.push_back(char(0x80 | ((cp >> 6) & 0x3f)));
                    out.push_back(char(0x80 | (cp & 0x3f)));
                }
                break;
            }
            default: out.push_back(e); break;
            }
        }
    }

    const std::string& s_;
    size_t i_ = 0;
};

Json Json::parse(const std::string& text) {
    JsonParser p(text);
    Json v = p.value();
    p.end();
    return v;
}

const Json& Json::operator[](const std::string& key) const {
    static const Json null;
    for (const auto& [k, v] : members_)
        if (k == key) return v;
    return null;
}

bool Json::has(const std::string& key) const {
    for (const auto& [k, v] : members_)
        if (k == key) return !v.isNull();
    return false;
}

const std::string& Json::str() const {
    if (type_ != Type::string) throw std::runtime_error("json: not a string");
    return text_;
}

bool Json::boolean() const {
    if (type_ != Type::boolean) throw std::runtime_error("json: not a boolean");
    return bool_;
}

int64_t Json::i64() const {
    if (type_ != Type::number) throw std::runtime_error("json: not a number");
    return std::stoll(text_);
}

uint64_t Json::u64() const {
    if (type_ != Type::number) throw std::runtime_error("json: not a number");
    return std::stoull(text_);
}

double Json::dbl() const {
    if (type_ != Type::number) throw std::runtime_error("json: not a number");
    std::istringstream in(text_);
    in.imbue(std::locale::classic());
    double v;
    in >> v;
    return v;
}

Bytes Json::hex() const { return h(str()); }

std::vector<std::string> Json::strings() const {
    std::vector<std::string> out;
    for (const auto& item : items_) out.push_back(item.str());
    return out;
}

std::vector<Bytes> Json::hexes() const {
    std::vector<Bytes> out;
    for (const auto& item : items_) out.push_back(item.hex());
    return out;
}

namespace {

std::filesystem::path fixturePath(const std::string& file) { return std::filesystem::path(TWOTBIT_FIXTURES_DIR) / file; }

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("missing fixture " + path.string());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

} // namespace

const Json& fixture(const std::string& name) {
    static std::mutex mutex;
    static std::map<std::string, Json> loaded;
    std::lock_guard lock(mutex);
    auto it = loaded.find(name);
    if (it == loaded.end()) it = loaded.emplace(name, Json::parse(readFile(fixturePath(name + ".json")))).first;
    return it->second;
}

Bytes fixtureData(const std::string& file) {
    std::string data = readFile(fixturePath(file));
    return Bytes(data.begin(), data.end());
}

Bytes h(const std::string& hex) {
    auto data = fromHex(hex);
    if (!data) throw std::runtime_error("bad hex " + hex);
    return *data;
}

const Network& networkNamed(const std::string& name) {
    const Network* n = Network::named(name);
    if (!n) throw std::runtime_error("unknown network " + name);
    return *n;
}

} // namespace tb::test

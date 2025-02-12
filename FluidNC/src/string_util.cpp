#include "string_util.h"
#include <cstdlib>
#include <cctype>

namespace string_util {
    char tolower(char c) {
        if (c >= 'A' && c <= 'Z') {
            return c + ('a' - 'A');
        }
        return c;
    }

    // cppcheck-suppress unusedFunction
    bool equal_ignore_case(std::string_view a, std::string_view b) {
        return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](auto a, auto b) { return tolower(a) == tolower(b); });
    }

    // cppcheck-suppress unusedFunction
    bool starts_with_ignore_case(std::string_view a, std::string_view b) {
        return std::equal(a.begin(), a.begin() + b.size(), b.begin(), b.end(), [](auto a, auto b) { return tolower(a) == tolower(b); });
    }

    // cppcheck-suppress unusedFunction
    const std::string_view trim(std::string_view s) {
        auto start = s.find_first_not_of(" \t\n\r\f\v");
        if (start == std::string_view::npos) {
            return "";
        }
        auto end = s.find_last_not_of(" \t\n\r\f\v");
        if (end == std::string_view::npos) {
            return s.substr(start);
        }
        return s.substr(start, end - start + 1);
    }

    // cppcheck-suppress unusedFunction
    bool is_int(std::string_view s, int32_t& value) {
        char* end;
        value = std::strtol(s.cbegin(), &end, 10);
        return end == s.cend();
    }

    // cppcheck-suppress unusedFunction
    bool is_uint(std::string_view s, uint32_t& value) {
        char* end;
        value = std::strtoul(s.cbegin(), &end, 10);
        return end == s.cend();
    }

    // cppcheck-suppress unusedFunction
    bool is_float(std::string_view s, float& value) {
        char* end;
        value = std::strtof(s.cbegin(), &end);
        return end == s.cend();
    }

    bool from_xdigit(char c, uint8_t& value) {
        if (isdigit(c)) {
            value = c - '0';
            return true;
        }
        c = tolower(c);
        if (c >= 'a' && c <= 'f') {
            value = 10 + c - 'a';
            return true;
        }
        return false;
    }

    bool from_hex(std::string_view str, uint8_t& value) {
        value = 0;
        if (str.size() == 0 || str.size() > 2) {
            return false;
        }
        uint8_t x;
        while (str.size()) {
            value <<= 4;
            if (!from_xdigit(str[0], x)) {
                return false;
            }
            value += x;
            str = str.substr(1);
        }
        return true;
    }
    bool from_decimal(std::string_view str, uint32_t& value) {
        value = 0;
        if (str.size() == 0) {
            return false;
        }
        while (str.size()) {
            if (!isdigit(str[0])) {
                return false;
            }
            value = value * 10 + str[0] - '0';
            str   = str.substr(1);
        }
        return true;
    }

}

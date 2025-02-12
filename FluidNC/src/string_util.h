#pragma once

#include <cstdint>
#include <string_view>

namespace string_util {
    char                   tolower(char c);
    bool                   equal_ignore_case(std::string_view a, std::string_view b);
    bool                   starts_with_ignore_case(std::string_view a, std::string_view b);
    const std::string_view trim(std::string_view s);

    bool is_int(std::string_view s, int32_t& value);
    bool is_uint(std::string_view s, uint32_t& value);
    bool is_float(std::string_view s, float& value);
    bool from_xdigit(char c, uint8_t& value);
    bool from_hex(std::string_view str, uint8_t& value);
    bool from_decimal(std::string_view str, uint32_t& value);
}

// Copyright (c) 2021 -	Stefan de Bruijn
// Copyright (c) 2023 -	Dylan Knutson <dymk@dymk.co>
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Parser.h"

#include "ParseException.h"
#include "../EnumItem.h"

#include "../Config.h"
#include "../string_util.h"

#include <climits>
#include <math.h>  // round
#include <string_view>

namespace Configuration {
    Parser::Parser(std::string_view yaml_string) : Tokenizer(yaml_string) {}

    void Parser::parseError(const char* description) const {
        // Attempt to use the correct position in the parser:
        if (!_token._key.empty()) {
            throw ParseException(_linenum, description);
        } else {
            Tokenizer::ParseError(description);
        }
    }

    bool Parser::is(const char* expected) {
        if (_token._state != TokenState::Matching || _token._key.empty()) {
            return false;
        }
        auto len = strlen(expected);
        if (len != _token._key.size()) {
            return false;
        }
        bool result = !strncasecmp(expected, _token._key.cbegin(), len);
        if (result) {
            _token._state = TokenState::Matched;
        }
        return result;
    }

    // String values might have meaningful leading and trailing spaces so we avoid trimming the string (false)

    // cppcheck-suppress unusedFunction
    std::string_view Parser::stringValue() const {
        return _token._value;
    }

    // cppcheck-suppress unusedFunction
    bool Parser::boolValue() const {
        return string_util::equal_ignore_case(string_util::trim(_token._value), "true");
    }

    // cppcheck-suppress unusedFunction
    int Parser::intValue() const {
        auto    value_token = string_util::trim(_token._value);
        int32_t int_value;
        if (string_util::is_int(value_token, int_value)) {
            return int_value;
        }

        // TODO(dymk) - is there a situation where we want to round a float
        // to an int, rather than throwing?
        float float_value;
        if (string_util::is_float(value_token, float_value)) {
            return lroundf(float_value);
        }

        parseError("Expected an integer value");
        return 0;
    }

    uint32_t Parser::uintValue() const {
        auto     token = string_util::trim(_token._value);
        uint32_t uint_value;
        if (string_util::is_uint(token, uint_value)) {
            return uint_value;
        }

        float float_value;
        if (string_util::is_float(token, float_value)) {
            return lroundf(float_value);
        }

        parseError("Expected an integer value");
        return 0;
    }

    // cppcheck-suppress unusedFunction
    float Parser::floatValue() const {
        auto  token = string_util::trim(_token._value);
        float float_value;
        if (string_util::is_float(token, float_value)) {
            return float_value;
        }
        parseError("Expected a float value like 123.456");
        return NAN;
    }

    // cppcheck-suppress unusedFunction
    std::vector<speedEntry> Parser::speedEntryValue() const {
        auto str = string_util::trim(_token._value);

        std::vector<speedEntry> speed_entries;

        while (!str.empty()) {
            auto next_ws_delim = str.find(' ');
            auto entry_str     = string_util::trim(str.substr(0, next_ws_delim));
            if (next_ws_delim == std::string::npos) {
                next_ws_delim = str.length();
            } else {
                next_ws_delim += 1;
            }
            str.remove_prefix(next_ws_delim);

            speedEntry entry;
            auto       next_eq_delim = entry_str.find('=');
            auto       speed_str     = string_util::trim(entry_str.substr(0, next_eq_delim));
            if (!string_util::is_uint(speed_str, entry.speed)) {
                log_error("Bad speed number " << speed_str);
                return {};
            }
            entry_str.remove_prefix(next_eq_delim + 1);

            auto next_pct_delim = entry_str.find('%');
            auto percent_str    = string_util::trim(entry_str.substr(0, next_pct_delim));
            if (!string_util::is_float(percent_str, entry.percent)) {
                log_error("Bad speed percent " << percent_str);
                return {};
            }
            entry_str.remove_prefix(next_pct_delim + 1);

            speed_entries.push_back(entry);
        }

        if (!speed_entries.size()) {
            log_info("Using default speed map");
        }

        return speed_entries;
    }

    std::vector<float> Parser::floatArray() const {
        auto               str = string_util::trim(_token._value);
        std::vector<float> values;
        float              float_value;

        while (!str.empty()) {
            str                = string_util::trim(str);
            auto next_ws_delim = str.find(' ');
            auto entry_str     = string_util::trim(str.substr(0, next_ws_delim));

            str.remove_prefix(next_ws_delim + 1);

            if (!string_util::is_float(entry_str, float_value)) {
                log_error("Bad number " << entry_str);
                values.clear();
                break;
            }
            values.push_back(float_value);

            if (str == entry_str)
                break;
        }

        if (!values.size())
            log_info("Using default value");

        return values;
    }

    // cppcheck-suppress unusedFunction
    Pin Parser::pinValue() const {
        return Pin::create(string_util::trim(_token._value));
    }

    // cppcheck-suppress unusedFunction
    IPAddress Parser::ipValue() const {
        IPAddress ip;
        if (!ip.fromString(std::string(string_util::trim(_token._value)).c_str())) {
            parseError("Expected an IP address like 192.168.0.100");
        }
        return ip;
    }

    // cppcheck-suppress unusedFunction
    int Parser::enumValue(const EnumItem* e) const {
        auto token = string_util::trim(_token._value);
        for (; e->name; ++e) {
            if (string_util::equal_ignore_case(token, e->name)) {
                break;
            }
        }
        return e->value;  // Terminal value is default.
    }

    void Parser::uartMode(UartData& wordLength, UartParity& parity, UartStop& stopBits) const {
        const char* errstr = decodeUartMode(_token._value, wordLength, parity, stopBits);
        if (*errstr) {
            parseError(errstr);
        }
    }
}

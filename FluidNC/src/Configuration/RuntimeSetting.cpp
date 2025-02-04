// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "RuntimeSetting.h"

#include "src/Report.h"
#include "src/Protocol.h"  // send_line()
#include "src/string_util.h"

#include <cstdlib>
#include <atomic>

namespace Configuration {
    RuntimeSetting::RuntimeSetting(const char* key, const char* value, Channel& out) : newValue_(value), out_(out) {
        // Remove leading '/' if it is present
        setting_ = (*key == '/') ? key + 1 : key;
        // Also remove trailing '/' if it is present

        start_ = setting_;
        // Read fence for config. Shouldn't be necessary, but better safe than sorry.
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);
    }

    std::string RuntimeSetting::setting_prefix() {
        std::string s("$/");
        s += setting_;
        s += "=";
        return s;
    }

    void RuntimeSetting::enterSection(const char* name, Configuration::Configurable* value) {
        if (is(name) && !isHandled_) {
            auto previous = start_;

            // Figure out next node
            auto next = start_;
            for (; *next && *next != '/'; ++next) {}

            // Do we have a child?
            if (*next == '/' && next[1] != '\0') {
                ++next;
                start_ = next;

                // Handle child:
                value->group(*this);
            } else {
                if (newValue_ == nullptr) {
                    log_stream(out_, "/" << setting_ << ":");
                    Configuration::Generator generator(out_, 1);
                    value->group(generator);
                    isHandled_ = true;
                } else {
                    log_error("Can't set a value on a section");
                }
            }

            // Restore situation:
            start_ = previous;
        }
    }

    void RuntimeSetting::item(const char* name, bool& value) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                log_stream(out_, setting_prefix() << (value ? "true" : "false"));
            } else {
                value = (!strcasecmp(newValue_, "true") || !strcasecmp(newValue_, "yes") || !strcasecmp(newValue_, "1"));
            }
        }
    }

    void RuntimeSetting::item(const char* name, int32_t& value, const int32_t minValue, const int32_t maxValue) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                log_stream(out_, setting_prefix() << value);
            } else {
                value = atoi(newValue_);
            }
        }
    }

    void RuntimeSetting::item(const char* name, uint32_t& value, const uint32_t minValue, const uint32_t maxValue) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                out_ << "$/" << setting_ << "=" << value << '\n';
            } else {
                if (newValue_[0] == '-') {  // constrain negative values to 0
                    value = 0;
                    log_warn("Negative value not allowed");
                } else
                    value = atoll(newValue_);
            }
            constrain_with_message(value, minValue, maxValue);
        }
    }

    void RuntimeSetting::item(const char* name, float& value, const float minValue, const float maxValue) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                // XXX precision
                log_stream(out_, setting_prefix() << value);
            } else {
                char* floatEnd;
                value = strtof(newValue_, &floatEnd);
            }
        }
    }

    void RuntimeSetting::item(const char* name, UartData& wordLength, UartParity& parity, UartStop& stopBits) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                log_stream(out_, setting_prefix() << encodeUartMode(wordLength, parity, stopBits));
            } else {
                const char* errstr = decodeUartMode(newValue_, wordLength, parity, stopBits);
                if (*errstr) {
                    log_error_to(out_, errstr);
                }
            }
        }
    }

    void RuntimeSetting::item(const char* name, std::string& value, const int minLength, const int maxLength) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                log_stream(out_, setting_prefix() << value);
            } else {
                value = newValue_;
            }
        }
    }

    void RuntimeSetting::item(const char* name, int& value, const EnumItem* e) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                for (auto e2 = e; e2->name; ++e2) {
                    if (e2->value == value) {
                        log_stream(out_, setting_prefix() << e2->name);
                        return;
                    }
                }

            } else {
                if (isdigit(newValue_[0])) {  // if the first char is a number. assume it is an index of a webui enum list
                    int indexVal = 0;
                    indexVal     = atoi(newValue_);
                    for (auto e2 = e; e2->name; ++e2) {
                        if (e2->value == indexVal) {
                            value     = e2->value;
                            newValue_ = e2->name;
                            return;
                        }
                    }
                }
                for (auto e2 = e; e2->name; ++e2) {
                    if (!strcasecmp(newValue_, e2->name)) {
                        value = e2->value;
                        return;
                    }
                }

                if (strlen(newValue_) == 0) {
                    value = e->value;
                    return;
                } else {
                    Assert(false, "Provided enum value %s is not valid", newValue_);
                }
            }
        }
    }

    void RuntimeSetting::item(const char* name, std::vector<speedEntry>& value) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                if (value.size() == 0) {
                    log_string(out_, "None");
                } else {
                    LogStream msg(out_, "");
                    msg << setting_prefix();
                    const char* separator = "";
                    for (speedEntry n : value) {
                        msg << separator << n.speed << "=" << setprecision(2) << n.percent << "%";
                        separator = " ";
                    }
                    // The destructor sends the line when msg goes out of scope
                }
            } else {
                // It is distasteful to have this code that essentially duplicates
                // Parser.cpp speedEntryValue(), albeit using std::string instead of
                // StringRange.  It might be better to have a single std::string version,
                // then pass it StringRange.str()
                std::string             newStr(newValue_);
                std::vector<speedEntry> smValue;
                while (newStr.length()) {
                    speedEntry  entry;
                    std::string entryStr;
                    auto        i = newStr.find(' ');
                    if (i != std::string::npos) {
                        entryStr = newStr.substr(0, i);
                        newStr   = newStr.substr(i + 1);
                    } else {
                        entryStr = newStr;
                        newStr   = "";
                    }
                    std::string speed;
                    i = entryStr.find('=');
                    Assert(i != std::string::npos, "Bad speed map entry");
                    entry.speed   = ::atoi(entryStr.substr(0, i).c_str());
                    entry.percent = ::atof(entryStr.substr(i + 1).c_str());
                    smValue.push_back(entry);
                }
                value = smValue;
            }
        }
    }

    void RuntimeSetting::item(const char* name, std::vector<float>& value) {
        if (is(name)) {
            LogStream msg(out_, "");
            isHandled_ = true;
            if (newValue_ == nullptr) {
                if (value.size() == 0) {
                    out_ << "None";
                } else {
                    String separator = "";
                    for (float n : value) {
                        out_ << separator.c_str();
                        out_ << n;
                        separator = " ";
                    }
                }
                msg << '\n';
            } else {
                // It is distasteful to have this code that essentially duplicates
                // Parser.cpp speedEntryValue(), albeit using String instead of
                // StringRange.  It would be better to have a single String version,
                // then pass it StringRange.str()
                auto               newStr = String(newValue_);
                std::vector<float> smValue;
                while (newStr.trim(), newStr.length()) {
                    float  entry;
                    String entryStr;
                    auto   i = newStr.indexOf(' ');
                    if (i >= 0) {
                        entryStr = newStr.substring(0, i);
                        newStr   = newStr.substring(i + 1);
                    } else {
                        entryStr = newStr;
                        newStr   = "";
                    }
                    char* floatEnd;
                    entry = float(strtod(entryStr.c_str(), &floatEnd));
                    Assert(entryStr.length() == (floatEnd - entryStr.c_str()), "Bad float value");

                    smValue.push_back(entry);
                }
                value = smValue;

                if (!value.size())
                    log_info("Using default value");
                return;
            }
        }
    }

    void RuntimeSetting::item(const char* name, IPAddress& value) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                log_stream(out_, setting_prefix() << IP_string(value));
            } else {
                IPAddress ip;
                if (!ip.fromString(newValue_)) {
                    Assert(false, "Expected an IP address like 192.168.0.100");
                }
                value = ip;
            }
        }
    }

    void RuntimeSetting::item(const char* name, Pin& value) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                log_stream(out_, setting_prefix() << value.name());
            } else {
                log_string(out_, "Runtime setting of Pin objects is not supported");
                // auto parsed = Pin::create(newValue);
                // value.swap(parsed);
            }
        }
    }

    void RuntimeSetting::item(const char* name, Macro& value) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                log_stream(out_, setting_prefix() << value.get());
            } else {
                value.set(newValue_);
            }
        }
    }

    RuntimeSetting::~RuntimeSetting() {
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);  // Write fence for config
    }
}

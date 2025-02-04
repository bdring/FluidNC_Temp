// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "HandlerBase.h"
#include "Configurable.h"

namespace Configuration {
    class RuntimeSetting : public Configuration::HandlerBase {
    private:
        const char* setting_;  // foo/bar
        const char* start_;

        const char* newValue_;  // null (read) or 123 (value)

        Channel& out_;

        bool is(const char* name) const {
            if (start_ != nullptr) {
                auto len    = strlen(name);
                auto result = !strncasecmp(name, start_, len) && (start_[len] == '\0' || start_[len] == '/');
                return result;
            } else {
                return false;
            }
        }

    protected:
        void enterSection(const char* name, Configuration::Configurable* value) override;
        bool matchesUninitialized(const char* name) override { return false; }

    public:
        RuntimeSetting(const char* key, const char* value, Channel& out);

        void item(const char* name, bool& value) override;
        void item(const char* name, int32_t& value, const int32_t minValue, const int32_t maxValue) override;
        void item(const char* name, uint32_t& value, const uint32_t minValue, const uint32_t maxValue) override;
        void item(const char* name, float& value, const float minValue, const float maxValue) override;
        void item(const char* name, std::vector<speedEntry>& value) override;
        void item(const char* name, std::vector<float>& value) override;
        void item(const char* name, UartData& wordLength, UartParity& parity, UartStop& stopBits) override;
        void item(const char* name, std::string& value, const int minLength, const int maxLength) override;
        void item(const char* name, Pin& value) override;
        void item(const char* name, Macro& value) override;
        void item(const char* name, IPAddress& value) override;
        void item(const char* name, int& value, const EnumItem* e) override;

        std::string setting_prefix();

        HandlerType handlerType() override { return HandlerType::Runtime; }

        bool isHandled_ = false;

        virtual ~RuntimeSetting();
    };
}

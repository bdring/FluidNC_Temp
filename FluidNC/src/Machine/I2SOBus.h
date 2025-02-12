// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Configuration/Configurable.h"

namespace Machine {
    class I2SOBus : public Configuration::Configurable {
    public:
        I2SOBus() = default;

        Pin _bck;
        Pin _data;
        Pin _ws;

        int _min_pulse_us = 2;

        void validate() override;
        void group(Configuration::HandlerBase& handler) override;

        void init();

        ~I2SOBus() = default;
    };
}

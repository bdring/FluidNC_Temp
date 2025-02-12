// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "I2SOBus.h"
#include "Driver/i2s_out.h"  // i2s_out_init()

namespace Machine {
    const EnumItem pulseUsValues[] = { { 1, "1" }, { 2, "2" }, { 4, "4" }, EnumItem(2) };

    void I2SOBus::validate() {
        Assert(_min_pulse_us == 1 || _min_pulse_us == 2 || _min_pulse_us == 4, "min_pulse_us must be 1, 2 or 4");
        if (_bck.defined() || _data.defined() || _ws.defined()) {
            Assert(_bck.defined(), "I2SO BCK pin should be configured once");
            Assert(_data.defined(), "I2SO Data pin should be configured once");
            Assert(_ws.defined(), "I2SO WS pin should be configured once");
        }
    }

    void I2SOBus::group(Configuration::HandlerBase& handler) {
        handler.item("bck_pin", _bck);
        handler.item("data_pin", _data);
        handler.item("ws_pin", _ws);
        handler.item("min_pulse_us", _min_pulse_us, pulseUsValues);
    }

    void I2SOBus::init() {
        log_info("I2SO BCK:" << _bck.name() << " WS:" << _ws.name() << " DATA:" << _data.name() << "Min Pulse:" << _min_pulse_us << "us");

        // Check capabilities:
        if (!_ws.capabilities().has(Pin::Capabilities::Output | Pin::Capabilities::Native)) {
            log_info("Not setting up I2SO: WS pin has incorrect capabilities");
            return;
        } else if (!_bck.capabilities().has(Pin::Capabilities::Output | Pin::Capabilities::Native)) {
            log_info("Not setting up I2SO: BCK pin has incorrect capabilities");
            return;
        } else if (!_data.capabilities().has(Pin::Capabilities::Output | Pin::Capabilities::Native)) {
            log_info("Not setting up I2SO: DATA pin has incorrect capabilities");
            return;
        } else {
            i2s_out_init_t params;
            params.ws_pin   = _ws.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
            params.bck_pin  = _bck.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
            params.data_pin = _data.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
            params.init_val = 0;

            params.min_pulse_us = _min_pulse_us;

            i2s_out_init(&params);
        }
    }
}

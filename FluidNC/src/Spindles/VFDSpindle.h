// Copyright (c) 2020 -	Bart Dring
// Copyright (c) 2020 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Spindle.h"
#include "../Types.h"

#include "../Uart.h"

namespace Spindles {
    extern Uart _uart;

    namespace VFD {
        // VFDProtocol resides in a separate class because it doesn't need to be in IRAM. This contains all the
        // VFD specific code, which is called from a separate task.
        class VFDProtocol;
    }

    // VFD base class. Called by the stepper engine. Normally you don't want to touch this.
    class VFDSpindle : public Spindle {
    private:
        friend class Spindles::VFD::VFDProtocol;

        VFD::VFDProtocol* detail_ = nullptr;

        int32_t  _current_dev_speed   = -1;
        uint32_t _last_speed          = 0;
        Percent  _last_override_value = 100;  // no override is 100 percent

        void set_mode(SpindleState mode, bool critical);

    protected:
        // The constructor sets these
        int      _uart_num  = -1;
        Uart*    _uart      = nullptr;
        uint8_t  _modbus_id = 1;
        uint8_t  _debug     = 0;
        uint32_t _poll_ms   = 250;
        uint32_t _retries   = 5;

        void setSpeed(uint32_t dev_speed);

        volatile bool _syncing;

    public:
        VFDSpindle(const char* name, VFD::VFDProtocol* detail) : Spindle(name), detail_(detail) {}
        VFDSpindle(const VFDSpindle&)            = delete;
        VFDSpindle(VFDSpindle&&)                 = delete;
        VFDSpindle& operator=(const VFDSpindle&) = delete;
        VFDSpindle& operator=(VFDSpindle&&)      = delete;

        void init();
        void config_message();
        void setState(SpindleState state, SpindleSpeed speed);
        void setSpeedfromISR(uint32_t dev_speed) override;

        // volatile uint32_t _sync_dev_speed;
        uint32_t     _sync_dev_speed;
        SpindleSpeed _slop;

        // Configuration handlers:
        void validate() override;
        void afterParse() override;
        void group(Configuration::HandlerBase& handler) override;

        virtual ~VFDSpindle() {}
    };
}

// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	Experimental Plasma Spindle
*/

#include "Spindle.h"
#include "esp32-hal.h"         // millis()
#include "../MotionControl.h"  // mc_critical

namespace Spindles {
    // This is for an on/off spindle all RPMs above 0 are on
    class PlasmaSpindle : public Spindle {
    private:
        class ArcOkEventPin : public EventPin {
        private:
            bool           _value = false;
            Pin*           _pin   = nullptr;
            PlasmaSpindle* _parent;

        public:
            ArcOkEventPin(const char* legend, Pin& pin, PlasmaSpindle* parent) : EventPin(nullptr, legend), _pin(&pin), _parent(parent) {}

            void init() {
                if (_pin->undefined()) {
                    return;
                }
                _value = _pin->read();
                _pin->report(_legend);
                _pin->setAttr(Pin::Attr::Input);
                _pin->registerEvent(static_cast<EventPin*>(this));
                update(_pin->read());
            }
            void update(bool state) { _value = state; }

            // Differs from the base class version by sending the event on either edge
            void trigger(bool active) override {
                update(active);
                if (!active && _parent->_arc_on) {
                    _parent->_arc_on = false;
                    send_alarm(ExecAlarm::AbortCycle);
                }
            }

            bool get() { return _value; }
        };

    private:
        ArcOkEventPin* _arcOkEventPin;

    protected:
        // This includes all items except direction_pin.  direction_pin applies
        // to most but not all of OnOff's derived classes.  Derived classes that
        // do not support direction_pin can invoke OnOff::groupCommon() instead
        // of OnOff::group()
        void groupCommon(Configuration::HandlerBase& handler) {
            handler.item("enable_pin", _enable_pin);
            handler.item("arc_ok_pin", _arc_ok_pin);
            handler.item("arc_wait_ms", _max_arc_wait, 0, 3000);
            Spindle::group(handler);
        }

    public:
        PlasmaSpindle(const char* name) : Spindle(name) {}

        PlasmaSpindle(const PlasmaSpindle&)            = delete;
        PlasmaSpindle(PlasmaSpindle&&)                 = delete;
        PlasmaSpindle& operator=(const PlasmaSpindle&) = delete;
        PlasmaSpindle& operator=(PlasmaSpindle&&)      = delete;

        void init() override;

        void setSpeedfromISR(uint32_t dev_speed) override;
        void setState(SpindleState state, SpindleSpeed speed) override;
        void config_message() override;

        // Methods introduced by this base clase
        virtual void set_direction(bool Clockwise);
        virtual void set_enable(bool enable);

        bool wait_for_arc_ok();

        // Configuration handlers:
        void validate() override { Spindle::validate(); }

        void group(Configuration::HandlerBase& handler) override { groupCommon(handler); }

        virtual ~PlasmaSpindle() {}

    protected:
        Pin _enable_pin;
        Pin _arc_ok_pin;

        uint32_t _max_arc_wait = 1000;

        // TO DO. These are not used in the class
        // _disable_with_zero_speed forces a disable when speed is 0
        bool _disable_with_zero_speed = false;
        // _zero_speed_with_disable forces speed to 0 when disabled
        bool _zero_speed_with_disable = false;

        bool         _arc_on = false;
        bool         use_delay_settings() const override { return false; }
        virtual void set_output(uint32_t speed);
        virtual void deinit();
    };
}

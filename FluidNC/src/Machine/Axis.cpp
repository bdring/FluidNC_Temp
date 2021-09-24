#include "Axes.h"
#include "Axis.h"
#include "MachineConfig.h"  // config

#include <cstring>

namespace Machine {
    void Axis::group(Configuration::HandlerBase& handler) {
        handler.item("steps_per_mm", _stepsPerMm);
        handler.item("max_rate", _maxRate);
        handler.item("acceleration", _acceleration);
        handler.item("max_travel", _maxTravel);
        handler.item("soft_limits", _softLimits);
        handler.item("backlash", _backlash);
        handler.section("homing", _homing);

        char tmp[7];
        tmp[0] = 0;
        strcat(tmp, "motor");

        for (size_t i = 0; i < MAX_MOTORS_PER_AXIS; ++i) {
            tmp[5] = char(i + '0');
            tmp[6] = '\0';
            handler.section(tmp, _motors[i], _axis, i);
        }
    }

    void Axis::afterParse() {
        uint32_t stepRate = uint32_t(_stepsPerMm * _maxRate / 60.0);
        auto     maxRate  = config->_stepping->maxPulsesPerSec();
        Assert(stepRate <= maxRate, "Stepping rate %d steps/sec exceeds the maximum rate %d", stepRate, maxRate);
    }

    void Axis::init() {
        for (size_t i = 0; i < Axis::MAX_MOTORS_PER_AXIS; i++) {
            auto m = _motors[i];
            if (m) {
                log_info("  Motor" << i);
                m->init();
            }
        }
        if (_homing) {
            _homing->init();
            set_bitnum(Axes::homingMask, _axis);
        }

        // If dual motors and only one motor has switches, this is the configuration
        // for a POG style squaring. The switch should report as being on both axes
        if (hasDualMotor() && (motorsWithSwitches() == 1)) {
            _motors[0]->makeDualSwitches();
            _motors[1]->makeDualSwitches();
        }
    }

    // Checks if a motor matches this axis:
    bool Axis::hasMotor(const MotorDrivers::MotorDriver* const driver) const {
        for (size_t i = 0; i < MAX_MOTORS_PER_AXIS; i++) {
            auto m = _motors[i];
            if (m && m->_driver == driver) {
                return true;
            }
        }
        return false;
    }

    // Does this axis have 2 motors?
    bool Axis::hasDualMotor() { return _motors[0] && _motors[1]; }

    // How many motors have switches defined?
    int Axis::motorsWithSwitches() {
        int count = 0;
        for (size_t i = 0; i < MAX_MOTORS_PER_AXIS; i++) {
            auto m = _motors[i];
            if (m && m->hasSwitches()) {
                count++;
            }
        }
        return count;
    }

    // returns the offset between the pulloffs
    // value is positive when motor1 has a larger pulloff
    float Axis::pulloffOffset() { return hasDualMotor()? _motors[1]->_pulloff - _motors[0]->_pulloff : 0.0f; }

    Axis::~Axis() {
        for (size_t i = 0; i < MAX_MOTORS_PER_AXIS; i++) {
            if (_motors[i]) {
                delete _motors[i];
            }
        }
    }
}

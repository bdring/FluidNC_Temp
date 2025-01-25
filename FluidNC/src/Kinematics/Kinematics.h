#pragma once

// Kinematics interface.
#include "../Configuration/Configurable.h"
#include "../Configuration/GenericFactory.h"
#include "../MotionControl.h"
#include "../Planner.h"
#include "../Types.h"
#include "src/Machine/Homing.h"

// Kinematics types.
#define tangential_knife_kinematics

/*
Special types

You can add your own type of kinematics by adding 2 new files to the Kinematics folder.
my_delta.h
my_delta.cpp

Use some of the others as an example.

You will be able to add your kinematics using the config file.

*/

namespace Kinematics {
    class KinematicSystem;

    class Kinematics : public Configuration::Configurable {
    public:
        Kinematics() {}
        ~Kinematics();

        // Configuration system helpers:
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override;
        void init();

        void init_position();

        bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position);
        void motors_to_cartesian(float* cartesian, float* motors, int n_axis);
        bool transform_cartesian_to_motors(float* motors, float* cartesian);

        void constrain_jog(float* target, plan_line_data_t* pl_data, float* position);
        bool invalid_line(float* target);
        bool invalid_arc(
            float* target, plan_line_data_t* pl_data, float* position, float center[3], float radius, size_t caxes[3], bool is_clockwise_arc);

        bool canHome(AxisMask axisMask);
        bool kinematics_homing(AxisMask axisMask);
        void releaseMotors(AxisMask axisMask, MotorMask motors);
        bool limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited);

    private:
        ::Kinematics::KinematicSystem* _system = nullptr;
    };

    class KinematicSystem : public Configuration::Configurable {
        const char* _name;

    public:
        KinematicSystem(const char* name) : _name(name) {}

        KinematicSystem(const KinematicSystem&)            = delete;
        KinematicSystem(KinematicSystem&&)                 = delete;
        KinematicSystem& operator=(const KinematicSystem&) = delete;
        KinematicSystem& operator=(KinematicSystem&&)      = delete;

        // Kinematic system interface.
        virtual bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) = 0;
        virtual void init()                                                                         = 0;
        virtual void init_position() = 0;  // used to set the machine position at init

        virtual void constrain_jog(float* cartesian, plan_line_data_t* pl_data, float* position) {}
        virtual bool invalid_line(float* cartesian) { return false; }
        virtual bool invalid_arc(
            float* target, plan_line_data_t* pl_data, float* position, float center[3], float radius, size_t caxes[3], bool is_clockwise_arc) {
            return false;
        }

        virtual void motors_to_cartesian(float* cartesian, float* motors, int n_axis) = 0;

        virtual bool transform_cartesian_to_motors(float* motors, float* cartesian) = 0;

        virtual bool canHome(AxisMask axisMask) { return false; }
        virtual void releaseMotors(AxisMask axisMask, MotorMask motors) {}
        virtual bool limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited) { return false; }
        virtual bool kinematics_homing(AxisMask& axisMask) { return false; }

        // Configuration interface.
        void afterParse() override {}
        void group(Configuration::HandlerBase& handler) override {}
        void validate() override {}

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() { return _name; }

        // Virtual base classes require a virtual destructor.
        virtual ~KinematicSystem() {}
    };

    using KinematicsFactory = Configuration::GenericFactory<KinematicSystem>;
};

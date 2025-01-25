#ifdef tangential_knife_kinematics
#pragma once

/*
	TangentialKnife.h

	This implements TangentialKnife Kinematics

    Author: Jérôme Parent
    jejmule@github
    jejmule@discord

*/

// Kinematics interface.
#include "Cartesian.h"

//tangential kinematics is derived from cartesian kinematics
namespace Kinematics {
    class TangentialKnife : public Cartesian {
    public:
        TangentialKnife() = default;

        TangentialKnife(const TangentialKnife&) = delete;
        TangentialKnife(TangentialKnife&&)      = delete;
        TangentialKnife& operator=(const TangentialKnife&) = delete;
        TangentialKnife& operator=(TangentialKnife&&) = delete;

        // Kinematic Interface
        void         init() override;
        bool         cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) override;

        // Configuration handlers:
        void         group(Configuration::HandlerBase& handler) override;

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "TangentialKnife"; }

        ~TangentialKnife() {}

    private:

        //Functions
        //Rotate knife outside of material
        bool lift_up_knife(float* position, plan_line_data_t* pl_data);
        bool lift_down_knife(float* position, plan_line_data_t* pl_data);
        bool rotate_knife(float* position, plan_line_data_t* pl_data, float angle);
        //Variables
        //The previous target position
        float _previous_target[MAX_N_AXIS] = { 0 };
        bool _previous_target_valid        = false;

        // Parameters for tangential knife kinematics
        //Tangential knife safe angle threshold
        //The angle between two movements that will trigger a Z-axis lift when exceeded, in order to rotate the knife safely.
        //If the angle is lower, the knife is rotated during motion, without lifting it up.
        float _tan_knife_safe_angle = 3;
        //Tangential knife blend angle
        //When angle between subsequent motion segments is less than this value, the knife is not rotated before the angle but during motion.The motion segments also have to be shorter than the tangential knife blend distance.
        float _tan_knife_blend_angle = 1;
        //Tangential knife blend distance
        //When angle between subsequent motion segments is less than tangential knife blend angle and the motion segments are shorter than this value, the knife is not rotated before the angle but during motion.
        float _tan_knife_blend_distance = 50;
        //Tangential knife lift up distance
        //The distance the knife is lifted up when the angle between two movements exceeds the tangential knife safe angle threshold.
        float _tan_knife_lift_up_distance = 5;
        //Tangential knife Z axis is pneumatic
        //If true, the Z axis is a pneumatic axis. Z axis height will be set to tangential knife cutting height durint G1 G2 and G3 move
        bool _tan_knife_z_axis_is_pneumatic = true;
        //Tangential knife cutting height
        //The height of the tangential knife when cutting
        float _tan_knife_cutting_height = -1;

    };
}  //  namespace Kinematics

#endif
#ifdef tangential_knife_kinematics

/*
	TangentialKnife.cpp

	This implements TangentialKnife Kinematics

    Author: Jérôme Parent
    jejmule@github
    jejmule@discord

*/

#include "TangentialKnife.h"
#include "../Machine/MachineConfig.h"


namespace Kinematics {
    void TangentialKnife::group(Configuration::HandlerBase& handler) {
        handler.item("tan_knife_safe_angle_deg", _tan_knife_safe_angle);
        handler.item("tan_knife_blend_angle_deg", _tan_knife_blend_angle);
        handler.item("tan_knife_blend_distance_mm", _tan_knife_blend_distance);
        handler.item("tan_knife_lift_distance_mm", _tan_knife_lift_up_distance);
        handler.item("tan_knife_z_axis_is_pneumatic", _tan_knife_z_axis_is_pneumatic);
        handler.item("tan_knife_cutting_height_mm", _tan_knife_cutting_height);
    }

    void TangentialKnife::init() {
        log_info("Kinematic system: " << name());
        // Call parent init
        Cartesian::init();
    }

    //This function is called by the planner to calculate the motor positions for a given cartesian target position
    //For tangential knife the GCODE contains XY coordinates for pneumatic machine and XYZ coordinates for machine with a motorized Z axis
    //In both case the C axis position is computed by the kinematics
    //For pneumatic machine the Z axis is set to the tangential knife cutting height when move is not a rapid move and to safe height when move is a rapid move
    bool TangentialKnife::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        //Check if previous target is not valid
        if(!_previous_target_valid) {
            //If previous target is not valid set previous target to current position
            memcpy(_previous_target, position, sizeof(float) * MAX_N_AXIS);
            _previous_target_valid = true;
        }

        //Define target height for pneumatic machine and move the knife according to the move type
        if(_tan_knife_z_axis_is_pneumatic) {
            //If move is a rapid move set Z axis to safe height
            if(pl_data->motion.rapidMotion) {
                target[Z_AXIS] = _tan_knife_lift_up_distance;
                lift_up_knife(position,pl_data);
            }
            //If move is not a rapid move set Z axis to tangential knife cutting height
            else {
                target[Z_AXIS] = _tan_knife_cutting_height;
                lift_down_knife(position,pl_data);
            }
        }

        
        //compute target_angle between target and position in XY plane
        float target_angle = atan2(target[1] - position[1], target[0] - position[0]) * 180 / M_PI;
        //compute knife_angle between position and previous_target in XY plane
        float knife_angle = atan2(position[1] - previous_target[1], position[0] - previous_target[0]) * 180 / M_PI;
        //compute target distance to position in XY plane
        float distance = vector_distance(target, position, 2);
        //compare target_angle with knife_angle modulo 360°
        float angle_diff = fmod(target_angle - knife_angle, 360);
        
        //If angle is greater than the safe angle threshold, lift up, rotate and lift down the knife
        if (abs(angle_diff) > _tan_knife_safe_angle) {
            if(!lift_up_knife(position,pl_data)) {
                return false;
            }
            if(!rotate_knife(position,pl_data,target_angle)) {
                return false;
            }
            if(!lift_down_knife(position,pl_data)) {
                return false;
            }
            //move the knife to the target position
            target[C_AXIS] = target_angle;
            return mc_move_motors(target, pl_data);
        }
        //Turn the knife in the material while moving, if the angle between two movements is less than the blend angle and the distance is less than the blend distance
        else if (abs(angle_diff) <= _tan_knife_blend_angle && distance <= _tan_knife_blend_distance) {
            //rotate the knife during motion
            target[C_AXIS] = target_angle;
            return mc_move_motors(target, pl_data);
        }
        //rotate the knife in the material before the movement
        else {
            if(!rotate_knife(position,pl_data,target_angle)) {
                return false;
            }
            //move the knife to the target position
            target[C_AXIS] = target_angle;
            return mc_move_motors(target, pl_data);
        }
    }

    //lift_up the knife
    bool TangentialKnife::lift_up_knife(float* position, plan_line_data_t* pl_data){
        //check if knife is already lifted up
        if(position[Z_AXIS] == _tan_knife_lift_up_distance){
            return true;
        }
        //else lift up knife
        else {
            position[Z_AXIS] = _tan_knife_lift_up_distance;
            return mc_move_motors(position, pl_data);
        }
    }
    //lift_down the knife
    bool TangentialKnife::lift_down_knife(float* position, plan_line_data_t* pl_data){
        //check if knife is already lifted down
        if(position[Z_AXIS] == _tan_knife_cutting_height){
            return true;
        }
        //else lift down knife
        else {
            position[Z_AXIS] = _tan_knife_cutting_height;
            return mc_move_motors(position, pl_data);
        }
    }
    //rotate the knife
    bool TangentialKnife::rotate_knife(float* position, plan_line_data_t* pl_data, float knife_angle){
        //check if knife is already rotated
        if(position[C_AXIS] == knife_angle){
            return true;
        }
        //else rotate knife
        else {
            position[C_AXIS] = knife_angle;
            return mc_move_motors(position, pl_data);
        }
    }

     // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<TangentialKnife> registration("TangentialKnife");
    }
}
#endif
/**
 * Copyright (C) 2024 Simon D. Levy
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>

#include <webots/camera.h>
#include <webots/gps.h>
#include <webots/gyro.h>
#include <webots/inertial_unit.h>
#include <webots/motor.h>
#include <webots/robot.h>

#include <datatypes.h>
#include <num.hpp>

#include <closedloops/pitchroll_angle.hpp>
#include <closedloops/pitchroll_rate.hpp>
#include <closedloops/position.hpp>
#include <closedloops/yaw_angle.hpp>
#include <closedloops/yaw_rate.hpp>

#include "sticks.hpp"

static const float THRUST_BASE  = 48.0;
static const float THRUST_SCALE = 0.25;
static const float THRUST_MIN   = 0.0;
static const float THRUST_MAX   = 60;
 
static float _pitchRollScale;
static float _yawScale;

static PitchRollAngleController _pitchRollAngleController;
static PitchRollRateController _pitchRollRateController;
static PositionController _positionController;
static YawAngleController _yawAngleController;
static YawRateController _yawRateController;


static WbDeviceTag m1_motor;
static WbDeviceTag m2_motor;
static WbDeviceTag m3_motor;
static WbDeviceTag m4_motor;

static float m1;
static float m2;
static float m3;
static float m4;

static void initClosedLoopControllers(const Clock::rate_t pidUpdateRate) 
{
    _pitchRollAngleController.init(pidUpdateRate);
    _pitchRollRateController.init(pidUpdateRate);
    _yawAngleController.init(pidUpdateRate);
    _yawRateController.init(pidUpdateRate);
    _positionController.init(pidUpdateRate);
}

void resetControllers(void)
{
    _pitchRollAngleController.resetPids();
    _pitchRollRateController.resetPids();
    _positionController.resetPids();

    _positionController.resetFilters();
}

static float constrain(float val, float min, float max)
{
    return val < min ? min : val > max ? max : val;
}

static float rescale(
        const float value,
        const float oldmin, 
        const float oldmax, 
        const float newmin, 
        const float newmax) 
{
    return (value - oldmin) / (oldmax - oldmin) * 
        (newmax - newmin) + newmin;
}


static float altitudePid(const float desired, const float measured)
{
    static const float KP = 2;
    static const float KI = 0.5;
    static const float DT = 0.01;

    static const float INTEGRATION_LIMIT = 5000;

    static float _integ;     

    auto error = desired - measured;

    _integ = constrain(_integ + error * DT, 
            -INTEGRATION_LIMIT, INTEGRATION_LIMIT);

    return KP * error + KI * _integ;
}


static float climbRatePid(const float desired, const float measured)
{
    static const float KP = 25;
    static const float KI = 15;
    static const float DT = 0.01;

    static const float INTEGRATION_LIMIT = 5000;

    static float _integ;     

    auto error = desired - measured;

    _integ = constrain(_integ + error * DT, 
            -INTEGRATION_LIMIT, INTEGRATION_LIMIT);

    return KP * error + KI * _integ;
}

static float runAltitudeHold(const float z, const float dz, const float thrust)
{
    // In hover mode, thrust demand comes in as [-1,+1], so
    // we convert it to a target altitude in meters
    const auto sthrust = rescale(thrust, -1, +1, 0.2, 2.0);

    // Set climb rate based on target altitude
    auto climbRate = altitudePid(sthrust, z);

    // Set thrust for desired climb rate
    return climbRatePid(climbRate, dz);
}

static float altitudeHold(
        const bool inHoverMode,
        const float thrust,
        const float z, 
        const float dz) 
{
    return inHoverMode ? runAltitudeHold(z, dz, thrust) : thrust;
}

//////////////////////////////////////////////////////////////////////////////

// Shared with Haskell Copilot

vehicleState_t state;

demands_t demands;

bool in_hover_mode;

extern "C" {

    void step(void);

    void runMotors(float m1, float m2, float m3, float m4)
    {
        // Set simulated motor values
        wb_motor_set_velocity(m1_motor, +m1);
        wb_motor_set_velocity(m2_motor, -m2);
        wb_motor_set_velocity(m3_motor, +m3);
        wb_motor_set_velocity(m4_motor, -m4);
    }

    void setDemands(const float t, const float r, const float p, const float y)
    {
        auto m1 = t - r + p  + y;
        auto m2 = t - r - p  - y;
        auto m3 = t + r - p  + y;
        auto m4 = t + r + p  - y;

        runMotors(m1, m2, m3, m4);
    }
}

//////////////////////////////////////////////////////////////////////////////

static float fconstrain(const float val, const float lo, const float hi)
{
    return val < lo ? lo : val > hi ? hi : val;
}

/*
   static float rad2deg(const float rad)
   {
   return rad * 180 / M_PI;
   }*/

static WbDeviceTag makeMotor(const char * name, const float direction)
{
    auto motor = wb_robot_get_device(name);

    wb_motor_set_position(motor, INFINITY);
    wb_motor_set_velocity(motor, direction);

    return motor;
}

static void getVehicleState(
        WbDeviceTag & gyro, 
        WbDeviceTag & imu, 
        WbDeviceTag & gps)
{
    // Track previous time and position for calculating motion
    static float tprev;
    static float xprev;
    static float yprev;
    static float zprev;

    const auto tcurr = wb_robot_get_time();
    const auto dt =  tcurr - tprev;
    tprev = tcurr;

    // Get state values (meters, degrees) from ground truth:
    //   x: positive forward
    //   y: positive leftward
    //   z: positive upward
    //   phi, dphi: positive roll right
    //   theta,dtheta: positive nose up (requires negating imu, gyro)
    //   psi,dpsi: positive nose left
    state.x = wb_gps_get_values(gps)[0];
    state.y = wb_gps_get_values(gps)[1];
    state.z = wb_gps_get_values(gps)[2];
    state.phi =     rad2deg(wb_inertial_unit_get_roll_pitch_yaw(imu)[0]);
    state.dphi =    rad2deg(wb_gyro_get_values(gyro)[0]);
    state.theta =  -rad2deg(wb_inertial_unit_get_roll_pitch_yaw(imu)[1]);
    state.dtheta = -rad2deg(wb_gyro_get_values(gyro)[1]); 
    state.psi =     rad2deg(wb_inertial_unit_get_roll_pitch_yaw(imu)[2]);
    state.dpsi =    rad2deg(wb_gyro_get_values(gyro)[2]);

    // Use temporal first difference to get world-cooredinate velocities
    state.dx = (state.x - xprev) / dt;
    state.dy = (state.y - yprev) / dt;
    state.dz = (state.z - zprev) / dt;

    // Save past time and position for next time step
    xprev = state.x;
    yprev = state.y;
    zprev = state.z;
}

static WbDeviceTag makeSensor(
        const char * name, 
        const uint32_t timestep,
        void (*f)(WbDeviceTag tag, int sampling_period))
{
    auto sensor = wb_robot_get_device(name);
    f(sensor, timestep);
    return sensor;
}

int main(int argc, char ** argv)
{
    static const Clock::rate_t PID_UPDATE_RATE = Clock::RATE_100_HZ;
    static const float PITCH_ROLL_SCALE = 1e-4;
    static const float YAW_SCALE = 4e-5;

    wb_robot_init();

    const int timestep = (int)wb_robot_get_basic_time_step();

    // Initialize motors
    m1_motor = makeMotor("m1_motor", +1);
    m2_motor = makeMotor("m2_motor", -1);
    m3_motor = makeMotor("m3_motor", +1);
    m4_motor = makeMotor("m4_motor", -1);

    // Initialize sensors
    auto imu = makeSensor("inertial_unit", timestep, wb_inertial_unit_enable);
    auto gyro = makeSensor("gyro", timestep, wb_gyro_enable);
    auto gps = makeSensor("gps", timestep, wb_gps_enable);
    auto camera = makeSensor("camera", timestep, wb_camera_enable);

    sticksInit();

    _pitchRollScale = PITCH_ROLL_SCALE;
    _yawScale = YAW_SCALE;

    initClosedLoopControllers(PID_UPDATE_RATE);

    while (wb_robot_step(timestep) != -1) {

        // Get open-loop demands from input device (keyboard, joystick, etc.)
        sticksRead(demands);

        // Reset closed-loop controllers on zero thrust
        if (demands.thrust == 0) {

            demands.roll = 0;
            demands.pitch = 0;
            demands.yaw = 0;

            resetControllers();
        }

        // Check where we're in hover mode (button press on game controller)
        in_hover_mode = sticksInHoverMode();

        // Altitude target, normalized to [-1,+1]
        static float _altitudeTarget;

        // Get vehicle state from sensors
        getVehicleState(gyro, imu, gps);

        // Hover mode: integrate stick demand
        if (in_hover_mode) {
            const float DT = .01;
            _altitudeTarget = fconstrain(_altitudeTarget + demands.thrust * DT, -1, +1);
            demands.thrust = _altitudeTarget;
        }

        // Non-hover mode: use raw stick value with min 0
        else {
            demands.thrust = fconstrain(demands.thrust, 0, 1);
            _altitudeTarget = 0;
        }

        if (in_hover_mode) {

            // Position controller converts meters per second to
            // degrees
            _positionController.run(state, demands); 

        }

        else {

            // In non-hover mode, pitch/roll demands come in as
            // [-1,+1], which we convert to degrees for input to
            // pitch/roll controller
            demands.roll *= 30;
            demands.pitch *= 30;
        }

        _pitchRollAngleController.run(state, demands);

        _pitchRollRateController.run(state, demands);

        _yawAngleController.run(state, demands);

        _yawRateController.run(state, demands);

        // Scale yaw, pitch and roll demands for mixer
        demands.yaw *= _yawScale;
        demands.roll *= _pitchRollScale;
        demands.pitch *= _pitchRollScale;

        demands.thrust = 
            altitudeHold(in_hover_mode, demands.thrust, state.z, state.dz);

        demands.thrust = demands.thrust * (in_hover_mode ? 1 : THRUST_MAX);

        demands.thrust = constrain(demands.thrust * THRUST_SCALE + THRUST_BASE,
                THRUST_MIN THRUST_MAX);

        // Call Haskell Copilot
        step();
    }

    wb_robot_cleanup();

    return 0;
}
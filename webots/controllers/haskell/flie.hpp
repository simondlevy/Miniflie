/**
 * Miniflie class for real and simulated flight controllers.
 *
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

#pragma once

#include <math.h>

#include <clock.hpp>
#include <datatypes.h>
#include <num.hpp>

#include <closedloops/pitchroll_angle.hpp>
#include <closedloops/pitchroll_rate.hpp>
#include <closedloops/position.hpp>
#include <closedloops/yaw_angle.hpp>
#include <closedloops/yaw_rate.hpp>

#include "tude.hpp"

class Miniflie {

    public:

        static const uint8_t MAX_MOTOR_COUNT = 20; // whatevs

        void init(
                const Clock::rate_t pidUpdateRate,
                const float thrustScale,
                const float thrustBase,
                const float thrustMin,
                const float thrustMax)
        {
            init(
                    pidUpdateRate, 
                    thrustScale, 
                    thrustBase, 
                    thrustMin, 
                    thrustMax, 
                    1, 
                    1);
         }

        void init(
                const Clock::rate_t pidUpdateRate,
                const float thrustScale,
                const float thrustBase,
                const float thrustMin,
                const float thrustMax,
                const float pitchRollScale,
                const float yawScale)
        {

            _thrustScale = thrustScale;
            _thrustBase = thrustBase;
            _thrustMin = thrustMin;
            _thrustMax = thrustMax;
            _pitchRollScale = pitchRollScale;
            _yawScale = yawScale;

            initClosedLoopControllers(pidUpdateRate);
        }

        void step(
                const bool inHoverMode,
                const vehicleState_t & vehicleState,
                demands_t & demands)
        {
            if (inHoverMode) {

                // Position controller converts meters per second to
                // degrees
                _positionController.run(vehicleState, demands); 

                // In hover mode, thrust demand comes in as [-1,+1], so
                // we convert it to a target altitude in meters
                demands.thrust = Num::rescale(demands.thrust, -1, +1, 0.2, 2.0);
                demands.thrust = runAltitudeController(
                        vehicleState.z, vehicleState.dz, demands.thrust); 
            }

            else {

                // In non-hover mode, pitch/roll demands come in as
                // [-1,+1], which we convert to degrees for input to
                // pitch/roll controller
                demands.roll *= 30;
                demands.pitch *= 30;
            }

            _pitchRollAngleController.run(vehicleState, demands);

            _pitchRollRateController.run(vehicleState, demands);

            _yawAngleController.run(vehicleState, demands);

            _yawRateController.run(vehicleState, demands);

            // Reset closed-loop controllers on zero thrust
            if (demands.thrust == 0) {

                demands.roll = 0;
                demands.pitch = 0;
                demands.yaw = 0;

                resetControllers();
            }

            // Scale yaw, pitch and roll demands for mixer
            demands.yaw *= _yawScale;
            demands.roll *= _pitchRollScale;
            demands.pitch *= _pitchRollScale;
        }

        void resetControllers(void)
        {
            _pitchRollAngleController.resetPids();
            _pitchRollRateController.resetPids();
            _positionController.resetPids();

            _positionController.resetFilters();
        }

        static void gyroToVehicleState(
                const Axis3f & gyro, vehicleState_t & vehicleState)
        {
            vehicleState.dphi =    gyro.x;     
            vehicleState.dtheta = -gyro.y; // (negate for ENU)
            vehicleState.dpsi =    gyro.z; 
        }

    private:

        float _thrustScale;
        float _thrustBase;
        float _thrustMin;
        float _thrustMax;
        float _pitchRollScale;
        float _yawScale;

        PitchRollAngleController _pitchRollAngleController;
        PitchRollRateController _pitchRollRateController;
        PositionController _positionController;
        YawAngleController _yawAngleController;
        YawRateController _yawRateController;

        void initClosedLoopControllers(const Clock::rate_t pidUpdateRate) 
        {
            _pitchRollAngleController.init(pidUpdateRate);
            _pitchRollRateController.init(pidUpdateRate);
            _yawAngleController.init(pidUpdateRate);
            _yawRateController.init(pidUpdateRate);
            _positionController.init(pidUpdateRate);
        }
};

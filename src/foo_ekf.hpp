#pragma once

#include <string.h>

#include <BasicLinearAlgebra.h>

#include "math3d.h"
#include "datatypes.h"

// Quaternion used for initial orientation
static const float QW_INIT = 1;
static const float QX_INIT = 0;
static const float QY_INIT = 0;
static const float QZ_INIT = 0;

// Initial variances, uncertain of position, but know we're
// stationary and roughly flat
static const float STDEV_INITIAL_POSITION_Z = 1;
static const float STDEV_INITIAL_ATTITUDE_ROLL_PITCH = 0.01;
static const float STDEV_INITIAL_ATTITUDE_YAW = 0.01;

static const float PROC_NOISE_ATT = 0;
static const float MEAS_NOISE_GYRO = 0.1; // radians per second

static const float GRAVITY_MAGNITUDE = 9.81;

// The bounds on the covariance, these shouldn't be hit, but sometimes are... why?
static const float MAX_COVARIANCE = 100;
static const float MIN_COVARIANCE = 1e-6;

// Small number epsilon, to prevent dividing by zero
static const float EPS = 1e-6f;

// the reversion of pitch and roll to zero
static const float ROLLPITCH_ZERO_REVERSION = 0.001;

// This is slower than the IMU update rate of 1000Hz
static const uint32_t PREDICTION_RATE = 100;
static const uint32_t PREDICTION_UPDATE_INTERVAL_MS = 1000 / PREDICTION_RATE;

static const uint16_t RANGEFINDER_OUTLIER_LIMIT_MM = 5000;

// Rangefinder measurement noise model
static constexpr float RANGEFINDER_EXP_POINT_A = 2.5;
static constexpr float RANGEFINDER_EXP_STD_A = 0.0025; 
static constexpr float RANGEFINDER_EXP_POINT_B = 4.0;
static constexpr float RANGEFINDER_EXP_STD_B = 0.2;   

static constexpr float RANGEFINDER_EXP_COEFF = 
logf( RANGEFINDER_EXP_STD_B / RANGEFINDER_EXP_STD_A) / 
(RANGEFINDER_EXP_POINT_B - RANGEFINDER_EXP_POINT_A);

static const uint8_t N = 4;

extern float stream_gyro_x, stream_gyro_y, stream_gyro_z;
extern float stream_accel_x, stream_accel_y, stream_accel_z;
extern float stream_rangefinder_distance;
extern uint32_t stream_now_msec;

class Ekf { 

    public:

        static void step(/*vehicleState_t & vehicleState*/)
        {
            // Internal state ------------------------------------------------

            static bool _didInit;

            // Quaternion (angular state components)
            static float _qw, _qx, _qy, _qz;

            _qw = !_didInit ? QW_INIT : _qw;
            _qx = !_didInit ? QX_INIT : _qx;
            _qy = !_didInit ? QY_INIT : _qy;
            _qz = !_didInit ? QZ_INIT : _qz;

            _didInit = true;

            // Predict --------------------------------------------------------

            const auto isFlying = true; // XXX

            const float dt = 0.005;

            const auto gyro_sample_x = stream_gyro_x * DEGREES_TO_RADIANS;
            const auto gyro_sample_y = stream_gyro_y * DEGREES_TO_RADIANS;
            const auto gyro_sample_z = stream_gyro_z * DEGREES_TO_RADIANS;

            const auto dtwx = dt * gyro_sample_x;
            const auto dtwy = dt * gyro_sample_y;
            const auto dtwz = dt * gyro_sample_z;

            // compute the quaternion values in [w,x,y,z] order
            const auto angle = sqrt(dtwx*dtwx + dtwy*dtwy + dtwz*dtwz) + EPS;
            const auto ca = cos(angle/2);
            const auto sa = sin(angle/2);
            const auto dqw = ca;
            const auto dqx = sa*dtwx/angle;
            const auto dqy = sa*dtwy/angle;
            const auto dqz = sa*dtwz/angle;

            // rotate the quad's attitude by the delta quaternion
            // vector computed above

            const auto tmpq0 = rotateQuat(
                    dqw*_qw - dqx*_qx - dqy*_qy - dqz*_qz, QW_INIT, isFlying);

            const auto tmpq1 = rotateQuat(
                    dqx*_qw + dqw*_qx + dqz*_qy - dqy*_qz, QX_INIT, isFlying);

            const auto tmpq2 = rotateQuat(
                    dqy*_qw - dqz*_qx + dqw*_qy + dqx*_qz, QY_INIT, isFlying);

            const auto tmpq3 = rotateQuat(
                    dqz*_qw + dqy*_qx - dqx*_qy + dqw*_qz, QZ_INIT, isFlying);

            // normalize and store the result
            const auto norm = 
                sqrt(tmpq0*tmpq0 + tmpq1*tmpq1 + tmpq2*tmpq2 + tmpq3*tmpq3) + 
                EPS;

            Serial.printf("c: %f\n", tmpq0/norm);

            _qw = tmpq0/norm;
            _qx = tmpq1/norm; 
            _qy = tmpq2/norm; 
            _qz = tmpq3/norm;
        }

    private:

        static float dot(const BLA::Matrix<N> x, const BLA::Matrix<N> y) 
        {
            float d = 0;

            for (uint8_t i=0; i<N; ++i) {
                d += x(i) * y(i);
            }

            return d;
        }

        static void outer( const BLA::Matrix<N> x, const BLA::Matrix<N> y,
                BLA::Matrix<N,N> & a) 
        {
            for (uint8_t i=0; i<N; ++i) {
                for (uint8_t j=0; j<N; ++j) {
                    a(i,j) = x(i) * y(j);
                }
            }
        }


        static void initEntry(float & entry, const bool didInit, const float value=0)
        {
            entry = didInit ? entry : value;
        }

        static float rotateQuat(
                const float val, 
                const float initVal,
                const bool isFlying)
        {
            const auto keep = 1.0f - ROLLPITCH_ZERO_REVERSION;

            return (val * (isFlying ? 1: keep)) +
                (isFlying ? 0 : ROLLPITCH_ZERO_REVERSION * initVal);
        }

        static void updateCovarianceMatrix(
                const BLA::Matrix<N, N> p_in,
                BLA::Matrix<N, N> & p_out)
        {
            // Enforce symmetry of the covariance matrix, and ensure the
            // values stay bounded
            for (int i=0; i<N; i++) {

                for (int j=i; j<N; j++) {

                    const auto pval = (p_in(i,j) + p_in(j,i)) / 2;

                    p_out(i,j) = p_out(j,i) =
                        pval > MAX_COVARIANCE ?  MAX_COVARIANCE :
                        (i==j && pval < MIN_COVARIANCE) ?  MIN_COVARIANCE :
                        pval;
                }
            }
        }

        static bool isErrorLarge(const float v)
        {
            return fabs(v) > 0.1e-3f;
        }

        static bool isErrorInBounds(const float v)
        {
            return fabs(v) < 10;
        }

        static float max(const float val, const float maxval)
        {
            return val > maxval ? maxval : val;
        }

        static float square(const float val)
        {
            return val * val;
        }
};

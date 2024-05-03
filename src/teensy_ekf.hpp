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
            static uint32_t _nextPredictionMsec;
            static uint32_t _lastPredictionMsec;
            static uint32_t _lastUpdateMsec;

            // Covariance matrix entries
            static float 
                _p00, _p01, _p02, _p03, 
                _p10, _p11, _p12, _p13,
                _p20, _p21, _p22, _p23,
                _p30, _p31, _p32, _p33;

            // Quaternion (angular state components)
            static float _qw, _qx, _qy, _qz;

            // Angular state components
            static float _ang_x, _ang_y, _ang_z;

            // Linear state components
            static float _z;

            // Third row (Z) of attitude as a rotation matrix (used by
            // prediction, updated by finalization)
            static float _r20, _r21, _r22;

            // Attitude error
            static float _e0, _e1, _e2;

            // Initialize -----------------------------------------------------

            initEntry(_p00, _didInit, square(STDEV_INITIAL_POSITION_Z));
            initEntry(_p01, _didInit);
            initEntry(_p02, _didInit);
            initEntry(_p03, _didInit);

            initEntry(_p10, _didInit);
            initEntry(_p11, _didInit, square(STDEV_INITIAL_ATTITUDE_ROLL_PITCH));
            initEntry(_p12, _didInit);
            initEntry(_p13, _didInit);

            initEntry(_p20, _didInit);
            initEntry(_p21, _didInit);
            initEntry(_p22, _didInit, square(STDEV_INITIAL_ATTITUDE_ROLL_PITCH));
            initEntry(_p23, _didInit);

            initEntry(_p30, _didInit);
            initEntry(_p31, _didInit);
            initEntry(_p32, _didInit);
            initEntry(_p33, _didInit, square(STDEV_INITIAL_ATTITUDE_YAW));

            _qw = !_didInit ? QW_INIT : _qw;
            _qx = !_didInit ? QX_INIT : _qx;
            _qy = !_didInit ? QY_INIT : _qy;
            _qz = !_didInit ? QZ_INIT : _qz;

            // Set the initial rotation matrix to the identity. This only
            // affects the first prediction step, since in the finalization,
            // after shifting attitude errors into the attitude state, the
            // rotation matrix is updated.

            _r20 = !_didInit ? 0 : _r20;
            _r21 = !_didInit ? 0 : _r21;
            _r22 = !_didInit ? 1 : _r22;

            _e0 = !_didInit ? 0 : _e0;
            _e1 = !_didInit ? 0 : _e1;
            _e2 = !_didInit ? 0 : _e2;

            _didInit = true;

            // Predict --------------------------------------------------------

            const auto isFlying = true; // XXX

            const auto shouldPredict = stream_now_msec >= _nextPredictionMsec;

            const float dt = (stream_now_msec - _lastPredictionMsec) / 1000.0f;

            const auto gyro_sample_x = stream_gyro_x * DEGREES_TO_RADIANS;
            const auto gyro_sample_y = stream_gyro_y * DEGREES_TO_RADIANS;
            const auto gyro_sample_z = stream_gyro_z * DEGREES_TO_RADIANS;

            //const auto accel_sample_x = stream_accel_x * GRAVITY_MAGNITUDE;
            //const auto accel_sample_y = stream_accel_y * GRAVITY_MAGNITUDE;
            //const auto accel_sample_z = stream_accel_z * GRAVITY_MAGNITUDE;

            const auto e0 = gyro_sample_x * dt/2;
            const auto e1 = gyro_sample_y * dt/2;
            const auto e2 = gyro_sample_z * dt/2;

            const auto e0e0 =  1 - e1*e1/2 - e2*e2/2;
            const auto e0e1 =  e2 + e0*e1/2;
            const auto e0e2 = -e1 + e0*e2/2;

            const auto e1e0 = -e2 + e0*e1/2;
            const auto e1e1 =  1 - e0*e0/2 - e2*e2/2;
            const auto e1e2 =  e0 + e1*e2/2;

            const auto e2e0 =  e1 + e0*e2/2;
            const auto e2e1 = -e0 + e1*e2/2;
            const auto e2e2 = 1 - e0*e0/2 - e1*e1/2;

            // attitude update (rotate by gyroscope), we do this in quaternions
            // this is the gyroscope angular velocity integrated over
            // the sample period
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

            // ====== COVARIANCE UPDATE ======

            const BLA::Matrix<N, N> A = {
                0,    0,    0,    0,
                0, e0e0, e0e1, e0e2, 
                0, e1e0, e1e1, e1e2,
                0, e2e0, e2e1, e2e2,
            };

            const BLA::Matrix<N, N> P = { 
                _p00, _p01, _p02, _p03,
                _p10, _p11, _p12, _p13,
                _p20, _p21, _p22, _p23,
                _p30, _p31, _p32, _p33
            };

            const auto APA = A * P * ~A;

            _lastPredictionMsec = 
                _lastPredictionMsec == 0 || shouldPredict ? 
                stream_now_msec :
                _lastPredictionMsec;

            _nextPredictionMsec = _nextPredictionMsec == 0 ? 
                stream_now_msec : 
                stream_now_msec >= _nextPredictionMsec ?
                stream_now_msec + PREDICTION_UPDATE_INTERVAL_MS :
                _nextPredictionMsec;

            // Process noise is added after the return from the
            // prediction step

            // ====== PREDICTION STEP ======
            // The prediction depends on whether we're on the
            // ground, or in flight.  When flying, the
            // accelerometer directly measures thrust (hence is
            // useless to estimate body angle while flying)

            _qw = shouldPredict ? tmpq0/norm : _qw;
            _qx = shouldPredict ? tmpq1/norm : _qx; 
            _qy = shouldPredict ? tmpq2/norm : _qy; 
            _qz = shouldPredict ? tmpq3/norm : _qz;

            const auto dt1 = 
                (stream_now_msec - _lastUpdateMsec) / 1000.0f;

            const auto isDtPositive = dt1 > 0;

            const auto noise =
                isDtPositive ?  
                square(MEAS_NOISE_GYRO * dt1 + PROC_NOISE_ATT) :
                0;

            const auto p00_pred = shouldPredict ? APA(0,0) + noise : _p00;
            const auto p01_pred = shouldPredict ? APA(0,1) : _p01;
            const auto p02_pred = shouldPredict ? APA(0,2) : _p02;
            const auto p03_pred = shouldPredict ? APA(0,3) : _p03;

            const auto p10_pred = shouldPredict ? APA(1,0) : _p10;
            const auto p11_pred = shouldPredict ? APA(1,1) + noise: _p11;
            const auto p12_pred = shouldPredict ? APA(1,2) : _p12;
            const auto p13_pred = shouldPredict ? APA(1,3) : _p13;

            const auto p20_pred = shouldPredict ? APA(2,0) : _p20;
            const auto p21_pred = shouldPredict ? APA(2,1) : _p21;
            const auto p22_pred = shouldPredict ? APA(2,2) + noise : _p22;
            const auto p23_pred = shouldPredict ? APA(2,3) : _p23;

            const auto p30_pred = shouldPredict ? APA(3,0) : _p30;
            const auto p31_pred = shouldPredict ? APA(3,1) : _p31;
            const auto p32_pred = shouldPredict ? APA(3,2) : _p31;
            const auto p33_pred = shouldPredict ? APA(3,3) + noise : _p33;

            const BLA::Matrix<N, N> P_pred = { 
                p00_pred, p01_pred, p02_pred, p03_pred,
                p10_pred, p11_pred, p12_pred, p13_pred,
                p20_pred, p21_pred, p22_pred, p23_pred,
                p30_pred, p31_pred, p32_pred, p33_pred
            };

            BLA::Matrix<N, N> P3; 

            updateCovarianceMatrix(P_pred, P3);

            const auto p00_noise = isDtPositive ? P3(0,0) : p00_pred;
            const auto p01_noise = isDtPositive ? P3(0,1) : p01_pred;
            const auto p02_noise = isDtPositive ? P3(0,2) : p02_pred;
            const auto p03_noise = isDtPositive ? P3(0,3) : p03_pred;

            const auto p10_noise = isDtPositive ? P3(1,0) : p10_pred;
            const auto p11_noise = isDtPositive ? P3(1,1) : p11_pred;
            const auto p12_noise = isDtPositive ? P3(1,2) : p12_pred;
            const auto p13_noise = isDtPositive ? P3(1,3) : p13_pred;

            const auto p20_noise = isDtPositive ? P3(2,0) : p20_pred;
            const auto p21_noise = isDtPositive ? P3(2,1) : p21_pred;
            const auto p22_noise = isDtPositive ? P3(2,2) : p22_pred;
            const auto p23_noise = isDtPositive ? P3(2,3) : p23_pred;

            const auto p30_noise = isDtPositive ? P3(3,0) : p30_pred;
            const auto p31_noise = isDtPositive ? P3(3,1) : p31_pred;
            const auto p32_noise = isDtPositive ? P3(3,2) : p32_pred;
            const auto p33_noise = isDtPositive ? P3(3,3) : p33_pred;

            _lastUpdateMsec = _lastUpdateMsec == 0 || isDtPositive ?  
                stream_now_msec : 
                _lastUpdateMsec;

            // Update with range ---------------------------------------------

            const auto angle1 = max(0, 
                    fabsf(acosf(_r22)) - DEGREES_TO_RADIANS * (15.0f / 2.0f));

            const auto predictedDistance = _z / cosf(angle1);

            // mm => m
            const auto measuredDistance = stream_rangefinder_distance / 1000; 

            const auto stdMeasNoise =
                RANGEFINDER_EXP_STD_A * 
                (1 + expf(RANGEFINDER_EXP_COEFF * 
                          (measuredDistance - RANGEFINDER_EXP_POINT_A)));

            const BLA::Matrix<N> h = {1 / cosf(angle1), 0, 0, 0};

            const auto ph = P3 * h;

            const auto shouldUpdate = fabs(_r22) > 0.1f && _r22 > 0 && 
                stream_rangefinder_distance < RANGEFINDER_OUTLIER_LIMIT_MM;

            const auto r = stdMeasNoise * stdMeasNoise;

            const auto hphr = r + dot(h, ph);

            const BLA::Matrix<N> g = {
                ph(0) / hphr, 
                ph(1) / hphr, 
                ph(2) / hphr, 
                ph(3) / hphr
            }; 

            const auto error = measuredDistance - predictedDistance; 

            _z     += shouldUpdate ? g(0) * error : 0;
            _ang_x += shouldUpdate ? g(1) * error : 0;
            _ang_y += shouldUpdate ? g(2) * error : 0;
            _ang_z += shouldUpdate ? g(3) * error : 0;

            BLA::Matrix<N,N> GH;

            outer(g, h, GH);

            BLA::Matrix<N,N> I = {

                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1
            };

            GH -= I;

            /*
            auto P4 = GH * P3 * ~GH;

            for (int i=0; i<N; i++) {
                for (int j=0; j<N; j++) {
                    P4(i,j) += j < i ? 0 : r * g(i) * g(j);
                }
            }

            BLA::Matrix<N,N> P5;

            updateCovarianceMatrix(P4, P5);
            */

            // Finalize ------------------------------------------------------

            // Incorporate the attitude error (Kalman filter state) with the
            // attitude
            const auto v0 = _ang_x;
            const auto v1 = _ang_y;
            const auto v2 = _ang_z;

            const auto angle2 = sqrt(v0*v0 + v1*v1 + v2*v2) + EPS;
            const auto newca = cos(angle2 / 2.0f);
            const auto newsa = sin(angle2 / 2.0f);

            const auto newdqw = newca;
            const auto newdqx = newsa * v0 / angle2;
            const auto newdqy = newsa * v1 / angle2;
            const auto newdqz = newsa * v2 / angle2;

            // Rotate the quad's attitude by the delta quaternion vector
            // computed above
            const auto newtmpq0 = 
                newdqw * _qw - newdqx * _qx - newdqy * _qy - newdqz * _qz;
            const auto newtmpq1 = 
                newdqx * _qw + newdqw * _qx + newdqz * _qy - newdqy * _qz;
            const auto newtmpq2 = 
                newdqy * _qw - newdqz * _qx + newdqw * _qy + newdqx * _qz;
            const auto newtmpq3 = 
                newdqz * _qw + newdqy * _qx - newdqx * _qy + newdqw * _qz;

            // normalize and store the result
            const auto newnorm = 
                sqrt(newtmpq0 * newtmpq0 + newtmpq1 * newtmpq1 + 
                        newtmpq2 * newtmpq2 + newtmpq3 * newtmpq3) + EPS;

            // the attitude error vector (v0,v1,v2) is small,
            // so we use a first order approximation to e0 = tan(|v0|/2)*v0/|v0|
            const auto newe0 = v0/2; 
            const auto newe1 = v1/2; 
            const auto newe2 = v2/2;

            /** Rotate the covariance, since we've rotated the body
             *
             * This comes from a second order approximation to:
             * Sigma_post = exps(-d) Sigma_pre exps(-d)'
             *            ~ (I + [[-d]] + [[-d]]^2 / 2) 
             Sigma_pre (I + [[-d]] + [[-d]]^2 / 2)'
             * where d is the attitude error expressed as Rodriges parameters,
             * ie.  d = tan(|v|/2)*v/|v|
             * As derived in "Covariance Correction Step for Kalman Filtering
             * with an Attitude"
             * http://arc.aiaa.org/doi/abs/10.2514/1.G000848
             */

            const auto newe0e0 =  1 - newe1*newe1/2 - newe2*newe2/2;
            const auto newe0e1 =  newe2 + newe0*newe1/2;
            const auto newe0e2 = -newe1 + newe0*newe2/2;

            const auto newe1e0 =  -newe2 + newe0*newe1/2;
            const auto newe1e1 = 1 - newe0*newe0/2 - newe2*newe2/2;
            const auto newe1e2 = newe0 + newe1*newe2/2;

            const auto newe2e0 = newe1 + newe0*newe2/2;
            const auto newe2e1 = -newe0 + newe1*newe2/2;
            const auto newe2e2 = 1 - newe0*newe0/2 - newe1*newe1/2;

            const auto isErrorSufficient  = 
                (isErrorLarge(v0) || isErrorLarge(v1) || isErrorLarge(v2)) &&
                isErrorInBounds(v0) && isErrorInBounds(v1) && isErrorInBounds(v2);

            // Matrix to rotate the attitude covariances once updated
            const BLA::Matrix<N, N> newA = {
                0,       0,       0,       0,
                0, newe0e0, newe0e1, newe0e2,
                0, newe1e0, newe1e1, newe1e2,
                0, newe2e0, newe2e1, newe2e2
            };

            const BLA::Matrix<N, N> P4 = { 
                0,         0,         0,         0,
                0, p11_noise, p12_noise, p13_noise, 
                0, p21_noise, p22_noise, p23_noise, 
                0, p31_noise, p32_noise, p33_noise
            };

            const auto newAPA = newA * P4 * ~newA;

            const auto p00_final = isErrorSufficient ? newAPA(0,0) : p00_noise;
            const auto p01_final = isErrorSufficient ? newAPA(0,1) : p01_noise;
            const auto p02_final = isErrorSufficient ? newAPA(0,2) : p02_noise;
            const auto p03_final = isErrorSufficient ? newAPA(0,3) : p03_noise;

            const auto p10_final = isErrorSufficient ? newAPA(1,0) : p10_noise;
            const auto p11_final = isErrorSufficient ? newAPA(1,1) : p11_noise;
            const auto p12_final = isErrorSufficient ? newAPA(1,2) : p12_noise;
            const auto p13_final = isErrorSufficient ? newAPA(1,3) : p13_noise;

            const auto p20_final = isErrorSufficient ? newAPA(2,0) : p20_noise;
            const auto p21_final = isErrorSufficient ? newAPA(2,1) : p21_noise;
            const auto p22_final = isErrorSufficient ? newAPA(2,2) : p22_noise;
            const auto p23_final = isErrorSufficient ? newAPA(2,3) : p23_noise;

            const auto p30_final = isErrorSufficient ? newAPA(3,0) : p30_noise;
            const auto p31_final = isErrorSufficient ? newAPA(3,1) : p31_noise;
            const auto p32_final = isErrorSufficient ? newAPA(3,2) : p32_noise;
            const auto p33_final = isErrorSufficient ? newAPA(3,3) : p33_noise;

            (void)p00_final;
            (void)p01_final;
            (void)p02_final;
            (void)p03_final;
            (void)p10_final;
            (void)p20_final;
            (void)p30_final;

            const BLA::Matrix<N, N> P5 = {
                0,         0,         0,         0,
                0, p11_final, p12_final, p13_final,
                0, p21_final, p22_final, p23_final,
                0, p31_final, p32_final, p33_final,
            };

            BLA::Matrix<N, N> P6;

            updateCovarianceMatrix(P5, P6);

            _p00 = isErrorSufficient ? P6(0,0) : _p00;
            _p01 = isErrorSufficient ? P6(0,1) : _p01;
            _p02 = isErrorSufficient ? P6(0,2) : _p02;
            _p03 = isErrorSufficient ? P6(0,3) : _p03;

            _p10 = isErrorSufficient ? P6(1,0) : _p10;
            _p11 = isErrorSufficient ? P6(1,1) : _p11;
            _p12 = isErrorSufficient ? P6(1,2) : _p12;
            _p13 = isErrorSufficient ? P6(1,3) : _p13;

            _p20 = isErrorSufficient ? P6(2,0) : _p20;
            _p21 = isErrorSufficient ? P6(2,1) : _p21;
            _p22 = isErrorSufficient ? P6(2,2) : _p22;
            _p23 = isErrorSufficient ? P6(2,3) : _p23;

            _p30 = isErrorSufficient ? P6(3,0) : _p30;
            _p31 = isErrorSufficient ? P6(3,1) : _p31;
            _p32 = isErrorSufficient ? P6(3,2) : _p32;
            _p33 = isErrorSufficient ? P6(3,3) : _p33;

            _qw = isErrorSufficient ? newtmpq0 / newnorm : _qw;
            _qx = isErrorSufficient ? newtmpq1 / newnorm : _qx;
            _qy = isErrorSufficient ? newtmpq2 / newnorm : _qy;
            _qz = isErrorSufficient ? newtmpq3 / newnorm : _qz;

            // Convert the new attitude to a rotation matrix, such
            // that we can rotate body-frame velocity and acc
            _r20 = isErrorSufficient ?  
                2 * _qx * _qz - 2 * _qw * _qy : 
                _r20;

            _r21 = isErrorSufficient ? 
                2 * _qy * _qz + 2 * _qw * _qx : 
                _r21;

            _r22 = isErrorSufficient ? 
                _qw * _qw - _qx * _qx - _qy * _qy + _qz * _qz :
                _r22;

            // Reset the attitude error
            _e0 = isErrorSufficient ?  0 : _e0;
            _e1 = isErrorSufficient ?  0 : _e1;
            _e2 = isErrorSufficient ?  0 : _e2;

            // Get the vehicle state -----------------------------------------

            /*
            vehicleState.phi = 
                RADIANS_TO_DEGREES * atan2((2 * (_qy*_qz + _qw*_qx)),
                        (_qw*_qw - _qx*_qx - _qy*_qy + _qz*_qz));

            // Negate for ENU
            vehicleState.theta = -RADIANS_TO_DEGREES * asin((-2) * 
                    (_qx*_qz - _qw*_qy));

            vehicleState.psi = RADIANS_TO_DEGREES * 
                atan2((2 * (_qx*_qy + _qw*_qz)),
                        (_qw*_qw + _qx*_qx - _qy*_qy - _qz*_qz));

            // Get angular velocities directly from gyro
            vehicleState.dphi =    stream_gyro_x;
            vehicleState.dtheta = -stream_gyro_y; // negate for ENU
            vehicleState.dpsi =    stream_gyro_z;
            */
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

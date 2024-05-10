#pragma once

#include <string.h>

#define EKF_N 7

#include <clock.hpp>

class EkfImpl {

    void do_init(const float diag[EKF_N]);

    void get_prediction(
            const float dt,
            const bool didAddProcessNoise,
            const float xold[EKF_N],
            float xnew[EKF_N],
            float F[EKF_N][EKF_N]);

    bool did_finalize(float x[EKF_N], float A[EKF_N][EKF_N]);
};


class Ekf {

    // General ================================================================

    private:

        typedef struct {

            float dat[EKF_N];

        } vector_t;

        typedef struct {

            float dat[EKF_N][EKF_N];

        } matrix_t;

        matrix_t _p;

        vector_t _x;

        bool _isUpdated;

        uint32_t _lastPredictionMsec;

        uint32_t _lastProcessNoiseUpdateMsec;

        uint32_t _predictionIntervalMsec;

        void multiplyCovariance(const matrix_t & a)
        {
            matrix_t  at = {};
            transpose(a, at);  
            matrix_t ap = {};
            multiply(a, _p, ap);
            multiply(ap, at, _p);

        }

        void cleanupCovariance(void)
        {
            // Enforce symmetry of the covariance matrix, and ensure the
            // values stay bounded
            for (int i=0; i<EKF_N; i++) {

                for (int j=i; j<EKF_N; j++) {

                    const auto pval = (_p.dat[i][j] + _p.dat[j][i]) / 2;

                    _p.dat[i][j] = _p.dat[j][i] = 
                        pval > _max_covariance ?  _max_covariance :
                        (i==j && pval < _min_covariance) ?  _min_covariance :
                        pval;
                }
            }
        }

        static void makevec(const float dat[EKF_N], vector_t & x)
        {
            for (uint8_t i=0; i<EKF_N; ++i) {
                x.dat[i] = dat[i];
            }
        }

        static void makemat(const float dat[EKF_N][EKF_N], matrix_t & a)
        {
            for (uint8_t i=0; i<EKF_N; ++i) {
                for (uint8_t j=0; j<EKF_N; ++j) {
                    a.dat[i][j] = dat[i][j];
                }
            }
        }

        static void transpose(const matrix_t & a, matrix_t & at)
        {
            for (uint8_t i=0; i<EKF_N; ++i) {
                for (uint8_t j=0; j<EKF_N; ++j) {
                    auto tmp = a.dat[i][j];
                    at.dat[i][j] = a.dat[j][i];
                    at.dat[j][i] = tmp;
                }
            }
        }

        static float dot(const vector_t & x, const vector_t & y) 
        {
            float d = 0;

            for (uint8_t k=0; k<EKF_N; k++) {
                d += x.dat[k] * y.dat[k];
            }

            return d;
        }

        static float get(const matrix_t & a, const uint8_t i, const uint8_t j)
        {
            return a.dat[i][j];
        }

        static float get(const vector_t & x, const uint8_t i)
        {
            return x.dat[i];
        }

        static void set(vector_t & x, const uint8_t i, const float val)
        {
            x.dat[i] = val;
        }

        static void set(matrix_t & a, const uint8_t i, const uint8_t j, const float val)
        {
            a.dat[i][j] = val;
        }

        static float dot(
                const matrix_t & a, 
                const matrix_t & b, 
                const uint8_t i, 
                const uint8_t j)
        {
            float d = 0;

            for (uint8_t k=0; k<EKF_N; k++) {
                d += a.dat[i][k] * b.dat[k][j];
            }

            return d;
        }

        // Matrix * Matrix
        static void multiply( const matrix_t a, const matrix_t b, matrix_t & c)
        {
            for (uint8_t i=0; i<EKF_N; i++) {

                for (uint8_t j=0; j<EKF_N; j++) {

                    c.dat[i][j] = dot(a, b, i, j);
                }
            }
        }

        // Matrix * Vector
        static void multiply(const matrix_t & a, const vector_t & x, vector_t & y)
        {
            for (uint8_t i=0; i<EKF_N; i++) {
                y.dat[i] = 0;
                for (uint8_t j=0; j<EKF_N; j++) {
                    y.dat[i] += a.dat[i][j] * x.dat[j];
                }
            }
        }

        // Outer product
        static void multiply(const vector_t & x, const vector_t & y, matrix_t & a)
        {
            for (uint8_t i=0; i<EKF_N; i++) {
                for (uint8_t j=0; j<EKF_N; j++) {
                    a.dat[i][j] = x.dat[i] * y.dat[j];
                }
            }
        }

        float _min_covariance;
        float _max_covariance;

    public:

        void initialize(
                const uint32_t nowMsec,
                const uint32_t predictionIntervalMsec,
                const float min_covariance, 
                const float max_covariance)
        {
            _predictionIntervalMsec = predictionIntervalMsec;

            _lastProcessNoiseUpdateMsec = nowMsec;
            _lastPredictionMsec = nowMsec;
            _isUpdated = false;

            _min_covariance = min_covariance;
            _max_covariance = max_covariance;

            float diag[EKF_N] = {};

            do_init(diag);

            for (uint8_t i=0; i<EKF_N; ++i) {

                for (uint8_t j=0; j<EKF_N; ++j) {

                    set(_p, i, j, i==j ? diag[i] : 0);
                }

                set(_x, i, 0);
            }

        }

        void predict(const uint32_t nowMsec)
        {
            static uint32_t _nextPredictionMsec;

            _nextPredictionMsec = nowMsec > _nextPredictionMsec ?
                nowMsec + _predictionIntervalMsec :
                _nextPredictionMsec;

            if (nowMsec >= _nextPredictionMsec) {

                _isUpdated = true;

                float xnew[EKF_N] = {};

                for (uint8_t i=0; i<EKF_N; ++i) {
                    xnew[i] = get(_x, i);
                }

                float Fdat[EKF_N][EKF_N] = {};

                const auto shouldAddProcessNoise = 
                    nowMsec - _lastProcessNoiseUpdateMsec > 0;

                const float dt = (nowMsec - _lastPredictionMsec) / 1000.0f;

                get_prediction(dt, shouldAddProcessNoise, _x.dat,
                        xnew, Fdat);

                matrix_t F = {};
                makemat(Fdat, F);

                multiplyCovariance(F);

                cleanupCovariance();

                _lastPredictionMsec = nowMsec;

                if (shouldAddProcessNoise) {

                    _lastProcessNoiseUpdateMsec = nowMsec;

                    for (uint8_t i=0; i<EKF_N; ++i) {
                        set(_x, i, xnew[i]);
                    }

                }
            }
        }

        void update(
                const float hdat[EKF_N], 
                const float error, 
                const float stdMeasNoise)
        {
            vector_t h = {};
            makevec(hdat, h);

            // ====== INNOVATION COVARIANCE ======
            vector_t ph = {};
            multiply(_p, h, ph);
            const auto r = stdMeasNoise * stdMeasNoise;
            const auto hphr = r + dot(h, ph); // HPH' + R

            // Compute the Kalman gain as a column vector
            vector_t g = {};
            for (uint8_t i=0; i<EKF_N; ++i) {
                set(g, i, get(ph, i) / hphr);
            }

            // Perform the state update
            for (uint8_t i=0; i<EKF_N; ++i) {
                set(_x, i, get(_x, i) + get(g, i) * error);
            }

            matrix_t GH = {};
            multiply(g, h, GH); 

            for (int i=0; i<EKF_N; i++) { 
                set(GH, i, i, get(GH, i, i) - 1);
            }

            multiplyCovariance(GH);

            // Add the measurement variance 
            for (int i=0; i<EKF_N; i++) {
                for (int j=0; j<EKF_N; j++) {
                    _p.dat[i][j] += j < i ? 0 : r * get(g, i) * get(g, j);
                    set(_p, i, j, get(_p, i, j));
                }
            }

            cleanupCovariance();

            _isUpdated = true;
        }

        void finalize(const uint32_t nowMsec)
        {
            if (_isUpdated) {

                matrix_t  A = {};

                // Move attitude error into attitude if any of the angle errors are
                // large enough
                if (did_finalize(_x.dat, A.dat)) {

                    multiplyCovariance(A);

                    cleanupCovariance();
                }

                _isUpdated = false;
            }
        }

        // Crazyflie ==============================================================

    private:

        typedef struct {

            float w;
            float x;
            float y;
            float z;

        } new_quat_t;

        typedef struct {

            axis3_t sum;
            uint32_t count;

        } imu_t;

        axis3_t _gyroLatest;

        new_quat_t _quat;

        uint32_t _nextPredictionMsec;

        axis3_t _r;

        imu_t _gyroSum;
        imu_t _accelSum;


    public:

        // Set by Estimator
        bool isFlying;

        bool shouldUpdateWithRange(const uint32_t distance, 
                float h[7], float & error, float & noise)
        {
            const auto angle = max(0, 
                    fabsf(acosf(_r.z)) - 
                    DEGREES_TO_RADIANS * (15.0f / 2.0f));

            const auto predictedDistance = get(_x, STATE_Z) / cosf(angle);

            const auto measuredDistance = distance / 1000.f; // mm => m

            h[0] = 1/cosf(angle);

            noise = RANGEFINDER_EXP_STD_A * 
                (1 + expf(RANGEFINDER_EXP_COEFF * 
                          (measuredDistance - RANGEFINDER_EXP_POINT_A)));

            error = measuredDistance-predictedDistance;

            return fabs(_r.z) > 0.1f && _r.z > 0 && 
                    distance < RANGEFINDER_OUTLIER_LIMIT_MM;
        }

        void updateWithFlow(
                const uint32_t nowMsec, 
                const float flow_dt,
                const float flow_dpixelx,
                const float flow_dpixely)
        {
            // Inclusion of flow measurements in the EKF done by two scalar updates

            // ~~~ Camera constants ~~~
            // The angle of aperture is guessed from the raw data register and
            // thankfully look to be symmetric

            float Npix = 35.0;                      // [pixels] (same in x and y)

            // 2*sin(42/2); 42degree is the agnle of aperture, here we computed the
            // corresponding ground length
            float thetapix = 0.71674f;

            //~~~ Body rates ~~~
            // TODO check if this is feasible or if some filtering has to be done
            const auto omegax_b = _gyroLatest.x * DEGREES_TO_RADIANS;
            const auto omegay_b = _gyroLatest.y * DEGREES_TO_RADIANS;

            const auto dx_g = get(_x, STATE_DX);
            const auto dy_g = get(_x, STATE_DY);

            // Saturate elevation in prediction and correction to avoid singularities
            const auto z_g = get(_x, STATE_Z) < 0.1f ? 0.1f : get(_x, STATE_Z);

            // ~~~ X velocity prediction and update ~~~
            // predicts the number of accumulated pixels in the x-direction
            auto predictedNX = (flow_dt * Npix / thetapix ) * 
                ((dx_g * _r.z / z_g) - omegay_b);
            auto measuredNX = flow_dpixelx*FLOW_RESOLUTION;

            // derive measurement equation with respect to dx (and z?)
            const float hx[7] =  {
                (Npix * flow_dt / thetapix) * ((_r.z * dx_g) / (-z_g * z_g)),
                (Npix * flow_dt / thetapix) * (_r.z / z_g),
                0,
                0,
                0,
                0,
                0
            };

            //First update
            update(hx, measuredNX-predictedNX, FLOW_STD_FIXED*FLOW_RESOLUTION);

            // ~~~ Y velocity prediction and update ~~~
            auto predictedNY = (flow_dt * Npix / thetapix ) * 
                ((dy_g * _r.z / z_g) + omegax_b);
            auto measuredNY = flow_dpixely*FLOW_RESOLUTION;

            // derive measurement equation with respect to dy (and z?)
            const float hy[7] = {
                (Npix * flow_dt / thetapix) * ((_r.z * dy_g) / (-z_g * z_g)),
                0,
                (Npix * flow_dt / thetapix) * (_r.z / z_g),
                0,
                0,
                0,
                0
            };

            // Second update
            update(hy, measuredNY-predictedNY, FLOW_STD_FIXED*FLOW_RESOLUTION);

        }

        void updateWithGyro(const uint32_t nowMsec, const axis3_t & gyro) 
        {
            imuAccum(gyro, _gyroSum);

            memcpy(&_gyroLatest, &gyro, sizeof(axis3_t));
        }

        void updateWithAccel(const uint32_t nowMsec, const axis3_t & accel) 
        {
            imuAccum(accel, _accelSum);
        }

        void getState(vehicleState_t & state)
        {
            state.dx = get(_x, STATE_DX);

            state.dy = get(_x, STATE_DY);

            state.z = get(_x, STATE_Z);

            state.z = min(0, state.z);

            state.dz = _r.x * get(_x, STATE_DX) + _r.y * get(_x, STATE_DY) + 
                _r.z * get(_x, STATE_DZ);

            // Pack Z and DZ into a single float for transmission to client
            const int8_t sgn = state.dz < 0 ? -1 : +1;
            const float s = 1000;
            state.z_dz = (int)(state.dz * s) + sgn * state.z / s;

            const auto qw = _quat.w;
            const auto qx = _quat.x;
            const auto qy = _quat.y;
            const auto qz = _quat.z;

            state.phi = RADIANS_TO_DEGREES * atan2((2 * (qy*qz + qw*qx)),
                    (qw*qw - qx*qx - qy*qy + qz*qz));

            // Negate for ENU
            state.theta = -RADIANS_TO_DEGREES * asin((-2) * (qx*qz - qw*qy));

            state.psi = RADIANS_TO_DEGREES * atan2((2 * (qx*qy + qw*qz)),
                    (qw*qw + qx*qx - qy*qy - qz*qz));

            // Get angular velocities directly from gyro
            state.dphi =    _gyroLatest.x;
            state.dtheta = -_gyroLatest.y; // negate for ENU
            state.dpsi =    _gyroLatest.z;
        }

        bool isStateWithinBounds(void)
        {
            return
                isPositionWithinBounds(get(_x, STATE_Z)) &&
                isVelocityWithinBounds(get(_x, STATE_DX)) &&
                isVelocityWithinBounds(get(_x, STATE_DY)) &&
                isVelocityWithinBounds(get(_x, STATE_DZ));
        }

    private:

        // Indexes to access the state
        enum {

            STATE_Z,
            STATE_DX,
            STATE_DY,
            STATE_DZ,
            STATE_E0,
            STATE_E1,
            STATE_E2
        };

        // Quaternion used for initial orientation
        static constexpr float QW_INIT = 1;
        static constexpr float QX_INIT = 0;
        static constexpr float QY_INIT = 0;
        static constexpr float QZ_INIT = 0;

        // Initial variances, uncertain of position, but know we're
        // stationary and roughly flat
        static constexpr float STDEV_INITIAL_POSITION_Z = 1;
        static constexpr float STDEV_INITIAL_VELOCITY = 0.01;
        static constexpr float STDEV_INITIAL_ATTITUDE_ROLL_PITCH = 0.01;
        static constexpr float STDEV_INITIAL_ATTITUDE_YAW = 0.01;

        static constexpr float MSS_TO_GS = 9.81;

        //We do get the measurements in 10x the motion pixels (experimentally measured)
        static constexpr float FLOW_RESOLUTION = 0.1;

        // The bounds on states, these shouldn't be hit...
        static constexpr float MAX_POSITION = 100; //meters
        static constexpr float MAX_VELOCITY = 10; //meters per second

        // Small number epsilon, to prevent dividing by zero
        static constexpr float EPS = 1e-6f;

        // the reversion of pitch and roll to zero
        static constexpr float ROLLPITCH_ZERO_REVERSION = 0.001;

        static const uint16_t RANGEFINDER_OUTLIER_LIMIT_MM = 5000;

        // Rangefinder measurement noise model
        static constexpr float RANGEFINDER_EXP_POINT_A = 2.5;
        static constexpr float RANGEFINDER_EXP_STD_A = 0.0025; 
        static constexpr float RANGEFINDER_EXP_POINT_B = 4.0;
        static constexpr float RANGEFINDER_EXP_STD_B = 0.2;   

        static constexpr float RANGEFINDER_EXP_COEFF = 
            logf( RANGEFINDER_EXP_STD_B / RANGEFINDER_EXP_STD_A) / 
            (RANGEFINDER_EXP_POINT_B - RANGEFINDER_EXP_POINT_A);

        static constexpr float FLOW_STD_FIXED = 2.0;


        static void imuAccum(const axis3_t vals, imu_t & imu)
        {
            imu.sum.x += vals.x;
            imu.sum.y += vals.y;
            imu.sum.z += vals.z;
            imu.count++;
        }

        static void imuTakeMean(
                const imu_t & imu, 
                const float conversionFactor, 
                axis3_t & mean)
        {
            const auto count = imu.count;

            const auto isCountNonzero = count > 0;

            mean.x = isCountNonzero ? imu.sum.x * conversionFactor / count : mean.x;
            mean.y = isCountNonzero ? imu.sum.y * conversionFactor / count : mean.y;
            mean.z = isCountNonzero ? imu.sum.z * conversionFactor / count : mean.z;
        }

        static const float max(const float val, const float maxval)
        {
            return val > maxval ? maxval : val;
        }

        static const float min(const float val, const float maxval)
        {
            return val < maxval ? maxval : val;
        }

        static const float square(const float x)
        {
            return x * x;
        }

        static float rotateQuat(
                const bool isFlying, const float val, const float initVal)
        {
            const auto keep = 1.0f - ROLLPITCH_ZERO_REVERSION;

            return (val * (isFlying ? 1: keep)) +
                (isFlying ? 0 : ROLLPITCH_ZERO_REVERSION * initVal);
        }

        static bool isPositionWithinBounds(const float pos)
        {
            return fabs(pos) < MAX_POSITION;
        }

        static bool isVelocityWithinBounds(const float vel)
        {
            return fabs(vel) < MAX_VELOCITY;
        }

        static bool isErrorLarge(const float v)
        {
            return fabs(v) > 0.1e-3f;
        }

        static bool isErrorInBounds(const float v)
        {
            return fabs(v) < 10;
        }

    protected:

        void do_init(float x[EKF_N])
        {

            x[STATE_Z] = square(STDEV_INITIAL_POSITION_Z);
            x[STATE_DX] = square(STDEV_INITIAL_VELOCITY);
            x[STATE_DY] = square(STDEV_INITIAL_VELOCITY);
            x[STATE_DZ] = square(STDEV_INITIAL_VELOCITY);
            x[STATE_E0] = square(STDEV_INITIAL_ATTITUDE_ROLL_PITCH);
            x[STATE_E1] = square(STDEV_INITIAL_ATTITUDE_ROLL_PITCH);
            x[STATE_E2] = square(STDEV_INITIAL_ATTITUDE_YAW);

            _quat.w = QW_INIT;
            _quat.x = QX_INIT;
            _quat.y = QY_INIT;
            _quat.z = QZ_INIT;

            _r.x = 0;
            _r.y = 0;
            _r.z = 0;

            isFlying = false;
        }

        void get_prediction(
                const float dt,
                const bool didAddProcessNoise,
                const float xold[EKF_N],
                float xnew[EKF_N],
                float F[EKF_N][EKF_N])
        {

            static axis3_t _gyro;
            static axis3_t _accel;

            const auto dt2 = dt * dt;

            imuTakeMean(_gyroSum, DEGREES_TO_RADIANS, _gyro);
            imuTakeMean(_accelSum, MSS_TO_GS, _accel);

            // Position updates in the body frame (will be rotated to inertial frame);
            // thrust can only be produced in the body's Z direction
            const auto dx = xold[STATE_DX] * dt + 
                isFlying ? 0 : _accel.x * dt2 / 2;
            const auto dy = xold[STATE_DY] * dt + 
                isFlying ? 0 : _accel.y * dt2 / 2;
            const auto dz = xold[STATE_DZ] * dt + _accel.z * dt2 / 2; 

            const auto accx = isFlying ? 0 : _accel.x;
            const auto accy = isFlying ? 0 : _accel.y;

            // attitude update (rotate by gyroscope), we do this in quaternions
            // this is the gyroscope angular velocity integrated over the sample period
            const auto dtwx = dt*_gyro.x;
            const auto dtwy = dt*_gyro.y;
            const auto dtwz = dt*_gyro.z;

            // compute the quaternion values in [w,x,y,z] order
            const auto angle = sqrt(dtwx*dtwx + dtwy*dtwy + dtwz*dtwz) + EPS;
            const auto ca = cos(angle/2);
            const auto sa = sin(angle/2);
            const auto dqw = ca;
            const auto dqx = sa*dtwx/angle;
            const auto dqy = sa*dtwy/angle;
            const auto dqz = sa*dtwz/angle;

            // rotate the quad's attitude by the delta quaternion vector
            // computed above

            const auto qw = _quat.w;
            const auto qx = _quat.x;
            const auto qy = _quat.y;
            const auto qz = _quat.z;

            const auto tmpq0 = rotateQuat(isFlying,
                    dqw*qw - dqx*qx - dqy*qy - dqz*qz, QW_INIT);
            const auto tmpq1 = rotateQuat(isFlying,
                    dqx*qw + dqw*qx + dqz*qy - dqy*qz, QX_INIT);
            const auto tmpq2 = rotateQuat(isFlying,
                    dqy*qw - dqz*qx + dqw*qy + dqx*qz, QY_INIT);
            const auto tmpq3 = rotateQuat(isFlying,
                    dqz*qw + dqy*qx - dqx*qy + dqw*qz, QZ_INIT);

            // normalize and store the result
            const auto norm = 
                sqrt(tmpq0*tmpq0 + tmpq1*tmpq1 + tmpq2*tmpq2 + tmpq3*tmpq3) + 
                EPS;

            // Process noise is added after the return from the prediction step

            // ====== PREDICTION STEP ======
            // The prediction depends on whether we're on the ground, or in flight.
            // When flying, the accelerometer directly measures thrust
            // (hence is useless to estimate body angle while flying)

            const auto tmpSDX = xold[STATE_DX];
            const auto tmpSDY = xold[STATE_DY];
            const auto tmpSDZ = xold[STATE_DZ];

            xnew[STATE_Z] = xold[STATE_Z] + 
                _r.x * dx + _r.y * dy + _r.z * dz - MSS_TO_GS * dt2 / 2;

            xnew[STATE_DX] = xold[STATE_DX] +
                dt * (accx + _gyro.z * tmpSDY - _gyro.y * tmpSDZ -
                        MSS_TO_GS * _r.x);

            xnew[STATE_DY] =
                xold[STATE_DY] +
                dt * (accy - _gyro.z * tmpSDX + _gyro.x * tmpSDZ -
                        MSS_TO_GS * _r.y); 

            xnew[STATE_DZ] =
                xold[STATE_DZ] +
                dt * (_accel.z + _gyro.y * tmpSDX - _gyro.x * tmpSDY -
                        MSS_TO_GS * _r.z); 

            new_quat_t quat_predicted = {};

            quat_predicted.w = tmpq0/norm;
            quat_predicted.x = tmpq1/norm; 
            quat_predicted.y = tmpq2/norm; 
            quat_predicted.z = tmpq3/norm;

            const auto e0 = _gyro.x*dt/2;
            const auto e1 = _gyro.y*dt/2;
            const auto e2 = _gyro.z*dt/2;

            F[STATE_E0][STATE_E0] =  1 - e1*e1/2 - e2*e2/2;
            F[STATE_E0][STATE_E1] =  e2 + e0*e1/2;
            F[STATE_E0][STATE_E2] = -e1 + e0*e2/2;

            F[STATE_E1][STATE_E0] =  -e2 + e0*e1/2;
            F[STATE_E1][STATE_E1] = 1 - e0*e0/2 - e2*e2/2;
            F[STATE_E1][STATE_E2] = e0 + e1*e2/2;

            F[STATE_E2][STATE_E0] = e1 + e0*e2/2;
            F[STATE_E2][STATE_E1] = -e0 + e1*e2/2;
            F[STATE_E2][STATE_E2] = 1 - e0*e0/2 - e1*e1/2;

            // altitude from body-frame velocity
            F[STATE_Z][STATE_DX] = _r.x*dt;
            F[STATE_Z][STATE_DY] = _r.y*dt;
            F[STATE_Z][STATE_DZ] = _r.z*dt;

            // altitude from attitude error
            F[STATE_Z][STATE_E0] = (xnew[ STATE_DY]*_r.z -
                    xnew[ STATE_DZ]*_r.y)*dt;

            F[STATE_Z][STATE_E1] = (- xnew[ STATE_DX]*_r.z +
                    xnew[ STATE_DZ]*_r.x)*dt;

            F[STATE_Z][STATE_E2] = (xnew[ STATE_DX]*_r.y -
                    xnew[ STATE_DY]*_r.x)*dt;

            // body-frame velocity from body-frame velocity
            F[STATE_DX][STATE_DX] = 1; //drag negligible
            F[STATE_DY][STATE_DX] =  -_gyro.z*dt;
            F[STATE_DZ][STATE_DX] = _gyro.y*dt;

            F[STATE_DX][STATE_DY] = _gyro.z*dt;
            F[STATE_DY][STATE_DY] = 1; //drag negligible
            F[STATE_DZ][STATE_DY] = _gyro.x*dt;

            F[STATE_DX][STATE_DZ] =  _gyro.y*dt;
            F[STATE_DY][STATE_DZ] = _gyro.x*dt;
            F[STATE_DZ][STATE_DZ] = 1; //drag negligible

            // body-frame velocity from attitude error
            F[STATE_DX][STATE_E0] = 0;
            F[STATE_DY][STATE_E0] = -MSS_TO_GS*_r.z*dt;
            F[STATE_DZ][STATE_E0] = MSS_TO_GS*_r.y*dt;

            F[STATE_DX][STATE_E1] = MSS_TO_GS*_r.z*dt;
            F[STATE_DY][STATE_E1] = 0;
            F[STATE_DZ][STATE_E1] = -MSS_TO_GS*_r.x*dt;

            F[STATE_DX][STATE_E2] = -MSS_TO_GS*_r.y*dt;
            F[STATE_DY][STATE_E2] = MSS_TO_GS*_r.x*dt;
            F[STATE_DZ][STATE_E2] = 0;

            if (didAddProcessNoise) {

                _quat.w = quat_predicted.w;
                _quat.x = quat_predicted.x;
                _quat.y = quat_predicted.y;
                _quat.z = quat_predicted.z;

                memset(&_gyroSum, 0, sizeof(_gyroSum));
                memset(&_accelSum, 0, sizeof(_accelSum));
            }
        }

        bool did_finalize(float x[EKF_N], float A[EKF_N][EKF_N])
        {
            // Incorporate the attitude error (Kalman filter state) with the attitude
            const auto v0 = get(_x, STATE_E0);
            const auto v1 = get(_x, STATE_E1);
            const auto v2 = get(_x, STATE_E2);

            const auto angle = sqrt(v0*v0 + v1*v1 + v2*v2) + EPS;
            const auto ca = cos(angle / 2.0f);
            const auto sa = sin(angle / 2.0f);

            const auto dqw = ca;
            const auto dqx = sa * v0 / angle;
            const auto dqy = sa * v1 / angle;
            const auto dqz = sa * v2 / angle;

            const auto qw = _quat.w;
            const auto qx = _quat.x;
            const auto qy = _quat.y;
            const auto qz = _quat.z;

            // Rotate the quad's attitude by the delta quaternion vector
            // computed above
            const auto tmpq0 = dqw * qw - dqx * qx - dqy * qy - dqz * qz;
            const auto tmpq1 = dqx * qw + dqw * qx + dqz * qy - dqy * qz;
            const auto tmpq2 = dqy * qw - dqz * qx + dqw * qy + dqx * qz;
            const auto tmpq3 = dqz * qw + dqy * qx - dqx * qy + dqw * qz;

            // normalize and store the result
            const auto norm = sqrt(tmpq0 * tmpq0 + tmpq1 * tmpq1 + tmpq2 * tmpq2 + 
                    tmpq3 * tmpq3) + EPS;

            const auto isErrorSufficient  = 
                (isErrorLarge(v0) || isErrorLarge(v1) || isErrorLarge(v2)) &&
                isErrorInBounds(v0) && isErrorInBounds(v1) && isErrorInBounds(v2);

            _quat.w = isErrorSufficient ? tmpq0 / norm : _quat.w;
            _quat.x = isErrorSufficient ? tmpq1 / norm : _quat.x;
            _quat.y = isErrorSufficient ? tmpq2 / norm : _quat.y;
            _quat.z = isErrorSufficient ? tmpq3 / norm : _quat.z;

            x[STATE_E0] = 0;
            x[STATE_E1] = 0;
            x[STATE_E2] = 0;

            _r.x = 2 * _quat.x * _quat.z - 2 * _quat.w * _quat.y;
            _r.y = 2 * _quat.y * _quat.z + 2 * _quat.w * _quat.x; 
            _r.z = _quat.w*_quat.w-_quat.x*_quat.x-_quat.y*_quat.y+_quat.z*_quat.z;

            // the attitude error vector (v0,v1,v2) is small,
            // so we use a first order approximation to e0 = tan(|v0|/2)*v0/|v0|
            const auto e0 = v0 / 2; 
            const auto e1 = v1 / 2; 
            const auto e2 = v2 / 2;

            A[STATE_DX][STATE_DX] = 1;
            A[STATE_DY][STATE_DY] = 1;
            A[STATE_DZ][STATE_DZ] = 1;

            A[STATE_E0][STATE_E0] =  1 - e1*e1/2 - e2*e2/2;
            A[STATE_E0][STATE_E1] =  e2 + e0*e1/2;
            A[STATE_E0][STATE_E2] = -e1 + e0*e2/2;

            A[STATE_E1][STATE_E0] =  -e2 + e0*e1/2;
            A[STATE_E1][STATE_E1] = 1 - e0*e0/2 - e2*e2/2;
            A[STATE_E1][STATE_E2] = e0 + e1*e2/2;

            A[STATE_E2][STATE_E0] = e1 + e0*e2/2;
            A[STATE_E2][STATE_E1] = -e0 + e1*e2/2;
            A[STATE_E2][STATE_E2] = 1 - e0*e0/2 - e1*e1/2;

            return isErrorSufficient;
        }
};

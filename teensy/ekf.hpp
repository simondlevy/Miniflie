#pragma once

#include <string.h>

#include <clock.hpp>
#include <math3d.h>
#include <datatypes.h>
#include <linalg.h>

#include "streams.h"

// Initial variances, uncertain of position, but know we're
// stationary and roughly flat
static const float STDEV_INITIAL_POSITION_Z = 1;
static const float STDEV_INITIAL_VELOCITY = 0.01;
static const float STDEV_INITIAL_ATTITUDE_ROLL_PITCH = 0.01;
static const float STDEV_INITIAL_ATTITUDE_YAW = 0.01;

static const float MSS_TO_GS = 9.81;

//We do get the measurements in 10x the motion pixels (experimentally measured)
static const float FLOW_RESOLUTION = 0.1;

// The bounds on the covariance, these shouldn't be hit, but sometimes are... why?
static const float MAX_COVARIANCE = 100;
static const float MIN_COVARIANCE = 1e-6;

// The bounds on states, these shouldn't be hit...
static const float MAX_POSITION = 100; //meters
static const float MAX_VELOCITY = 10; //meters per second

// Small number epsilon, to prevent dividing by zero
static const float EPS = 1e-6f;

// the reversion of pitch and roll to zero
static const float ROLLPITCH_ZERO_REVERSION = 0.001;

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

// this is slower than the IMU update rate of 1000Hz
static const uint32_t PREDICT_RATE = Clock::RATE_100_HZ; 
static const uint32_t PREDICTION_UPDATE_INTERVAL_MS = 1000 / PREDICT_RATE;

// Indexes to access the state
enum {

    KC_STATE_Z,
    KC_STATE_DX,
    KC_STATE_DY,
    KC_STATE_DZ,
    KC_STATE_E0,
    KC_STATE_E1,
    KC_STATE_E2,
    KC_STATE_DIM
};

typedef struct {

    float dat[KC_STATE_DIM][KC_STATE_DIM];

} matrix_t;

typedef struct {

    float z;
    float dx;
    float dy;
    float dz;

} ekfLinear_t;

typedef struct {

    ekfLinear_t lin;

    axis3_t ang;

} ekfState_t;

static const float square(const float x)
{
    return x * x;
}

static void updateCovarianceMatrix(const matrix_t & p_in, matrix_t & p_out) 
{
    // Enforce symmetry of the covariance matrix, and ensure the
    // values stay bounded
    for (int i=0; i<KC_STATE_DIM; i++) {

        for (int j=i; j<KC_STATE_DIM; j++) {

            const auto pval = (p_in.dat[i][j] + p_in.dat[j][i]) / 2;

            p_out.dat[i][j] = p_out.dat[j][i] = 
                pval > MAX_COVARIANCE ?  MAX_COVARIANCE :
                (i==j && pval < MIN_COVARIANCE) ?  MIN_COVARIANCE :
                pval;
        }
    }
}

static void scalarUpdate(
        const matrix_t & p_in,
        const ekfState_t & ekfs_in,
        const float h[KC_STATE_DIM],
        const float error, 
        const float stdMeasNoise,
        matrix_t & p_out,
        ekfState_t & ekfs_out)
{

    // ====== INNOVATION COVARIANCE ======
    float ph[KC_STATE_DIM] = {};
    multiply(p_in.dat, h, ph);
    const auto r = stdMeasNoise * stdMeasNoise;
    const auto hphr = r + dot(h, ph); // HPH' + R

    // Compute the Kalman gain as a column vector
    const float g[KC_STATE_DIM] = {

        // kalman gain = (PH' (HPH' + R )^-1)
        ph[0] / hphr, 
        ph[1] / hphr, 
        ph[2] / hphr, 
        ph[3] / hphr, 
        ph[4] / hphr, 
        ph[5] / hphr, 
        ph[6] / hphr
    };

    // Perform the state update
    ekfs_out.lin.z = ekfs_in.lin.z  + g[0] * error;
    ekfs_out.lin.dx = ekfs_in.lin.dx + g[1] * error;
    ekfs_out.lin.dy = ekfs_in.lin.dy + g[2] * error;
    ekfs_out.lin.dz = ekfs_in.lin.dz + g[3] * error;
    ekfs_out.ang.x = ekfs_in.ang.x + g[4] * error;
    ekfs_out.ang.y = ekfs_in.ang.y + g[5] * error;
    ekfs_out.ang.z = ekfs_in.ang.z + g[6] * error;

    // ====== COVARIANCE UPDATE ======

    matrix_t GH = {};
    multiply(g, h, GH.dat); // KH

    for (int i=0; i<KC_STATE_DIM; i++) { 
        GH.dat[i][i] -= 1;
    } // KH - I

    matrix_t GHt = {};
    transpose(GH.dat, GHt.dat);      // (KH - I)'
    matrix_t GHIP = {};
    multiply(GH.dat, p_in.dat, GHIP.dat);  // (KH - I)*P
    multiply(GHIP.dat, GHt.dat, p_out.dat); // (KH - I)*P*(KH - I)'

    // Add the measurement variance 
    for (int i=0; i<KC_STATE_DIM; i++) {
        for (int j=0; j<KC_STATE_DIM; j++) {
            p_out.dat[i][j] += j < i ? 0 : r * g[i] * g[j];
        }
    }

    updateCovarianceMatrix(p_out, p_out);
}

static bool isErrorLarge(const float v)
{
    return fabs(v) > 0.1e-3f;
}

static bool isErrorInBounds(const float v)
{
    return fabs(v) < 10;
}

static void afinalize(
        const float v0, 
        const float v1, 
        const float v2,
        matrix_t & A)
{
    // the attitude error vector (v0,v1,v2) is small,
    // so we use a first order approximation to e0 = tan(|v0|/2)*v0/|v0|
    const auto e0 = v0/2; 
    const auto e1 = v1/2; 
    const auto e2 = v2/2;

    const auto e0e0 =  1 - e1*e1/2 - e2*e2/2;
    const auto e0e1 =  e2 + e0*e1/2;
    const auto e0e2 = -e1 + e0*e2/2;

    const auto e1e0 =  -e2 + e0*e1/2;
    const auto e1e1 = 1 - e0*e0/2 - e2*e2/2;
    const auto e1e2 = e0 + e1*e2/2;

    const auto e2e0 = e1 + e0*e2/2;
    const auto e2e1 = -e0 + e1*e2/2;
    const auto e2e2 = 1 - e0*e0/2 - e1*e1/2;

    const float a[KC_STATE_DIM][KC_STATE_DIM] = 
    { 
        //    Z  DX DY DZ    E0     E1    E2
        /*Z*/   {0, 0, 0, 0, 0,     0,    0},   
        /*DX*/  {0, 1, 0, 0, 0,     0,    0},  
        /*DY*/  {0, 0, 1, 0, 0,     0,    0}, 
        /*DX*/  {0, 0, 0, 1, 0,     0,    0},  
        /*E0*/  {0, 0, 0, 0, e0e0, e0e1, e0e2},
        /*E1*/  {0, 0, 0, 0, e1e0, e1e1, e1e2},
        /*E2*/  {0, 0, 0, 0, e2e0, e2e1, e2e2}
    };

    memcpy(&A.dat, a, sizeof(A));
} 

static void subSamplerTakeMean(
        const float sumx,
        const float sumy,
        const float sumz,
        const uint32_t count,
        const float conversionFactor,
        float & avgx,
        float & avgy,
        float & avgz)
{
    const auto isCountNonzero = count > 0;

    avgx = isCountNonzero ? sumx * conversionFactor / count : avgx;
    avgy = isCountNonzero ? sumy * conversionFactor / count : avgy;
    avgz = isCountNonzero ? sumz * conversionFactor / count : avgz;
}

// ===========================================================================

static void ekf_init(matrix_t & p_out)
{
    memset(&p_out, 0, sizeof(p_out));
    p_out.dat[KC_STATE_Z][KC_STATE_Z] = square(STDEV_INITIAL_POSITION_Z);
    p_out.dat[KC_STATE_DX][KC_STATE_DX] = square(STDEV_INITIAL_VELOCITY);
    p_out.dat[KC_STATE_DY][KC_STATE_DY] = square(STDEV_INITIAL_VELOCITY);
    p_out.dat[KC_STATE_DZ][KC_STATE_DZ] = square(STDEV_INITIAL_VELOCITY);
    p_out.dat[KC_STATE_E0][KC_STATE_E0] = square(STDEV_INITIAL_ATTITUDE_ROLL_PITCH);
    p_out.dat[KC_STATE_E1][KC_STATE_E1] = square(STDEV_INITIAL_ATTITUDE_ROLL_PITCH);
    p_out.dat[KC_STATE_E2][KC_STATE_E2] = square(STDEV_INITIAL_ATTITUDE_YAW);
}

static void ekf_predict(
        const float gyroSum_x,
        const float gyroSum_y,
        const float gyroSum_z,
        const uint32_t gyroCount,
        const float accelSum_x,
        const float accelSum_y,
        const float accelSum_z,
        const uint32_t accelCount,
        const matrix_t & p_in,
        const ekfLinear_t & linear_in,
        const axis3_t & r,
        const uint32_t lastPredictionMsec, 
        matrix_t & p_out,
        ekfLinear_t & linear_out) 
{
    static float _gyro_x;
    static float _gyro_y;
    static float _gyro_z;

    static float _accel_x;
    static float _accel_y;
    static float _accel_z;

    const float dt = (stream_now_msec - lastPredictionMsec) / 1000.0f;
    const auto dt2 = dt * dt;

    subSamplerTakeMean(gyroSum_x, gyroSum_y, gyroSum_z, gyroCount,
            DEGREES_TO_RADIANS,_gyro_x, _gyro_y, _gyro_z);

    subSamplerTakeMean(accelSum_x, accelSum_y, accelSum_z, accelCount, 
            MSS_TO_GS, _accel_x, _accel_y, _accel_z);

    // Position updates in the body frame (will be rotated to inertial frame);
    // thrust can only be produced in the body's Z direction
    const auto dx = linear_in.dx * dt + stream_is_flying ? 0 : _accel_x * dt2 / 2;
    const auto dy = linear_in.dy * dt + stream_is_flying ? 0 : _accel_y * dt2 / 2;
    const auto dz = linear_in.dz * dt + _accel_z * dt2 / 2; 

    // keep previous time step's state for the update
    const auto tmpSDX = linear_in.dx;
    const auto tmpSDY = linear_in.dy;
    const auto tmpSDZ = linear_in.dz;

    const auto accx = stream_is_flying ? 0 : _accel_x;
    const auto accy = stream_is_flying ? 0 : _accel_y;

    // ====== PREDICTION STEP ======
    // The prediction depends on whether we're on the ground, or in flight.
    // When flying, the accelerometer directly measures thrust (hence is useless
    // to estimate body angle while flying)

    // altitude update
    linear_out.z = linear_in.z + r.x * dx + r.y * dy + r.z * dz - MSS_TO_GS * dt2 / 2;

    static uint32_t _count;
    if (!(_count++ % 50)) {
        Serial.printf("%f\n", (double)linear_in.z);
    }

    // body-velocity update: accelerometers - gyros cross velocity
    // - gravity in body frame

    linear_out.dx = linear_in.dx +
        dt * (accx + _gyro_z * tmpSDY - _gyro_y * tmpSDZ - MSS_TO_GS * r.x);

    linear_out.dy = linear_in.dy +
        dt * (accy - _gyro_z * tmpSDX + _gyro_x * tmpSDZ - MSS_TO_GS * r.y); 

    linear_out.dz =  linear_in.dz +
        dt * (_accel_z + _gyro_y * tmpSDX - _gyro_x * tmpSDY - MSS_TO_GS * r.z);

    // ====== COVARIANCE UPDATE ======

    const auto e0 = _gyro_x*dt/2;
    const auto e1 = _gyro_y*dt/2;
    const auto e2 = _gyro_z*dt/2;

    const auto e0e0 =  1 - e1*e1/2 - e2*e2/2;
    const auto e0e1 =  e2 + e0*e1/2;
    const auto e0e2 = -e1 + e0*e2/2;

    const auto e1e0 =  -e2 + e0*e1/2;
    const auto e1e1 = 1 - e0*e0/2 - e2*e2/2;
    const auto e1e2 = e0 + e1*e2/2;

    const auto e2e0 = e1 + e0*e2/2;
    const auto e2e1 = -e0 + e1*e2/2;
    const auto e2e2 = 1 - e0*e0/2 - e1*e1/2;

    // altitude from body-frame velocity
    const auto zdx  = r.x*dt;
    const auto zdy  = r.y*dt;
    const auto zdz  = r.z*dt;

    // altitude from attitude error
    const auto ze0  = (linear_out.dy*r.z - linear_out.dz*r.y)*dt;
    const auto ze1  = (- linear_out.dx*r.z + linear_out.dz*r.x)*dt;
    const auto ze2  = (linear_out.dx*r.y - linear_out.dy*r.x)*dt;

    // body-frame velocity from body-frame velocity
    const auto dxdx  = 1; //drag negligible
    const auto dydx =  -_gyro_z*dt;
    const auto dzdx  = _gyro_y*dt;

    const auto dxdy  = _gyro_z*dt;
    const auto dydy  = 1; //drag negligible
    const auto dzdy  = _gyro_x*dt;

    const auto dxdz =  _gyro_y*dt;
    const auto dydz  = _gyro_x*dt;
    const auto dzdz  = 1; //drag negligible

    // body-frame velocity from attitude error
    const auto dxe0  = 0;
    const auto dye0  = -MSS_TO_GS*r.z*dt;
    const auto dze0  = MSS_TO_GS*r.y*dt;

    const auto dxe1  = MSS_TO_GS*r.z*dt;
    const auto dye1  = 0;
    const auto dze1  = -MSS_TO_GS*r.x*dt;

    const auto dxe2  = -MSS_TO_GS*r.y*dt;
    const auto dye2  = MSS_TO_GS*r.x*dt;
    const auto dze2  = 0;

    const float A[KC_STATE_DIM][KC_STATE_DIM] = 
    { 
        //        Z  DX    DY    DZ    E0    E1    E2
        /*Z*/    {0, zdx,  zdy,  zdz,  ze0,  ze1,  ze2}, 
        /*DX*/   {0, dxdx, dxdy, dxdz, dxe0, dxe1, dxe2}, 
        /*DY*/   {0, dydx, dydy, dydz, dye0, dye1, dye2},
        /*DZ*/   {0, dzdx, dzdy, dzdz, dze0, dze1, dze2},
        /*E0*/   {0, 0,    0,    0,    e0e0, e0e1, e0e2}, 
        /*E1*/   {0, 0,    0,    0,    e1e0, e1e1, e1e2}, 
        /*E2*/   {0, 0,    0,    0,    e2e0, e2e1, e2e2}  
    };

    matrix_t  At = {};
    transpose(A, At.dat);     // A'
    matrix_t AP = {};
    multiply(A, p_in.dat, AP.dat);  // AP
    matrix_t APA = {};
    multiply(AP.dat, At.dat, APA.dat); // APA'
    updateCovarianceMatrix(APA, p_out);
}

static bool ekf_updateWithRange(
        const matrix_t & p_in,
        const ekfState_t & ekfs_in,
        const float rz,
        matrix_t & p_out,
        ekfState_t & ekfs_out)
{
    const auto angle = fmax(0, 
            fabsf(acosf(rz)) - 
            DEGREES_TO_RADIANS * (15.0f / 2.0f));

    const auto predictedDistance = ekfs_in.lin.z / cosf(angle);

    const auto measuredDistance = stream_rangefinder_distance / 1000; // mm => m

    const auto stdDev =
        RANGEFINDER_EXP_STD_A * 
        (1 + expf(RANGEFINDER_EXP_COEFF * (measuredDistance - RANGEFINDER_EXP_POINT_A)));


    // The sensor model (Pg.95-96,
    // https://lup.lub.lu.se/student-papers/search/publication/8905295)
    //
    // h = z/((R*z_b).z_b) = z/cos(alpha)
    //
    // Here,
    // h (Measured variable)[m] = Distance given by TOF sensor. This is the 
    // closest point from any surface to the sensor in the measurement cone
    // z (Estimated variable)[m] = THe actual elevation of the crazyflie
    // z_b = Basis vector in z direction of body coordinate system
    // R = Rotation matrix made from ZYX Tait-Bryan angles. Assumed to be 
    // stationary
    // alpha = angle between [line made by measured point <---> sensor] 
    // and [the intertial z-axis] 

    const float h[KC_STATE_DIM] = {1 / cosf(angle), 0, 0, 0, 0, 0, 0};

    // Only update the filter if the measurement is reliable 
    // (\hat{h} -> infty when R[2][2] -> 0)
    const auto shouldUpdate = fabs(rz) > 0.1f && rz > 0 && 
        stream_rangefinder_distance < RANGEFINDER_OUTLIER_LIMIT_MM;

    if (shouldUpdate) {
        scalarUpdate(
                p_in,
                ekfs_in,
                h , 
                measuredDistance-predictedDistance, 
                stdDev, 
                p_out,
                ekfs_out);
    }
    return shouldUpdate;
}

static void ekf_finalize(
        const matrix_t & p_in,
        const ekfState_t & ekfs,
        matrix_t & p_out)
{
    // Incorporate the attitude error (Kalman filter state) with the attitude
    const auto v0 = ekfs.ang.x;
    const auto v1 = ekfs.ang.y;
    const auto v2 = ekfs.ang.z;

    const auto isErrorSufficient  = 
        (isErrorLarge(v0) || isErrorLarge(v1) || isErrorLarge(v2)) &&
        isErrorInBounds(v0) && isErrorInBounds(v1) && isErrorInBounds(v2);

    // Move attitude error into attitude if any of the angle errors are
    // large enough
    if (isErrorSufficient) {
        matrix_t  A = {};
        afinalize(v0, v2, v2, A);
        matrix_t At = {};
        transpose(A.dat, At.dat);     // A'
        matrix_t AP = {};
        multiply(A.dat, p_in.dat, AP.dat);  // AP
        matrix_t APA = {};
        multiply(AP.dat, At.dat, APA.dat); // APA'
        updateCovarianceMatrix(APA, p_out);
    }
} 

// ===========================================================================

static void ekf_step(void)
{
    static float _z;
    static float _dx;
    static float _dy;
    static float _dz;
    static float _e0;
    static float _e1;
    static float _e2;

    static matrix_t _p;

    static float _gyroSum_x;
    static float _gyroSum_y;
    static float _gyroSum_z;
    static uint32_t _gyroCount;

    static float _accelSum_x;
    static float _accelSum_y;
    static float _accelSum_z;
    static uint32_t _accelCount;

    static axis3_t _gyroLatest;

    static bool _isUpdated;
    static uint32_t _lastPredictionMsec;
    static uint32_t _lastProcessNoiseUpdateMsec;

    static float _rx;
    static float _ry;
    static float _rz;

    static uint32_t _nextPredictionMsec;

    const auto ekfs = ekfState_t { {_z, _dx, _dy, _dz}, {_e0, _e1, _e2}};

    const auto r = axis3_t {_rx, _ry, _rz};

    // Initialize
    bool didInitialize = stream_ekf_action == EKF_INIT;
    matrix_t p_initialized = {};
    ekf_init(p_initialized);

    _nextPredictionMsec = stream_now_msec > _nextPredictionMsec ?
        stream_now_msec + PREDICTION_UPDATE_INTERVAL_MS :
        _nextPredictionMsec;

    // Predict
    const auto shouldPredict = stream_ekf_action == EKF_PREDICT;

    const auto didPredict =
        shouldPredict && stream_now_msec >= _nextPredictionMsec;

    ekfLinear_t lin_predicted = {};

    if (didPredict) {

        ekf_predict(
                _gyroSum_x,
                _gyroSum_y,
                _gyroSum_z,
                _gyroCount,
                _accelSum_x,
                _accelSum_y,
                _accelSum_z,
                _accelCount,
                _p,
                ekfs.lin,
                r,
                _lastPredictionMsec, 

                _p,
                lin_predicted);
    }

    const auto isDtPositive = didPredict && 
        (stream_now_msec - _lastProcessNoiseUpdateMsec) / 1000.0f;

    // Finalize
    const auto finalizing = stream_ekf_action == EKF_FINALIZE;
    const auto didFinalize = finalizing && _isUpdated;
    if (didFinalize) {
        ekf_finalize(_p, ekfs, _p);
    }

    // Update with range
    ekfState_t ekfs_updatedWithRange = {};
    const auto didUpdateWithRange = stream_ekf_action == EKF_UPDATE_WITH_RANGE &&
        ekf_updateWithRange(_p, ekfs, _rz, 
                _p, ekfs_updatedWithRange);

    // Update with gyro
    const auto didUpdateWithGyro = stream_ekf_action == EKF_UPDATE_WITH_GYRO;

    // Update with accel
    const auto didUpdateWithAccel = stream_ekf_action == EKF_UPDATE_WITH_ACCEL;

    if (finalizing) {
        /*
        setStateIsInBounds(
                isPositionWithinBounds(ekfs.lin.z) &&
                isVelocityWithinBounds(ekfs.lin.dx) &&
                isVelocityWithinBounds(ekfs.lin.dy) &&
                isVelocityWithinBounds(ekfs.lin.dz));*/
    }

    //////////////////////////////////////////////////////////////////////////

    _gyroSum_x = didUpdateWithGyro ? _gyroSum_x + stream_gyro_x :
        isDtPositive ? 0 :
        _gyroSum_x;

    _gyroSum_y = didUpdateWithGyro ? _gyroSum_y + stream_gyro_y :
        isDtPositive ? 0 :
        _gyroSum_y;

    _gyroSum_z = didUpdateWithGyro ? _gyroSum_z + stream_gyro_z :
        isDtPositive ? 0 :
        _gyroSum_z;

    _accelSum_x = didUpdateWithAccel ? _accelSum_x + stream_accel_x :
        isDtPositive ? 0 :
        _accelSum_x;

    _accelSum_y = didUpdateWithAccel ? _accelSum_y + stream_accel_y :
        isDtPositive ? 0 :
        _accelSum_y;

    _accelSum_z = didUpdateWithAccel ? _accelSum_z + stream_accel_z :
        isDtPositive ? 0 :
        _accelSum_z;

    memcpy(&_p, 
            didInitialize ? &p_initialized :
            &_p, sizeof(_p));

    _gyroLatest.x =  didUpdateWithGyro ? stream_gyro_x : _gyroLatest.x;
    _gyroLatest.y =  didUpdateWithGyro ? stream_gyro_y : _gyroLatest.y;
    _gyroLatest.z =  didUpdateWithGyro ? stream_gyro_z : _gyroLatest.z;

    _gyroCount = isDtPositive ? 0 : 
        didUpdateWithGyro ? _gyroCount + 1 :
        _gyroCount;

    _accelCount = isDtPositive ? 0 : 
        didUpdateWithAccel ? _accelCount + 1 :
        _accelCount;

    const auto qw = stream_quat_w;
    const auto qx = stream_quat_x;
    const auto qy = stream_quat_y;
    const auto qz = stream_quat_z;

    _rx = didInitialize ? 0 : 
        didFinalize ? 2 * qx * qz - 2 * qw * qy :
        _rx;

    _ry = didInitialize ? 0 : 
        didFinalize ? 2 * qy * qz + 2 * qw * qx :
        _ry;

    _rz = didInitialize ? 1 : 
        didFinalize ? qw*qw-qx*qx-qy*qy+qz*qz:
        _rz;

    _z = didInitialize ? 0 : 
        isDtPositive ? lin_predicted.z :
        didUpdateWithRange ? ekfs_updatedWithRange.lin.z :
        _z;

    _dx = didInitialize ? 0 : 
        isDtPositive ? lin_predicted.dx :
        didUpdateWithRange ? ekfs_updatedWithRange.lin.dx :
        _dx;

    _dy = didInitialize ? 0 : 
        isDtPositive ? lin_predicted.dy :
        didUpdateWithRange ? ekfs_updatedWithRange.lin.dy :
        _dy;

    _dz = didInitialize ? 0 : 
        isDtPositive ? lin_predicted.dz :
        didUpdateWithRange ? ekfs_updatedWithRange.lin.dz :
        _dz;

    _e0 = didInitialize || didFinalize ? 0 : 
        didUpdateWithRange ? ekfs_updatedWithRange.ang.x :
        _e0;

    _e1 = didInitialize || didFinalize ? 0 : 
        didUpdateWithRange ? ekfs_updatedWithRange.ang.y :
        _e1;

    _e2 = didInitialize || didFinalize ? 0 : 
        didUpdateWithRange ? ekfs_updatedWithRange.ang.z :
        _e2;

    _lastProcessNoiseUpdateMsec = 
        didInitialize || isDtPositive ?  
        stream_now_msec : 
        _lastProcessNoiseUpdateMsec;

    _lastPredictionMsec = 
        didInitialize || didPredict ? stream_now_msec :
        _lastPredictionMsec;

    _isUpdated = 
        didInitialize || didFinalize ? false :
        stream_ekf_action == EKF_PREDICT ? true :
        _isUpdated;
}
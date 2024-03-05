#include <streams.h>

demands_t openLoopDemands;
vehicleState_t vehicleState;
bool stream_inHoverMode;
bool resetPids;

extern Axis3f stream_gyro;

copilotMode_e copilotMode;
uint32_t kalmanNowMsec;
uint32_t kalmanNextPredictionMsec;
bool kalmanIsFlying;
measurement_t kalmanMeasurement;

 /*
   Based on https://github.com/nickrehm/dRehmFlight
 */


#include <Wire.h> 

#include <sbus.h>

#include <usfs.hpp>

#include <oneshot125.hpp>
#include <vector>

#include <teensy_ekf.hpp>

void copilot_step(void);

// LED settings -------------------------------------------------------------

static const uint8_t LED_PIN = 13;
static const uint32_t NUM_BLINKS = 3;
static const uint32_t BLINK_UPTIME = 160;
static const uint32_t BLINK_DOWNTIME = 70;

// USFS settings ------------------------------------------------------------

static const uint8_t ACCEL_BANDWIDTH = 3;
static const uint8_t GYRO_BANDWIDTH  = 3;
static const uint8_t QUAT_DIVISOR    = 1;
static const uint8_t MAG_RATE        = 100;
static const uint8_t ACCEL_RATE      = 20; // Multiply by 10 to get actual rate
static const uint8_t GYRO_RATE       = 100; // Multiply by 10 to get actual rate
static const uint8_t BARO_RATE       = 50;

static const uint8_t INTERRUPT_ENABLE = Usfs::INTERRUPT_RESET_REQUIRED |
                                        Usfs::INTERRUPT_ERROR |
                                        Usfs::INTERRUPT_QUAT;

static const bool VERBOSE = false;

// ---------------------------------------------------------------------------

// Do not exceed 2000Hz, all filter paras tuned to 2000Hz by default
static const uint32_t LOOP_RATE = 2000;

static const std::vector<uint8_t> MOTOR_PINS = {0, 1, 2, 3};

static auto motors = OneShot125(MOTOR_PINS);

bfs::SbusRx sbus(&Serial2);

// Streams read by Haskell
uint32_t stream_now_msec;
float stream_dt;
float stream_channel1_raw;
float stream_channel2_raw;
float stream_channel3_raw;
float stream_channel4_raw;
float stream_channel5_raw;
bool stream_radio_failsafe;
float stream_accel_x;
float stream_accel_y;
float stream_accel_z;
float stream_gyro_x;
float stream_gyro_y;
float stream_gyro_z;
float stream_state_phi;
float stream_state_theta;

// Motors set by Haskell
static int m1_command_PWM;
static int m2_command_PWM;
static int m3_command_PWM;
static int m4_command_PWM;

static float _statePsi;

static vehicleState_t _vehicleState;

// ---------------------------------------------------------------------------

static const uint8_t REPORT_HZ = 2;

static Usfs usfs;

static void powerPin(const uint8_t pin, const bool hilo)
{
    pinMode(pin, OUTPUT);
    digitalWrite(pin, hilo);
}

static void ledInit(void)
{
    pinMode(LED_PIN, OUTPUT); 
    digitalWrite(LED_PIN, HIGH);

    for (uint32_t j = 1; j<= NUM_BLINKS; j++) {
        digitalWrite(LED_PIN, LOW);
        delay(BLINK_DOWNTIME);
        digitalWrite(LED_PIN, HIGH);
        delay(BLINK_UPTIME);
    }
}

static void ledBlink(const uint32_t current_time) 
{
    //DESCRIPTION: Blink LED on board to indicate main loop is running
    /*
     * It looks cool.
     */
    static uint32_t blink_counter;
    static uint32_t blink_delay;
    static bool blinkAlternate;

    if (current_time - blink_counter > blink_delay) {
        blink_counter = micros();
        digitalWrite(LED_PIN, blinkAlternate); //Pin LED_PIN is built in LED

        if (blinkAlternate == 1) {
            blinkAlternate = 0;
            blink_delay = 100000;
        }
        else if (blinkAlternate == 0) {
            blinkAlternate = 1;
            blink_delay = 2000000;
        }
    }
}

static void imuInit(void)
{
    powerPin(21, HIGH);
    powerPin(22, LOW);

    Wire.begin(); 
    Wire.setClock(400000); 
    delay(100);

    usfs.loadFirmware(VERBOSE); 

    usfs.begin(
            ACCEL_BANDWIDTH,
            GYRO_BANDWIDTH,
            QUAT_DIVISOR,
            MAG_RATE,
            ACCEL_RATE,
            GYRO_RATE,
            BARO_RATE,
            INTERRUPT_ENABLE,
            VERBOSE); 

    // Clear interrupts
    Usfs::checkStatus();
}

static void imuRead(void)
{
    const auto eventStatus = Usfs::checkStatus(); 

    if (Usfs::eventStatusIsError(eventStatus)) { 

        Usfs::reportError(eventStatus);
    }

    if (Usfs::eventStatusIsAccelerometer(eventStatus)) { 

        usfs.readAccelerometerScaled(
                stream_accel_x, stream_accel_y, stream_accel_z);
    }

    if (Usfs::eventStatusIsGyrometer(eventStatus)) { 

        usfs.readGyrometerScaled(
                stream_gyro_x, stream_gyro_y, stream_gyro_z);
    }
}

static void readReceiver() 
{
    if (sbus.Read()) {

        stream_channel1_raw = sbus.data().ch[0];
        stream_channel2_raw = sbus.data().ch[1];
        stream_channel3_raw = sbus.data().ch[2];
        stream_channel4_raw = sbus.data().ch[3];
        stream_channel5_raw = sbus.data().ch[4];
    }

    if (sbus.data().failsafe) {
        stream_radio_failsafe = true;
    }
}

static void maintainLoopRate(const uint32_t current_time) 
{
    //DESCRIPTION: Regulate main loop rate to specified frequency in Hz
    /*
     * It's good to operate at a constant loop rate for filters to remain
     * stable and whatnot. Interrupt routines running in the background cause
     * the loop rate to fluctuate. This function basically just waits at the
     * end of every loop iteration until the correct time has passed since the
     * start of the current loop for the desired loop rate in Hz. 2kHz is a
     * good rate to be at because the loop nominally will run between 2.8kHz -
     * 4.2kHz. This lets us have a little room to add extra computations and
     * remain above 2kHz, without needing to retune all of our filtering
     * parameters.
     */
    float invFreq = 1.0/LOOP_RATE*1000000.0;
    auto checker = micros();

    //Sit in loop until appropriate time has passed
    while (invFreq > (checker - current_time)) {
        checker = micros();
    }
}


static void ekfStep(void)
{
    stream_now_msec = millis();

    Ekf::step(_vehicleState);

    stream_state_phi = _vehicleState.phi;
    stream_state_theta = -_vehicleState.theta; // note negation
    _statePsi = _vehicleState.psi;
}

static void debug(const uint32_t current_time)
{
    //Print data at 100hz (uncomment one at a time for troubleshooting) - SELECT ONE:
    //debugAccel(current_time);  
    //debugGyro(current_time);  
    debugState(current_time);  
    //debugMotorCommands(current_time); 
    //debugLoopRate(current_time);      
}

void debugRadio(const uint32_t current_time) 
{
    static uint32_t print_counter;
    if (current_time - print_counter > 10000) {
        print_counter = micros();
        Serial.printf("ch1:%4.0f ch2:%4.0f ch3:%4.0f ch4:%4.0f ch5:%4.0f\n",
                stream_channel1_raw, stream_channel2_raw, stream_channel3_raw, 
                stream_channel4_raw, stream_channel5_raw);

    }
}

void debugAccel(const uint32_t current_time) 
{
    static uint32_t print_counter;
    if (current_time - print_counter > 10000) {
        print_counter = micros();
        Serial.printf("accelX:%+3.3f accelY:%+3.3f accelZ:%+3.3f\n", 
                stream_accel_x, stream_accel_y, stream_accel_z);
    }
}

void debugGyro(const uint32_t current_time) 
{
    static uint32_t print_counter;
    if (current_time - print_counter > 10000) {
        print_counter = micros();
        Serial.printf("gyroX:%+03.3f gyroY:%+03.3f gyroZ:%+03.3f\n", 
                stream_gyro_x, stream_gyro_y, stream_gyro_z);
    }
}


void debugState(const uint32_t current_time) 
{
    static uint32_t print_counter;
    if (current_time - print_counter > 10000) {
        print_counter = micros();
        Serial.printf("roll:%2.2f pitch:%2.2f yaw:%2.2f\n", 
                stream_state_phi, stream_state_theta, _statePsi);
    }
}

void debugMotorCommands(const uint32_t current_time) 
{
    static uint32_t print_counter;
    if (current_time - print_counter > 10000) {
        print_counter = micros();
        Serial.printf(
                "m1_command:%d m2_command:%d m3_command:%d m4_command:%d\n",
                m1_command_PWM, m2_command_PWM,
                m3_command_PWM, m4_command_PWM);
    }
}

void debugLoopRate(const uint32_t current_time) 
{
    static uint32_t print_counter;
    if (current_time - print_counter > 10000) {
        print_counter = micros();
        Serial.printf("dt:%f\n", stream_dt*1e6);
    }
}

void setup()
{
    Serial.begin(115200);

    imuInit();

    stream_radio_failsafe = false;

    delay(5);

    sbus.Begin();

    ledInit();

    delay(5);

    //motors.arm();

} // setup

void loop()
{
    static uint32_t _current_time;
    static uint32_t _prev_time;

    // Keep track of what time it is and how much time has elapsed since the last loop
    _prev_time = _current_time;      
    _current_time = micros();      
    stream_dt = (_current_time - _prev_time)/1e6;

    ledBlink(_current_time);

    debug(_current_time);

    imuRead();

    ekfStep();

    copilot_step(); 

    readReceiver();

    maintainLoopRate(_current_time); 

}  // loop

// Called by Copilot ---------------------------------------------------------

void debugEkf(const float qw)
{
    //Serial.printf("%+3.3f\n", qw);
}

void setMotors(const float m1, const float m2, const float m3, const float m4)
{
    m1_command_PWM = m1;
    m2_command_PWM = m2;
    m3_command_PWM = m3;
    m4_command_PWM = m4;
}

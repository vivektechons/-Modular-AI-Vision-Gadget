#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "Wire.h"
#include <Adafruit_PWMServoDriver.h>

//========================================Variables for reading gyro values=============================================
bool dmpReady = false;  // set true if DMP init was successful
uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[64]; // FIFO storage buffer

// orientation/motion vars
Quaternion q;
VectorFloat gravity;
float ypr[3];

volatile bool mpuInterrupt = false;
void IRAM_ATTR dmpDataReady() {
  mpuInterrupt = true;
}
//=====================================================================================================================

// ESP32 pin used for the MPU6050 INT line (any free GPIO that supports interrupts works, e.g. 4, 15, 34)
#define INTERRUPT_PIN 4

// Default ESP32 I2C pins (change if you wired SDA/SCL elsewhere)
#define SDA_PIN 21
#define SCL_PIN 22

MPU6050 mpu;

// PCA9685 driver, default I2C address 0x40
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

// PCA9685 channels the 3 servos are plugged into
#define CH_YAW   0
#define CH_PITCH 1
#define CH_ROLL  2

// Servo pulse-length calibration for PCA9685 (12-bit, 50Hz).
// These are "ticks" out of 4096 at 50Hz. 150 ~ 0.5ms (0 deg), 600 ~ 2.5ms (180 deg).
// Adjust per your specific servos if they buzz at the ends.
#define SERVO_MIN_TICKS 150
#define SERVO_MAX_TICKS 600

void writeServoAngle(uint8_t channel, int angleDeg) {
  angleDeg = constrain(angleDeg, 0, 180);
  int ticks = map(angleDeg, 0, 180, SERVO_MIN_TICKS, SERVO_MAX_TICKS);
  pwm.setPWM(channel, 0, ticks);
}

void setup() {
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000); // 400kHz I2C clock

  // --- PCA9685 init ---
  pwm.begin();
  pwm.setPWMFreq(50); // standard analog servo frequency
  delay(10);

  // --- MPU6050 init ---
  mpu.initialize();
  pinMode(INTERRUPT_PIN, INPUT);
  devStatus = mpu.dmpInitialize();

  // You need to set your own offset values to get stable output. Find these by trial and error
  // (or run the IMU_Zero / MPU6050 calibration sketch once and paste the results here).
  mpu.setXGyroOffset(17);
  mpu.setYGyroOffset(-69);
  mpu.setZGyroOffset(80);
  mpu.setZAccelOffset(1450);

  if (devStatus == 0) {
    Serial.println(F("Enabling DMP..."));
    mpu.setDMPEnabled(true);

    attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), dmpDataReady, RISING);
    mpuIntStatus = mpu.getIntStatus();

    Serial.println(F("DMP ready! Waiting for first interrupt..."));
    dmpReady = true;

    packetSize = mpu.dmpGetFIFOPacketSize();
  } else {
    Serial.print(F("DMP Initialization failed (code "));
    Serial.print(devStatus);
    Serial.println(F(")"));
  }

  // Center all 3 servos at startup
  writeServoAngle(CH_YAW, 90);
  writeServoAngle(CH_PITCH, 90);
  writeServoAngle(CH_ROLL, 90);
}

void loop() {
  if (!dmpReady) return;

  readMPU6050();

  // You need to find your own servo directions/mapping depending on how each servo is mounted.
  int yawValue   = map((int)ypr[0], -90, 90, 0, 180);
  int pitchValue = map((int)ypr[1], -90, 90, 180, 0);
  int rollValue  = map((int)ypr[2], -90, 90, 0, 180);

  writeServoAngle(CH_YAW, yawValue);
  writeServoAngle(CH_PITCH, pitchValue);
  writeServoAngle(CH_ROLL, rollValue);

  /*Serial.print("yaw : ");
  Serial.print(yawValue);
  Serial.print(", pitch : ");
  Serial.print(pitchValue);
  Serial.print(", roll : ");
  Serial.println(rollValue);*/
}

float correct;
int j = 0;

void readMPU6050() {
  while (!mpuInterrupt && fifoCount < packetSize) {
    if (mpuInterrupt && fifoCount < packetSize) {
      fifoCount = mpu.getFIFOCount();
    }
  }

  mpuInterrupt = false;
  mpuIntStatus = mpu.getIntStatus();

  fifoCount = mpu.getFIFOCount();
  if ((mpuIntStatus & _BV(MPU6050_INTERRUPT_FIFO_OFLOW_BIT)) || fifoCount >= 1024) {
    mpu.resetFIFO();
    fifoCount = mpu.getFIFOCount();
    Serial.println(F("FIFO overflow!"));
  } else if (mpuIntStatus & _BV(MPU6050_INTERRUPT_DMP_INT_BIT)) {
    while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();

    mpu.getFIFOBytes(fifoBuffer, packetSize);
    fifoCount -= packetSize;

    mpu.dmpGetQuaternion(&q, fifoBuffer);
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);

    ypr[0] = ypr[0] * 180 / M_PI;
    ypr[1] = ypr[1] * 180 / M_PI;
    ypr[2] = ypr[2] * 180 / M_PI;

    if (j <= 300) {
      j++;
      correct = ypr[0];
      readMPU6050();
      return;
    }
    ypr[0] = ypr[0] - correct;
  }
}

// On-board: the real MPU6050 behind Esp32Imu. Failures name the likely
// hardware cause so they can be told apart from code faults.
#include <Arduino.h>
#include <unity.h>

#include <cmath>

#include "Esp32Ports.h"

static Esp32Imu imu;

void setUp() {}
void tearDown() {}

void test_mpu6050_answers_on_i2c() {
  TEST_ASSERT_TRUE_MESSAGE(imu.begin(),
                           "MPU6050 not responding at its I2C address: check SDA21/SCL22 "
                           "wiring and 3V3 power");
}

void test_total_acceleration_is_about_1g_when_resting() {
  double sum = 0;
  const int n = 100;
  for (int i = 0; i < n; ++i) {
    ImuSample s;
    TEST_ASSERT_TRUE_MESSAGE(imu.readSample(s), "MPU6050 stopped answering mid-read: loose I2C wire?");
    sum += std::sqrt(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
  }
  const double g = sum / n;
  char msg[96];
  snprintf(msg, sizeof msg, "total %.3f g, expected ~1 g: board moving, wrong range, or bad sensor", g);
  TEST_ASSERT_TRUE_MESSAGE(g > 0.9 && g < 1.1, msg);
}

void test_temperature_is_plausible() {
  float c = 0;
  TEST_ASSERT_TRUE_MESSAGE(imu.readTemperatureC(c), "MPU6050 temperature read failed: I2C fault");
  char msg[64];
  snprintf(msg, sizeof msg, "MPU6050 says %.1f C: sensor fault?", c);
  TEST_ASSERT_TRUE_MESSAGE(c > -20.0f && c < 70.0f, msg);
}

void setup() {
  delay(2000);  // give the serial port time to connect
  UNITY_BEGIN();
  RUN_TEST(test_mpu6050_answers_on_i2c);
  RUN_TEST(test_total_acceleration_is_about_1g_when_resting);
  RUN_TEST(test_temperature_is_plausible);
  UNITY_END();
}

void loop() {}

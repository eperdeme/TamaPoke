#include "imu.h"
#include "pin_config.h"
#include <Wire.h>
#include <SensorQMI8658.hpp>

static SensorQMI8658 qmi;
static bool imuOk = false;
static bool imuActive = false;

bool imuBegin() {
  imuOk = qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  if (!imuOk) {
    Serial.println("QMI8658 not detected");
    return false;
  }
  // 62.5 Hz is plenty for a walking gait (~2 Hz) and the main loop cannot poll
  // faster than a render frame anyway. 4 g covers a heavy footfall.
  qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_62_5Hz);
  return true;
}

void imuSetEnabled(bool on) {
  imuActive = on;
  if (!imuOk) return;
  if (on) qmi.enableAccelerometer();
  else qmi.disableAccelerometer();
}

bool imuReadAccel(float &x, float &y, float &z) {
  if (!imuOk || !imuActive || !qmi.getDataReady()) return false;
  return qmi.getAccelerometer(x, y, z);
}

#pragma once
#include <Arduino.h>

// QMI8658 6-axis IMU. Only the accelerometer is used (for step counting).
bool imuBegin();                                   // false if the chip is not found
void imuSetEnabled(bool on);                       // powers accelerometer up or down
bool imuReadAccel(float &x, float &y, float &z);   // in g; false when off or no new sample ready

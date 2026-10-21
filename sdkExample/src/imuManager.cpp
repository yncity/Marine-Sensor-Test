//------------------------------------------ Includes ----------------------------------------------

#include "imuManager.h"
#include "platform/debug.h"

using namespace IslSdk;

//--------------------------------------------------------------------------------------------------
void AhrsManager::connectSignals(Ahrs& sensor, const std::string& deviceName)
{
	disconnectSignals();
	ahrs = &sensor;
	name = deviceName;
	ahrs->onData.connect(slotAhrsData);
}
//--------------------------------------------------------------------------------------------------
void AhrsManager::disconnectSignals()
{
	if (ahrs)
	{
		ahrs->onData.disconnect(slotAhrsData);
	}
}
//--------------------------------------------------------------------------------------------------
void AhrsManager::callbackAhrs(Ahrs& ahrs, uint64_t timeUs, const Math::Quaternion& q, real_t magHeadingRad, real_t turnsCount)
{
    Math::EulerAngles euler = q.toEulerAngles(0);
    euler.radToDeg();

    Debug::log(Debug::Severity::Info, name.c_str(), "H:%.1f    P:%.2f    R%.2f", euler.heading, euler.pitch, euler.roll);
}
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
void GyroManager::connectSignals(GyroSensor& sensor, const std::string& deviceName)
{
	disconnectSignals();
	gyro = &sensor;
	name = deviceName;
	gyro->onData.connect(slotData);
	gyro->onCalChange.connect(slotCalChange);
}
//--------------------------------------------------------------------------------------------------
void GyroManager::disconnectSignals()
{
	if (gyro)
	{
		gyro->onData.disconnect(slotData);
		gyro->onCalChange.disconnect(slotCalChange);
	}
}
//--------------------------------------------------------------------------------------------------
void GyroManager::callbackData(GyroSensor& gyro, const Math::Vector3& v)
{
	Debug::log(Debug::Severity::Info, name.c_str(), "Gyro(%u) x:%.2f, y:%.2f, z:%.2f", gyro.sensorNumber, v.x, v.y, v.z);
}
//--------------------------------------------------------------------------------------------------
void GyroManager::callbackCalChange(GyroSensor& gyro, const Math::Vector3& v)
{
	Debug::log(Debug::Severity::Info, name.c_str(), "Gyro(%u) cal change x:%.2f, y:%.2f, z:%.2f", gyro.sensorNumber, v.x, v.y, v.z);
}
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
void AccelManager::connectSignals(AccelSensor& sensor, const std::string& deviceName)
{
	disconnectSignals();
	accel = &sensor;
	name = deviceName;
	accel->onData.connect(slotData);
	accel->onCalChange.connect(slotCalChange);
	accel->onCalProgress.connect(slotCal);
}
//--------------------------------------------------------------------------------------------------
void AccelManager::disconnectSignals()
{
	if (accel)
	{
		accel->onData.disconnect(slotData);
		accel->onCalChange.disconnect(slotCalChange);
		accel->onCalProgress.disconnect(slotCal);
	}
}
//--------------------------------------------------------------------------------------------------
void AccelManager::callbackData(AccelSensor& accel, const Math::Vector3& v)
{
	Debug::log(Debug::Severity::Info, name.c_str(), "Accel(%u) x:%.2f, y:%.2f, z:%.2f", accel.sensorNumber, v.x, v.y, v.z);
}
//--------------------------------------------------------------------------------------------------
void AccelManager::callbackCalChange(AccelSensor& accel, const Math::Vector3& v, const Math::Matrix3x3& transform)
{
	Debug::log(Debug::Severity::Info, name.c_str(), "Accel(%u) cal change bias: %.2f, %.2f, %.2f", accel.sensorNumber, v.x, v.y, v.z);
}
//--------------------------------------------------------------------------------------------------
void AccelManager::callbackCal(AccelSensor& accel, const Math::Vector3& v, uint_t count)
{
	Debug::log(Debug::Severity::Info, name.c_str(), "Accel(%u) cal progress: %u, x:%.2f, y:%.2f, z:%.2f", accel.sensorNumber, count, v.x, v.y, v.z);
}
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
void MagManager::connectSignals(MagSensor& sensor, const std::string& deviceName)
{
	disconnectSignals();
	mag = &sensor;
	name = deviceName;
	mag->onData.connect(slotData);
	mag->onCalChange.connect(slotCalChange);
	mag->onCalProgress.connect(slotCal);
}
//--------------------------------------------------------------------------------------------------
void MagManager::disconnectSignals()
{
	if (mag)
	{
		mag->onData.disconnect(slotData);
		mag->onCalChange.disconnect(slotCalChange);
		mag->onCalProgress.disconnect(slotCal);
	}
}
//--------------------------------------------------------------------------------------------------
void MagManager::callbackData(MagSensor& mag, const Math::Vector3& v)
{
	Debug::log(Debug::Severity::Info, name.c_str(), "Mag(%u) x:%.2f, y:%.2f, z:%.2f", mag.sensorNumber, v.x, v.y, v.z);
}
//--------------------------------------------------------------------------------------------------
void MagManager::callbackCalChange(MagSensor& mag, const Math::Vector3& v, const Math::Matrix3x3& transform)
{
	Debug::log(Debug::Severity::Info, name.c_str(), "Mag(%u) cal change bias: %.2f, %.2f, %.2f", mag.sensorNumber, v.x, v.y, v.z);
}
//--------------------------------------------------------------------------------------------------
void MagManager::callbackCal(MagSensor& mag, const Math::Vector3& v, uint_t count)
{
	Debug::log(Debug::Severity::Info, name.c_str(), "Mag(%u) cal progress: %u, x:%.2f, y:%.2f, z:%.2f", mag.sensorNumber, count, v.x, v.y, v.z);
}
//--------------------------------------------------------------------------------------------------

#ifndef IMUMANAGER_H_
#define IMUMANAGER_H_

//------------------------------------------ Includes ----------------------------------------------

#include "devices/ahrs.h"

//--------------------------------------- Class Definition -----------------------------------------

namespace IslSdk
{
    class AhrsManager
    {
    public:
        AhrsManager() : ahrs(nullptr) {};
        void connectSignals(Ahrs& sensor, const std::string& name);
        void disconnectSignals();
        
    private:
        std::string name;
        Ahrs* ahrs;
        Slot<Ahrs&, uint64_t, const Math::Quaternion&, real_t, real_t> slotAhrsData{ this, &AhrsManager::callbackAhrs };

        void callbackAhrs(Ahrs& ahrs, uint64_t timeUs, const Math::Quaternion& q, real_t magHeadingRad, real_t turnsCount);
    };

    class GyroManager
	{
    public:
        GyroManager() : gyro(nullptr) {};
        void connectSignals(GyroSensor& sensor, const std::string& name);
        void disconnectSignals();
       
    private:
        std::string name;
        GyroSensor* gyro;
        Slot<GyroSensor&, const Math::Vector3&> slotData{ this, & GyroManager::callbackData };
        Slot<GyroSensor&, const Math::Vector3&> slotCalChange { this, & GyroManager::callbackCalChange };

        void callbackData(GyroSensor& gyro, const Math::Vector3& v);
        void callbackCalChange(GyroSensor& gyro, const Math::Vector3& v);
    };

    class AccelManager
    {
    public:
        AccelManager() : accel(nullptr) {};
		void connectSignals(AccelSensor& sensor, const std::string& name);
		void disconnectSignals();
		
    private:
        std::string name;
        AccelSensor* accel;
        Slot<AccelSensor&, const Math::Vector3&> slotData{ this, & AccelManager::callbackData };
        Slot<AccelSensor&, const Math::Vector3&, const Math::Matrix3x3&> slotCalChange { this, & AccelManager::callbackCalChange };
        Slot<AccelSensor&, const Math::Vector3&, uint_t> slotCal{ this, & AccelManager::callbackCal };

        void callbackData(AccelSensor& accel, const Math::Vector3& v);
        void callbackCalChange(AccelSensor& accel, const Math::Vector3& v, const Math::Matrix3x3& transform);
        void callbackCal(AccelSensor& accel, const Math::Vector3& v, uint_t count);
	};

	class MagManager
	{
    public:
        MagManager() : mag(nullptr) {};
        void connectSignals(MagSensor& sensor, const std::string& name);
        void disconnectSignals();

    private:
        std::string name;
        MagSensor* mag;
        Slot<MagSensor&, const Math::Vector3&> slotData{ this, & MagManager::callbackData };
        Slot<MagSensor&, const Math::Vector3&, const Math::Matrix3x3&> slotCalChange { this, & MagManager::callbackCalChange };
        Slot<MagSensor&, const Math::Vector3&, uint_t> slotCal{ this, & MagManager::callbackCal };

        void callbackData(MagSensor& mag, const Math::Vector3& v);
        void callbackCalChange(MagSensor& mag, const Math::Vector3& v, const Math::Matrix3x3& transform);
        void callbackCal(MagSensor& mag, const Math::Vector3& v, uint_t count);
    };
}

//--------------------------------------------------------------------------------------------------
#endif

#ifndef GPSAPP_H_
#define GPSAPP_H_

//------------------------------------------ Includes ----------------------------------------------

#include "nmeaDevices/gpsDevice.h"

//--------------------------------------- Class Definition -----------------------------------------

namespace IslSdk
{
    class GpsApp
    {
    public:
        GpsApp(const std::string& name);
        ~GpsApp();
        void setDevice(const NmeaDevice::SharedPtr& device);
        const std::string name;

        Slot<NmeaDevice&, const std::string&> slotError{ this, & GpsApp::callbackError };
        Slot<NmeaDevice&> slotDelete{ this, & GpsApp::callbackDeleteted };
        Slot<GpsDevice&, const std::string&> slotData{ this, & GpsApp::callbackData };

    private:
        NmeaDevice::SharedPtr m_device;
        void callbackError(NmeaDevice& device, const std::string& msg);
        void callbackDeleteted(NmeaDevice& device);
        void callbackData(GpsDevice& device, const std::string& data);
    };
}

//--------------------------------------------------------------------------------------------------
#endif

//------------------------------------------ Includes ----------------------------------------------

#include "gpsApp.h"
#include "platform/debug.h"

using namespace IslSdk;

//--------------------------------------------------------------------------------------------------
GpsApp::GpsApp(const std::string& name) : name(name), m_device(nullptr)
{
}
//--------------------------------------------------------------------------------------------------
GpsApp::~GpsApp()
{
}
//--------------------------------------------------------------------------------------------------
void GpsApp::setDevice(const NmeaDevice::SharedPtr& device)
{
    if (m_device != nullptr)
    {
        GpsDevice& gps = reinterpret_cast<GpsDevice&>(*m_device);

        m_device->onError.disconnect(slotError);
        m_device->onDelete.disconnect(slotDelete);
        gps.onData.disconnect(slotData);
    }

    m_device = device;

    if (m_device != nullptr)
    {
        GpsDevice& gps = reinterpret_cast<GpsDevice&>(*m_device);

        m_device->onError.connect(slotError);
        m_device->onDelete.connect(slotDelete);
        gps.onData.connect(slotData);
    }
}
//--------------------------------------------------------------------------------------------------
void GpsApp::callbackError(NmeaDevice& device, const std::string& msg)
{
    Debug::log(Debug::Severity::Error, name.c_str(), "%s", msg.c_str());
}
//--------------------------------------------------------------------------------------------------
void GpsApp::callbackDeleteted(NmeaDevice& device)
{
    Debug::log(Debug::Severity::Warning, name.c_str(), "GPS deleteted");
}
//--------------------------------------------------------------------------------------------------
void GpsApp::callbackData(GpsDevice& device, const std::string& str)
{
    GpsDevice::GpsDevice::SentenceType sentenceType = GpsDevice::getSentenceType(str);

    switch (sentenceType)
    {
    case GpsDevice::SentenceType::Gll:
    {
        GpsDevice::Gpgll gpgll;
        if (device.parseStringGPGLL(str, gpgll))
        {
            Debug::log(Debug::Severity::Info, name.c_str(), "GPGLL: latitude:%f, longitude:%f", gpgll.latitudeDeg, gpgll.longitudeDeg);
        }
        break;
    }
    case GpsDevice::SentenceType::Gga:
    {
        GpsDevice::Gpgga gpgga;
        if (device.parseStringGPGGA(str, gpgga))
        {
            Debug::log(Debug::Severity::Info, name.c_str(), "GPGGA: latitude:%f, longitude:%f", gpgga.latitudeDeg, gpgga.longitudeDeg);
        }
        break;
    }
    case GpsDevice::SentenceType::Gsv:
    {
        GpsDevice::Gpgsv gpgsv;
        if (device.parseStringGPGSV(str, gpgsv))
        {
            Debug::log(Debug::Severity::Info, name.c_str(), "GPGSV: satellites in view:%u", gpgsv.satellitesInView);
        }
        break;
    }
    case GpsDevice::SentenceType::Gsa:
    {
        GpsDevice::Gpgsa gpgsa;
        if (device.parseStringGPGSA(str, gpgsa))
        {
            Debug::log(Debug::Severity::Info, name.c_str(), "GPGSA: fix type %s", gpgsa.fixType == 1 ? "NONE" : gpgsa.fixType == 2 ? "2D" : "3D");
        }
        break;
    }
    case GpsDevice::SentenceType::Vtg:
    {
        GpsDevice::Gpvtg gpvtg;
        if (device.parseStringGPVTG(str, gpvtg))
        {
            Debug::log(Debug::Severity::Info, name.c_str(), "GPVTG: speed:%.3f Km/h", gpvtg.speedKm);
        }
        break;
    }
    case GpsDevice::SentenceType::Rmc:
    {
        GpsDevice::Gprmc gprmc;
        if (device.parseStringGPRMC(str, gprmc))
        {
            Debug::log(Debug::Severity::Info, name.c_str(), "GPRMC: latitude:%f, longitude:%f", gprmc.latitudeDeg, gprmc.longitudeDeg);
        }
        break;
    }
    default:
        break;
    }

    Debug::log(Debug::Severity::Info, name.c_str(), "data: %s", str.c_str());
}
//--------------------------------------------------------------------------------------------------

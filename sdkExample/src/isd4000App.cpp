//------------------------------------------ Includes ----------------------------------------------

#include "isd4000App.h"
#include "maths/maths.h"
#include "platform/debug.h"

using namespace IslSdk;

//--------------------------------------------------------------------------------------------------
Isd4000App::Isd4000App(void) : App("Isd4000App")
{
    Debug::log(Debug::Severity::Notice, name.c_str(), "created" NEW_LINE
                                                      "d -> Set settings to defualt" NEW_LINE
                                                      "s -> Save settings to file" NEW_LINE);
}
//--------------------------------------------------------------------------------------------------
Isd4000App::~Isd4000App(void)
{
}
//--------------------------------------------------------------------------------------------------
void Isd4000App::connectSignals(Device & device)
{
    Isd4000& isd4000 = reinterpret_cast<Isd4000&>(device);

    ahrs.connectSignals(isd4000.ahrs, name);
    gyro.connectSignals(isd4000.gyro, name);
    accel.connectSignals(isd4000.accel, name);
    mag.connectSignals(isd4000.mag, name);

    isd4000.onPressure.connect(slotPressure);                   // Subscribing to this event causes data to be sent from the device at the rate defined by setSensorRates()
    isd4000.onTemperature.connect(slotTemperature);             // Subscribing to this event causes data to be sent from the device at the rate defined by setSensorRates()
    isd4000.onScriptDataReceived.connect(slotScriptDataReceived);
    isd4000.onSettingsUpdated.connect(slotSettingsUpdated);
    isd4000.onPressureCalCert.connect(slotPressureCalCert);
    isd4000.onTemperatureCalCert.connect(slotTemperatureCalCert);

    Isd4000::SensorRates rates;
    rates.pressure = 100;
    rates.ahrs = 100;
    rates.gyro = 0;
    rates.accel = 0;
    rates.mag = 0;
    rates.temperature = 200;
    isd4000.setSensorRates(rates);
}
//--------------------------------------------------------------------------------------------------
void Isd4000App::disconnectSignals(Device& device)
{
    Isd4000& isd4000 = reinterpret_cast<Isd4000&>(device);

    ahrs.disconnectSignals();
    gyro.disconnectSignals();
    accel.disconnectSignals();
    mag.disconnectSignals();

    isd4000.onPressure.disconnect(slotPressure);
    isd4000.onTemperature.disconnect(slotTemperature);
    isd4000.onScriptDataReceived.disconnect(slotScriptDataReceived);
    isd4000.onSettingsUpdated.disconnect(slotSettingsUpdated);
    isd4000.onPressureCalCert.disconnect(slotPressureCalCert);
    isd4000.onTemperatureCalCert.disconnect(slotTemperatureCalCert);
}
//--------------------------------------------------------------------------------------------------
void Isd4000App::doTask(int_t key, const std::string& path)
{
    if (m_device)
    {
        Isd4000& isd4000 = reinterpret_cast<Isd4000&>(*m_device);
        switch (key)
        {
        case 'd':
            isd4000.setSettings(Isd4000::Settings(), true);
            break;

        case 's':
            isd4000.saveConfig(path + m_device->info.pnSnAsStr() + " settings.xml");
            break;

        default:
            break;
        }
        App::doTask(key, path);
    }
}
//--------------------------------------------------------------------------------------------------
void Isd4000App::callbackPressureData(Isd4000& isd4000, uint64_t timeUs, real_t pressureBar, real_t depthM, real_t pressureBarRaw)
{
    Debug::log(Debug::Severity::Info, name.c_str(), "Pressure %.5f Bar, Depth %.3f Meters", pressureBar, depthM);
}
//--------------------------------------------------------------------------------------------------
void Isd4000App::callbackTemperatureData(Isd4000& isd4000, real_t temperatureC, real_t temperatureRawC)
{
    Debug::log(Debug::Severity::Info, name.c_str(), "Temperature %.2fC", temperatureRawC);
}
//--------------------------------------------------------------------------------------------------
void Isd4000App::callbackScriptDataReceived(Isd4000& isd4000)
{
    Debug::log(Debug::Severity::Info, name.c_str(), "Script data received");
}
//--------------------------------------------------------------------------------------------------
void Isd4000App::callbackSettingsUpdated(Isd4000& isd4000, bool_t ok)
{
    if (ok)
    {
        Debug::log(Debug::Severity::Info, name.c_str(), "Settings updated ok");
    }
    else
    {
        Debug::log(Debug::Severity::Warning, name.c_str(), "Settings failed to update");
    }
}
//--------------------------------------------------------------------------------------------------
void Isd4000App::callbackPressureCal(Isd4000& isd4000, const Isd4000::PressureCal& cal)
{
    Debug::log(Debug::Severity::Info, name.c_str(), "Pressure cal received");
}
//--------------------------------------------------------------------------------------------------
void Isd4000App::callbackTemperatureCal(Isd4000& isd4000, const Isd4000::TemperatureCal& cal)
{
    Debug::log(Debug::Severity::Info, name.c_str(), "Temperature cal received");
}
//--------------------------------------------------------------------------------------------------

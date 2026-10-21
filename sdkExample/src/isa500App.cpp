//------------------------------------------ Includes ----------------------------------------------

#include "isa500App.h"
#include "maths/maths.h"
#include "platform/debug.h"

using namespace IslSdk;

//--------------------------------------------------------------------------------------------------
Isa500App::Isa500App(void) : App("Isa500App")
{
    Debug::log(Debug::Severity::Notice, name.c_str(), "created" NEW_LINE
                                                      "d -> Set settings to defualt" NEW_LINE
                                                      "s -> Save settings to file" NEW_LINE
                                                      "p -> Ping now" NEW_LINE);
}
//--------------------------------------------------------------------------------------------------
Isa500App::~Isa500App(void)
{
}
//--------------------------------------------------------------------------------------------------
void Isa500App::connectSignals(Device& device)
{
    Isa500& isa500 = reinterpret_cast<Isa500&>(device);

    ahrs.connectSignals(isa500.ahrs, name);
    gyro.connectSignals(isa500.gyro, name);
    accel.connectSignals(isa500.accel, name);
    mag.connectSignals(isa500.mag, name);

    isa500.onEcho.connect(slotEchoData);                      // Subscribing to this event causes data to be sent from the device at the rate defined by setSensorRates()
    isa500.onEchogramData.connect(slotPingData);
    isa500.onTemperature.connect(slotTemperatureData);        // Subscribing to this event causes data to be sent from the device at the rate defined by setSensorRates()
    isa500.onVoltage.connect(slotVoltageData);                // Subscribing to this event causes data to be sent from the device at the rate defined by setSensorRates()
    isa500.onTrigger.connect(slotTriggerData);
    isa500.onScriptDataReceived.connect(slotScriptDataReceived);
    isa500.onSettingsUpdated.connect(slotSettingsUpdated);

    Isa500::SensorRates rates;
    rates.ping = 0;
    rates.ahrs = 1000;
    rates.gyro = 0;
    rates.accel = 0;
    rates.mag = 0;
    rates.temperature = 0;
    rates.voltage = 0;
    isa500.setSensorRates(rates);
}
//--------------------------------------------------------------------------------------------------
void Isa500App::disconnectSignals(Device& device)
{
    Isa500& isa500 = reinterpret_cast<Isa500&>(device);

    ahrs.disconnectSignals();
    gyro.disconnectSignals();
    accel.disconnectSignals();
    mag.disconnectSignals();

    isa500.onEcho.disconnect(slotEchoData);
    isa500.onEchogramData.disconnect(slotPingData);
    isa500.onTemperature.disconnect(slotTemperatureData);
    isa500.onVoltage.disconnect(slotVoltageData);
    isa500.onTrigger.disconnect(slotTriggerData);
    isa500.onScriptDataReceived.disconnect(slotScriptDataReceived);
    isa500.onSettingsUpdated.disconnect(slotSettingsUpdated);
}
//--------------------------------------------------------------------------------------------------
void Isa500App::doTask(int_t key, const std::string& path)
{
    if (m_device)
    {
        Isa500& isa500 = reinterpret_cast<Isa500&>(*m_device);

        switch (key)
        {
        case 'd':
            isa500.setSettings(Isa500::Settings(), true);
            break;

        case 's':
            isa500.saveConfig(path + m_device->info.pnSnAsStr() + " settings.xml");
            break;

        case 'p':
            isa500.pingNow();
            break;

        default:
            break;
        }

        App::doTask(key, path);
    }
}
//--------------------------------------------------------------------------------------------------
void Isa500App::connectEvent(Device& device)
{
    Isa500& isa500 = reinterpret_cast<Isa500&>(device);
}
//--------------------------------------------------------------------------------------------------
void Isa500App::callbackEchoData(Isa500& isa500, uint64_t timeUs, uint_t selectedIdx, uint_t totalEchoCount, const std::vector<Isa500::Echo>& echoes)
{
    if (echoes.size())
    {
        // echoes.size() is limited to isa500.settings.multiEchoLimit
        // totalEchoCount is the number of received echoes and has nothing to do with the length of echoes array
        Debug::log(Debug::Severity::Info, name.c_str(), "Echo received, range %.3f meters. Total Echoes: %u", echoes[selectedIdx].totalTof * isa500.settings.speedOfSound * 0.5, totalEchoCount);
    }
    else
    {
        Debug::log(Debug::Severity::Info, name.c_str(), "No echoes received");
    }
}
//--------------------------------------------------------------------------------------------------
void Isa500App::callbackEchogramData(Isa500& isa500, const std::vector<uint8_t>& data)
{
    Debug::log(Debug::Severity::Info, name.c_str(), "Echogram data size: %u bytes", data.size());

    /*for (size_t i = 0; i < data.size(); i++)
    {
        Debug::log(Debug::Severity::Info, name.c_str(), "%u", FMT_U(data[i]));
    }*/
}
//--------------------------------------------------------------------------------------------------
void Isa500App::callbackTemperatureData(Isa500& isa500, real_t temperatureC)
{
    Debug::log(Debug::Severity::Info, name.c_str(), "Temperature %.2f", temperatureC);
}
//--------------------------------------------------------------------------------------------------
void Isa500App::callbackVoltageData(Isa500& isa500, real_t voltage12)
{
    Debug::log(Debug::Severity::Info, name.c_str(), "Voltage %.2fV", voltage12);
}
//--------------------------------------------------------------------------------------------------
void Isa500App::callbackTriggerData(Isa500& isa500, bool_t risingEdge)
{
    const char* lable[] = { "falling", "rising" };

    Debug::log(Debug::Severity::Info, name.c_str(), "Trigger, %s edge", lable[risingEdge]);
}
//--------------------------------------------------------------------------------------------------
void Isa500App::callbackScriptDataReceived(Isa500& isa500)
{
    Debug::log(Debug::Severity::Info, name.c_str(), "Script data received");
}
//--------------------------------------------------------------------------------------------------
void Isa500App::callbackSettingsUpdated(Isa500& isa500, bool_t ok)
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


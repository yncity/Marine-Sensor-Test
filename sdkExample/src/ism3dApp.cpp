//------------------------------------------ Includes ----------------------------------------------

#include "ism3dApp.h"
#include "maths/maths.h"
#include "platform/debug.h"

using namespace IslSdk;

//--------------------------------------------------------------------------------------------------
Ism3dApp::Ism3dApp(void) : App("Ism3dApp")
{
    Debug::log(Debug::Severity::Notice, name.c_str(), "created" NEW_LINE
                                                      "d -> Set settings to defualt" NEW_LINE
                                                      "s -> Save settings to file" NEW_LINE);
}
//--------------------------------------------------------------------------------------------------
Ism3dApp::~Ism3dApp(void)
{
}
//--------------------------------------------------------------------------------------------------
void Ism3dApp::connectSignals(Device& device)
{
    Ism3d& ism3d = reinterpret_cast<Ism3d&>(device);

    ahrs.connectSignals(ism3d.ahrs, name);
    gyro.connectSignals(ism3d.gyro, name);
    accel.connectSignals(ism3d.accel, name);
    mag.connectSignals(ism3d.mag, name);
    gyro2.connectSignals(ism3d.gyroSec, name);
    accel2.connectSignals(ism3d.accelSec, name);

    ism3d.onScriptDataReceived.connect(slotScriptDataReceived);
    ism3d.onSettingsUpdated.connect(slotSettingsUpdated);

    Ism3d::SensorRates rates;
    rates.ahrs = 100;
    rates.gyro = 0;
    rates.accel = 0;
    rates.mag = 0;
    ism3d.setSensorRates(rates);
}
//--------------------------------------------------------------------------------------------------
void Ism3dApp::disconnectSignals(Device& device)
{
    Ism3d& ism3d = reinterpret_cast<Ism3d&>(device);

    ahrs.disconnectSignals();
    gyro.disconnectSignals();
    accel.disconnectSignals();
    mag.disconnectSignals();
    gyro2.disconnectSignals();
    accel2.disconnectSignals();

    ism3d.onScriptDataReceived.disconnect(slotScriptDataReceived);
    ism3d.onSettingsUpdated.disconnect(slotSettingsUpdated);
}
//--------------------------------------------------------------------------------------------------
void Ism3dApp::doTask(int_t key, const std::string& path)
{
    if (m_device)
    {
        Ism3d& ism3d = reinterpret_cast<Ism3d&>(*m_device);

        switch (key)
        {
        case 'd':
            ism3d.setSettings(Ism3d::Settings(), true);
            break;

        case 's':
            ism3d.saveConfig(path + m_device->info.pnSnAsStr() + " settings.xml");
            break;

        default:
            break;
        }

        App::doTask(key, path);
    }
}
//--------------------------------------------------------------------------------------------------
void Ism3dApp::callbackScriptDataReceived(Ism3d& ism3d)
{
    Debug::log(Debug::Severity::Info, name.c_str(), "Script data received");
}
//--------------------------------------------------------------------------------------------------
void Ism3dApp::callbackSettingsUpdated(Ism3d& ism3d, bool_t ok)
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

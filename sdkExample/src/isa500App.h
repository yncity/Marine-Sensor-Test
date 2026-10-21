#ifndef ISA500APP_H_
#define ISA500APP_H_

//------------------------------------------ Includes ----------------------------------------------

#include "app.h"
#include "devices/isa500.h"
#include "imuManager.h"

//--------------------------------------- Class Definition -----------------------------------------

namespace IslSdk
{
    class Isa500App : public App
    {
    public:
        Isa500App(void);
        ~Isa500App(void);
        void connectSignals(Device& device) override;
        void disconnectSignals(Device& device) override;
        void doTask(int_t key, const std::string& path) override;

        Slot<Isa500&, uint64_t, uint_t, uint_t, const std::vector<Isa500::Echo>&> slotEchoData{ this, &Isa500App::callbackEchoData };
        Slot<Isa500&, const std::vector<uint8_t>&> slotPingData{ this, &Isa500App::callbackEchogramData };
        Slot<Isa500&, real_t> slotTemperatureData{ this, &Isa500App::callbackTemperatureData };
        Slot<Isa500&, real_t> slotVoltageData{ this, &Isa500App::callbackVoltageData };
        Slot<Isa500&, bool_t> slotTriggerData{ this, &Isa500App::callbackTriggerData };
        Slot<Isa500&> slotScriptDataReceived{ this, &Isa500App::callbackScriptDataReceived };
        Slot<Isa500&, bool_t> slotSettingsUpdated{ this, &Isa500App::callbackSettingsUpdated };

    private:
        AhrsManager ahrs;
        GyroManager gyro;
        AccelManager accel;
        MagManager mag;

        void connectEvent(Device& device);
        void callbackEchoData(Isa500& isa500, uint64_t timeUs, uint_t selectedIdx, uint_t totalEchoCount, const std::vector<Isa500::Echo>& echoes);
        void callbackEchogramData(Isa500& isa500, const std::vector<uint8_t>& data);
        void callbackTemperatureData(Isa500& isa500, real_t temperatureC);
        void callbackVoltageData(Isa500& isa500, real_t voltage12);
        void callbackTriggerData(Isa500& isa500, bool_t risingEdge);
        void callbackScriptDataReceived(Isa500& isa500);
        void callbackSettingsUpdated(Isa500& isa500, bool_t ok);
    };
}

//--------------------------------------------------------------------------------------------------
#endif

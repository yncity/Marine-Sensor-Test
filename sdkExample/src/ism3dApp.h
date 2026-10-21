#ifndef ISM3DAPP_H_
#define ISM3DAPP_H_

//------------------------------------------ Includes ----------------------------------------------

#include "app.h"
#include "devices/ism3d.h"
#include "imuManager.h"

//--------------------------------------- Class Definition -----------------------------------------

namespace IslSdk
{
    class Ism3dApp : public App
    {
    public:
        Ism3dApp(void);
        ~Ism3dApp(void);
        void connectSignals(Device& device) override;
        void disconnectSignals(Device& device) override;
        void doTask(int_t key, const std::string& path) override;

        Slot<Ism3d&> slotScriptDataReceived{ this, & Ism3dApp::callbackScriptDataReceived };
        Slot<Ism3d&, bool_t> slotSettingsUpdated{ this, & Ism3dApp::callbackSettingsUpdated };

    private:
        AhrsManager ahrs;
        GyroManager gyro;
        AccelManager accel;
        MagManager mag;
        GyroManager gyro2;
        AccelManager accel2;

        void callbackScriptDataReceived(Ism3d& ism3d);
        void callbackSettingsUpdated(Ism3d& ism3d, bool_t ok);
    };
}

//--------------------------------------------------------------------------------------------------
#endif

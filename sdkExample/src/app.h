#ifndef APP_H_
#define APP_H_

//------------------------------------------ Includes ----------------------------------------------

#include "devices/device.h"

//--------------------------------------- Class Definition -----------------------------------------

namespace IslSdk
{
    class App
    {
    public:
        App(const std::string& name);
        ~App();
        void setDevice(const Device::SharedPtr& device);
        virtual void doTask(int_t key, const std::string& path);

        const std::string name;

        Slot<Device&, const std::string&> slotError{ this, &App::callbackError };
        Slot<Device&> slotDelete{ this, &App::callbackDeleteted };
        Slot<Device&> slotConnect{ this, &App::callbackConnect };
        Slot<Device&> slotDisconnect{ this, &App::callbackDisconnect };
        Slot<Device&, SysPort&, const ConnectionMeta&> slotPortAdded{ this, &App::callbackPortAdded };
        Slot<Device&, SysPort&, const ConnectionMeta&> slotPortChanged{ this, &App::callbackPortChanged };
        Slot<Device&, SysPort&> slotPortRemoved{ this, &App::callbackPortRemoved };
        Slot<Device&, const Device::Info&> slotDeviceInfo{ this, &App::callbackDeviceInfo };
        Slot<Device&, uint_t, uint_t, uint_t, uint_t> slotPacketCount{ this, &App::callbackPacketCount };

    protected:
        Device::SharedPtr m_device;
        virtual void connectSignals(Device& device) {};
        virtual void disconnectSignals(Device& device) {};
        virtual void connectEvent(Device& device) {};

    private:
        void callbackError(Device& device, const std::string& msg);
        void callbackDeleteted(Device& device);
        void callbackConnect(Device& device);
        void callbackDisconnect(Device& device);
        void callbackPortAdded(Device& device, SysPort& port, const ConnectionMeta& meta);
        void callbackPortChanged(Device& device, SysPort& port, const ConnectionMeta& meta);
        void callbackPortRemoved(Device& device, SysPort& port);
        void callbackDeviceInfo(Device& device, const Device::Info& info);
        void callbackPacketCount(Device& device, uint_t tx, uint_t rx, uint_t resent, uint_t missed);
    };
}

//--------------------------------------------------------------------------------------------------
#endif

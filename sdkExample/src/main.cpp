//------------------------------------------ Includes ----------------------------------------------

// SDK includes
#include "sdk.h" // The main SDK header. This include most of the SDK functionality
#include "platform/debug.h"
#include "utils/utils.h"

// sdkExample includes
#include "platform.h"
#include "isa500App.h"
#include "isd4000App.h"
#include "ism3dApp.h"
#include "sonarApp.h"
#include "gpsApp.h"
#include "opencv2/highgui.hpp"
#include "sonarCommandSocket.h"
#include <cstring>
#include <csignal>
#include <string>
#include <chrono>
#include <cstdlib>

using namespace IslSdk;

//------------------------------------------- Globals ----------------------------------------------

std::vector<App *> apps;
std::shared_ptr<GpsApp> gpsApp;

// These functions are the callbacks.
void newPort(const SysPort::SharedPtr &sysPort);
void newDevice(const Device::SharedPtr &device, const SysPort::SharedPtr &sysPort, const ConnectionMeta &meta);
void newNmeaDevice(const NmeaDevice::SharedPtr &device, const SysPort::SharedPtr &sysPort, const ConnectionMeta &meta);
void portOpen(SysPort &sysPort, bool_t failed);
void portClosed(SysPort &sysPort);
void portStats(SysPort &sysPort, uint_t txBytes, uint_t rxBytes, uint_t badPackets);
void portDiscoveryStarted(SysPort &sysPort, AutoDiscovery::Type type);
void portDiscoveryEvent(SysPort &sysPort, const ConnectionMeta &, AutoDiscovery::Type type, uint_t discoveryCount);
void portDiscoveryFinished(SysPort &sysPort, AutoDiscovery::Type type, uint_t discoveryCount, bool_t wasCancelled);
void portData(SysPort &sysPort, const uint8_t *data, uint_t size);
void portDeleted(SysPort &sysPort);
static bool g_serviceMode = false;
static volatile std::sig_atomic_t g_stopRequested = 0;

static void handleStopSignal(int)
{
    g_stopRequested = 1;
}

// These slots are used to connect to the SDK signals. Each slot is initialised with the function to call.
Slot<const SysPort::SharedPtr &> slotNewPort(&newPort);
Slot<const Device::SharedPtr &, const SysPort::SharedPtr &, const ConnectionMeta &> slotNewDevice(&newDevice);
Slot<const NmeaDevice::SharedPtr &, const SysPort::SharedPtr &, const ConnectionMeta &> slotNewNmeaDevice(&newNmeaDevice);
Slot<SysPort &, bool_t> slotPortOpen(&portOpen);
Slot<SysPort &> slotPortClosed(&portClosed);
Slot<SysPort &, uint_t, uint_t, uint_t> slotPortStats(&portStats);
Slot<SysPort &, AutoDiscovery::Type> slotPortDiscoveryStarted(&portDiscoveryStarted);
Slot<SysPort &, const ConnectionMeta &, AutoDiscovery::Type, uint_t> slotPortDiscoveryEvent(&portDiscoveryEvent);
Slot<SysPort &, AutoDiscovery::Type, uint_t, bool_t> slotPortDiscoveryFinished(&portDiscoveryFinished);
Slot<SysPort &, const uint8_t *, uint_t> slotPortData(&portData);
Slot<SysPort &> slotPortDeleted(&portDeleted);

//-------------------------------------------------------------------------------------------------
int main(int argc, char **argv)
{
    g_serviceMode = (argc > 1 && std::string(argv[1]) == "--service");

    std::signal(SIGTERM, handleStopSignal);
    std::signal(SIGINT, handleStopSignal);

    if (!g_serviceMode)
    {
        Platform::setTerminalMode();
    }
    const std::string appPath = Platform::getExePath(argv[0]);
    Sdk sdk; // Create the SDK instance

    Debug::log(Debug::Severity::Notice, "Main", "Impact Subsea SDK version %s    press\033[31m x\033[36m to exit", sdk.version.c_str());
    Platform::sleepMs(1000);

    sdk.ports.onNew.connect(slotNewPort);             // Connect to the new port signal
    sdk.devices.onNew.connect(slotNewDevice);         // Connect to the new device signal
    sdk.nmeaDevices.onNew.connect(slotNewNmeaDevice); // Connect to the new NMEA device signal

    // Serial over Lan ports can be created using the following function, change the IP address and port to suit your needs
    // sdk.ports.createSol("SOL1", false, true, Utils::ipToUint(192, 168, 1, 215), 1001);

    SonarCommandSocket sonarCommandSocket;

    if (g_serviceMode && !sonarCommandSocket.start())
    {
        return EXIT_FAILURE;
    }

    const auto sonarServiceStartedAt =
        std::chrono::steady_clock::now();
    bool exitRequested = false;

    while (!exitRequested && !g_stopRequested)
    {
        Platform::sleepMs(40);

        // SDK 데이터 및 이벤트 처리
        sdk.run();

        if (g_serviceMode)
        {
            sonarCommandSocket.poll([&](char key) -> std::string
                                    {

        // Sonar health query
        if (key == '?')
        {
            for (App *app : apps)
            {
                SonarApp *sonar =
                    dynamic_cast<SonarApp *>(app);

                if (sonar != nullptr)
                {
                    return sonar->healthReport();
                }
            }

            // No Sonar instance discovered yet
            const auto elapsed =
                std::chrono::steady_clock::now()
                - sonarServiceStartedAt;

            if (elapsed >= std::chrono::seconds(20))
            {
                return "SONAR STATE: ERROR\n"
                    "ERROR: DISCOVERY_TIMEOUT\n";
            }

            return "SONAR STATE: STARTING\n"
                "ERROR: NONE\n";
        }
        // 서비스 모드에서 허용된 명령만 처리
        if (std::strchr("relv12345678ptcza", key) == nullptr)
        {
            return "DENIED: key not permitted in Sonar service mode\n";
        }

        // GUI 실행 환경 확인
        if (key == 'z')
        {
            const char *display = std::getenv("DISPLAY");
            const char *wayland = std::getenv("WAYLAND_DISPLAY");

            if ((!display || !*display) &&
                (!wayland || !*wayland))
            {
                return "GUI_UNAVAILABLE: no display environment\n";
            }
        }

        bool sonarFound = false;

        for (App *app : apps)
        {
            SonarApp *sonar =
                dynamic_cast<SonarApp *>(app);

            if (sonar != nullptr)
            {
                sonarFound = true;

                // 기존 Sonar 키보드 명령 처리 함수 재사용
                sonar->doTask(
                    static_cast<int_t>(key),
                    appPath
                );
            }
        }

        if (!sonarFound)
        {
            return "SONAR_NOT_READY: no Sonar instance discovered\n";
        }

        return std::string("DISPATCHED: Sonar key '")
            + key
            + "' (hardware success not verified)\n"; });
        }

        // 터미널 키보드 입력 처리
        if (!g_serviceMode && Platform::keyboardPressed())
        {
            int_t key = Platform::getKey();

            // x = 종료
            // 27 = ESC
            // 3 = Ctrl+C (Raw 터미널)
            if (key == 'x' || key == 27 || key == 3)
            {
                exitRequested = true;
            }
            else
            {
                for (App *app : apps)
                {
                    app->doTask(key, appPath);
                }
            }
        }

        if (exitRequested)
            break;

        // 소나 GUI 갱신 및 ESC 입력 처리
        for (App *app : apps)
        {
            SonarApp *sonarApp =
                dynamic_cast<SonarApp *>(app);

            if (sonarApp != nullptr)
            {
                if (sonarApp->updateViewer(appPath))
                {
                    if (g_serviceMode)
                    {
                        // 서비스 모드에서는 GUI만 종료
                        sonarApp->doTask('a', appPath);
                    }
                    else
                    {
                        // 일반 실행에서는 기존처럼 프로그램 종료
                        exitRequested = true;
                        break;
                    }
                }
            }
        }
    }

    // 프로그램 종료 전 녹화 및 GUI 자원 정리
    for (App *app : apps)
    {
        SonarApp *sonarApp =
            dynamic_cast<SonarApp *>(app);

        if (sonarApp != nullptr)
        {
            sonarApp->shutdown();
        }
    }

    // 모든 OpenCV 창 정리
    if (!g_serviceMode)
    {
        cv::destroyAllWindows();
    }

    return 0;
}
//--------------------------------------------------------------------------------------------------
// This function is called when a new port is found. It's address has been initialised inside the slot class defined above.
void newPort(const SysPort::SharedPtr &sysPort)
{
    // Connect to the port signals
    sysPort->onOpen.connect(slotPortOpen);
    sysPort->onClose.connect(slotPortClosed);
    sysPort->onPortStats.connect(slotPortStats);
    sysPort->onDiscoveryStarted.connect(slotPortDiscoveryStarted);
    sysPort->onDiscoveryEvent.connect(slotPortDiscoveryEvent);
    sysPort->onDiscoveryFinished.connect(slotPortDiscoveryFinished);
    sysPort->onDelete.connect(slotPortDeleted);

    Debug::log(Debug::Severity::Info, "Main", "Found new SysPort %s", sysPort->name.c_str());

    /*
    To find devices connected to the port, we need to start discovery. There are a few virtual and
    overloaded functions that can be used to do this. The simplest is to call the virtual function in
    the SysPort base class sysPort->discoverIslDevices() which will start discovery with default parameters.
    The overloaded version of this function can be used to specify the PID, part number and serial number
    (found on the device label). This only need to be specified in RS485 multi-drop networks.
    eg.
        sysPort->discoverIslDevices();
        sysPort->discoverIslDevices(0, 1336, 135);        // a value of 0 means any PID, part number or serial number
     */

    // The following code will start discovery on the port depending on the port type.
    // It casts the SysPort to the derived type and call the discover function for that type.

    if (sysPort->type == SysPort::Type::Net)
    {
        NetPort &port = reinterpret_cast<NetPort &>(*sysPort);

        if (sysPort->name == "NETWORK")
        {
            uint32_t ipAddress = Utils::ipToUint(192, 168, 1, 255);
            port.discoverIslDevices(0xffff, 0xffff, 0xffff, ipAddress, 33005, 2000); // These are the default parameters, same as port.discoverIslDevices()
        }
    }
    else if (sysPort->type == SysPort::Type::Serial)
    {
        UartPort &port = reinterpret_cast<UartPort &>(*sysPort);
        // This call is the same as calling port.discoverIslDevices(pid, pn, sn, baudrate, timeout) multiple times with different baudrates
        port.discoverIslDevices();

        // To discover NMEA devices, call port.discoverNmeaDevices() or port.discoverNmeaDevices(baudrate, timeout)
        // calling this function will stop discovery of ISL devices
    }
}
//--------------------------------------------------------------------------------------------------
// This function is called when a new Device is found. It's address has been initialised inside the slot class defined above.
void newDevice(const Device::SharedPtr &device, const SysPort::SharedPtr &sysPort, const ConnectionMeta &meta)
{
    App *app = nullptr;

    switch (device->info.pid)
    {
    case Device::Pid::Isa500:
        Debug::log(Debug::Severity::Notice, "Main", "Found ISA500 altimeter %04u.%04u on port %s", FMT_U(device->info.pn), FMT_U(device->info.sn), sysPort->name.c_str());
        app = new Isa500App();
        break;

    case Device::Pid::Isd4000:
        Debug::log(Debug::Severity::Notice, "Main", "Found ISD4000 depth sensor %04u.%04u on port %s", FMT_U(device->info.pn), FMT_U(device->info.sn), sysPort->name.c_str());
        app = new Isd4000App();
        break;

    case Device::Pid::Ism3d:
        Debug::log(Debug::Severity::Notice, "Main", "Found ISM3D ahrs sensor %04u.%04u on port %s", FMT_U(device->info.pn), FMT_U(device->info.sn), sysPort->name.c_str());
        app = new Ism3dApp();
        break;

    case Device::Pid::Sonar:
        Debug::log(Debug::Severity::Notice, "Main", "Found Sonar %04u.%04u on port %s", FMT_U(device->info.pn), FMT_U(device->info.sn), sysPort->name.c_str());
        app = new SonarApp(g_serviceMode);
        break;

    default:
        Debug::log(Debug::Severity::Notice, "Main", "Found device %04u.%04u on port %s", device->info.pn, device->info.sn, sysPort->name.c_str());
        app = new App("Device");
        break;
    }

    if (app)
    {
        app->setDevice(device);
        apps.push_back(app);

        device->connect();
    }
}
//--------------------------------------------------------------------------------------------------
// This function is called when a new Nmea device is found. It's address has been initialised inside the slot class defined above.
void newNmeaDevice(const NmeaDevice::SharedPtr &device, const SysPort::SharedPtr &sysPort, const ConnectionMeta &meta)
{
    if (device->type == NmeaDevice::Type::Gps)
    {
        Debug::log(Debug::Severity::Notice, "Main", "Found GPS device on port %s", sysPort->name.c_str());
        gpsApp = std::make_shared<GpsApp>("GPS");
        gpsApp->setDevice(device);
    }
}
//--------------------------------------------------------------------------------------------------
void portOpen(SysPort &sysPort, bool_t isOpen)
{
    if (isOpen)
    {
        Debug::log(Debug::Severity::Info, "Main", "%s open", sysPort.name.c_str());
    }
    else
    {
        Debug::log(Debug::Severity::Warning, "Main", "%s failed to open", sysPort.name.c_str());
    }
}
//--------------------------------------------------------------------------------------------------
void portClosed(SysPort &sysPort)
{
    Debug::log(Debug::Severity::Info, "Main", "%s closed", sysPort.name.c_str());
}
//--------------------------------------------------------------------------------------------------
void portStats(SysPort &sysPort, uint_t txBytes, uint_t rxBytes, uint_t badPackets)
{
    // Debug::log(Debug::Severity::Info, "Main", "%s TX:%u, RX:%u, Bad packets:%u", sysPort.name.c_str(), FMT_U(txBytes), FMT_U(rxBytes), FMT_U(badPackets));
}
//--------------------------------------------------------------------------------------------------
void portDiscoveryStarted(SysPort &sysPort, AutoDiscovery::Type type)
{
    Debug::log(Debug::Severity::Info, "Main", "%s discovery started", sysPort.name.c_str());
}
//--------------------------------------------------------------------------------------------------
void portDiscoveryEvent(SysPort &sysPort, const ConnectionMeta &meta, AutoDiscovery::Type type, uint_t discoveryCount)
{
    if (sysPort.type == SysPort::Type::Net)
    {
        uint_t ip = meta.ipAddress;
        Debug::log(Debug::Severity::Info, "Main", "%s Discovering at IP %u.%u.%u.%u:%u", sysPort.name.c_str(), ip & 0xff, (ip >> 8) & 0xff, (ip >> 16) & 0xff, (ip >> 24) & 0xff, meta.port);
    }
    else
    {
        Debug::log(Debug::Severity::Info, "Main", "%s Discovering at baudrate %u", sysPort.name.c_str(), meta.baudrate);
    }
}
//--------------------------------------------------------------------------------------------------
void portDiscoveryFinished(SysPort &sysPort, AutoDiscovery::Type type, uint_t discoveryCount, bool_t wasCancelled)
{
    Debug::log(Debug::Severity::Info, "Main", "%s Discovery Finished", sysPort.name.c_str());
}
//--------------------------------------------------------------------------------------------------
void portData(SysPort &sysPort, const uint8_t *data, uint_t size)
{
    Debug::log(Debug::Severity::Info, "Main", "Port data, size: %u", size);
}
//--------------------------------------------------------------------------------------------------
void portDeleted(SysPort &sysPort)
{
    Debug::log(Debug::Severity::Warning, "Main", "Port %s deleted", sysPort.name.c_str());
}
//--------------------------------------------------------------------------------------------------

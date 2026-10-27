// Three-terminal Sonar health IPC mock. Does not touch real sonar hardware.
// Build from controller/: c++ -std=c++11 -O2 -Wall -Wextra -Werror sonar_health_ipc_mock.cpp -o sonar_health_ipc_mock
#include "../sdkExample/src/sonarHealth.h"
#include "../sdkExample/src/sonarCommandSocket.h"
#include <csignal>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

static volatile std::sig_atomic_t running = 1;
static void requestStop(int) { running = 0; }

int main()
{
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
    SonarHealth health;
    health.onConnected(); // Fake device connection.
    SonarCommandSocket socket;
    if (!socket.start())
        return 1;
    std::cout << "SONAR_HEALTH_IPC_MOCK_READY (commands: ?, r, e, c; Ctrl+C to stop)\n";
    while (running)
    {
        socket.poll([&](char key) -> std::string {
            if (key == '?')
                return health.report();
            if (key == 'r')
            {
                health.onScanRequested();
                health.onPing(); // Fake one ping, for IPC test only.
                return "MOCK_SCAN_START_AND_PING\n";
            }
            if (key == 'e')
            {
                health.onScanStopped();
                return "MOCK_SCAN_STOP\n";
            }
            if (key == 'c')
            {
                health.onHeadSync(true);
                return "MOCK_HEAD_SYNC_OK\n";
            }
            return "MOCK_IGNORED\n";
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    socket.stop();
    return 0;
}

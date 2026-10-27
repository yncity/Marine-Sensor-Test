// Offline state-machine test, no sonar hardware/SDK required.
// Build from repository's controller/ dir:
//   c++ -std=c++11 -O2 -Wall -Wextra -Werror sonar_health_mock.cpp -o sonar_health_mock
#include "../sdkExample/src/sonarHealth.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>

static void expect(const SonarHealth &h, SonarHealth::TimePoint when,
                   const char *state, const char *error)
{
    const std::string text = h.report(when);
    assert(text.find(std::string("SONAR STATE: ") + state + "\n") != std::string::npos);
    assert(text.find(std::string("ERROR: ") + error + "\n") != std::string::npos);
    std::cout << text << "---\n";
}

int main()
{
    using namespace std::chrono;
    const SonarHealth::TimePoint t0 = SonarHealth::Clock::now();
    SonarHealth h(t0);
    expect(h, t0, "STARTING", "NONE");
    expect(h, t0 + seconds(21), "ERROR", "CONNECTION_TIMEOUT");
    h.onConnected(t0 + seconds(22));
    expect(h, t0 + seconds(22), "CONNECTED", "NONE");
    h.onScanRequested(t0 + seconds(23));
    expect(h, t0 + seconds(23), "SCANNING", "NONE");
    expect(h, t0 + seconds(39), "ERROR", "PING_TIMEOUT");
    h.onPing(t0 + seconds(40));
    expect(h, t0 + seconds(40), "RUNNING", "NONE");
    h.onHeadSync(false);
    expect(h, t0 + seconds(41), "DEGRADED", "HEAD_SYNC_ERROR");
    h.onMotorSlip();
    expect(h, t0 + seconds(42), "DEGRADED", "MOTOR_SLIP");
    h.onHeadSync(true);
    expect(h, t0 + seconds(43), "RUNNING", "NONE");
    expect(h, t0 + seconds(51), "ERROR", "PING_TIMEOUT");
    h.onScanStopped();
    expect(h, t0 + seconds(52), "CONNECTED", "NONE");
    h.onScanRequested(t0 + seconds(53));
    h.onDisconnected();
    expect(h, t0 + seconds(54), "ERROR", "CONNECTION_LOST");
    std::cout << "SONAR_HEALTH_MOCK_PASS\n";
}

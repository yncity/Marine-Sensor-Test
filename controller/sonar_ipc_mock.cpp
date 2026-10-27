#include "../sdkExample/src/sonarCommandSocket.h"
#include <csignal>
#include <cstdio>

static volatile sig_atomic_t running = 1;
static void stop(int) { running = 0; }

int main()
{
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    SonarCommandSocket sock;
    if (!sock.start())
        return 1;
    std::puts("Mock Sonar started (no actual sensor required)");
    while (running)
    {
        sock.poll([](char key) -> std::string
                  {
            std::printf("MOCK: Sonar received key '%c'\n", key);
            std::fflush(stdout);
            return std::string("MOCK_RECEIVED: ") + key + "\n"; });
        usleep(40000);
    }
    return 0;
}

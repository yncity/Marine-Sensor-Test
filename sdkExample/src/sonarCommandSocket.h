#ifndef MARINE_SONAR_COMMAND_SOCKET_H
#define MARINE_SONAR_COMMAND_SOCKET_H

// 3-B-1: UNIX socket command receiver for Sonar; header-only, C++11, macOS/Linux.
// Called from the SDK main loop: never accesses SDK objects from another thread.

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>

class SonarCommandSocket
{
public:
    SonarCommandSocket() : server_(-1) {}
    ~SonarCommandSocket() { stop(); }

    SonarCommandSocket(const SonarCommandSocket &) = delete;
    SonarCommandSocket &operator=(const SonarCommandSocket &) = delete;

    static std::string socketPath()
    {
        const char *overridePath = std::getenv("MARINE_SONAR_SOCKET");
        if (overridePath && overridePath[0])
            return overridePath;
        return "/tmp/marine-sonar-" + std::to_string(static_cast<unsigned long>(getuid())) + ".sock";
    }

    bool start()
    {
        if (server_ >= 0)
            return true;
        path_ = socketPath();
        sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        if (path_.size() >= sizeof(addr.sun_path))
        {
            std::fprintf(stderr, "Sonar IPC socket path too long: %s\n", path_.c_str());
            return false;
        }
        std::memcpy(addr.sun_path, path_.c_str(), path_.size() + 1);

        server_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (server_ < 0)
        {
            std::perror("Sonar IPC socket");
            return false;
        }
        const int flags = fcntl(server_, F_GETFL, 0);
        if (flags < 0 || fcntl(server_, F_SETFL, flags | O_NONBLOCK) < 0)
        {
            std::perror("Sonar IPC nonblock");
            close(server_);
            server_ = -1;
            return false;
        }

        // No automatic unlink of an existing socket: never disrupt a running server.
        // A stale socket left by a crash must be investigated/removed after confirming
        // that no sdkExample process owns it.
        const mode_t old = umask(0077);
        const int bindResult = bind(server_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        umask(old);
        if (bindResult != 0)
        {
            std::perror("Sonar IPC bind (already running or stale socket?)");
            close(server_);
            server_ = -1;
            return false;
        }
        if (listen(server_, 8) != 0)
        {
            std::perror("Sonar IPC listen");
            stop();
            return false;
        }
        std::printf("Sonar IPC listening: %s\n", path_.c_str());
        std::fflush(stdout);
        return true;
    }

    // Poll exactly once per SDK loop iteration. The handler is invoked on the
    // SDK's own thread after a single ASCII byte has arrived.
    void poll(const std::function<std::string(char)> &handler)
    {
        if (server_ < 0)
            return;
        int client = accept(server_, nullptr, nullptr);
        if (client < 0)
            return; // EAGAIN: no pending command; do not block SDK loop

        timeval limit = {};
        limit.tv_usec = 200000; // protect the SDK loop from stalled local clients
        (void)setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit));
        (void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit));

#if defined(SO_NOSIGPIPE)
        int suppressSigpipe = 1;
        (void)setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE,
                         &suppressSigpipe, sizeof(suppressSigpipe));
#endif

        char command = '\0';
        const ssize_t received = recv(client, &command, 1, 0);
        std::string response = (received == 1) ? handler(command) : "ERROR: empty command\n";
        if (response.empty())
            response = "ERROR: empty response\n";
#if defined(MSG_NOSIGNAL)
        (void)send(client, response.c_str(), response.size(), MSG_NOSIGNAL);
#else
        (void)send(client, response.c_str(), response.size(), 0);
#endif
        close(client);
    }

    void stop()
    {
        if (server_ >= 0)
        {
            close(server_);
            server_ = -1;
            if (!path_.empty())
                (void)unlink(path_.c_str());
        }
    }

private:
    int server_;
    std::string path_;
};

#endif

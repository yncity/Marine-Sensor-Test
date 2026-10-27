#ifndef MARINE_SONAR_HEALTH_H
#define MARINE_SONAR_HEALTH_H

// Sonar application health snapshot; C++11, no SDK dependency.
// All updates and report() should run on sdkExample's SDK/event thread.
#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>

class SonarHealth
{
public:
    typedef std::chrono::steady_clock Clock;
    typedef Clock::time_point TimePoint;

    // Provisional thresholds; tune against the real sonar's scan rate.
    static const int CONNECTION_TIMEOUT_SECONDS = 20;
    static const int FIRST_PING_TIMEOUT_SECONDS = 15;
    static const int PING_STALE_TIMEOUT_SECONDS = 10;

    explicit SonarHealth(TimePoint now = Clock::now())
        : startedAt_(now), scanRequestedAt_(now), lastPingAt_(now) {}

    void onConnected(TimePoint now = Clock::now())
    {
        connected_ = true;
        wasConnected_ = true;
        scanRequested_ = false;
        hasPing_ = false;
        headSyncKnown_ = false;
        headSyncOk_ = false;
        motorSlip_ = false;
        startedAt_ = now;
    }

    void onDisconnected()
    {
        connected_ = false;
        scanRequested_ = false;
        hasPing_ = false;
    }

    void onScanRequested(TimePoint now = Clock::now())
    {
        scanRequested_ = true;
        hasPing_ = false;
        scanRequestedAt_ = now;
    }

    void onScanStopped()
    {
        scanRequested_ = false;
        hasPing_ = false;
    }

    void onPing(TimePoint now = Clock::now())
    {
        // A late ping after 'e' is counted, but must not switch scan state to RUNNING.
        ++pingCount_;
        if (connected_ && scanRequested_)
        {
            hasPing_ = true;
            lastPingAt_ = now;
        }
    }

    void onHeadSync(bool ok)
    {
        headSyncKnown_ = true;
        headSyncOk_ = ok;
        if (ok)
            motorSlip_ = false; // Confirmed re-synchronization clears slip warning.
    }

    void onMotorSlip()
    {
        motorSlip_ = true;
    }

    std::string report(TimePoint now = Clock::now()) const
    {
        const char *state = "STARTING";
        const char *error = "NONE";

        if (!connected_)
        {
            if (wasConnected_)
            {
                state = "ERROR";
                error = "CONNECTION_LOST";
            }
            else if (ageSeconds(startedAt_, now) >= CONNECTION_TIMEOUT_SECONDS)
            {
                state = "ERROR";
                error = "CONNECTION_TIMEOUT";
            }
        }
        else if (!scanRequested_)
        {
            state = "CONNECTED"; // Idle, not scanning intentionally.
        }
        else if (!hasPing_)
        {
            if (ageSeconds(scanRequestedAt_, now) >= FIRST_PING_TIMEOUT_SECONDS)
            {
                state = "ERROR";
                error = "PING_TIMEOUT";
            }
            else
            {
                state = "SCANNING"; // Start requested, first ping pending.
            }
        }
        else if (ageSeconds(lastPingAt_, now) >= PING_STALE_TIMEOUT_SECONDS)
        {
            state = "ERROR";
            error = "PING_TIMEOUT";
        }
        else if (motorSlip_ || (headSyncKnown_ && !headSyncOk_))
        {
            state = "DEGRADED";
            error = motorSlip_ ? "MOTOR_SLIP" : "HEAD_SYNC_ERROR";
        }
        else
        {
            state = "RUNNING"; // Recent pings; head sync can still be UNKNOWN.
        }

        std::ostringstream text;
        text << "SONAR STATE: " << state << '\n'
             << "CONNECTED: " << (connected_ ? "yes" : "no") << '\n'
             << "SCAN_REQUESTED: " << (scanRequested_ ? "yes" : "no") << '\n'
             << "PING_COUNT: " << pingCount_ << '\n'
             << "PING_AGE_MS: ";
        if (connected_ && scanRequested_ && hasPing_)
            text << std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPingAt_).count();
        else
            text << "N/A";
        text << "\nHEAD_SYNC: ";
        if (!headSyncKnown_)
            text << "UNKNOWN";
        else
            text << (headSyncOk_ ? "OK" : "ERROR");
        text << "\nMOTOR_SLIP: " << (motorSlip_ ? "yes" : "no")
             << "\nERROR: " << error << '\n';
        return text.str();
    }

private:
    static long long ageSeconds(TimePoint since, TimePoint now)
    {
        if (now < since)
            return 0;
        return std::chrono::duration_cast<std::chrono::seconds>(now - since).count();
    }

    bool connected_ = false;
    bool wasConnected_ = false;
    bool scanRequested_ = false;
    bool hasPing_ = false;
    bool headSyncKnown_ = false;
    bool headSyncOk_ = false;
    bool motorSlip_ = false;
    std::uint64_t pingCount_ = 0;
    TimePoint startedAt_;
    TimePoint scanRequestedAt_;
    TimePoint lastPingAt_;
};

#endif


#include "logging/logPlayer.h"
#include "devices/sonar.h"
#include "helpers/sonarDataStore.h"
#include "helpers/sonarImage.h"
#include "helpers/palette.h"

#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <cmath>

using namespace IslSdk;

class SonarReplay
{
public:
    SonarReplay()
        : m_trackSlot(this, &SonarReplay::onNewTrack),
          m_pingSlot(this, &SonarReplay::onPing),
          m_settingsSlot(this, &SonarReplay::onSettings)
    {
        m_image.setBuffer(1000, 1000, true);
        m_player.onNewTrack.connect(m_trackSlot);
    }

    bool open(const std::string& filename)
    {
        if (!m_player.open(filename))
        {
            std::cerr << "Failed to open: "
                      << filename << std::endl;
            return false;
        }

        if (!m_sonar)
        {
            std::cerr << "No ISS360 sonar track found."
                      << std::endl;
            m_player.close();
            return false;
        }

        return true;
    }

    void run()
    {
        cv::namedWindow(
            "ISS360 Log Replay",
            cv::WINDOW_AUTOSIZE
        );

        // 원래 기록 속도
        m_player.play(1.0);

        std::cout << "Replay started. ESC = Exit"
                  << std::endl;

        bool running = true;

        while (running)
        {
            // 저장된 로그 데이터를 시간 순서대로 처리
            m_player.process();

            // 새 Ping 데이터가 있을 때 영상 갱신
            if (m_imageDirty)
            {
                renderFrame();
                m_imageDirty = false;
            }

            // GUI 입력
            int key = cv::waitKey(1);

            if (key == 27 || key == 'x')
            {
                running = false;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(10)
            );
        }

        m_player.play(0.0);
        cv::destroyAllWindows();

        // 로그 재생 객체와 이벤트 연결 정리
        m_player.close();

        std::cout << "Replay finished." << std::endl;
    }

private:
    LogPlayer m_player;

    std::shared_ptr<Sonar> m_sonar;

    SonarDataStore m_store;
    SonarImage m_image;
    Palette m_palette;

    uint32_t m_maxRangeMm = 5000;
    bool m_imageDirty = false;

    Slot<LogPlayer&, const LogFile::Track&> m_trackSlot;
    Slot<Sonar&, const Sonar::Ping&> m_pingSlot;
    Slot<Sonar&, bool_t, Sonar::Settings::Type> m_settingsSlot;

    void onNewTrack(
        LogPlayer& player,
        const LogFile::Track& track)
    {
        Device::Info info;

        if (!player.getIslDeviceTrackData(track, info))
        {
            return;
        }

        if (info.pid != Device::Pid::Sonar)
        {
            return;
        }

        // 우선 단일 ISS360 트랙만 재생
        if (m_sonar)
        {
            return;
        }

        m_sonar = std::make_shared<Sonar>(info);

        // 설정 및 Ping 이벤트 연결
        m_sonar->onSettingsUpdated.connect(m_settingsSlot);
        m_sonar->onPingData.connect(m_pingSlot);

        // 이 소나 트랙을 재생 장치와 연결
        player.addDevice(m_sonar, track);

        std::cout << "Sonar track connected."
                  << std::endl;
    }

    void onSettings(
        Sonar& sonar,
        bool_t ok,
        Sonar::Settings::Type type)
    {
        if (!ok)
            return;

        if (type == Sonar::Settings::Type::Setup)
        {
            uint32_t range =
                sonar.settings.setup.maxRangeMm;

            if (range > 0)
            {
                m_maxRangeMm = range;

                m_store.clear();

                m_image.setSectorArea(
                    0,
                    m_maxRangeMm,
                    0,
                    Sonar::maxAngle
                );

                m_imageDirty = true;

                std::cout << "Range: "
                          << m_maxRangeMm / 1000.0
                          << " m" << std::endl;
            }
        }
    }

    void onPing(
        Sonar& sonar,
        const Sonar::Ping& ping)
    {
        if (ping.maxRangeMm == 0)
            return;

        // 로그에 저장된 Ping의 측정 거리 반영
        if (m_maxRangeMm != ping.maxRangeMm)
        {
            m_maxRangeMm = ping.maxRangeMm;
            m_store.clear();

            m_image.setSectorArea(
                0,
                m_maxRangeMm,
                0,
                Sonar::maxAngle
            );
        }

        // 소나 Ping 데이터 저장
        m_store.add(ping, 0);

        m_imageDirty = true;
    }

    void renderFrame()
    {
        if (m_maxRangeMm == 0)
            return;

        m_image.render(
            m_store,
            m_palette,
            true
        );

        cv::Mat rawBgra(
            m_image.height,
            m_image.width,
            CV_8UC4
        );

        const size_t imageBytes =
            rawBgra.total() * rawBgra.elemSize();

        if (m_image.buf.size() < imageBytes)
        {
            return;
        }

        std::memcpy(
            rawBgra.data,
            m_image.buf.data(),
            imageBytes
        );

        cv::Mat displayBgr;

        cv::cvtColor(
            rawBgra,
            displayBgr,
            cv::COLOR_BGRA2BGR
        );

        drawGrid(displayBgr);

        cv::imshow(
            "ISS360 Log Replay",
            displayBgr
        );
    }

    void drawGrid(cv::Mat& img)
    {
        cv::Point center(
            img.cols / 2,
            img.rows / 2
        );

        int maxRadius =
            std::min(img.cols, img.rows) / 2 - 1;

        const cv::Scalar grid(65, 65, 65);
        const cv::Scalar text(200, 200, 200);

        for (int i = 1; i <= 5; ++i)
        {
            int radius = maxRadius * i / 5;

            float meters =
                (m_maxRangeMm / 1000.0f) * i / 5;

            cv::circle(
                img, center, radius,
                grid, 1, cv::LINE_AA
            );

            cv::putText(
                img,
                cv::format("%.1f m", meters),
                cv::Point(
                    center.x + 10,
                    center.y - radius + 18
                ),
                cv::FONT_HERSHEY_SIMPLEX,
                0.45,
                text,
                1,
                cv::LINE_AA
            );
        }

        for (int deg = 0; deg < 360; deg += 30)
        {
            double rad = deg * CV_PI / 180.0;

            cv::Point end(
                static_cast<int>(
                    center.x + maxRadius * std::sin(rad)
                ),
                static_cast<int>(
                    center.y - maxRadius * std::cos(rad)
                )
            );

            cv::line(
                img, center, end,
                grid, 1, cv::LINE_AA
            );
        }

        cv::circle(
            img, center, 4,
            cv::Scalar(0, 255, 255),
            cv::FILLED
        );

        cv::putText(
            img,
            cv::format(
                "LOG REPLAY | Range: %.1f m",
                m_maxRangeMm / 1000.0
            ),
            cv::Point(20, 30),
            cv::FONT_HERSHEY_SIMPLEX,
            0.7,
            text,
            2,
            cv::LINE_AA
        );
    }
};

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout
            << "Usage: sonarReplay <file.islog>"
            << std::endl;

        return 1;
    }

    SonarReplay replay;

    if (!replay.open(argv[1]))
    {
        return 1;
    }

    replay.run();

    return 0;
}

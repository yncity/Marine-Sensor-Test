//------------------------------------------ Includes ----------------------------------------------

#include "sonarApp.h"
#include "maths/maths.h"
#include "platform/debug.h"
#include "files/bmpFile.h"
#include "utils/utils.h"

// OpenCV Headers
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

// C/C++ Standard Headers
#include <cstring>
#include <cmath>
#include <chrono>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>
#include <cstdlib>

using namespace IslSdk;

// 홈 디렉터리 경로를 구하는 함수
std::string getHomeDir()
{
    const char *home = std::getenv("HOME");
    return home ? std::string(home) : "";
}

// 동적 경로 생성 (~/ 확장 문제 완벽 해결)
const std::string VIDEO_DIR = getHomeDir() + "/Marine-Sensor-Test/Video/";
const std::string LOG_DIR = getHomeDir() + "/Marine-Sensor-Test/Logfile/";

//--------------------------------------------------------------------------------------------------
SonarApp::SonarApp(bool autoScan)
    : App("SonarApp"),
      m_pingCount(0),
      m_autoScan(autoScan)
{
    Debug::log(Debug::Severity::Notice, name.c_str(), "created" NEW_LINE "d -> Set settings to defualt" NEW_LINE "s -> Save settings to file" NEW_LINE "r -> Start scaning" NEW_LINE "R -> Stop scaning" NEW_LINE "p -> Save palette" NEW_LINE "t -> Save sonar texture" NEW_LINE "i -> Save sonar image" NEW_LINE "c -> Check head is sync'ed" NEW_LINE);
}
//--------------------------------------------------------------------------------------------------
SonarApp::~SonarApp(void)
{
}
//--------------------------------------------------------------------------------------------------
void SonarApp::renderPalette(const std::string &path)
{
    const uint_t w = 100, h = 1000;

    std::unique_ptr<Palette> p = std::make_unique<Palette>();
    std::vector<uint32_t> buf(w * h);

    p->render(&buf[0], w, h, false);
    BmpFile::save(path + "palette.bmp", &buf[0], 32, w, h);
}

// Grid/meter overlay 함수
static void drawSonarGrid(cv::Mat &img, float maxRangeM)
{
    cv::Point center(
        img.cols / 2,
        img.rows / 2);

    // SDK의 전체 360도 렌더링 반지름과 일치
    int maxRadius =
        std::min(img.cols, img.rows) / 2 - 1;

    const cv::Scalar gridColor(65, 65, 65);
    const cv::Scalar textColor(200, 200, 200);

    // Range Ring 5개
    const int numRings = 5;

    for (int i = 1; i <= numRings; i++)
    {
        int radius = maxRadius * i / numRings;

        float distanceM =
            maxRangeM * i / numRings;

        // 원형 거리선
        cv::circle(
            img,
            center,
            radius,
            gridColor,
            1,
            cv::LINE_AA);

        // 거리 표시
        cv::putText(
            img,
            cv::format("%.1f m", distanceM),
            cv::Point(
                center.x + 10,
                center.y - radius + 18),
            cv::FONT_HERSHEY_SIMPLEX,
            0.45,
            textColor,
            1,
            cv::LINE_AA);
    }

    // 30도 간격의 방사형 선
    for (int deg = 0; deg < 360; deg += 30)
    {
        double rad = deg * CV_PI / 180.0;

        int x = static_cast<int>(
            center.x + maxRadius * std::sin(rad));

        int y = static_cast<int>(
            center.y - maxRadius * std::cos(rad));

        cv::line(
            img,
            center,
            cv::Point(x, y),
            gridColor,
            1,
            cv::LINE_AA);
    }

    // 소나 중심점
    cv::circle(
        img,
        center,
        4,
        cv::Scalar(0, 255, 255),
        cv::FILLED,
        cv::LINE_AA);

    // 현재 Range 표시
    cv::putText(
        img,
        cv::format("Range: %.1f m", maxRangeM),
        cv::Point(20, 30),
        cv::FONT_HERSHEY_SIMPLEX,
        0.7,
        textColor,
        2,
        cv::LINE_AA);
}

// sector 시작각 / 끝각
static void drawSectorBoundary(cv::Mat &img, float startDeg, float endDeg)
{
    cv::Point center(img.cols / 2, img.rows / 2);
    int maxRadius = std::min(img.cols, img.rows) / 2 - 30;

    auto drawRay = [&](float deg)
    {
        double rad = deg * CV_PI / 180.0;
        int x = static_cast<int>(center.x + maxRadius * std::cos(rad));
        int y = static_cast<int>(center.y - maxRadius * std::sin(rad));

        cv::line(img, center, cv::Point(x, y), cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
    };

    drawRay(startDeg);
    drawRay(endDeg);
}

//--------------------------------------------------------------------------------------------------
void SonarApp::connectSignals(Device &device)
{
    Sonar &sonar = reinterpret_cast<Sonar &>(device);

    ahrs.connectSignals(sonar.ahrs, name);
    gyro.connectSignals(sonar.gyro, name);
    accel.connectSignals(sonar.accel, name);

    sonar.onSettingsUpdated.connect(slotSettingsUpdated);
    sonar.onHeadIndexesAcquired.connect(slotHeadIndexesAcquired);
    sonar.onPingData.connect(slotPingData);     // Subscribing to this event causes ping data to be sent
    sonar.onEchoData.connect(slotEchoData);     // Subscribing to this event causes profiling data to be sent
    sonar.onPwrAndTemp.connect(slotPwrAndTemp); // Subscribing to this event causes data to be sent from the device at the rate defined by setSensorRates()
    sonar.onMotorSlip.connect(slotMotorSlip);
    sonar.onMotorMoveComplete.connect(slotMotorMoveComplete);

    Sonar::SensorRates rates;
    rates.ahrs = 100;
    rates.gyro = 0;
    rates.accel = 0;
    rates.mag = 0;
    rates.voltageAndTemp = 1000;
    sonar.setSensorRates(rates);
}
//--------------------------------------------------------------------------------------------------
void SonarApp::disconnectSignals(Device &device)
{
    Sonar &sonar = reinterpret_cast<Sonar &>(device);

    ahrs.disconnectSignals();
    gyro.disconnectSignals();
    accel.disconnectSignals();

    sonar.onSettingsUpdated.disconnect(slotSettingsUpdated);
    sonar.onHeadIndexesAcquired.disconnect(slotHeadIndexesAcquired);
    sonar.onPingData.disconnect(slotPingData);
    sonar.onEchoData.disconnect(slotEchoData);
    sonar.onPwrAndTemp.disconnect(slotPwrAndTemp);
    sonar.onMotorSlip.disconnect(slotMotorSlip);
    sonar.onMotorMoveComplete.disconnect(slotMotorMoveComplete);
    m_health.onDisconnected();
}
//--------------------------------------------------------------------------------------------------
void SonarApp::doTask(int_t key, const std::string &path)
{
    if (m_device)
    {
        Sonar &sonar = reinterpret_cast<Sonar &>(*m_device);

        switch (key)
        {
        case 'd':
            sonar.setSystemSettings(Sonar::System(), true);
            sonar.setAcousticSettings(Sonar::Acoustic(), true);
            sonar.setSetupSettings(Sonar::Setup(), true);
            break;

        case 's':
            sonar.saveConfig(path + m_device->info.pnSnAsStr() + " settings.xml");
            break;

        case 'r':
        {
            // 소나 스캔만 시작
            sonar.startScanning();
            m_health.onScanRequested();
            break;
        }

        case 'e':
        {

            // 로그 저장을 먼저 마무리
            stopLogRecording();

            // 영상 녹화 중이면 종료
            stopRecording();

            // 스캔 정지
            sonar.stopScanning();
            m_health.onScanStopped();

            // GUI 닫기
            closeViewer();

            break;
        }

        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        {
            // Range 프리셋 (단위: mm)
            const uint32_t rangesMm[] =
                {
                    1000,  // 1m
                    2000,  // 2m
                    5000,  // 5m
                    10000, // 10m
                    20000, // 20m
                    30000, // 30m
                    50000, // 50m
                    80000  // 80m
            };

            int index = key - '1';
            uint32_t newRangeMm = rangesMm[index];

            // 기존 센서 설정 복사
            Sonar::Setup setup = sonar.settings.setup;

            // 측정 시작과 종료 거리 변경
            setup.minRangeMm = 0;
            setup.maxRangeMm = newRangeMm;

            // 실제 소나 장치에 변경 요청
            // false : Flash 메모리에 영구 저장하지 않음
            bool_t ok = sonar.setSetupSettings(setup, false);

            if (ok)
            {
                Debug::log(
                    Debug::Severity::Info,
                    name.c_str(),
                    "Range change requested: %.1f m",
                    newRangeMm / 1000.0);
            }
            else
            {
                Debug::log(
                    Debug::Severity::Warning,
                    name.c_str(),
                    "Range change request rejected");
            }

            break;
        }

        case 'p':
            renderPalette(path);
            break;

        // 영상 log 기록/종료
        case 'l':
        {
            // =====================================
            // 1. 이미 기록 중이면 종료
            // =====================================
            if (m_islogRecording)
            {
                stopLogRecording();
                break;
            }

            // 소나 연결 상태 확인
            if (!sonar.isConnected())
            {
                Debug::log(
                    Debug::Severity::Warning,
                    name.c_str(),
                    "Sonar is not connected");
                break;
            }

            // =====================================
            // 2. 저장 파일명 생성
            // =====================================
            std::time_t currentTime = std::time(nullptr);
            std::tm localTime{};

            localtime_r(&currentTime, &localTime);

            char timestamp[32];

            std::strftime(
                timestamp,
                sizeof(timestamp),
                "%Y%m%d_%H%M%S",
                &localTime);

            std::string filename =
                LOG_DIR + "sonar_" + std::to_string(sonar.settings.setup.maxRangeMm / 1000) + "m_" + timestamp + "_" +
                std::to_string(++m_logFileIndex) + ".islog";

            // =====================================
            // 3. SDK LogWriter 생성
            // =====================================
            m_logWriter = std::make_shared<LogWriter>();

            if (!m_logWriter->startNewFile(filename))
            {
                Debug::log(
                    Debug::Severity::Error,
                    name.c_str(),
                    "Failed to create ISLOG file");

                m_logWriter.reset();
                break;
            }

            // =====================================
            // 4. LogWriter를 소나 장치에 연결
            // =====================================

            // 추가 트랙 데이터는 사용하지 않음
            uint8_t unusedTrackData = 0;

            sonar.setLogger(
                m_logWriter,
                &unusedTrackData,
                0);

            // =====================================
            // 5. SDK 데이터 기록 시작
            // =====================================
            if (!sonar.startLogging())
            {
                sonar.stopLogging();

                std::shared_ptr<LogWriter> emptyLogger;

                sonar.setLogger(
                    emptyLogger,
                    &unusedTrackData,
                    0);

                m_logWriter->close();
                m_logWriter.reset();

                Debug::log(
                    Debug::Severity::Error,
                    name.c_str(),
                    "Failed to start ISLOG recording");

                break;
            }

            // =====================================
            // 6. 기록 상태 저장
            // =====================================
            m_islogRecording = true;
            m_islogPath = filename;

            Debug::log(
                Debug::Severity::Notice,
                name.c_str(),
                "ISLOG recording started: %s",
                m_islogPath.c_str());

            break;
        }

        // 영상 녹화 시작/종료
        case 'v':
        {
            // 이미 녹화 중이면 종료
            if (m_recording)
            {
                stopRecording();
                break;
            }

            // 영상 버퍼가 생성되었는지 확인
            if (m_circular.width <= 0 ||
                m_circular.height <= 0)
            {
                Debug::log(
                    Debug::Severity::Warning,
                    name.c_str(),
                    "Sonar image buffer is not ready");
                break;
            }

            // 녹화 파일명에 날짜와 시간 사용
            std::time_t t = std::time(nullptr);
            std::tm localTime{};
            localtime_r(&t, &localTime);

            char timestamp[32];
            std::strftime(
                timestamp,
                sizeof(timestamp),
                "%Y%m%d_%H%M%S",
                &localTime);

            m_recordPath =
                VIDEO_DIR + "sonar_" + std::to_string(sonar.settings.setup.maxRangeMm / 1000) + "m_" + timestamp + "_" +
                std::to_string(++m_recordSequence) + ".avi";

            // MJPEG, 10 FPS
            const int codec =
                cv::VideoWriter::fourcc('M', 'J', 'P', 'G');

            const double fps = 10.0;

            bool opened = m_videoWriter.open(
                m_recordPath,
                codec,
                fps,
                cv::Size(
                    m_circular.width,
                    m_circular.height),
                true);

            if (!opened)
            {
                Debug::log(
                    Debug::Severity::Error,
                    name.c_str(),
                    "Failed to open video recording");
                break;
            }

            // 녹화 시작
            m_recording = true;

            m_nextVideoFrame =
                std::chrono::steady_clock::now();

            m_imageDirty = true;

            Debug::log(
                Debug::Severity::Notice,
                name.c_str(),
                "Recording started: %s",
                m_recordPath.c_str());

            break;
        }

        case 't':
            m_texture.renderTexture(sonarDataStore, m_palette, false);
            BmpFile::save(path + "texture.bmp", reinterpret_cast<const uint32_t *>(&m_texture.buf[0]), 32, m_texture.width, m_texture.height);
            break;

        case 'c':
            sonar.acquireHeadIdx(false);
            break;

        case 'z':
        {
            // GUI가 닫혀 있을 때만 생성
            if (!m_viewerOpen)
            {
                cv::namedWindow(
                    "ISS360 Sonar",
                    cv::WINDOW_AUTOSIZE);

                m_viewerOpen = true;
                m_imageDirty = true;
            }
            break;
        }

        case 'a':
        {
            // GUI만 닫기. 소나와 녹화는 유지
            closeViewer();
            break;
        }

        case 'n':
        {
            // 현재 소나 시스템 설정 복사
            Sonar::System system = sonar.settings.system;

            // 고정 IP 설정
            system.ipAddress =
                Utils::ipToUint(192, 168, 1, 201);

            system.netmask =
                Utils::ipToUint(255, 255, 255, 0);

            // DHCP 비활성화
            system.useDhcp = false;

            // 소나 펌웨어에 변경 설정 저장 요청
            bool_t ok = sonar.setSystemSettings(system, true);

            if (!ok)
            {
                Debug::log(
                    Debug::Severity::Warning,
                    "SonarApp",
                    "Failed to request network settings update");
            }

            break;
        }

        default:
            break;
        }

        App::doTask(key, path);
    }
}

void SonarApp::closeViewer()
{
    if (m_viewerOpen)
    {
        cv::destroyWindow("ISS360 Sonar");
        m_viewerOpen = false;
    }
}

//----------------------------------------------------

void SonarApp::stopLogRecording()
{
    if (!m_islogRecording)
        return;

    // =====================================
    // 1. 소나의 로그 기록 중단
    // =====================================
    if (m_device)
    {
        Sonar &sonar =
            reinterpret_cast<Sonar &>(*m_device);

        sonar.stopLogging();

        // 장치와 LogWriter 연결 해제
        std::shared_ptr<LogWriter> emptyLogger;
        uint8_t unusedTrackData = 0;

        sonar.setLogger(
            emptyLogger,
            &unusedTrackData,
            0);
    }

    // =====================================
    // 2. 로그 파일 닫기
    // =====================================
    uint_t recordCount = 0;
    bool_t closed = false;

    if (m_logWriter)
    {
        recordCount = m_logWriter->recordCount();

        closed = m_logWriter->close();

        m_logWriter.reset();
    }

    m_islogRecording = false;

    // =====================================
    // 3. 결과 출력
    // =====================================
    Debug::log(
        closed
            ? Debug::Severity::Notice
            : Debug::Severity::Warning,
        name.c_str(),
        "ISLOG %s: %s (records: %u)",
        closed ? "saved" : "close failed",
        m_islogPath.c_str(),
        static_cast<unsigned int>(recordCount));
}

void SonarApp::stopRecording()
{
    if (!m_recording)
        return;

    // 영상 파일 마무리
    m_videoWriter.release();

    m_recording = false;

    Debug::log(
        Debug::Severity::Notice,
        name.c_str(),
        "Recording saved: %s",
        m_recordPath.c_str());
}

//----------------------------------------------------

void SonarApp::shutdown()
{
    // 로그 정상 종료
    stopLogRecording();
    m_health.onScanStopped();

    // 프로그램 종료 시 장치 스캔 정지 요청
    if (m_device)
    {
        Sonar &sonar =
            reinterpret_cast<Sonar &>(*m_device);

        sonar.stopScanning();
    }

    // 녹화 파일 마무리
    stopRecording();

    // GUI 종료
    closeViewer();
}

//--------------------------------------------------------------------------------------------------
void SonarApp::connectEvent(Device &device)
{
    Sonar &sonar = reinterpret_cast<Sonar &>(device);

    m_circular.setBuffer(1000, 1000, true);
    m_circular.setSectorArea(
        0,
        sonar.settings.setup.maxRangeMm,
        0,
        Sonar::maxAngle);
    m_circular.useBilinerInterpolation = true;

    // Optimal texture size to pass to the GPU - each pixel represents a data point. The GPU can then map this texture to circle (triangle fan)
    m_texture.useBilinerInterpolation = false;
    m_texture.setBuffer(sonar.settings.setup.imageDataPoint, Sonar::maxAngle / Math::abs(sonar.settings.setup.stepSize), true);
    m_texture.setSectorArea(0, sonar.settings.setup.maxRangeMm, sonar.settings.setup.sectorStart, sonar.settings.setup.sectorSize);
    m_health.onConnected();
    if (m_autoScan && !sonar.bootloaderMode())
    {
        sonar.startScanning();
        m_health.onScanRequested();

        Debug::log(
            Debug::Severity::Notice,
            name.c_str(),
            "Automatic scan start requested");
    }
}
//--------------------------------------------------------------------------------------------------
void SonarApp::callbackSettingsUpdated(Sonar &sonar, bool_t ok, Sonar::Settings::Type settingsType)
{
    const char *settingsTypeStr[] = {"System", "Acostic", "Setup"};

    if (ok)
    {
        Debug::log(Debug::Severity::Info, name.c_str(), "%s settings updated", settingsTypeStr[static_cast<uint_t>(settingsType)]);

        if (settingsType == Sonar::Settings::Type::Setup)
        {
            // 이전 Range에서 수신한 Ping 데이터 제거
            sonarDataStore.clear();

            // 전체 360도 렌더링 및 새 거리 설정
            m_circular.setSectorArea(
                0,
                sonar.settings.setup.maxRangeMm,
                0,
                Sonar::maxAngle);

            // Texture 설정 갱신
            m_texture.setBuffer(
                sonar.settings.setup.imageDataPoint,
                Sonar::maxAngle /
                    Math::abs(sonar.settings.setup.stepSize),
                true);

            m_texture.setSectorArea(
                0,
                sonar.settings.setup.maxRangeMm,
                sonar.settings.setup.sectorStart,
                sonar.settings.setup.sectorSize);

            Debug::log(
                Debug::Severity::Info,
                name.c_str(),
                "Current Range: %.1f m",
                sonar.settings.setup.maxRangeMm / 1000.0);
        }
    }
    else
    {
        Debug::log(Debug::Severity::Warning, name.c_str(), "%s settings failed to update", settingsTypeStr[static_cast<uint_t>(settingsType)]);
    }
}
//--------------------------------------------------------------------------------------------------
void SonarApp::callbackHeadIndexesAcquired(Sonar &sonar, const Sonar::HeadIndexes &data)
{
    m_health.onHeadSync(
        data.state == Sonar::HeadIndexes::State::Ok);
    Debug::log(Debug::Severity::Info, name.c_str(), "Head Sync:%s. Slippage:%i", data.state == Sonar::HeadIndexes::State::Ok ? "ok" : "error", data.slippage);
}
//--------------------------------------------------------------------------------------------------
void SonarApp::callbackPingData(Sonar &sonar, const Sonar::Ping &ping)
{
    Debug::log(Debug::Severity::Info, name.c_str(), "Ping data");

    uint_t txPulseLengthMm = static_cast<uint_t>(sonar.settings.system.speedOfSound * sonar.settings.acoustic.txPulseWidthUs * 0.001 * 0.5);
    txPulseLengthMm = Math::max<uint_t>(txPulseLengthMm, 150);

    sonarDataStore.add(ping, txPulseLengthMm);
    m_health.onPing();

    m_hasPing = true;
    m_imageDirty = true;

    m_pingCount++;

    if (m_pingCount % (Sonar::maxAngle / sonar.settings.setup.stepSize) == 0)
    {
        m_pingCount = 0;
        /*
        m_texture.renderTexture(sonarDataStore, m_palette, false);
        BmpFile::save("snrTex.bmp", reinterpret_cast<const uint32_t*>(&m_texture.buf[0]), 32, m_texture.width, m_texture.height);

        m_circular.render(sonarDataStore, m_palette, true);
        BmpFile::save("snrCi.bmp", reinterpret_cast<const uint32_t*>(&m_circular.buf[0]), 32, m_circular.width, m_circular.height);
        */
    }
}

bool SonarApp::updateViewer(const std::string &path, bool serviceMode)
{
    using Clock = std::chrono::steady_clock;

    const auto now = Clock::now();
    const auto frameInterval =
        std::chrono::milliseconds(100); // 10 FPS

    // ========================================
    // 1. 영상 렌더링 여부 확인
    // ========================================

    bool recordDue =
        m_recording && now >= m_nextVideoFrame;

    bool needRender =
        m_hasPing &&
        m_imageDirty &&
        (m_viewerOpen || recordDue);

    // ========================================
    // 2. 소나 데이터 -> OpenCV 이미지
    // ========================================

    if (needRender && m_device)
    {
        Sonar &sonar =
            reinterpret_cast<Sonar &>(*m_device);

        m_circular.render(
            sonarDataStore,
            m_palette,
            true);

        cv::Mat rawBgra(
            m_circular.height,
            m_circular.width,
            CV_8UC4);

        size_t imageBytes =
            rawBgra.total() * rawBgra.elemSize();

        if (m_circular.buf.size() >= imageBytes)
        {
            std::memcpy(
                rawBgra.data,
                m_circular.buf.data(),
                imageBytes);

            cv::Mat displayBgr;

            cv::cvtColor(
                rawBgra,
                displayBgr,
                cv::COLOR_BGRA2BGR);

            // 현재 소나의 실제 측정 Range
            float maxRangeM =
                sonar.settings.setup.maxRangeMm / 1000.0f;

            // 거리 그리드 추가
            drawSonarGrid(displayBgr, maxRangeM);

            // 최신 영상 프레임 보관
            m_lastFrame = displayBgr;

            m_imageDirty = false;

            // GUI가 활성화된 경우에만 화면 표시
            if (m_viewerOpen)
            {
                cv::imshow(
                    "ISS360 Sonar",
                    m_lastFrame);
            }
        }
    }

    // ========================================
    // 3. 영상 녹화 처리
    // GUI 활성화 여부와 독립적
    // ========================================

    if (recordDue && !m_lastFrame.empty())
    {
        int written = 0;

        while (now >= m_nextVideoFrame && written < 3)
        {
            m_videoWriter.write(m_lastFrame);

            m_nextVideoFrame += frameInterval;
            ++written;
        }

        if (now >= m_nextVideoFrame)
        {
            m_nextVideoFrame = now + frameInterval;
        }
    }

    // ========================================
    // 4. OpenCV 키보드 명령어 처리
    // ========================================

    if (m_viewerOpen)
    {
        int key = cv::waitKey(1);

        if (key != -1)
        {
            std::cout << "Key pressed: " << key
                      << " / Character: "
                      << static_cast<char>(key)
                      << std::endl;
        }

        // ESC 또는 x -> 프로그램 전체 종료
        if (key == 27 || key == 'x')
        {
            return true;
        }

        // OpenCV GUI 키보드 명령 처리
        if (key >= 0 && key <= 255)
        {
            // 서비스 모드에서는 IP 변경 및 초기화 명령 차단
            if (serviceMode && (key == 'n' || key == 'd'))
            {
                std::cout
                    << "[DENIED] Command '" << static_cast<char>(key)
                    << "' is not allowed in service mode."
                    << std::endl;
            }
            else
            {
                // 일반 명령은 기존 doTask()로 전달
                doTask(key, path);
            }
        }
        // GUI가 여전히 열려 있는 경우 창 상태 확인
        if (m_viewerOpen)
        {
            try
            {
                double visible =
                    cv::getWindowProperty(
                        "ISS360 Sonar",
                        cv::WND_PROP_VISIBLE);

                if (visible < 1)
                {
                    m_viewerOpen = false;
                }
            }
            catch (const cv::Exception &)
            {
                m_viewerOpen = false;
            }
        }
    }

    return false;
}

//--------------------------------------------------------------------------------------------------
void SonarApp::callbackEchoData(Sonar &sonar, const Sonar::Echos &data)
{
    Debug::log(Debug::Severity::Info, name.c_str(), "Echo data, size:%u", data.data.size());
}
//--------------------------------------------------------------------------------------------------
void SonarApp::callbackPwrAndTemp(Sonar &sonar, const Sonar::CpuPowerTemp &data)
{
    Debug::log(Debug::Severity::Info, name.c_str(), "CPU:%.1f, SYS:%.1f, 1.0V:%.2f, 1.8V:%.2f, 1.35V:%.2f", data.cpuTemperature, data.auxTemperature, data.core1V0, data.aux1V8, data.ddr1V35);
}
//--------------------------------------------------------------------------------------------------
void SonarApp::callbackMotorSlip(Sonar &sonar)
{
    m_health.onMotorSlip();
    Debug::log(Debug::Severity::Info, name.c_str(), "Motor has slipped");
}
//--------------------------------------------------------------------------------------------------
void SonarApp::callbackMotorMoveComplete(Sonar &sonar, bool_t ok)
{
    Debug::log(Debug::Severity::Info, name.c_str(), "Motor move %s", ok ? "complete" : "busy");
}
//--------------------------------------------------------------------------------------------------

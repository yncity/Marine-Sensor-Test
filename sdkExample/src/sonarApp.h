#ifndef SONARAPP_H_
#define SONARAPP_H_

//------------------------------------------ Includes ----------------------------------------------

#include "app.h"
#include "devices/sonar.h"
#include "imuManager.h"
#include "helpers/sonarDataStore.h"
#include "helpers/sonarImage.h"
#include "opencv2/core.hpp"
#include "opencv2/videoio.hpp"
#include "chrono"
#include "string"
#include "logging/logWriter.h"
#include "memory"
#include "sonarHealth.h"

//--------------------------------------- Class Definition -----------------------------------------

namespace IslSdk
{
    class SonarApp : public App
    {
    public:
        SonarApp(bool autoScan = false);
        ~SonarApp(void);
        void renderPalette(const std::string &path);
        void connectSignals(Device &device) override;
        void disconnectSignals(Device &device) override;
        void doTask(int_t key, const std::string &path) override;
        // GUI 화면 갱신 및 ESC 입력 처리
        // ESC를 누르면 true 반환
        bool updateViewer(const std::string &path);

        // 프로그램 종료 시 자원 정리
        void shutdown();
        std::string healthReport() const
        {
            return m_health.report();
        }

        Slot<Sonar &, bool_t, Sonar::Settings::Type> slotSettingsUpdated{this, &SonarApp::callbackSettingsUpdated};
        Slot<Sonar &, const Sonar::HeadIndexes &> slotHeadIndexesAcquired{this, &SonarApp::callbackHeadIndexesAcquired};
        Slot<Sonar &, const Sonar::Ping &> slotPingData{this, &SonarApp::callbackPingData};
        Slot<Sonar &, const Sonar::Echos &> slotEchoData{this, &SonarApp::callbackEchoData};
        Slot<Sonar &, const Sonar::CpuPowerTemp &> slotPwrAndTemp{this, &SonarApp::callbackPwrAndTemp};
        Slot<Sonar &> slotMotorSlip{this, &SonarApp::callbackMotorSlip};
        Slot<Sonar &, bool_t> slotMotorMoveComplete{this, &SonarApp::callbackMotorMoveComplete};

    private:
        AhrsManager ahrs;
        GyroManager gyro;
        AccelManager accel;

        Palette m_palette;
        SonarImage m_circular;
        SonarImage m_texture;
        uint_t m_pingCount;
        bool m_autoScan = false; // 프로그램 시작 시 자동 스캔 여부
        SonarHealth m_health;
        SonarDataStore sonarDataStore;
        virtual void connectEvent(Device &device);

        void callbackSettingsUpdated(Sonar &sonar, bool_t ok, Sonar::Settings::Type settingsType);
        void callbackHeadIndexesAcquired(Sonar &sonar, const Sonar::HeadIndexes &data);
        void callbackEchoData(Sonar &sonar, const Sonar::Echos &data);
        void callbackPingData(Sonar &sonar, const Sonar::Ping &data);
        void callbackPwrAndTemp(Sonar &sonar, const Sonar::CpuPowerTemp &data);
        void callbackMotorSlip(Sonar &sonar);
        void callbackMotorMoveComplete(Sonar &sonar, bool_t ok);

        bool m_viewerOpen = false; // GUI 창 활성화 여부, z를 눌러 GUI를 활성화했는지 확인
        bool m_hasPing = false;    // Ping 데이터 수신 여부, 최소 한 번 이상 소나 데이터를 수신했는지 확인
        bool m_imageDirty = false; // 새로운 영상 데이터 존재 여부, 새 Ping이 들어와 화면을 다시 그려야 하는지 확인

        // 영상 녹화 상태
        bool m_recording = false;

        // 녹화 파일 객체
        cv::VideoWriter m_videoWriter;

        // 최근에 렌더링된 소나 이미지
        cv::Mat m_lastFrame;

        // 녹화 파일 경로
        std::string m_recordPath;

        // 녹화 파일 구분용 번호
        unsigned int m_recordSequence = 0;

        // 다음 녹화 프레임 시간
        std::chrono::steady_clock::time_point m_nextVideoFrame{};

        // GUI 종료
        void closeViewer();

        // 영상 녹화 종료
        void stopRecording();

        // ISLOG 기록 객체
        std::shared_ptr<LogWriter> m_logWriter;

        // 기록 중인지 확인
        bool m_islogRecording = false;

        // 현재 저장 파일명
        std::string m_islogPath;

        // 파일명 중복 방지 번호
        unsigned int m_logFileIndex = 0;

        // 기록 종료 함수
        void stopLogRecording();
    };
}

//--------------------------------------------------------------------------------------------------
#endif

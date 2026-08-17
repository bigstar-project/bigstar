/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#include <optional>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

#include <SDL2/SDL.h>

#include <QCoreApplication>
#include <QMetaObject>

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#endif

#include "main.h"

#include "types.h"
#include "version.h"

#include "ScreenLayout.h"

#include "Args.h"
#include "NDS.h"
#include "NDSCart.h"
#include "GBACart.h"
#include "GPU.h"
#include "SPU.h"
#include "Wifi.h"
#include "Platform.h"
#include "LocalMP.h"
#include "Config.h"
#include "RTC.h"
#include "DSi.h"
#include "DSi_I2C.h"
#include "GPU_Soft.h"
#include "GPU_OpenGL.h"

#include "Savestate.h"

#include "EmuInstance.h"
#include "SaveBootstrap.h"
#include "NsmbMvlNetplayRuntime.h"

using namespace melonDS;

namespace
{

unsigned long long NsmlUnixMs()
{
    return static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

double NsmlCurrentThreadCpuSeconds()
{
#ifdef _WIN32
    FILETIME creationTime {};
    FILETIME exitTime {};
    FILETIME kernelTime {};
    FILETIME userTime {};
    if (!GetThreadTimes(GetCurrentThread(), &creationTime, &exitTime, &kernelTime, &userTime))
        return 0.0;

    ULARGE_INTEGER kernel {};
    kernel.LowPart = kernelTime.dwLowDateTime;
    kernel.HighPart = kernelTime.dwHighDateTime;
    ULARGE_INTEGER user {};
    user.LowPart = userTime.dwLowDateTime;
    user.HighPart = userTime.dwHighDateTime;
    return static_cast<double>(kernel.QuadPart + user.QuadPart) / 10000000.0;
#else
    return 0.0;
#endif
}

int NsmlCurrentProcessorNumber()
{
#ifdef _WIN32
    return static_cast<int>(GetCurrentProcessorNumber());
#else
    return -1;
#endif
}

std::string NsmlJsonEscape(const char* value)
{
    std::string escaped;
    if (!value)
        return escaped;
    for (const unsigned char ch : std::string(value))
    {
        switch (ch)
        {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (ch >= 0x20)
                escaped += static_cast<char>(ch);
            break;
        }
    }
    return escaped;
}

struct NsmlPerformanceSample
{
    melonDS::u32 Frame = 0;
    double TotalMs = 0.0;
    double ThreadCpuMs = 0.0;
    double MpMs = 0.0;
    double InputMs = 0.0;
    double BeforeHookMs = 0.0;
    double RunFrameMs = 0.0;
    double AfterHookMs = 0.0;
    double DrawMs = 0.0;
    double AudioMs = 0.0;
    double LimitMs = 0.0;
    double UnaccountedMs = 0.0;
    double LimitRequestedMs = 0.0;
    double LimitMaxDelayMs = 0.0;
    double DeadlineLateMs = 0.0;
    unsigned int LimitYieldCalls = 0;
    unsigned int LimitSleepCalls = 0;
    melonDS::u32 AudioQueueBefore = 0;
    melonDS::u32 AudioQueueAfter = 0;
    int Processor = -1;
    NsmbMvlNetplay::PerformanceCounters Netplay;
};

class NsmlPerformanceLog
{
public:
    explicit NsmlPerformanceLog(const char* path)
    {
        RotationDisabled = getenv("MELONDS_NSML_DISABLE_LOG_ROTATION") != nullptr;
        if (path && *path)
        {
            Path = path;
            File = Platform::OpenFile(Path.c_str(), Platform::FileMode::WriteText);
        }
    }

    ~NsmlPerformanceLog()
    {
        if (!File)
            return;
        Platform::FileFlush(File);
        Platform::CloseFile(File);
    }

    bool Enabled() const { return File != nullptr; }

    void WriteStartup(
        int instanceID,
        bool useOpenGL,
        int videoRenderer,
        double targetFPS,
        bool limitFPS,
        bool audioSync,
        int audioFrequency,
        int audioBufferSize)
    {
        if (!File)
            return;

        int logicalProcessors = 0;
        unsigned long long processAffinity = 0;
        unsigned long long systemAffinity = 0;
        unsigned long processPriority = 0;
        int threadPriority = 0;
        int acLineStatus = -1;
        int batteryPercent = -1;
#ifdef _WIN32
        SYSTEM_INFO systemInfo {};
        GetNativeSystemInfo(&systemInfo);
        logicalProcessors = static_cast<int>(systemInfo.dwNumberOfProcessors);
        DWORD_PTR processMask = 0;
        DWORD_PTR systemMask = 0;
        if (GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask))
        {
            processAffinity = static_cast<unsigned long long>(processMask);
            systemAffinity = static_cast<unsigned long long>(systemMask);
        }
        processPriority = GetPriorityClass(GetCurrentProcess());
        threadPriority = GetThreadPriority(GetCurrentThread());
        SYSTEM_POWER_STATUS powerStatus {};
        if (GetSystemPowerStatus(&powerStatus))
        {
            acLineStatus = powerStatus.ACLineStatus;
            batteryPercent = powerStatus.BatteryLifePercent == 255
                ? -1
                : static_cast<int>(powerStatus.BatteryLifePercent);
        }
#endif
        const char* audioDriver = SDL_GetCurrentAudioDriver();
        const char* audioDevice = SDL_GetNumAudioDevices(0) > 0
            ? SDL_GetAudioDeviceName(0, 0)
            : nullptr;
        const char* videoDriver = SDL_GetCurrentVideoDriver();
        const char* processor = std::getenv("PROCESSOR_IDENTIFIER");
        const char* role = std::getenv("MELONDS_NSML_ROLE");

        std::ostringstream line;
        line << std::fixed << std::setprecision(3)
             << "{\"type\":\"startup\",\"unix_ms\":" << NsmlUnixMs()
             << ",\"instance\":" << instanceID
             << ",\"role\":\"" << NsmlJsonEscape(role) << "\""
             << ",\"renderer\":\"" << (useOpenGL ? "opengl" : "software") << "\""
             << ",\"video_renderer\":" << videoRenderer
             << ",\"video_driver\":\"" << NsmlJsonEscape(videoDriver) << "\""
             << ",\"target_fps\":" << targetFPS
             << ",\"limit_fps\":" << (limitFPS ? "true" : "false")
             << ",\"audio_sync\":" << (audioSync ? "true" : "false")
             << ",\"audio_driver\":\"" << NsmlJsonEscape(audioDriver) << "\""
             << ",\"audio_device\":\"" << NsmlJsonEscape(audioDevice) << "\""
             << ",\"audio_frequency\":" << audioFrequency
             << ",\"audio_buffer_size\":" << audioBufferSize
             << ",\"processor\":\"" << NsmlJsonEscape(processor) << "\""
             << ",\"logical_processors\":" << logicalProcessors
             << ",\"process_affinity\":" << processAffinity
             << ",\"system_affinity\":" << systemAffinity
             << ",\"process_priority\":" << processPriority
             << ",\"thread_priority\":" << threadPriority
             << ",\"ac_line_status\":" << acLineStatus
             << ",\"battery_percent\":" << batteryPercent
             << "}\n";
        Write(line.str(), true);
    }

    void Add(const NsmlPerformanceSample& sample)
    {
        if (!File)
            return;
        Samples.push_back(sample);

        const unsigned long long now = NsmlUnixMs();
        if (sample.TotalMs >= 20.0 && now >= LastSpikeUnixMs + 1000)
        {
            LastSpikeUnixMs = now;
            WriteSample("slow_frame", now, sample);
        }
        if (Samples.size() >= 120)
            WriteSummary(now);
    }

private:
    template <typename Getter>
    double Average(Getter getter) const
    {
        double total = 0.0;
        for (const auto& sample : Samples)
            total += getter(sample);
        return Samples.empty() ? 0.0 : total / static_cast<double>(Samples.size());
    }

    template <typename Getter>
    double Maximum(Getter getter) const
    {
        double maximum = 0.0;
        for (const auto& sample : Samples)
            maximum = std::max(maximum, getter(sample));
        return maximum;
    }

    template <typename Getter>
    double Percentile95(Getter getter) const
    {
        std::vector<double> values;
        values.reserve(Samples.size());
        for (const auto& sample : Samples)
            values.push_back(getter(sample));
        if (values.empty())
            return 0.0;
        std::sort(values.begin(), values.end());
        const std::size_t index = std::min(
            values.size() - 1,
            static_cast<std::size_t>((values.size() - 1) * 0.95));
        return values[index];
    }

    void WriteSummary(unsigned long long now)
    {
        const auto& first = Samples.front();
        const auto& last = Samples.back();
        unsigned int over16 = 0;
        unsigned int over25 = 0;
        unsigned int over33 = 0;
        unsigned int processorMigrations = 0;
        unsigned int yieldCalls = 0;
        unsigned int sleepCalls = 0;
        for (std::size_t i = 0; i < Samples.size(); i++)
        {
            const auto& sample = Samples[i];
            over16 += sample.TotalMs > 16.667 ? 1 : 0;
            over25 += sample.TotalMs > 25.0 ? 1 : 0;
            over33 += sample.TotalMs > 33.334 ? 1 : 0;
            yieldCalls += sample.LimitYieldCalls;
            sleepCalls += sample.LimitSleepCalls;
            if (i > 0 && sample.Processor != Samples[i - 1].Processor)
                processorMigrations++;
        }

        const auto avg = [this](auto member) {
            return Average([member](const NsmlPerformanceSample& sample) { return sample.*member; });
        };
        const auto max = [this](auto member) {
            return Maximum([member](const NsmlPerformanceSample& sample) { return sample.*member; });
        };
        const auto p95 = [this](auto member) {
            return Percentile95([member](const NsmlPerformanceSample& sample) { return sample.*member; });
        };
        const double averageTotalMs = avg(&NsmlPerformanceSample::TotalMs);
        const auto remoteWaitCountDelta = last.Netplay.RemoteInputWaitCount
            - LastSummaryNetplay.RemoteInputWaitCount;
        const auto remoteWaitUsDelta = last.Netplay.RemoteInputWaitUs
            - LastSummaryNetplay.RemoteInputWaitUs;
        const auto throttleCountDelta = last.Netplay.FrameLeadThrottleCount
            - LastSummaryNetplay.FrameLeadThrottleCount;
        const auto throttleUsDelta = last.Netplay.FrameLeadThrottleUs
            - LastSummaryNetplay.FrameLeadThrottleUs;

        std::ostringstream line;
        line << std::fixed << std::setprecision(3)
             << "{\"type\":\"summary\",\"unix_ms\":" << now
             << ",\"first_frame\":" << first.Frame
             << ",\"last_frame\":" << last.Frame
             << ",\"samples\":" << Samples.size()
             << ",\"effective_fps\":" << (averageTotalMs > 0.0 ? 1000.0 / averageTotalMs : 0.0)
             << ",\"total_ms\":{\"avg\":" << averageTotalMs
             << ",\"p95\":" << p95(&NsmlPerformanceSample::TotalMs)
             << ",\"max\":" << max(&NsmlPerformanceSample::TotalMs) << "}"
             << ",\"thread_cpu_ms\":{\"avg\":" << avg(&NsmlPerformanceSample::ThreadCpuMs)
             << ",\"p95\":" << p95(&NsmlPerformanceSample::ThreadCpuMs)
             << ",\"max\":" << max(&NsmlPerformanceSample::ThreadCpuMs) << "}"
             << ",\"phase_avg_ms\":{\"mp\":" << avg(&NsmlPerformanceSample::MpMs)
             << ",\"input\":" << avg(&NsmlPerformanceSample::InputMs)
             << ",\"before_hook\":" << avg(&NsmlPerformanceSample::BeforeHookMs)
             << ",\"run_frame\":" << avg(&NsmlPerformanceSample::RunFrameMs)
             << ",\"after_hook\":" << avg(&NsmlPerformanceSample::AfterHookMs)
             << ",\"draw\":" << avg(&NsmlPerformanceSample::DrawMs)
             << ",\"audio\":" << avg(&NsmlPerformanceSample::AudioMs)
             << ",\"limit\":" << avg(&NsmlPerformanceSample::LimitMs)
             << ",\"unaccounted\":" << avg(&NsmlPerformanceSample::UnaccountedMs) << "}"
             << ",\"phase_max_ms\":{\"before_hook\":" << max(&NsmlPerformanceSample::BeforeHookMs)
             << ",\"run_frame\":" << max(&NsmlPerformanceSample::RunFrameMs)
             << ",\"after_hook\":" << max(&NsmlPerformanceSample::AfterHookMs)
             << ",\"draw\":" << max(&NsmlPerformanceSample::DrawMs)
             << ",\"audio\":" << max(&NsmlPerformanceSample::AudioMs)
             << ",\"limit\":" << max(&NsmlPerformanceSample::LimitMs)
             << ",\"unaccounted\":" << max(&NsmlPerformanceSample::UnaccountedMs) << "}"
             << ",\"limiter\":{\"requested_avg_ms\":" << avg(&NsmlPerformanceSample::LimitRequestedMs)
             << ",\"max_delay_ms\":" << max(&NsmlPerformanceSample::LimitMaxDelayMs)
             << ",\"deadline_late_max_ms\":" << max(&NsmlPerformanceSample::DeadlineLateMs)
             << ",\"yield_calls\":" << yieldCalls
             << ",\"sleep_calls\":" << sleepCalls << "}"
             << ",\"slow_frames\":{\"over_16ms\":" << over16
             << ",\"over_25ms\":" << over25
             << ",\"over_33ms\":" << over33 << "}"
             << ",\"processor\":{\"last\":" << last.Processor
             << ",\"migrations\":" << processorMigrations << "}"
             << ",\"audio_queue\":{\"before_last\":" << last.AudioQueueBefore
             << ",\"after_last\":" << last.AudioQueueAfter << "}"
             << ",\"netplay\":{\"input_lead\":" << last.Netplay.InputLead
             << ",\"last_sent\":" << last.Netplay.LastSentInputFrame
             << ",\"last_received\":" << last.Netplay.LastReceivedInputFrame
             << ",\"remote_wait_count_delta\":" << remoteWaitCountDelta
             << ",\"remote_wait_ms_delta\":" << static_cast<double>(remoteWaitUsDelta) / 1000.0
             << ",\"remote_wait_max_ms\":" << static_cast<double>(last.Netplay.RemoteInputWaitMaxUs) / 1000.0
             << ",\"throttle_count_delta\":" << throttleCountDelta
             << ",\"throttle_ms_delta\":" << static_cast<double>(throttleUsDelta) / 1000.0
             << ",\"throttle_max_ms\":" << static_cast<double>(last.Netplay.FrameLeadThrottleMaxUs) / 1000.0
             << "},\"previous_log_write_ms\":" << LastWriteMs
             << "}\n";
        Write(line.str(), true);
        LastSummaryNetplay = last.Netplay;
        Samples.clear();
    }

    void WriteSample(const char* type, unsigned long long now, const NsmlPerformanceSample& sample)
    {
        std::ostringstream line;
        line << std::fixed << std::setprecision(3)
             << "{\"type\":\"" << type << "\",\"unix_ms\":" << now
             << ",\"frame\":" << sample.Frame
             << ",\"total_ms\":" << sample.TotalMs
             << ",\"thread_cpu_ms\":" << sample.ThreadCpuMs
             << ",\"mp_ms\":" << sample.MpMs
             << ",\"input_ms\":" << sample.InputMs
             << ",\"before_hook_ms\":" << sample.BeforeHookMs
             << ",\"run_frame_ms\":" << sample.RunFrameMs
             << ",\"after_hook_ms\":" << sample.AfterHookMs
             << ",\"draw_ms\":" << sample.DrawMs
             << ",\"audio_ms\":" << sample.AudioMs
             << ",\"limit_ms\":" << sample.LimitMs
             << ",\"unaccounted_ms\":" << sample.UnaccountedMs
             << ",\"limit_requested_ms\":" << sample.LimitRequestedMs
             << ",\"limit_max_delay_ms\":" << sample.LimitMaxDelayMs
             << ",\"deadline_late_ms\":" << sample.DeadlineLateMs
             << ",\"limit_yield_calls\":" << sample.LimitYieldCalls
             << ",\"limit_sleep_calls\":" << sample.LimitSleepCalls
             << ",\"audio_queue_before\":" << sample.AudioQueueBefore
             << ",\"audio_queue_after\":" << sample.AudioQueueAfter
             << ",\"processor\":" << sample.Processor
             << ",\"input_lead\":" << sample.Netplay.InputLead
             << ",\"remote_wait_total_ms\":" << static_cast<double>(sample.Netplay.RemoteInputWaitUs) / 1000.0
             << ",\"throttle_total_ms\":" << static_cast<double>(sample.Netplay.FrameLeadThrottleUs) / 1000.0
             << "}\n";
        Write(line.str(), false);
    }

    void Write(const std::string& line, bool flush)
    {
        constexpr std::size_t MaxLogBytes = 8 * 1024 * 1024;
        if (!RotationDisabled && BytesWritten + line.size() > MaxLogBytes && !Path.empty())
        {
            Platform::FileFlush(File);
            Platform::CloseFile(File);
            File = Platform::OpenFile(Path.c_str(), Platform::FileMode::WriteText);
            BytesWritten = 0;
            if (!File)
                return;
        }
        const auto start = std::chrono::steady_clock::now();
        const auto written = Platform::FileWrite(line.data(), 1, line.size(), File);
        BytesWritten += static_cast<std::size_t>(written);
        if (flush)
            Platform::FileFlush(File);
        LastWriteMs = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count()) / 1000.0;
        if (written != line.size())
        {
            std::printf("NSMB PerformanceLog: short write expected=%zu actual=%llu\n",
                line.size(),
                static_cast<unsigned long long>(written));
            std::fflush(stdout);
        }
    }

    Platform::FileHandle* File = nullptr;
    std::string Path;
    bool RotationDisabled = false;
    std::size_t BytesWritten = 0;
    std::vector<NsmlPerformanceSample> Samples;
    unsigned long long LastSpikeUnixMs = 0;
    double LastWriteMs = 0.0;
    NsmbMvlNetplay::PerformanceCounters LastSummaryNetplay;
};

}


EmuThread::EmuThread(EmuInstance* inst, QObject* parent) : QThread(parent)
{
    emuInstance = inst;

    emuStatus = emuStatus_Paused;
    emuPauseStack = emuPauseStackRunning;
    emuActive = false;
}

void EmuThread::attachWindow(MainWindow* window)
{
    connect(this, SIGNAL(windowTitleChange(QString)), window, SLOT(onTitleUpdate(QString)));
    connect(this, SIGNAL(windowEmuStart()), window, SLOT(onEmuStart()));
    connect(this, SIGNAL(windowEmuStop()), window, SLOT(onEmuStop()));
    connect(this, SIGNAL(windowEmuPause(bool)), window, SLOT(onEmuPause(bool)));
    connect(this, SIGNAL(windowEmuReset()), window, SLOT(onEmuReset()));
    connect(this, SIGNAL(autoScreenSizingChange(int)), window->panel, SLOT(onAutoScreenSizingChanged(int)));
    connect(this, SIGNAL(windowFullscreenToggle()), window, SLOT(onFullscreenToggled()));
    connect(this, SIGNAL(screenEmphasisToggle()), window, SLOT(onScreenEmphasisToggled()));

    if (window->winHasMenu())
    {
        connect(this, SIGNAL(windowLimitFPSChange()), window->actLimitFramerate, SLOT(trigger()));
        connect(this, SIGNAL(swapScreensToggle()), window->actScreenSwap, SLOT(trigger()));
    }
}

void EmuThread::detachWindow(MainWindow* window)
{
    disconnect(this, SIGNAL(windowTitleChange(QString)), window, SLOT(onTitleUpdate(QString)));
    disconnect(this, SIGNAL(windowEmuStart()), window, SLOT(onEmuStart()));
    disconnect(this, SIGNAL(windowEmuStop()), window, SLOT(onEmuStop()));
    disconnect(this, SIGNAL(windowEmuPause(bool)), window, SLOT(onEmuPause(bool)));
    disconnect(this, SIGNAL(windowEmuReset()), window, SLOT(onEmuReset()));
    disconnect(this, SIGNAL(autoScreenSizingChange(int)), window->panel, SLOT(onAutoScreenSizingChanged(int)));
    disconnect(this, SIGNAL(windowFullscreenToggle()), window, SLOT(onFullscreenToggled()));
    disconnect(this, SIGNAL(screenEmphasisToggle()), window, SLOT(onScreenEmphasisToggled()));

    if (window->winHasMenu())
    {
        disconnect(this, SIGNAL(windowLimitFPSChange()), window->actLimitFramerate, SLOT(trigger()));
        disconnect(this, SIGNAL(swapScreensToggle()), window->actScreenSwap, SLOT(trigger()));
    }
}

void EmuThread::run()
{
#ifdef _WIN32
    const char* priorityText = getenv("MELONDS_NSML_EMU_THREAD_PRIORITY");
    if (priorityText && strcmp(priorityText, "normal") != 0)
    {
        const int priority = strcmp(priorityText, "highest") == 0
            ? THREAD_PRIORITY_HIGHEST
            : THREAD_PRIORITY_ABOVE_NORMAL;
        if (SetThreadPriority(GetCurrentThread(), priority))
            std::printf("NSMB Test: emulation thread priority=%s\n", priorityText);
        else
            std::printf("NSMB Test: failed to set emulation thread priority=%s error=%lu\n",
                priorityText, GetLastError());
        std::fflush(stdout);
    }
    if (const char* affinityText = getenv("MELONDS_NSML_EMU_THREAD_AFFINITY_MASK"))
    {
        const auto affinityMask = static_cast<DWORD_PTR>(strtoull(affinityText, nullptr, 0));
        if (affinityMask != 0)
        {
            if (SetThreadAffinityMask(GetCurrentThread(), affinityMask) != 0)
                std::printf("NSMB Test: emulation thread affinity mask=0x%llX\n",
                    static_cast<unsigned long long>(affinityMask));
            else
                std::printf("NSMB Test: failed to set emulation thread affinity mask=0x%llX error=%lu\n",
                    static_cast<unsigned long long>(affinityMask), GetLastError());
            std::fflush(stdout);
        }
    }
#endif

    Config::Table& globalCfg = emuInstance->getGlobalConfig();
    u32 mainScreenPos[3];

    //emuInstance->updateConsole();
    // No carts are inserted when melonDS first boots

    mainScreenPos[0] = 0;
    mainScreenPos[1] = 0;
    mainScreenPos[2] = 0;
    autoScreenSizing = 0;

    //videoSettingsDirty = false;

    if (emuInstance->usesOpenGL())
    {
        emuInstance->initOpenGL(0);

        useOpenGL = true;
        videoRenderer = globalCfg.GetInt("3D.Renderer");
    }
    else
    {
        useOpenGL = false;
        videoRenderer = 0;
    }

    //updateRenderer();
    videoSettingsDirty = true;

    u32 nframes = 0;
    double perfCountsSec = 1.0 / SDL_GetPerformanceFrequency();
    double lastTime = SDL_GetPerformanceCounter() * perfCountsSec;
    double frameLimitError = 0.0;
    double lastMeasureTime = lastTime;

    u32 winUpdateCount = 0, winUpdateFreq = 1;
    u8 dsiVolumeLevel = 0x1F;

    char melontitle[100];

    bool fastforward = false;
    bool slowmo = false;
    emuInstance->fastForwardToggled = false;
    emuInstance->slowmoToggled = false;
    NsmlPerformanceLog nsmlPerformanceLog(getenv("MELONDS_NSML_PERFORMANCE_LOG"));
    const bool nsmlPerformanceLogEnabled = nsmlPerformanceLog.Enabled();
    const bool nsmlPerfBreakdown = getenv("MELONDS_NSML_PERF_BREAKDOWN") != nullptr;
    const bool nsmlPerfSpikePhaseTrace = getenv("MELONDS_NSML_PERF_SPIKE_PHASE_TRACE") != nullptr;
    const bool nsmlPerfPhaseTiming = nsmlPerfBreakdown || nsmlPerfSpikePhaseTrace || nsmlPerformanceLogEnabled;
    const double nsmlPerfSpikePhaseThresholdMs = std::max(
        1.0,
        std::atof(getenv("MELONDS_NSML_FPS_SPIKE_THRESHOLD_MS")
            ? getenv("MELONDS_NSML_FPS_SPIKE_THRESHOLD_MS")
            : "25"));
    double nsmlPerfBeforeHook = 0.0;
    double nsmlPerfRunFrame = 0.0;
    double nsmlPerfAfterHook = 0.0;
    double nsmlPerfDraw = 0.0;
    double nsmlPerfAudio = 0.0;
    double nsmlPerfLimit = 0.0;
    u32 nsmlPerfFrames = 0;
    double nsmlPerfPhaseLastFrameEnd = nsmlPerfPhaseTiming
        ? SDL_GetPerformanceCounter() * perfCountsSec
        : 0.0;
    double nsmlPerfThreadCpuLast = nsmlPerformanceLogEnabled
        ? NsmlCurrentThreadCpuSeconds()
        : 0.0;
    nsmlPerformanceLog.WriteStartup(
        emuInstance->instanceID,
        useOpenGL,
        videoRenderer,
        emuInstance->targetFPS,
        emuInstance->doLimitFPS,
        emuInstance->doAudioSync,
        emuInstance->audioFreq,
        emuInstance->audioBufSize);
    if (nsmlPerfPhaseTiming)
        nsmlPerfPhaseLastFrameEnd = SDL_GetPerformanceCounter() * perfCountsSec;
    if (nsmlPerformanceLogEnabled)
        nsmlPerfThreadCpuLast = NsmlCurrentThreadCpuSeconds();

    while (emuStatus != emuStatus_Exit)
    {
        double nsmlPhaseMP = 0.0;
        double nsmlPhaseInput = 0.0;
        const double nsmlPhaseMPStart = nsmlPerfPhaseTiming
            ? SDL_GetPerformanceCounter() * perfCountsSec
            : 0.0;
        if (emuInstance->instanceID == 0)
            MPInterface::Get().Process();
        if (nsmlPerfPhaseTiming)
            nsmlPhaseMP = SDL_GetPerformanceCounter() * perfCountsSec - nsmlPhaseMPStart;

        const double nsmlPhaseInputStart = nsmlPerfPhaseTiming
            ? SDL_GetPerformanceCounter() * perfCountsSec
            : 0.0;
        emuInstance->inputProcess();
        if (nsmlPerfPhaseTiming)
            nsmlPhaseInput = SDL_GetPerformanceCounter() * perfCountsSec - nsmlPhaseInputStart;

        if (emuInstance->hotkeyPressed(HK_FrameLimitToggle)) emit windowLimitFPSChange();

        if (emuInstance->hotkeyPressed(HK_Pause)) emuTogglePause();
        if (emuInstance->hotkeyPressed(HK_Reset)) emuReset();
        if (emuInstance->hotkeyPressed(HK_FrameStep)) emuFrameStep();

        if (emuInstance->hotkeyPressed(HK_FullscreenToggle)) emit windowFullscreenToggle();

        if (emuInstance->hotkeyPressed(HK_SwapScreens)) emit swapScreensToggle();
        if (emuInstance->hotkeyPressed(HK_SwapScreenEmphasis)) emit screenEmphasisToggle();

        if (emuStatus == emuStatus_Running || emuStatus == emuStatus_FrameStep)
        {
            if (emuStatus == emuStatus_FrameStep) emuStatus = emuStatus_Paused;
            double nsmlPhaseBeforeHook = 0.0;
            double nsmlPhaseRunFrame = 0.0;
            double nsmlPhaseAfterHook = 0.0;
            double nsmlPhaseDraw = 0.0;
            double nsmlPhaseAudio = 0.0;
            double nsmlPhaseLimit = 0.0;
            double nsmlLimitRequestedMs = 0.0;
            double nsmlLimitMaxDelayMs = 0.0;
            double nsmlDeadlineLateMs = 0.0;
            unsigned int nsmlLimitYieldCalls = 0;
            unsigned int nsmlLimitSleepCalls = 0;
            melonDS::u32 nsmlAudioQueueBefore = 0;
            melonDS::u32 nsmlAudioQueueAfter = 0;

            if (emuInstance->hotkeyPressed(HK_SolarSensorDecrease))
            {
                int level = emuInstance->nds->GBACartSlot.SetInput(GBACart::Input_SolarSensorDown, true);
                if (level != -1)
                {
                    emuInstance->osdAddMessage(0, "Solar sensor level: %d", level);
                }
            }
            if (emuInstance->hotkeyPressed(HK_SolarSensorIncrease))
            {
                int level = emuInstance->nds->GBACartSlot.SetInput(GBACart::Input_SolarSensorUp, true);
                if (level != -1)
                {
                    emuInstance->osdAddMessage(0, "Solar sensor level: %d", level);
                }
            }

            if (emuInstance->nds->ConsoleType == 1)
            {
                DSi* dsi = static_cast<DSi*>(emuInstance->nds);
                double currentTime = SDL_GetPerformanceCounter() * perfCountsSec;

                // Handle power button
                if (emuInstance->hotkeyDown(HK_PowerButton))
                {
                    dsi->I2C.GetBPTWL()->SetPowerButtonHeld(currentTime);
                }
                else if (emuInstance->hotkeyReleased(HK_PowerButton))
                {
                    dsi->I2C.GetBPTWL()->SetPowerButtonReleased(currentTime);
                }

                // Handle volume buttons
                if (emuInstance->hotkeyDown(HK_VolumeUp))
                {
                    dsi->I2C.GetBPTWL()->SetVolumeSwitchHeld(DSi_BPTWL::volumeKey_Up);
                }
                else if (emuInstance->hotkeyReleased(HK_VolumeUp))
                {
                    dsi->I2C.GetBPTWL()->SetVolumeSwitchReleased(DSi_BPTWL::volumeKey_Up);
                }

                if (emuInstance->hotkeyDown(HK_VolumeDown))
                {
                    dsi->I2C.GetBPTWL()->SetVolumeSwitchHeld(DSi_BPTWL::volumeKey_Down);
                }
                else if (emuInstance->hotkeyReleased(HK_VolumeDown))
                {
                    dsi->I2C.GetBPTWL()->SetVolumeSwitchReleased(DSi_BPTWL::volumeKey_Down);
                }

                dsi->I2C.GetBPTWL()->ProcessVolumeSwitchInput(currentTime);
            }

            if (useOpenGL)
                emuInstance->makeCurrentGL();

            // update render settings if needed
            if (videoSettingsDirty)
            {
                emuInstance->renderLock.lock();
                if (useOpenGL)
                {
                    emuInstance->setVSyncGL(true);
                    videoRenderer = globalCfg.GetInt("3D.Renderer");
                }
#ifdef OGLRENDERER_ENABLED
                else
#endif
                {
                    videoRenderer = 0;
                }

                updateRenderer();

                videoSettingsDirty = false;
                emuInstance->renderLock.unlock();
            }

            // process input and hotkeys
            NsmbMvlNetplay::InputState inputState {
                emuInstance->inputMask,
                emuInstance->isTouching,
                emuInstance->touchX,
                emuInstance->touchY,
            };
            const double nsmlBeforeHookStart = nsmlPerfPhaseTiming
                ? SDL_GetPerformanceCounter() * perfCountsSec
                : 0.0;
            inputState = NsmbMvlNetplay::BeforeRunFrame(
                emuInstance->instanceID,
                emuInstance->nds->NumFrames,
                emuInstance->nds,
                inputState);
            if (nsmlPerfPhaseTiming)
            {
                nsmlPhaseBeforeHook = SDL_GetPerformanceCounter() * perfCountsSec - nsmlBeforeHookStart;
                if (nsmlPerfBreakdown)
                    nsmlPerfBeforeHook += nsmlPhaseBeforeHook;
            }

            emuInstance->nds->SetKeyMask(inputState.KeyMask);

            if (inputState.Touching)
                emuInstance->nds->TouchScreen(inputState.TouchX, inputState.TouchY);
            else
                emuInstance->nds->ReleaseScreen();

            if (emuInstance->hotkeyPressed(HK_Lid))
            {
                bool lid = !emuInstance->nds->IsLidClosed();
                emuInstance->nds->SetLidClosed(lid);
                emuInstance->osdAddMessage(0, lid ? "Lid closed" : "Lid opened");
            }

            // auto screen layout
            {
                mainScreenPos[2] = mainScreenPos[1];
                mainScreenPos[1] = mainScreenPos[0];
                mainScreenPos[0] = emuInstance->nds->PowerControl9 >> 15;

                int guess;
                if (mainScreenPos[0] == mainScreenPos[2] &&
                    mainScreenPos[0] != mainScreenPos[1])
                {
                    // constant flickering, likely displaying 3D on both screens
                    // TODO: when both screens are used for 2D only...???
                    guess = screenSizing_Even;
                }
                else
                {
                    if (mainScreenPos[0] == 1)
                        guess = screenSizing_EmphTop;
                    else
                        guess = screenSizing_EmphBot;
                }

                if (guess != autoScreenSizing)
                {
                    autoScreenSizing = guess;
                    emit autoScreenSizingChange(autoScreenSizing);
                }
            }

            // RTC sync
            emuInstance->syncRTC();


            // emulate
            u32 nlines;
            if (emuInstance->nds->GPU.GetRenderer().NeedsShaderCompile())
            {
                compileShaders();
                nlines = 1;
            }
            else
            {
                const u32 frameBeforeRun = emuInstance->nds->NumFrames;
                const double nsmlRunFrameStart = nsmlPerfPhaseTiming
                    ? SDL_GetPerformanceCounter() * perfCountsSec
                    : 0.0;
                nlines = emuInstance->nds->RunFrame();
                const double nsmlAfterRunFrameStart = nsmlPerfPhaseTiming
                    ? SDL_GetPerformanceCounter() * perfCountsSec
                    : 0.0;
                if (nsmlPerfPhaseTiming)
                {
                    nsmlPhaseRunFrame = nsmlAfterRunFrameStart - nsmlRunFrameStart;
                    if (nsmlPerfBreakdown)
                        nsmlPerfRunFrame += nsmlPhaseRunFrame;
                }
                NsmbMvlNetplay::AfterRunFrame(
                    emuInstance->instanceID,
                    frameBeforeRun,
                    emuInstance->nds);
                if (nsmlPerfPhaseTiming)
                {
                    nsmlPhaseAfterHook = SDL_GetPerformanceCounter() * perfCountsSec - nsmlAfterRunFrameStart;
                    if (nsmlPerfBreakdown)
                        nsmlPerfAfterHook += nsmlPhaseAfterHook;
                }
                if (NsmbMvlNetplay::ShouldQuitAfterFrame(emuInstance->instanceID, frameBeforeRun))
                    QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
            }

            if (emuInstance->ndsSave)
            {
                emuInstance->ndsSave->CheckFlush();
                SaveBootstrap::Observe(emuInstance->ndsSave.get(), emuInstance->nds->NumFrames);
            }

            if (emuInstance->gbaSave)
                emuInstance->gbaSave->CheckFlush();

            if (emuInstance->firmwareSave)
                emuInstance->firmwareSave->CheckFlush();

            const double nsmlDrawStart = nsmlPerfPhaseTiming
                ? SDL_GetPerformanceCounter() * perfCountsSec
                : 0.0;
            if (!getenv("MELONDS_NSML_NO_DRAW_SCREEN"))
                emuInstance->drawScreen();
            if (nsmlPerfPhaseTiming)
            {
                nsmlPhaseDraw = SDL_GetPerformanceCounter() * perfCountsSec - nsmlDrawStart;
                if (nsmlPerfBreakdown)
                    nsmlPerfDraw += nsmlPhaseDraw;
            }

#ifdef MELONCAP
            MelonCap::Update();
#endif // MELONCAP

            winUpdateCount++;
            if (winUpdateCount >= winUpdateFreq && !useOpenGL)
            {
                emit windowUpdate();
                winUpdateCount = 0;
            }
            
            if (emuInstance->hotkeyPressed(HK_FastForwardToggle)) emuInstance->fastForwardToggled = !emuInstance->fastForwardToggled;
            if (emuInstance->hotkeyPressed(HK_SlowMoToggle)) emuInstance->slowmoToggled = !emuInstance->slowmoToggled;

            if (emuInstance->hotkeyPressed(HK_AudioMuteToggle)) emuInstance->toggleAudioMute();

            bool enablefastforward = emuInstance->hotkeyDown(HK_FastForward) | emuInstance->fastForwardToggled;
            bool enableslowmo = emuInstance->hotkeyDown(HK_SlowMo) | emuInstance->slowmoToggled;

            if (useOpenGL)
            {
                // when using OpenGL: when toggling fast-forward or slowmo, change the vsync interval
                if ((enablefastforward || enableslowmo) && !(fastforward || slowmo))
                {
                    emuInstance->setVSyncGL(false);
                }
                else if (!(enablefastforward || enableslowmo) && (fastforward || slowmo))
                {
                    emuInstance->setVSyncGL(true);
                }
            }

            fastforward = enablefastforward;
            slowmo = enableslowmo;
            emuInstance->updateFastForwardMute(fastforward);

            if (slowmo) emuInstance->curFPS = emuInstance->slowmoFPS;
            else if (fastforward) emuInstance->curFPS = emuInstance->fastForwardFPS;
            else if (!emuInstance->doLimitFPS && !emuInstance->doAudioSync) emuInstance->curFPS = 1000.0;
            else emuInstance->curFPS = emuInstance->targetFPS;

            if (emuInstance->audioDSiVolumeSync && emuInstance->nds->ConsoleType == 1)
            {
                DSi* dsi = static_cast<DSi*>(emuInstance->nds);
                u8 volumeLevel = dsi->I2C.GetBPTWL()->GetVolumeLevel();
                if (volumeLevel != dsiVolumeLevel)
                {
                    dsiVolumeLevel = volumeLevel;
                    emit syncVolumeLevel();
                }

                emuInstance->audioVolume = volumeLevel * (256.0 / 31.0);
            }

            const double nsmlAudioStart = nsmlPerfPhaseTiming
                ? SDL_GetPerformanceCounter() * perfCountsSec
                : 0.0;
            if (nsmlPerformanceLogEnabled)
                nsmlAudioQueueBefore = emuInstance->nds->SPU.GetOutputSize();
            if (emuInstance->doAudioSync && !(fastforward || slowmo))
                emuInstance->audioSync();
            if (nsmlPerformanceLogEnabled)
                nsmlAudioQueueAfter = emuInstance->nds->SPU.GetOutputSize();
            if (nsmlPerfPhaseTiming)
            {
                nsmlPhaseAudio = SDL_GetPerformanceCounter() * perfCountsSec - nsmlAudioStart;
                if (nsmlPerfBreakdown)
                    nsmlPerfAudio += nsmlPhaseAudio;
            }

            double frametimeStep = nlines / (emuInstance->curFPS * 263.0);
            const bool nsmlFixedFrameTimestep =
                !getenv("MELONDS_NSML_NO_FIXED_FRAME_TIMESTEP")
                && (getenv("MELONDS_NSML_FIXED_FRAME_TIMESTEP")
                    || getenv("MELONDS_NSML_INPUT_NETPLAY_ONLY"));
#ifdef _WIN32
            static bool nsmlTimerPeriodSet = false;
            if (nsmlFixedFrameTimestep && !nsmlTimerPeriodSet)
            {
                timeBeginPeriod(1);
                nsmlTimerPeriodSet = true;
            }
#endif
            static bool nsmlFixedFrameTimestepLogged = false;
            if (nsmlFixedFrameTimestep && !nsmlFixedFrameTimestepLogged)
            {
                std::printf("NSMB Test: fixed frame timestep enabled for netplay timing\n");
                std::fflush(stdout);
                nsmlFixedFrameTimestepLogged = true;
            }
            if (nsmlFixedFrameTimestep)
                frametimeStep = 1.0 / emuInstance->curFPS;

            if (frametimeStep < 0.001) frametimeStep = 0.001;

            if (emuInstance->doLimitFPS)
            {
                const double nsmlLimitStart = nsmlPerfPhaseTiming
                    ? SDL_GetPerformanceCounter() * perfCountsSec
                    : 0.0;
                double curtime = SDL_GetPerformanceCounter() * perfCountsSec;

                if (nsmlFixedFrameTimestep)
                {
                    lastTime += frametimeStep;
                    if (lastTime < curtime - frametimeStep)
                        lastTime = curtime;
                    nsmlLimitRequestedMs = std::max(0.0, (lastTime - curtime) * 1000.0);
                    for (;;)
                    {
                        curtime = SDL_GetPerformanceCounter() * perfCountsSec;
                        const double remaining = lastTime - curtime;
                        if (remaining <= 0.0)
                            break;
                        if (remaining > 0.003 && getenv("MELONDS_NSML_FIXED_FRAME_SLEEP"))
                        {
                            const Uint32 sleepMs = (Uint32)std::max(1.0, floor((remaining * 1000.0) - 1.0));
                            const double delayStart = SDL_GetPerformanceCounter() * perfCountsSec;
                            SDL_Delay(sleepMs);
                            const double delayMs = (SDL_GetPerformanceCounter() * perfCountsSec - delayStart) * 1000.0;
                            nsmlLimitMaxDelayMs = std::max(nsmlLimitMaxDelayMs, delayMs);
                            nsmlLimitSleepCalls++;
                        }
                        else if (remaining > 0.001)
                        {
                            const double delayStart = SDL_GetPerformanceCounter() * perfCountsSec;
                            SDL_Delay(0);
                            const double delayMs = (SDL_GetPerformanceCounter() * perfCountsSec - delayStart) * 1000.0;
                            nsmlLimitMaxDelayMs = std::max(nsmlLimitMaxDelayMs, delayMs);
                            nsmlLimitYieldCalls++;
                        }
                    }
                    nsmlDeadlineLateMs = std::max(0.0, (curtime - lastTime) * 1000.0);
                    frameLimitError = 0.0;
                    if (nsmlPerfPhaseTiming)
                    {
                        nsmlPhaseLimit = SDL_GetPerformanceCounter() * perfCountsSec - nsmlLimitStart;
                        if (nsmlPerfBreakdown)
                            nsmlPerfLimit += nsmlPhaseLimit;
                    }
                    goto frame_limit_done;
                }

                frameLimitError += frametimeStep - (curtime - lastTime);
                if (frameLimitError < -frametimeStep)
                    frameLimitError = -frametimeStep;
                if (frameLimitError > frametimeStep)
                    frameLimitError = frametimeStep;

                if (nsmlFixedFrameTimestep && frameLimitError > 0.0)
                {
                    const double targetTime = curtime + frameLimitError;
                    for (;;)
                    {
                        curtime = SDL_GetPerformanceCounter() * perfCountsSec;
                        const double remaining = targetTime - curtime;
                        if (remaining <= 0.0)
                            break;
                        if (remaining > 0.003)
                            SDL_Delay(1);
                    }
                    frameLimitError = 0.0;
                }
                else if (round(frameLimitError * 1000.0) > 0.0)
                {
                    SDL_Delay(round(frameLimitError * 1000.0));
                    double timeBeforeSleep = curtime;
                    curtime = SDL_GetPerformanceCounter() * perfCountsSec;
                    frameLimitError -= curtime - timeBeforeSleep;
                }

                lastTime = curtime;
                if (nsmlPerfPhaseTiming)
                {
                    nsmlPhaseLimit = SDL_GetPerformanceCounter() * perfCountsSec - nsmlLimitStart;
                    if (nsmlPerfBreakdown)
                        nsmlPerfLimit += nsmlPhaseLimit;
                }
            }
frame_limit_done:

            const double nsmlPhaseEnd = nsmlPerfPhaseTiming
                ? SDL_GetPerformanceCounter() * perfCountsSec
                : 0.0;
            const double nsmlPhaseTotal = nsmlPerfPhaseTiming
                ? nsmlPhaseEnd - nsmlPerfPhaseLastFrameEnd
                : 0.0;
            if (nsmlPerfPhaseTiming)
                nsmlPerfPhaseLastFrameEnd = nsmlPhaseEnd;

            if (nsmlPerfSpikePhaseTrace)
            {
                if (nsmlPhaseTotal * 1000.0 >= nsmlPerfSpikePhaseThresholdMs)
                {
                    const double nsmlPhaseAccounted =
                        nsmlPhaseMP
                        + nsmlPhaseInput
                        + nsmlPhaseBeforeHook
                        + nsmlPhaseRunFrame
                        + nsmlPhaseAfterHook
                        + nsmlPhaseDraw
                        + nsmlPhaseAudio
                        + nsmlPhaseLimit;
                    std::printf(
                        "NSMB PerfPhaseSpike: inst=%d frame=%u totalMs=%.3f mpMs=%.3f inputMs=%.3f beforeHookMs=%.3f runFrameMs=%.3f afterHookMs=%.3f drawMs=%.3f audioMs=%.3f limitMs=%.3f unaccountedMs=%.3f\n",
                        emuInstance->instanceID,
                        emuInstance->nds->NumFrames,
                        nsmlPhaseTotal * 1000.0,
                        nsmlPhaseMP * 1000.0,
                        nsmlPhaseInput * 1000.0,
                        nsmlPhaseBeforeHook * 1000.0,
                        nsmlPhaseRunFrame * 1000.0,
                        nsmlPhaseAfterHook * 1000.0,
                        nsmlPhaseDraw * 1000.0,
                        nsmlPhaseAudio * 1000.0,
                        nsmlPhaseLimit * 1000.0,
                        std::max(0.0, nsmlPhaseTotal - nsmlPhaseAccounted) * 1000.0);
                }
            }

            if (nsmlPerformanceLogEnabled)
            {
                const double threadCpuNow = NsmlCurrentThreadCpuSeconds();
                const double threadCpuMs = std::max(0.0, threadCpuNow - nsmlPerfThreadCpuLast) * 1000.0;
                nsmlPerfThreadCpuLast = threadCpuNow;
                const double accountedMs = (
                    nsmlPhaseMP
                    + nsmlPhaseInput
                    + nsmlPhaseBeforeHook
                    + nsmlPhaseRunFrame
                    + nsmlPhaseAfterHook
                    + nsmlPhaseDraw
                    + nsmlPhaseAudio
                    + nsmlPhaseLimit) * 1000.0;
                NsmlPerformanceSample sample;
                sample.Frame = emuInstance->nds->NumFrames;
                sample.TotalMs = nsmlPhaseTotal * 1000.0;
                sample.ThreadCpuMs = threadCpuMs;
                sample.MpMs = nsmlPhaseMP * 1000.0;
                sample.InputMs = nsmlPhaseInput * 1000.0;
                sample.BeforeHookMs = nsmlPhaseBeforeHook * 1000.0;
                sample.RunFrameMs = nsmlPhaseRunFrame * 1000.0;
                sample.AfterHookMs = nsmlPhaseAfterHook * 1000.0;
                sample.DrawMs = nsmlPhaseDraw * 1000.0;
                sample.AudioMs = nsmlPhaseAudio * 1000.0;
                sample.LimitMs = nsmlPhaseLimit * 1000.0;
                sample.UnaccountedMs = std::max(0.0, sample.TotalMs - accountedMs);
                sample.LimitRequestedMs = nsmlLimitRequestedMs;
                sample.LimitMaxDelayMs = nsmlLimitMaxDelayMs;
                sample.DeadlineLateMs = nsmlDeadlineLateMs;
                sample.LimitYieldCalls = nsmlLimitYieldCalls;
                sample.LimitSleepCalls = nsmlLimitSleepCalls;
                sample.AudioQueueBefore = nsmlAudioQueueBefore;
                sample.AudioQueueAfter = nsmlAudioQueueAfter;
                sample.Processor = NsmlCurrentProcessorNumber();
                sample.Netplay = NsmbMvlNetplay::GetPerformanceCounters();
                nsmlPerformanceLog.Add(sample);
            }

            if (nsmlPerfBreakdown)
            {
                nsmlPerfFrames++;
                if (nsmlPerfFrames >= 300)
                {
                    const double scale = 1000.0 / static_cast<double>(nsmlPerfFrames);
                    std::printf(
                        "NSMB Perf: inst=%d frame=%u beforeHookMs=%.3f runFrameMs=%.3f afterHookMs=%.3f drawMs=%.3f audioMs=%.3f limitMs=%.3f\n",
                        emuInstance->instanceID,
                        emuInstance->nds->NumFrames,
                        nsmlPerfBeforeHook * scale,
                        nsmlPerfRunFrame * scale,
                        nsmlPerfAfterHook * scale,
                        nsmlPerfDraw * scale,
                        nsmlPerfAudio * scale,
                        nsmlPerfLimit * scale);
                    std::fflush(stdout);
                    nsmlPerfBeforeHook = 0.0;
                    nsmlPerfRunFrame = 0.0;
                    nsmlPerfAfterHook = 0.0;
                    nsmlPerfDraw = 0.0;
                    nsmlPerfAudio = 0.0;
                    nsmlPerfLimit = 0.0;
                    nsmlPerfFrames = 0;
                }
            }

            nframes++;
            if (nframes >= 30)
            {
                double time = SDL_GetPerformanceCounter() * perfCountsSec;
                double dt = time - lastMeasureTime;
                lastMeasureTime = time;

                u32 fps = round(nframes / dt);
                nframes = 0;

                float fpstarget = 1.0/frametimeStep;

                winUpdateFreq = fps / (u32)round(fpstarget);
                if (winUpdateFreq < 1)
                    winUpdateFreq = 1;
                    
                double actualfps = (59.8261 * 263.0) / nlines;
                snprintf(melontitle, sizeof(melontitle), "[%d/%.0f] melonDS " MELONDS_VERSION, fps, actualfps);
                changeWindowTitle(melontitle);
            }
        }
        else
        {
            // paused
            nframes = 0;
            lastTime = SDL_GetPerformanceCounter() * perfCountsSec;
            lastMeasureTime = lastTime;

            emit windowUpdate();

            snprintf(melontitle, sizeof(melontitle), "melonDS " MELONDS_VERSION);
            changeWindowTitle(melontitle);

            SDL_Delay(75);
            if (nsmlPerfPhaseTiming)
                nsmlPerfPhaseLastFrameEnd = SDL_GetPerformanceCounter() * perfCountsSec;

            if (!getenv("MELONDS_NSML_NO_DRAW_SCREEN"))
                emuInstance->drawScreen();
        }

        handleMessages();
    }
}

void EmuThread::sendMessage(Message msg)
{
    msgMutex.lock();
    msgQueue.enqueue(msg);
    msgMutex.unlock();
}

void EmuThread::waitMessage(int num)
{
    if (QThread::currentThread() == this) return;
    msgSemaphore.acquire(num);
}

void EmuThread::waitAllMessages()
{
    if (QThread::currentThread() == this) return;
    while (!msgQueue.empty())
        msgSemaphore.acquire();
}

void EmuThread::handleMessages()
{
    bool glborrow = false;

    msgMutex.lock();
    while (!msgQueue.empty())
    {
        Message msg = msgQueue.dequeue();
        switch (msg.type)
        {
        case msg_Exit:
            emuStatus = emuStatus_Exit;
            emuPauseStack = emuPauseStackRunning;

            emuInstance->audioDisable();
            MPInterface::Get().End(emuInstance->instanceID);
            break;

        case msg_EmuRun:
            emuStatus = emuStatus_Running;
            emuPauseStack = emuPauseStackRunning;
            emuActive = true;

            emuInstance->audioEnable();
            emit windowEmuStart();
            break;

        case msg_EmuPause:
            emuPauseStack++;
            if (emuPauseStack > emuPauseStackPauseThreshold) break;

            prevEmuStatus = emuStatus;
            emuStatus = emuStatus_Paused;

            if (prevEmuStatus != emuStatus_Paused)
            {
                emuInstance->audioDisable();
                emit windowEmuPause(true);
                emuInstance->osdAddMessage(0, "Paused");
            }
            break;

        case msg_EmuUnpause:
            if (emuPauseStack < emuPauseStackPauseThreshold) break;

            emuPauseStack--;
            if (emuPauseStack >= emuPauseStackPauseThreshold) break;

            emuStatus = prevEmuStatus;

            if (emuStatus != emuStatus_Paused)
            {
                emuInstance->audioEnable();
                emit windowEmuPause(false);
                emuInstance->osdAddMessage(0, "Resumed");
            }
            break;

        case msg_EmuStop:
            if (msg.param.value<bool>())
                emuInstance->nds->Stop();
            emuStatus = emuStatus_Paused;
            emuActive = false;

            emuInstance->audioDisable();
            emit windowEmuStop();
            break;

        case msg_EmuFrameStep:
            emuStatus = emuStatus_FrameStep;
            break;

        case msg_EmuReset:
            emuInstance->reset();

            emuStatus = emuStatus_Running;
            emuPauseStack = emuPauseStackRunning;
            emuActive = true;

            emuInstance->audioEnable();
            emit windowEmuReset();
            emuInstance->osdAddMessage(0, "Reset");
            break;

        case msg_InitGL:
            emuInstance->initOpenGL(msg.param.value<int>());
            useOpenGL = true;
            break;

        case msg_DeInitGL:
            emuInstance->deinitOpenGL(msg.param.value<int>());
            if (msg.param.value<int>() == 0)
                useOpenGL = false;
            break;

        case msg_BorrowGL:
            emuInstance->releaseGL();
            glborrow = true;
            break;

        case msg_BootROM:
            msgResult = 0;
            if (!emuInstance->loadROM(msg.param.value<QStringList>(), true, msgError))
                break;

            assert(emuInstance->nds != nullptr);
            emuInstance->nds->Start();
            msgResult = 1;
            break;

        case msg_BootFirmware:
            msgResult = 0;
            if (!emuInstance->bootToMenu(msgError))
                break;

            assert(emuInstance->nds != nullptr);
            emuInstance->nds->Start();
            msgResult = 1;
            break;

        case msg_InsertCart:
            msgResult = 0;
            if (!emuInstance->loadROM(msg.param.value<QStringList>(), false, msgError))
                break;

            msgResult = 1;
            break;

        case msg_EjectCart:
            emuInstance->ejectCart();
            break;

        case msg_InsertGBACart:
            msgResult = 0;
            if (!emuInstance->loadGBAROM(msg.param.value<QStringList>(), msgError))
                break;

            msgResult = 1;
            break;

        case msg_InsertGBAAddon:
            msgResult = 0;
            emuInstance->loadGBAAddon(msg.param.value<int>(), msgError);
            msgResult = 1;
            break;

        case msg_EjectGBACart:
            emuInstance->ejectGBACart();
            break;

        case msg_SaveState:
            msgResult = emuInstance->saveState(msg.param.value<QString>().toStdString());
            break;

        case msg_LoadState:
            msgResult = emuInstance->loadState(msg.param.value<QString>().toStdString());
            break;

        case msg_UndoStateLoad:
            emuInstance->undoStateLoad();
            msgResult = 1;
            break;

        case msg_ImportSavefile:
            {
                msgResult = 0;
                auto f = Platform::OpenFile(msg.param.value<QString>().toStdString(), Platform::FileMode::Read);
                if (!f) break;

                u32 len = FileLength(f);

                std::unique_ptr<u8[]> data = std::make_unique<u8[]>(len);
                Platform::FileRewind(f);
                Platform::FileRead(data.get(), len, 1, f);

                assert(emuInstance->nds != nullptr);
                emuInstance->nds->SetNDSSave(data.get(), len);

                CloseFile(f);
                msgResult = 1;
            }
            break;

        case msg_EnableCheats:
            emuInstance->enableCheats(msg.param.value<bool>());
            break;
        }

        msgSemaphore.release();
    }
    msgMutex.unlock();

    if (glborrow)
    {
        glBorrowMutex.lock();
        glBorrowCond.wait(&glBorrowMutex);
        glBorrowMutex.unlock();
    }
}

void EmuThread::changeWindowTitle(char* title)
{
    emit windowTitleChange(QString(title));
}

void EmuThread::initContext(int win)
{
    sendMessage({.type = msg_InitGL, .param = win});
    waitMessage();
}

void EmuThread::deinitContext(int win)
{
    sendMessage({.type = msg_DeInitGL, .param = win});
    waitMessage();
}

void EmuThread::borrowGL()
{
    sendMessage(msg_BorrowGL);
    waitMessage();
}

void EmuThread::returnGL()
{
    glBorrowMutex.lock();
    glBorrowCond.wakeAll();
    glBorrowMutex.unlock();
}

void EmuThread::emuRun()
{
    sendMessage(msg_EmuRun);
    waitMessage();
}

void EmuThread::emuPause(bool broadcast)
{
    sendMessage(msg_EmuPause);
    waitMessage();

    if (broadcast)
        emuInstance->broadcastCommand(InstCmd_Pause);
}

void EmuThread::emuUnpause(bool broadcast)
{
    sendMessage(msg_EmuUnpause);
    waitMessage();

    if (broadcast)
        emuInstance->broadcastCommand(InstCmd_Unpause);
}

void EmuThread::emuTogglePause(bool broadcast)
{
    if (emuStatus == emuStatus_Paused)
        emuUnpause(broadcast);
    else
        emuPause(broadcast);
}

void EmuThread::emuStop(bool external)
{
    sendMessage({.type = msg_EmuStop, .param = external});
    waitMessage();
}

void EmuThread::emuExit()
{
    sendMessage(msg_Exit);
    waitAllMessages();
}

void EmuThread::emuFrameStep()
{
    if (emuPauseStack < emuPauseStackPauseThreshold)
        sendMessage(msg_EmuPause);
    sendMessage(msg_EmuFrameStep);
    waitAllMessages();
}

void EmuThread::emuReset()
{
    sendMessage(msg_EmuReset);
    waitMessage();
}

bool EmuThread::emuIsRunning()
{
    return emuStatus == emuStatus_Running;
}

bool EmuThread::emuIsActive()
{
    return emuActive;
}

int EmuThread::bootROM(const QStringList& filename, QString& errorstr)
{
    sendMessage({.type = msg_BootROM, .param = filename});
    waitMessage();
    if (!msgResult)
    {
        errorstr = msgError;
        return msgResult;
    }

    sendMessage(msg_EmuRun);
    waitMessage();
    errorstr = "";
    return msgResult;
}

int EmuThread::bootFirmware(QString& errorstr)
{
    sendMessage(msg_BootFirmware);
    waitMessage();
    if (!msgResult)
    {
        errorstr = msgError;
        return msgResult;
    }

    sendMessage(msg_EmuRun);
    waitMessage();
    errorstr = "";
    return msgResult;
}

int EmuThread::insertCart(const QStringList& filename, bool gba, QString& errorstr)
{
    MessageType msgtype = gba ? msg_InsertGBACart : msg_InsertCart;

    sendMessage({.type = msgtype, .param = filename});
    waitMessage();
    errorstr = msgResult ? "" : msgError;
    return msgResult;
}

void EmuThread::ejectCart(bool gba)
{
    sendMessage(gba ? msg_EjectGBACart : msg_EjectCart);
    waitMessage();
}

int EmuThread::insertGBAAddon(int type, QString& errorstr)
{
    sendMessage({.type = msg_InsertGBAAddon, .param = type});
    waitMessage();
    errorstr = msgResult ? "" : msgError;
    return msgResult;
}

int EmuThread::saveState(const QString& filename)
{
    sendMessage({.type = msg_SaveState, .param = filename});
    waitMessage();
    return msgResult;
}

int EmuThread::loadState(const QString& filename)
{
    sendMessage({.type = msg_LoadState, .param = filename});
    waitMessage();
    return msgResult;
}

int EmuThread::undoStateLoad()
{
    sendMessage(msg_UndoStateLoad);
    waitMessage();
    return msgResult;
}

int EmuThread::importSavefile(const QString& filename)
{
    sendMessage(msg_EmuReset);
    sendMessage({.type = msg_ImportSavefile, .param = filename});
    waitMessage(2);
    return msgResult;
}

void EmuThread::enableCheats(bool enable)
{
    sendMessage({.type = msg_EnableCheats, .param = enable});
    waitMessage();
}

void EmuThread::updateRenderer()
{
    auto nds = emuInstance->nds;

    if (videoRenderer != lastVideoRenderer)
    {
        switch (videoRenderer)
        {
            case renderer3D_Software:
                nds->SetRenderer(std::make_unique<SoftRenderer>(*nds));
                break;
            case renderer3D_OpenGL:
                nds->SetRenderer(std::make_unique<GLRenderer>(*nds, false));
                break;
            case renderer3D_OpenGLCompute:
                nds->SetRenderer(std::make_unique<GLRenderer>(*nds, true));
                break;
            default: __builtin_unreachable();
        }
    }
    lastVideoRenderer = videoRenderer;

    auto& cfg = emuInstance->getGlobalConfig();
    melonDS::RendererSettings settings = {
        .ScaleFactor = cfg.GetInt("3D.GL.ScaleFactor"),
        .Threaded = cfg.GetBool("3D.Soft.Threaded"),
        .HiresCoordinates = cfg.GetBool("3D.GL.HiresCoordinates"),
        .BetterPolygons = cfg.GetBool("3D.GL.BetterPolygons")
    };

    nds->GetRenderer().SetRenderSettings(settings);
}

void EmuThread::compileShaders()
{
    auto& renderer = emuInstance->nds->GPU.GetRenderer();
    int currentShader, shadersCount;
    u64 startTime = SDL_GetPerformanceCounter();
    // kind of hacky to look at the wallclock, though it is easier than
    // than disabling vsync
    do
    {
        renderer.ShaderCompileStep(currentShader, shadersCount);
    }
    while (renderer.NeedsShaderCompile() &&
             (SDL_GetPerformanceCounter() - startTime) * perfCountsSec < 1.0 / 6.0);
    emuInstance->osdAddMessage(0, "Compiling shader %d/%d", currentShader+1, shadersCount);
}

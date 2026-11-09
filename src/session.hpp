#pragma once
#include "config.hpp"
#include "lep.hpp"
#include "win.hpp"
#include <string>
#include <vector>

struct LaunchSpec {
    std::wstring executable;
    std::wstring arguments;
    bool claudeDesktop = false;
    bool useLep = true;
};

class Session {
public:
    ~Session();

    bool active() const { return active_; }
    DWORD pid() const { return pid_; }
    DWORD processCount() const;
    std::wstring statusText() const;
    std::wstring takeFailure();

    bool start(const Config& config, const LaunchSpec& spec, std::wstring& error,
               HANDLE cancelEvent = nullptr);
    void poll();
    void stop(const std::wstring& reason);

    // Runs EnvironmentProbe.exe under LEP in its own Job Object. Returns the
    // probe exit code, or -1 on setup/timeout failure.
    int runProbe(const Config& config, const std::wstring& arguments,
                 const std::wstring& probeLogPath, DWORD timeoutMs, std::wstring& error,
                 std::wstring& outputTail, HANDLE cancelEvent = nullptr);
    const std::wstring& lastProbeLog() const { return lastProbeLog_; }

private:
    bool preflight(const Config& config, std::wstring& error, HANDLE cancelEvent);
    bool hasLepLog(DWORD pid);
    bool isManagedImagePath(const std::wstring& path) const;
    bool handleLepFailure(DWORD childPid, const std::wstring& line);
    void verifyProcesses();
    void scanLepLogs();
    void failClosed(const std::wstring& detail);

    Handle job_;
    Handle process_;
    DWORD pid_ = 0;
    bool active_ = false;
    bool mainVerified_ = false;
    ULONGLONG startTick_ = 0;
    ULONGLONG lastVerifyTick_ = 0;
    ULONGLONG lastLogScanTick_ = 0;
    FILETIME startFileTime_{};
    Config config_;
    LepLoader lep_;
    std::wstring failure_;
    std::wstring lepLogDir_;
    std::vector<DWORD> knownPids_;
    std::vector<DWORD> warnedPids_;
    std::vector<DWORD> logVerifiedPids_;
    std::vector<DWORD> lepInjectedPids_;
    std::vector<std::wstring> seenLogFailures_;
    std::wstring targetDirectory_;
    std::wstring lastProbeLog_;
};

#include "../src/lep.hpp"
#include "../src/win.hpp"
#include <cstdio>

// Build this driver and EnvironmentProbe for the same architecture. It uses
// the matching loader, so WinRT can be checked on x86 without launching apps.
int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return 2;
    LepLoader loader;
    std::wstring error, warning;
    if (!loader.load(joinPath(argv[1], defaultLoaderFileName()), error, warning)) {
        fwprintf(stderr, L"%ls\n", error.c_str());
        return 1;
    }
    SetEnvironmentVariableW(L"CP_TIMEZONE", L"Pacific Standard Time");
    SetEnvironmentVariableW(L"CP_PROBE_LOG", argv[3]);
    SetEnvironmentVariableW(L"CP_LEP_LOG_DIR", joinPath(argv[1], L"Log").c_str());
    DeleteFileW(argv[3]);
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.h || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return 1;
    LepLaunchConfig cfg;
    cfg.timezoneId = L"Pacific Standard Time";
    cfg.localeId = 0x409;
    cfg.ansiCodePage = 1252;
    cfg.oemCodePage = 437;
    cfg.uiLanguageMode = 2;
    cfg.registryRedirectionMode = 2;
    std::wstring command = L"\"" + std::wstring(argv[2]) + L"\" --spawn-child --json --expect-lep --expect-job "
        L"--expect-timezone \"Pacific Standard Time\" --expect-locale en-US --expect-ui 1033 --expect-preferred";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (loader.createSuspended(cfg, argv[2], &command[0], directory(argv[2]).c_str(), 0,
                               &startup, &info, nullptr, error) < 0) {
        fwprintf(stderr, L"%ls\n", error.c_str());
        return 1;
    }
    Handle process(info.hProcess), thread(info.hThread);
    if (!AssignProcessToJobObject(job, process) || ResumeThread(thread) == (DWORD)-1) {
        TerminateProcess(process, 1);
        return 1;
    }
    if (WaitForSingleObject(process, 30000) != WAIT_OBJECT_0) return 1;
    DWORD code = 1;
    GetExitCodeProcess(process, &code);
    return (int)code;
}

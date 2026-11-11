// Exercise the callbacks used by the control panel and tray in one supervisor.
// No UI automation or access to another running supervisor is involved.
#include "../src/main.cpp"

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 3) return 2;
    bool realApplications = argc == 3 && std::wstring(argv[2]) == L"--applications";
    baseDir = argv[1];
    logPath = joinPath(baseDir, L"ClaudeProtect.log");
    auto require = [&](bool ok, const char* message) {
        if (ok) return;
        fprintf(stderr, "FAIL: %s\n", message);
        cancelOperation();
        for (auto& managed : sessions) managed->stop(L"test-failure");
        exit(1);
    };
    std::wstring error;
    require(loadAndPrepare(config, error), "load fixture configuration");
    auto actions = controlActions();
    auto finishStart = [&](size_t index) {
        auto deadline = GetTickCount64() + 15000;
        while (operation.valid() && GetTickCount64() < deadline) {
            pollOperation();
            Sleep(20);
        }
        require(!operation.valid() && sessions[index]->active(), "background session transferred");
        deadline = GetTickCount64() + 5000;
        while (sessions[index]->processCount() < 2 && GetTickCount64() < deadline) Sleep(20);
        require(sessions[index]->processCount() >= 2, "application child is in its Job");
    };

    actions.command(UiCommand::Start, L"desktop");
    finishStart(0);
    DWORD desktopPid = sessions[0]->pid();
    actions.command(UiCommand::Start, L"science");
    finishStart(1);
    DWORD sciencePid = sessions[1]->pid();
    auto status = actions.status();
    require(status.applications.size() == 2 && status.applications[0].active &&
            status.applications[1].active && status.processCount >= 4,
            "one control panel reports both live process trees");

    if (realApplications) {
        printf("Both applications started in supervisor %lu, Desktop %lu, Science %lu.\n",
               GetCurrentProcessId(), desktopPid, sciencePid);
        fflush(stdout);
        auto deadline = GetTickCount64() + 20000;
        while (GetTickCount64() < deadline) {
            for (auto& managed : sessions) managed->poll();
            require(sessions[0]->active() && sessions[1]->active(), "both real applications remain active");
            Sleep(200);
        }
    }

    Sleep(1500);
    for (auto& managed : sessions) managed->poll();
    require(sessions[0]->active() && sessions[1]->active(), "strict verification keeps both sessions active");
    actions.command(UiCommand::Stop, L"science");
    require(!sessions[1]->active() && sessions[0]->active() && sessions[0]->pid() == desktopPid,
            "stopping Science preserves Desktop");
    actions.command(UiCommand::Start, L"science");
    finishStart(1);
    sciencePid = sessions[1]->pid();

    Handle desktop(OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, desktopPid));
    require(desktop.h && TerminateProcess(desktop, 0), "simulate Desktop main exit");
    require(WaitForSingleObject(desktop, 3000) == WAIT_OBJECT_0, "Desktop main exited");
    sessions[0]->poll();
    require(!sessions[0]->active() && sessions[1]->active() && sessions[1]->pid() == sciencePid,
            "Desktop main exit preserves Science");

    actions.command(UiCommand::Start, L"desktop");
    cancelOperation();
    require(!sessions[0]->active() && sessions[1]->active(), "cancelled start preserves other session");
    actions.command(UiCommand::Start, L"desktop");
    finishStart(0);
    windowProc(nullptr, WM_QUERYENDSESSION, 0, 0);
    require(!sessions[0]->active() && !sessions[1]->active(), "shutdown stops both Jobs");
    puts("Dual sessions, independent Stop/main exit, cancellation, strict verification and shutdown passed.");
    return 0;
}

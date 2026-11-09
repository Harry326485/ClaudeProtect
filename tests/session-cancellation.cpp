#include "../src/session.hpp"
#include <cstdio>
#include <thread>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    Config config;
    std::wstring error, tail;
    auto require = [&](bool ok, const char* message) {
        if (!ok) { fprintf(stderr, "FAIL: %s (%ls)\n", message, error.c_str()); exit(1); }
    };
    require(loadConfig(argv[1], config, error) && prepareConfig(config, error), "load configuration");
    Handle cancel(CreateEventW(nullptr, TRUE, TRUE, nullptr));
    require(cancel.h != nullptr, "create cancellation event");
    Session session;
    LaunchSpec spec;
    spec.executable = config.probePath;
    spec.arguments = L"--seconds 60";
    spec.useLep = false;
    require(!session.start(config, spec, error, cancel) && !session.active(),
            "cancelled launch never becomes active");
    if (!fileExists(joinPath(config.lepDir, L"LoaderDll_x64.dll"))) {
        puts("Cancelled direct launch passed. LEP probe cancellation skipped (DLLs absent).");
        return 0;
    }
    ResetEvent(cancel);
    std::thread signal([&] { Sleep(1000); SetEvent(cancel); });
    auto start = GetTickCount64();
    int code = session.runProbe(config, L"--spawn-child --seconds 60 --child-seconds 60",
        joinPath(config.baseDir, L"cancel-probe.log"), 30000, error, tail, cancel);
    signal.join();
    require(code == -1 && error == L"操作已取消。", "probe wait interrupted by cancellation");
    require(GetTickCount64() - start < 6000, "cancellation avoids the 30-second probe timeout");
    puts("Cancelled launch and running parent/child probe cleanup passed.");
    return 0;
}

#include "../src/win.hpp"

int wmain(int argc, wchar_t** argv) {
    bool child = argc > 1 && std::wstring(argv[1]) == L"--child";
    if (!child && argc > 1 && std::wstring(argv[1]) == L"serve") {
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        DWORD parent = 0;
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (entry.th32ProcessID == GetCurrentProcessId()) parent = entry.th32ParentProcessID;
            } while (Process32NextW(snapshot, &entry));
        }
        if (!parent || env(L"OPERON_WIN_DESKTOP_LAUNCH") != std::to_wstring(parent)) return 3;
    }
    if (!child) {
        auto executable = selfPath();
        auto command = L"\"" + executable + L"\" --child";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(executable.c_str(), &command[0], nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) return 4;
        CloseHandle(info.hProcess);
        CloseHandle(info.hThread);
    }
    Sleep(INFINITE);
    return 0;
}

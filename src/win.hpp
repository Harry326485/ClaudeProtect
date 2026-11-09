#pragma once
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <mutex>
#include <cstdint>
#include <cwctype>

// Shared Win32 helpers for the supervisor and native probe.

inline std::wstring env(const wchar_t* name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (!n) return L"";
    std::wstring s(n, 0);
    DWORD written = GetEnvironmentVariableW(name, &s[0], n);
    if (!written || written >= n) return L"";
    s.resize(written);
    return s;
}

inline std::wstring directory(const std::wstring& s) {
    auto pos = s.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : s.substr(0, pos);
}

inline std::wstring fileName(const std::wstring& s) {
    auto pos = s.find_last_of(L"\\/");
    return pos == std::wstring::npos ? s : s.substr(pos + 1);
}

inline std::wstring joinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    wchar_t last = a.back();
    if (last == L'\\' || last == L'/') return a + b;
    return a + L"\\" + b;
}

inline std::wstring selfPath() {
    wchar_t p[32768];
    DWORD n = GetModuleFileNameW(nullptr, p, 32768);
    return {p, n};
}

inline std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

inline std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

inline std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

inline bool fileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

inline bool dirExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

inline std::wstring winErrorText(DWORD error) {
    wchar_t* buffer = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, error, 0, (LPWSTR)&buffer, 0, nullptr);
    std::wstring text = n && buffer ? std::wstring(buffer, n) : L"";
    if (buffer) LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
        text.pop_back();
    wchar_t code[32];
    swprintf(code, 32, L"%lu", error);
    return text.empty() ? std::wstring(code) : text + L" (" + code + L")";
}

inline std::wstring ntStatusText(LONG status) {
    wchar_t code[32];
    swprintf(code, 32, L"0x%08lX", (unsigned long)status);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    wchar_t* buffer = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                             ntdll, (DWORD)status, 0, (LPWSTR)&buffer, 0, nullptr);
    std::wstring text = n && buffer ? std::wstring(buffer, n) : L"";
    if (buffer) LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
        text.pop_back();
    return text.empty() ? std::wstring(code) : std::wstring(code) + L" " + text;
}

inline std::mutex& logMutex() {
    static std::mutex mutex;
    return mutex;
}

inline void logLine(const std::wstring& path, const std::wstring& message) {
    if (path.empty()) return;
    SYSTEMTIME t;
    GetSystemTime(&t);
    wchar_t stamp[80];
    swprintf(stamp, 80, L"%04u-%02u-%02uT%02u:%02u:%02uZ [%lu] ", t.wYear, t.wMonth, t.wDay,
             t.wHour, t.wMinute, t.wSecond, GetCurrentProcessId());
    auto line = utf8(std::wstring(stamp) + message + L"\r\n");
    std::lock_guard<std::mutex> lock(logMutex());
    HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD n = 0;
        WriteFile(f, line.data(), (DWORD)line.size(), &n, nullptr);
        CloseHandle(f);
    }
}

inline void check(BOOL ok, const char* operation) {
    if (!ok) throw std::runtime_error(std::string(operation) + " failed: " +
                                      std::to_string(GetLastError()));
}

struct Handle {
    HANDLE h = nullptr;
    Handle() = default;
    explicit Handle(HANDLE v) : h(v) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void reset(HANDLE v = nullptr) {
        if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
        h = v;
    }
    operator HANDLE() const { return h; }
};

inline bool processImagePath(DWORD pid, std::wstring& path) {
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process.h) return false;
    wchar_t buffer[32768];
    DWORD size = 32768;
    if (!QueryFullProcessImageNameW(process, 0, buffer, &size)) return false;
    path.assign(buffer, size);
    return true;
}

inline bool processName(DWORD pid, std::wstring& name) {
    std::wstring path;
    if (!processImagePath(pid, path)) return false;
    name = fileName(path);
    return true;
}

inline bool processAgeSeconds(HANDLE process, double& seconds) {
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return false;
    ULARGE_INTEGER c;
    c.LowPart = created.dwLowDateTime;
    c.HighPart = created.dwHighDateTime;
    FILETIME nowFt;
    GetSystemTimeAsFileTime(&nowFt);
    ULARGE_INTEGER n;
    n.LowPart = nowFt.dwLowDateTime;
    n.HighPart = nowFt.dwHighDateTime;
    if (n.QuadPart <= c.QuadPart) {
        seconds = 0;
        return true;
    }
    seconds = (double)(n.QuadPart - c.QuadPart) / 10000000.0;
    return true;
}

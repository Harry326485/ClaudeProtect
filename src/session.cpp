#include "session.hpp"
#include <algorithm>
#include <cstdio>
#include <cwchar>

namespace {

bool createJob(Handle& job, std::wstring& error) {
    job.reset(CreateJobObjectW(nullptr, nullptr));
    if (!job.h) {
        error = L"CreateJobObjectW failed: " + winErrorText(GetLastError());
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
        error = L"SetInformationJobObject failed: " + winErrorText(GetLastError());
        job.reset();
        return false;
    }
    return true;
}

bool assignToJob(HANDLE job, HANDLE process, std::wstring& error) {
    if (!AssignProcessToJobObject(job, process)) {
        error = L"AssignProcessToJobObject failed: " + winErrorText(GetLastError());
        return false;
    }
    return true;
}

DWORD jobActive(HANDLE job) {
    if (!job) return 0;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
    if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &info, sizeof(info),
                                   nullptr))
        return 0;
    return info.ActiveProcesses;
}

std::vector<DWORD> jobPids(HANDLE job) {
    std::vector<DWORD> result;
    if (!job) return result;
    DWORD capacity = 64;
    for (int attempt = 0; attempt < 4; ++attempt) {
        size_t bytes = sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + sizeof(ULONG_PTR) * capacity;
        std::vector<unsigned char> buffer(bytes);
        auto list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buffer.data());
        list->NumberOfAssignedProcesses = capacity;
        if (!QueryInformationJobObject(job, JobObjectBasicProcessIdList, list, (DWORD)bytes,
                                       nullptr))
            return result;
        if (list->NumberOfProcessIdsInList < capacity) {
            for (ULONG i = 0; i < list->NumberOfProcessIdsInList; ++i)
                result.push_back((DWORD)list->ProcessIdList[i]);
            return result;
        }
        capacity *= 2;
    }
    return result;
}

std::vector<wchar_t> buildChildEnvironment(
    const Config& config, const std::vector<std::pair<std::wstring, std::wstring>>& extra) {
    std::vector<std::wstring> variables;
    LPWCH block = GetEnvironmentStringsW();
    if (block) {
        for (LPWCH p = block; *p; p += wcslen(p) + 1) {
            std::wstring entry = p;
            if (entry.empty() || entry[0] == L'=') continue;
            auto equals = entry.find(L'=');
            if (equals == std::wstring::npos) continue;
            auto key = lower(entry.substr(0, equals));
            if (key == L"tz" || key == L"lang" || key == L"lc_all" || key == L"language" ||
                key == L"lc_messages" || key.rfind(L"cp_", 0) == 0 ||
                key.rfind(L"claude_protect_", 0) == 0)
                continue;
            variables.push_back(entry);
        }
        FreeEnvironmentStringsW(block);
    }
    if (!config.timezonePosix.empty()) variables.push_back(L"TZ=" + config.timezonePosix);
    // Git Bash expects a POSIX locale with an explicit encoding.
    std::wstring posixLocale = config.locale;
    std::replace(posixLocale.begin(), posixLocale.end(), L'-', L'_');
    posixLocale += L".UTF-8";
    variables.push_back(L"LANG=" + posixLocale);
    variables.push_back(L"LC_ALL=" + posixLocale);
    variables.push_back(L"CLAUDE_PROTECT_SESSION=1");
    variables.push_back(L"CP_TIMEZONE=" + config.timezoneId);
    variables.push_back(L"CP_LOCALE=" + config.locale);
    for (const auto& item : extra) variables.push_back(item.first + L"=" + item.second);
    std::sort(variables.begin(), variables.end(),
              [](const std::wstring& a, const std::wstring& b) {
                  return _wcsicmp(a.c_str(), b.c_str()) < 0;
              });
    std::vector<wchar_t> environment;
    for (const auto& variable : variables) {
        environment.insert(environment.end(), variable.begin(), variable.end());
        environment.push_back(0);
    }
    environment.push_back(0);
    return environment;
}

std::wstring quote(const std::wstring& value) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"') {
            out.append(slashes * 2 + 1, L'\\');
            out.push_back(L'"');
            slashes = 0;
            continue;
        }
        out.append(slashes, L'\\');
        slashes = 0;
        out.push_back(c);
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::wstring commandLine(const std::wstring& executable, const std::wstring& arguments) {
    std::wstring line = quote(executable);
    if (!arguments.empty()) line += L" " + arguments;
    return line;
}

bool readWideFile(const std::wstring& path, std::wstring& out) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.h) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart > 8 * 1024 * 1024) return false;
    std::vector<unsigned char> bytes((size_t)size.QuadPart);
    DWORD read = 0;
    if (!bytes.empty() && !ReadFile(file, bytes.data(), (DWORD)bytes.size(), &read, nullptr))
        return false;
    bytes.resize(read);
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        out.assign(reinterpret_cast<const wchar_t*>(bytes.data() + 2), (bytes.size() - 2) / 2);
        return true;
    }
    if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        size_t chars = (bytes.size() - 2) / 2;
        out.resize(chars);
        for (size_t i = 0; i < chars; ++i)
            out[i] = (wchar_t)((bytes[2 + i * 2] << 8) | bytes[3 + i * 2]);
        return true;
    }
    int length = (int)bytes.size();
    int wide = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(bytes.data()), length,
                                   nullptr, 0);
    if (wide <= 0) return false;
    out.resize(wide);
    MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(bytes.data()), length, &out[0],
                        wide);
    return true;
}

std::wstring tailFile(const std::wstring& path, DWORD maxBytes) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.h) return L"";
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) return L"";
    LONGLONG offset = size.QuadPart > maxBytes ? size.QuadPart - maxBytes : 0;
    LARGE_INTEGER distance{};
    distance.QuadPart = offset;
    if (!SetFilePointerEx(file, distance, nullptr, FILE_BEGIN)) return L"";
    DWORD length = (DWORD)(size.QuadPart - offset);
    if (!length) return L"";
    std::vector<char> bytes(length);
    DWORD read = 0;
    if (!ReadFile(file, bytes.data(), length, &read, nullptr)) return L"";
    int wide = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)read, nullptr, 0);
    if (wide <= 0) return L"";
    std::wstring out(wide, 0);
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)read, &out[0], wide);
    return out;
}

ULONGLONG fileTimeValue(const FILETIME& time) {
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}

DWORD logPidFromName(const std::wstring& name) {
    std::wstring lowerName = lower(name);
    auto broker = lowerName.find(L"-broker-");
    if (broker != std::wstring::npos) {
        wchar_t* end = nullptr;
        unsigned long value = wcstoul(name.c_str() + broker + 8, &end, 10);
        if (end && *end == L'.') return (DWORD)value;
    }
    static const std::wstring suffix = L".log.txt";
    if (lowerName.size() > suffix.size() &&
        lowerName.compare(lowerName.size() - suffix.size(), suffix.size(), suffix) == 0) {
        std::wstring base = name.substr(0, name.size() - suffix.size());
        auto dot = base.find_last_of(L'.');
        if (dot != std::wstring::npos) {
            wchar_t* end = nullptr;
            unsigned long value = wcstoul(base.c_str() + dot + 1, &end, 16);
            if (end && *end == 0) return (DWORD)value;
        }
    }
    return 0;
}

std::wstring tokenAfter(const std::wstring& line, const std::wstring& marker) {
    auto pos = line.find(marker);
    if (pos == std::wstring::npos) return L"";
    pos += marker.size();
    auto end = line.find_first_of(L" \t,;", pos);
    return line.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
}

bool isZeroHex(const std::wstring& value) {
    if (value.empty()) return false;
    for (wchar_t c : value) {
        if (c != L'0' && c != L'x' && c != L'X') return false;
    }
    return true;
}

DWORD numberAfter(const std::wstring& line, const std::wstring& marker) {
    auto value = tokenAfter(line, marker);
    if (value.empty()) return 0;
    wchar_t* end = nullptr;
    unsigned long parsed = wcstoul(value.c_str(), &end, 0);
    if (!end || *end != 0) return 0;
    return (DWORD)parsed;
}

void addPid(std::vector<DWORD>& values, DWORD pid) {
    if (pid != 0 && std::find(values.begin(), values.end(), pid) == values.end())
        values.push_back(pid);
}

bool hasLepCore(DWORD pid, bool& accessible) {
    accessible = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
        if (snapshot.h) {
            MODULEENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            if (!Module32FirstW(snapshot, &entry)) return false;
            accessible = true;
            do {
                std::wstring name = lower(entry.szModule);
                if (name == L"localeemulatorplus_x64.dll" || name == L"localeemulatorplus_x86.dll")
                    return true;
            } while (Module32NextW(snapshot, &entry));
            return false;
        }
        if (GetLastError() != ERROR_BAD_LENGTH) return false;
        Sleep(10);
    }
    return false;
}

bool isHelperToSkip(const std::wstring& name) {
    std::wstring lowerName = lower(name);
    return lowerName == L"rundll32.exe" || lowerName == L"conhost.exe" ||
           lowerName == L"werfault.exe";
}

bool contains(const std::vector<DWORD>& values, DWORD value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

} // namespace

Session::~Session() {
    stop(L"session-destructor");
}

DWORD Session::processCount() const {
    return jobActive(job_);
}

std::wstring Session::statusText() const {
    if (!active_) return L"stopped";
    wchar_t buffer[128];
    swprintf(buffer, 128, L"Claude running (%lu processes)", (unsigned long)processCount());
    return buffer;
}

std::wstring Session::takeFailure() {
    std::wstring value = failure_;
    failure_.clear();
    return value;
}

bool Session::preflight(const Config& config, std::wstring& error) {
    if (config.preflight == L"off") return true;
    if (!fileExists(config.probePath)) {
        if (config.preflight == L"require") {
            error = L"Isolation preflight requires " + config.probePath;
            return false;
        }
        logLine(config.logPath, L"preflight skipped: missing " + config.probePath);
        return true;
    }
    std::wstring probeLog = joinPath(config.baseDir, L"preflight.log");
    std::wstring arguments = L"--spawn-child --json";
    arguments += L" --expect-timezone " + quote(config.timezoneId);
    arguments += L" --expect-locale " + quote(config.locale);
    arguments += L" --expect-acp " + std::to_wstring(config.ansiCodePage);
    arguments += L" --expect-oemcp " + std::to_wstring(config.oemCodePage);
    if (config.uiLanguageMode > 0)
        arguments += L" --expect-ui " + std::to_wstring(config.localeId & 0xFFFF);
    if (config.uiLanguageMode == 2) arguments += L" --expect-preferred";
    std::wstring tail;
    int code = runProbe(config, arguments, probeLog, 20000, error, tail);
    if (code != 0) {
        if (error.empty())
            error = L"Isolation preflight failed (exit " + std::to_wstring(code) + L"). See " +
                    probeLog;
        if (!tail.empty()) logLine(config.logPath, L"preflight output: " + tail);
        return false;
    }
    logLine(config.logPath, L"preflight OK timezone=" + config.timezoneId + L" locale=" +
                                config.locale + L" acp=" + std::to_wstring(config.ansiCodePage));
    return true;
}

bool Session::start(const Config& config, const LaunchSpec& spec, std::wstring& error) {
    error.clear();
    if (active_) {
        error = L"A managed session is already running";
        return false;
    }
    if (!fileExists(spec.executable)) {
        error = L"Executable not found: " + spec.executable;
        return false;
    }

    config_ = config;
    targetDirectory_ = lower(directory(spec.executable));
    failure_.clear();
    mainVerified_ = false;
    knownPids_.clear();
    warnedPids_.clear();
    logVerifiedPids_.clear();
    lepInjectedPids_.clear();
    seenLogFailures_.clear();
    lepLogDir_.clear();
    startTick_ = 0;
    lastVerifyTick_ = 0;
    lastLogScanTick_ = 0;

    std::wstring warning;
    if (spec.claudeDesktop) {
        DWORD unmanagedPid = 0;
        std::wstring unmanagedPath, lookupWarning;
        if (findUnmanagedClaudeDesktop(spec.executable, unmanagedPid, unmanagedPath, lookupWarning)) {
            error = L"An unmanaged Claude Desktop is already running (pid " +
                    std::to_wstring(unmanagedPid) + L", " + unmanagedPath +
                    L"). Quit it before starting a managed session; Electron single-instance "
                    L"forwarding would bypass supervision.";
            return false;
        }
        if (!lookupWarning.empty()) {
            error = L"Cannot verify that no unmanaged Claude Desktop is running: " + lookupWarning +
                    L" Quit any Claude Desktop/Claude Code instances running at a different "
                    L"integrity level, or run ClaudeProtect at the same integrity level.";
            return false;
        }
    }

    if (spec.useLep) {
        if (!checkLepFiles(config_, error, warning)) return false;
        if (!warning.empty()) {
            config_.warnings.push_back(warning);
            logLine(config_.logPath, L"warning: " + warning);
        }
        if (!preflight(config_, error)) return false;
        if (!lep_.load(joinPath(config_.lepDir, L"LoaderDll_x64.dll"), error, warning))
            return false;
        if (!warning.empty()) logLine(config_.logPath, L"warning: " + warning);
        lepLogDir_ = joinPath(lep_.loaderDirectory(), L"Log");
    } else {
        // Direct mode is a lifecycle diagnostic only. It cannot satisfy the
        // LEP-Core module check, so disable injection verification for this
        // session rather than fail closed on every process.
        config_.verifyMode = L"off";
        logLine(config_.logPath, L"verification disabled (direct diagnostic mode)");
    }

    Handle job;
    if (!createJob(job, error)) return false;

    std::wstring launchArguments = spec.arguments;
    if (spec.claudeDesktop && lower(launchArguments).find(L"--lang=") == std::wstring::npos) {
        if (!launchArguments.empty()) launchArguments += L" ";
        launchArguments += L"--lang=" + config_.locale;
    }

    auto environment = buildChildEnvironment(config_, {});
    std::wstring command = commandLine(spec.executable, launchArguments);
    std::wstring workingDirectory = directory(spec.executable);
    if (workingDirectory.empty()) workingDirectory = config_.baseDir;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};

    if (spec.useLep) {
        LepLaunchConfig lepConfig;
        lepConfig.timezoneId = config_.timezoneId;
        lepConfig.standardName = config_.standardName;
        lepConfig.daylightName = config_.daylightName;
        lepConfig.ansiCodePage = config_.ansiCodePage;
        lepConfig.oemCodePage = config_.oemCodePage;
        lepConfig.localeId = config_.localeId;
        lepConfig.fontCharset = config_.fontCharset;
        lepConfig.registryRedirectionMode = config_.registryRedirectionMode;
        lepConfig.uiLanguageMode = config_.uiLanguageMode;
        LONG status = lep_.createSuspended(lepConfig, spec.executable.c_str(), &command[0],
                                           workingDirectory.c_str(), CREATE_UNICODE_ENVIRONMENT,
                                           &startup, &info, environment.data(), error);
        if (status < 0) return false;
    } else {
        if (!CreateProcessW(spec.executable.c_str(), &command[0], nullptr, nullptr, FALSE,
                            CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, environment.data(),
                            workingDirectory.c_str(), &startup, &info)) {
            error = L"CreateProcessW failed: " + winErrorText(GetLastError());
            return false;
        }
    }

    Handle process(info.hProcess);
    Handle thread(info.hThread);
    if (!assignToJob(job, process, error)) {
        TerminateProcess(process, 1);
        return false;
    }
    if (ResumeThread(thread) == (DWORD)-1) {
        error = L"ResumeThread failed: " + winErrorText(GetLastError());
        TerminateProcess(process, 1);
        return false;
    }

    job_.h = job.h;
    job.h = nullptr;
    process_.h = process.h;
    process.h = nullptr;
    pid_ = info.dwProcessId;
    active_ = true;
    startTick_ = GetTickCount64();
    GetSystemTimeAsFileTime(&startFileTime_);

    logLine(config_.logPath,
            L"started pid=" + std::to_wstring(pid_) + L" exe=" + spec.executable +
                L" virtualization=" + (spec.useLep ? L"lep" : L"off-diagnostic") +
                L" timezone=" + config_.timezoneId + L" locale=" + config_.locale +
                L" arguments=" + launchArguments);
    if (spec.useLep) {
        LepLaunchConfig lepConfig;
        lepConfig.timezoneId = config_.timezoneId;
        lepConfig.standardName = config_.standardName;
        lepConfig.daylightName = config_.daylightName;
        lepConfig.ansiCodePage = config_.ansiCodePage;
        lepConfig.oemCodePage = config_.oemCodePage;
        lepConfig.localeId = config_.localeId;
        lepConfig.fontCharset = config_.fontCharset;
        lepConfig.registryRedirectionMode = config_.registryRedirectionMode;
        lepConfig.uiLanguageMode = config_.uiLanguageMode;
        logLine(config_.logPath, LepLoader::describeBlock(LepLoader::buildBlock(lepConfig)));
    }
    return true;
}

void Session::poll() {
    if (!active_) return;
    DWORD exitCode = 0;
    if (WaitForSingleObject(process_, 0) == WAIT_OBJECT_0) {
        GetExitCodeProcess(process_, &exitCode);
        logLine(config_.logPath, L"main process exited code=" + std::to_wstring(exitCode));
        stop(L"main-exit");
        return;
    }
    ULONGLONG now = GetTickCount64();
    ULONGLONG grace = (ULONGLONG)config_.verifyGraceSeconds * 1000;
    if (config_.verifyMode != L"off" && now - startTick_ >= grace &&
        now - lastVerifyTick_ >= 1000) {
        lastVerifyTick_ = now;
        verifyProcesses();
        if (!active_) return;
    }
    if (config_.verifyMode != L"off" && now - lastLogScanTick_ >= 2000) {
        lastLogScanTick_ = now;
        scanLepLogs();
    }
}

bool Session::hasLepLog(DWORD pid) {
    if (contains(logVerifiedPids_, pid)) return true;
    if (lepLogDir_.empty() || !dirExists(lepLogDir_)) return false;
    WIN32_FIND_DATAW entry{};
    HANDLE search = FindFirstFileW(joinPath(lepLogDir_, L"*").c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE) return false;
    bool initialized = false;
    do {
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (logPidFromName(entry.cFileName) != pid) continue;
        std::wstring text;
        if (!readWideFile(joinPath(lepLogDir_, entry.cFileName), text)) continue;
        if (text.find(L"GlobalData.Initialize end status=00000000") != std::wstring::npos) {
            initialized = true;
            break;
        }
    } while (FindNextFileW(search, &entry));
    FindClose(search);
    if (initialized) logVerifiedPids_.push_back(pid);
    return initialized;
}

bool Session::isManagedImagePath(const std::wstring& path) const {
    if (targetDirectory_.empty()) return true;
    std::wstring lowerPath = lower(path);
    if (lowerPath.size() < targetDirectory_.size() ||
        lowerPath.compare(0, targetDirectory_.size(), targetDirectory_) != 0)
        return false;
    return lowerPath.size() == targetDirectory_.size() ||
           lowerPath[targetDirectory_.size()] == L'\\';
}

bool Session::handleLepFailure(DWORD childPid, const std::wstring& line) {
    if (childPid != 0) {
        std::wstring image;
        if (!processImagePath(childPid, image)) {
            if (!contains(warnedPids_, childPid)) {
                warnedPids_.push_back(childPid);
                logLine(config_.logPath,
                        L"LEP injection failure for exited/unreadable pid=" +
                            std::to_wstring(childPid) + L"; not failing the session: " + line);
            }
            return false;
        }
        if (!isManagedImagePath(image)) {
            if (!contains(warnedPids_, childPid)) {
                warnedPids_.push_back(childPid);
                logLine(config_.logPath,
                        L"LEP injection failure ignored for external helper pid=" +
                            std::to_wstring(childPid) + L" exe=" + image);
            }
            return false;
        }
    }
    logLine(config_.logPath, L"LEP failure evidence: " + line);
    if (config_.verifyMode == L"strict") {
        failClosed(L"LEP reported an injection failure for a managed process");
        return true;
    }
    return false;
}

void Session::verifyProcesses() {
    auto pids = jobPids(job_);
    for (DWORD pid : pids) {
        if (!contains(knownPids_, pid)) knownPids_.push_back(pid);
    }
    double grace = (double)config_.verifyGraceSeconds;
    bool mainPresent = false;
    for (DWORD pid : pids) {
        if (pid == 0) continue;
        std::wstring image;
        if (!processImagePath(pid, image)) continue;
        std::wstring name = fileName(image);
        if (pid == pid_) mainPresent = true;
        if (isHelperToSkip(name)) continue;

        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
        if (!process.h) continue;
        double age = 0;
        if (!processAgeSeconds(process, age) || age < grace) continue;

        bool accessible = false;
        bool hasCore = hasLepCore(pid, accessible);
        if (!hasCore) hasCore = contains(lepInjectedPids_, pid);
        if (!hasCore) hasCore = hasLepLog(pid);
        if (hasCore) {
            if (pid == pid_) mainVerified_ = true;
            continue;
        }
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) continue;

        if (!isManagedImagePath(image)) {
            if (!contains(warnedPids_, pid)) {
                warnedPids_.push_back(pid);
                logLine(config_.logPath,
                        L"verification warning: external helper is not virtualized pid=" +
                            std::to_wstring(pid) + L" exe=" + image);
            }
            continue;
        }

        std::wstring detail = L"process pid=" + std::to_wstring(pid) + L" exe=" + image +
                              L" has no visible LEP Core module, injection record, or "
                              L"successful LEP initialization log";
        logLine(config_.logPath, L"verification FAILED: " + detail);
        if (config_.verifyMode == L"strict") {
            failClosed(detail);
            return;
        }
    }
    if (!mainPresent) {
        double age = 0;
        if (processAgeSeconds(process_, age) && age >= grace) {
            if (!hasLepLog(pid_) && !contains(lepInjectedPids_, pid_)) {
                failClosed(L"main process pid=" + std::to_wstring(pid_) +
                           L" has no visible LEP Core module, injection record, or log");
            }
        }
    }
}

void Session::scanLepLogs() {
    if (lepLogDir_.empty() || !dirExists(lepLogDir_)) return;
    auto pids = jobPids(job_);
    for (DWORD pid : pids) {
        if (!contains(knownPids_, pid)) knownPids_.push_back(pid);
    }
    WIN32_FIND_DATAW entry{};
    HANDLE search = FindFirstFileW(joinPath(lepLogDir_, L"*").c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name = entry.cFileName;
        std::wstring lowerName = lower(name);
        bool broker = lowerName.find(L"localeemulatorplus-broker-") == 0;
        bool core = lowerName.size() > 8 &&
                    lowerName.compare(lowerName.size() - 8, 8, L".log.txt") == 0;
        if (!broker && !core) continue;
        if (fileTimeValue(entry.ftLastWriteTime) < fileTimeValue(startFileTime_)) continue;
        DWORD logPid = logPidFromName(name);
        if (logPid != 0 && !contains(knownPids_, logPid)) continue;
        std::wstring text;
        if (!readWideFile(joinPath(lepLogDir_, name), text)) continue;
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t end = text.find_first_of(L"\r\n", pos);
            if (end == std::wstring::npos) end = text.size();
            std::wstring line = text.substr(pos, end - pos);
            pos = end + 1;
            bool failure = false;
            DWORD childPid = 0;
            if (line.find(L"create edge") != std::wstring::npos) {
                auto status = tokenAfter(line, L"injectStatus=");
                childPid = numberAfter(line, L"childPid=");
                if (!status.empty()) {
                    if (isZeroHex(status))
                        addPid(lepInjectedPids_, childPid);
                    else
                        failure = true;
                }
            } else if (line.find(L"child injection complete") != std::wstring::npos) {
                auto status = tokenAfter(line, L"status=");
                DWORD pid = numberAfter(line, L"pid=");
                if (!status.empty()) {
                    if (isZeroHex(status))
                        addPid(lepInjectedPids_, pid);
                    else {
                        failure = true;
                        childPid = pid;
                    }
                }
            } else if (line.find(L"GlobalData.Initialize end status=00000000") !=
                       std::wstring::npos) {
                addPid(lepInjectedPids_, logPid);
            }
            if (!failure) continue;
            if (std::find(seenLogFailures_.begin(), seenLogFailures_.end(), line) !=
                seenLogFailures_.end())
                continue;
            seenLogFailures_.push_back(line);
            if (handleLepFailure(childPid, line)) {
                FindClose(search);
                return;
            }
        }
    } while (FindNextFileW(search, &entry));
    FindClose(search);
}

void Session::failClosed(const std::wstring& detail) {
    failure_ = detail;
    logLine(config_.logPath, L"FAIL-CLOSED " + detail);
    stop(L"injection-verification-failed");
}

void Session::stop(const std::wstring& reason) {
    if (!active_ && !job_.h) return;
    logLine(config_.logPath, L"stopping job reason=" + reason + L" pid=" +
                                std::to_wstring(pid_) + L" active=" +
                                std::to_wstring(jobActive(job_)));
    if (job_.h) {
        if (!TerminateJobObject(job_, 0))
            logLine(config_.logPath,
                    L"TerminateJobObject failed: " + winErrorText(GetLastError()));
        for (int i = 0; i < 150 && jobActive(job_) > 0; ++i) Sleep(20);
    }
    job_.reset();
    process_.reset();
    pid_ = 0;
    active_ = false;
    logLine(config_.logPath, L"managed job closed");
}

int Session::runProbe(const Config& config, const std::wstring& arguments,
                      const std::wstring& probeLogPath, DWORD timeoutMs, std::wstring& error,
                      std::wstring& outputTail) {
    error.clear();
    outputTail.clear();
    lastProbeLog_ = probeLogPath;
    if (active_) {
        error = L"Cannot run a probe while a managed session is active";
        return -1;
    }
    if (!fileExists(config.probePath)) {
        error = L"Probe not found: " + config.probePath;
        return -1;
    }
    std::wstring warning;
    LepLoader loader;
    if (!loader.load(joinPath(config.lepDir, L"LoaderDll_x64.dll"), error, warning)) return -1;
    if (!warning.empty()) logLine(config.logPath, L"warning: " + warning);

    DeleteFileW(probeLogPath.c_str());
    // Truncate (or create) so a stale probe log cannot be mistaken for this run.
    {
        Handle probeLogFile(CreateFileW(probeLogPath.c_str(), GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!probeLogFile.h)
            logLine(config.logPath, L"warning: cannot truncate probe log " + probeLogPath);
    }
    auto environment = buildChildEnvironment(config, {{L"CP_PROBE_LOG", probeLogPath},
                                                      {L"CP_LEP_LOG_DIR",
                                                       joinPath(config.lepDir, L"Log")}});
    Handle job;
    if (!createJob(job, error)) return -1;
    std::wstring command = commandLine(config.probePath, arguments);
    std::wstring workingDirectory = directory(config.probePath);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    LepLaunchConfig lepConfig;
    lepConfig.timezoneId = config.timezoneId;
    lepConfig.standardName = config.standardName;
    lepConfig.daylightName = config.daylightName;
    lepConfig.ansiCodePage = config.ansiCodePage;
    lepConfig.oemCodePage = config.oemCodePage;
    lepConfig.localeId = config.localeId;
    lepConfig.fontCharset = config.fontCharset;
    lepConfig.registryRedirectionMode = config.registryRedirectionMode;
    lepConfig.uiLanguageMode = config.uiLanguageMode;
    LONG status = loader.createSuspended(lepConfig, config.probePath.c_str(), &command[0],
                                         workingDirectory.c_str(), CREATE_UNICODE_ENVIRONMENT,
                                         &startup, &info, environment.data(), error);
    if (status < 0) return -1;
    Handle process(info.hProcess);
    Handle thread(info.hThread);
    if (!assignToJob(job, process, error)) {
        TerminateProcess(process, 1);
        return -1;
    }
    if (ResumeThread(thread) == (DWORD)-1) {
        error = L"ResumeThread failed for probe: " + winErrorText(GetLastError());
        TerminateProcess(process, 1);
        return -1;
    }

    DWORD wait = WaitForSingleObject(process, timeoutMs);
    DWORD exitCode = 0;
    if (wait == WAIT_TIMEOUT) {
        error = L"Probe timed out after " + std::to_wstring(timeoutMs) + L" ms";
        exitCode = (DWORD)-1;
    } else if (wait == WAIT_FAILED) {
        error = L"WaitForSingleObject failed for probe: " + winErrorText(GetLastError());
        exitCode = (DWORD)-1;
    } else {
        GetExitCodeProcess(process, &exitCode);
    }
    TerminateJobObject(job, 0);
    for (int i = 0; i < 150 && jobActive(job) > 0; ++i) Sleep(20);
    outputTail = tailFile(probeLogPath, 4096);
    return (int)exitCode;
}

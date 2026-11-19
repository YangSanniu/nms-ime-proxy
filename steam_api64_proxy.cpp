// steam_api64.dll proxy for No Man's Sky: local IME text panel instead of Steam's OSK.
// NMS uses ISteamUtils010::ShowGamepadTextInput. Outside Big Picture Steam's desktop OSK
// cannot host a CJK IME, so the proxy shows its own borderless text panel and synthesizes
// GamepadTextInputDismissed_t (callback id 714). The game then reads the UTF-8 text through
// GetEnteredGamepadTextInput/Length. All other SteamAPI calls are forwarded unchanged.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>
#include <memory>

typedef int32_t HSteamUser;

static HMODULE g_orig;
static FILE* g_log;
static LONG g_logInit;
static LONG g_ctxCount;

// ---- paths: panel/ini live next to this DLL, mutable data in %LOCALAPPDATA% ----
struct Paths {
    bool debug;
    std::wstring dir;
    std::wstring ini;
    std::wstring panel;
    std::wstring data;
    std::wstring log;
    std::wstring panelLog;
    std::wstring req;
    std::wstring res;
};

static bool FileExists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static Paths BuildPaths() {
    Paths p = {};
    wchar_t buf[MAX_PATH] = {};
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(reinterpret_cast<void*>(&BuildPaths)), &self);
    if (self && GetModuleFileNameW(self, buf, MAX_PATH)) {
        std::wstring full = buf;
        size_t slash = full.find_last_of(L'\\');
        if (slash != std::wstring::npos) p.dir = full.substr(0, slash + 1);
    }
    p.ini = p.dir + L"nms-ime-proxy.ini";
    p.panel = p.dir + L"nms-ime-panel.exe";
    p.debug = GetPrivateProfileIntW(L"nms-ime-proxy", L"Debug", 0, p.ini.c_str()) != 0;

    std::wstring data;
    wchar_t base[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n > 0 && n < MAX_PATH) data = std::wstring(base) + L"\\nms-ime-proxy";
    if (data.empty()) {
        wchar_t tmp[MAX_PATH] = {};
        DWORD m = GetTempPathW(MAX_PATH, tmp);
        if (m > 0 && m < MAX_PATH) data = std::wstring(tmp, m) + L"nms-ime-proxy";
    }
    if (!data.empty()) CreateDirectoryW(data.c_str(), nullptr);
    p.data = data.empty() ? L"" : data + L"\\";
    p.log = p.data + L"proxy.log";
    p.panelLog = p.data + L"panel.log";
    p.req = p.data + L"request.txt";
    p.res = p.data + L"response.txt";
    return p;
}

static const Paths& P() {
    static Paths p = BuildPaths();
    return p;
}

static void Log(const char* fmt, ...) {
    const Paths& p = P();
    if (!p.debug) return;
    if (!g_log && InterlockedCompareExchange(&g_logInit, 1, 0) == 0) {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(p.log.c_str(), GetFileExInfoStandard, &fad)) {
            ULARGE_INTEGER sz;
            sz.LowPart = fad.nFileSizeLow;
            sz.HighPart = fad.nFileSizeHigh;
            if (sz.QuadPart > 16ull * 1024 * 1024) DeleteFileW(p.log.c_str());
        }
        g_log = _wfopen(p.log.c_str(), L"a");
        if (g_log) {
            fprintf(g_log, "\n===== NMS steam_api proxy active (built %s %s) =====\n", __DATE__, __TIME__);
            fflush(g_log);
        }
    }
    if (!g_log) return;
    static SRWLOCK lock = SRWLOCK_INIT;
    AcquireSRWLockExclusive(&lock);
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(g_log, "%02u:%02u:%02u.%03u  ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    ReleaseSRWLockExclusive(&lock);
}

static bool EnsureOrig() {
    if (g_orig) return true;
    std::wstring path = P().dir + L"steam_api64_orig.dll";
    g_orig = LoadLibraryW(path.c_str());
    if (!g_orig) {
        Log("ERROR: LoadLibraryW(%ls) failed (%lu)", path.c_str(), GetLastError());
        return false;
    }
    Log("ORIG loaded: %ls", path.c_str());
    return true;
}

template <typename T>
static T Orig(const char* name) {
    static T fn = nullptr;
    if (!fn && EnsureOrig()) {
        fn = reinterpret_cast<T>(GetProcAddress(g_orig, name));
        if (!fn) Log("ERROR: missing export %s in orig dll", name);
    }
    return fn;
}

static bool IsInMainModule(void* fn) {
    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(fn, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
    return mbi.AllocationBase == GetModuleHandleW(nullptr);
}

// ---- panel plumbing ----
struct PanelRequest {
    std::string description;
    std::string existing;
    uint32_t maxChars;
};

static SRWLOCK g_resultLock = SRWLOCK_INIT;
static bool g_resultSubmitted;
static std::string g_resultText;
static LONG g_resultReady;
static LONG g_panelActive;
static void* g_cb714;
static SRWLOCK g_textLock = SRWLOCK_INIT;
static std::string g_lastText;

static std::string OneLine(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        if (c == '\r' || c == '\n') c = ' ';
    return out;
}

static void WriteFileUtf8(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        Log("PANEL ERROR: cannot write request (%lu)", GetLastError());
        return;
    }
    DWORD written = 0;
    WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    CloseHandle(h);
}

static std::string ReadFileUtf8(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size = {};
    std::string out;
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < 1024 * 1024) {
        out.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ReadFile(h, &out[0], static_cast<DWORD>(out.size()), &read, nullptr);
        out.resize(read);
    }
    CloseHandle(h);
    return out;
}

static DWORD WINAPI PanelThread(LPVOID param) {
    std::unique_ptr<PanelRequest> req(static_cast<PanelRequest*>(param));
    const Paths& p = P();
    std::string request = std::to_string(req->maxChars) + "\n" + OneLine(req->description) + "\n" + OneLine(req->existing);
    DeleteFileW(p.res.c_str());
    WriteFileUtf8(p.req, request);

    bool submitted = false;
    std::string text;
    bool launched = false;
    PROCESS_INFORMATION pi = {};

    if (!FileExists(p.panel)) Log("PANEL ERROR: %ls not found", p.panel.c_str());
    std::wstring cmd = L"\"" + p.panel + L"\" \"" + p.req + L"\" \"" + p.res + L"\" " +
                       (p.debug ? L"1 \"" : L"0 \"") + p.panelLog + L"\"";
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    Log("PANEL launch: %ls", cmd.c_str());
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        Log("PANEL ERROR: CreateProcess failed (%lu)", GetLastError());
    } else {
        launched = true;
        CloseHandle(pi.hThread);
        // The panel publishes its result atomically, so poll for the file instead of waiting for
        // the process to exit: exit cleanup (foreground restore) costs ~100ms and would gate delivery.
        std::string result;
        DWORD t0 = GetTickCount();
        for (;;) {
            result = ReadFileUtf8(p.res);
            if (!result.empty()) break;
            if (WaitForSingleObject(pi.hProcess, 10) == WAIT_OBJECT_0) {
                result = ReadFileUtf8(p.res);
                break;
            }
            if (GetTickCount() - t0 > 30u * 60u * 1000u) break;
        }
        DWORD elapsed = GetTickCount() - t0;
        if (!result.empty() && result[0] == '1') {
            submitted = true;
            size_t nl = result.find('\n');
            if (nl != std::string::npos) text = result.substr(nl + 1);
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
            while (!text.empty() && (text.front() == '\n' || text.front() == '\r')) text.erase(text.begin());
            Log("PANEL result: submitted, bytes=%zu text=\"%s\" after=%lums", text.size(), text.c_str(), elapsed);
        } else {
            Log("PANEL result: cancelled after=%lums", elapsed);
        }
    }

    {
        AcquireSRWLockExclusive(&g_resultLock);
        g_resultSubmitted = submitted;
        g_resultText = text;
        ReleaseSRWLockExclusive(&g_resultLock);
    }
    InterlockedExchange(&g_resultReady, 1);
    InterlockedExchange(&g_panelActive, 0);

    if (launched) {
        WaitForSingleObject(pi.hProcess, 5000);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        Log("PANEL exit code=%lu", code);
    }
    return 0;
}

// ---- ISteamUtils010 hooks (vtable offsets measured from this steam_api64.dll) ----
static const int kVtSlots = 40;
static void* g_cloneVt[kVtSlots];
static LONG g_patched;

static bool Hook_ShowGamepadTextInput(void* self, int inputMode, int lineMode, const char* description,
                                      uint32_t maxChars, const char* existingText);
static uint32_t Hook_GetEnteredGamepadTextLength(void* self);
static bool Hook_GetEnteredGamepadTextInput(void* self, char* text, uint32_t cch);
static void EnsureClickShield();

static void PatchUtils(void* obj) {
    if (InterlockedCompareExchange(&g_patched, 1, 0) != 0) return;
    void** vt = *reinterpret_cast<void***>(obj);
    memcpy(g_cloneVt, vt, sizeof(g_cloneVt));
    g_cloneVt[0xa0 / 8] = reinterpret_cast<void*>(&Hook_ShowGamepadTextInput);
    g_cloneVt[0xa8 / 8] = reinterpret_cast<void*>(&Hook_GetEnteredGamepadTextLength);
    g_cloneVt[0xb0 / 8] = reinterpret_cast<void*>(&Hook_GetEnteredGamepadTextInput);
    *reinterpret_cast<void***>(obj) = g_cloneVt;
    Log("PATCHED SteamUtils010 obj=%p vt=%p clone=%p", obj, static_cast<void*>(vt),
        static_cast<void*>(g_cloneVt));
}

static bool Hook_ShowGamepadTextInput(void* self, int inputMode, int lineMode, const char* description,
                                      uint32_t maxChars, const char* existingText) {
    (void)self;
    Log("Utils::ShowGamepadTextInput(mode=%d lineMode=%d maxChars=%u desc=\"%s\" existing=\"%s\") -> local panel",
        inputMode, lineMode, maxChars, description ? description : "",
        existingText ? existingText : "");
    EnsureClickShield();
    if (InterlockedCompareExchange(&g_panelActive, 1, 0) != 0) {
        Log("ShowGamepadTextInput: panel already active, request ignored");
        return true;
    }
    auto* req = new PanelRequest{description ? description : "", existingText ? existingText : "", maxChars};
    HANDLE h = CreateThread(nullptr, 0, PanelThread, req, 0, nullptr);
    if (h) {
        CloseHandle(h);
    } else {
        Log("ShowGamepadTextInput: CreateThread failed (%lu)", GetLastError());
        delete req;
        InterlockedExchange(&g_panelActive, 0);
    }
    return true;
}

static uint32_t Hook_GetEnteredGamepadTextLength(void* self) {
    (void)self;
    uint32_t n = 0;
    AcquireSRWLockShared(&g_textLock);
    n = static_cast<uint32_t>(g_lastText.size());
    ReleaseSRWLockShared(&g_textLock);
    Log("Utils::GetEnteredGamepadTextLength() -> %u", n);
    return n;
}

static bool Hook_GetEnteredGamepadTextInput(void* self, char* text, uint32_t cch) {
    (void)self;
    bool ok = false;
    if (text && cch > 0) {
        AcquireSRWLockShared(&g_textLock);
        size_t n = g_lastText.size();
        if (n > cch - 1) n = cch - 1;
        memcpy(text, g_lastText.data(), n);
        text[n] = '\0';
        ok = true;
        ReleaseSRWLockShared(&g_textLock);
    }
    Log("Utils::GetEnteredGamepadTextInput(cch=%u) -> %d text=\"%s\"", cch, ok ? 1 : 0,
        ok ? text : "(none)");
    return ok;
}

// ---- click shield: while the IME panel is open, the game must not receive mouse clicks ----
static WNDPROC g_gameWndProc = nullptr;
static LONG g_clickShield;

static LRESULT CALLBACK GameWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (InterlockedCompareExchange(&g_panelActive, 0, 0) != 0) {
        switch (msg) {
        case WM_MOUSEACTIVATE:
            Log("CLICK SHIELD: WM_MOUSEACTIVATE eaten (panel active)");
            return MA_NOACTIVATEANDEAT;
        case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP:
            return 0;
        }
    }
    return CallWindowProcW(g_gameWndProc, h, msg, wp, lp);
}

static void EnsureClickShield() {
    if (InterlockedCompareExchange(&g_clickShield, 1, 0) != 0) return;
    HWND w = FindWindowW(L"GLFW30", nullptr);
    if (!w) {
        Log("CLICK SHIELD: game window not found");
        InterlockedExchange(&g_clickShield, 0);
        return;
    }
    LONG_PTR prev = SetWindowLongPtrW(w, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&GameWndProc));
    if (!prev) {
        Log("CLICK SHIELD: SetWindowLongPtr failed (%lu)", GetLastError());
        InterlockedExchange(&g_clickShield, 0);
        return;
    }
    g_gameWndProc = reinterpret_cast<WNDPROC>(prev);
    Log("CLICK SHIELD: installed on hwnd=%p origProc=%p", w, reinterpret_cast<void*>(prev));
}

static void DrainPendingDismissal() {
    if (InterlockedCompareExchange(&g_resultReady, 0, 0) == 0) return;
    if (!g_cb714) {
        Log("DISPATCH pending but GamepadTextInputDismissed callback not registered yet");
        return;
    }
    bool submitted;
    std::string text;
    AcquireSRWLockExclusive(&g_resultLock);
    submitted = g_resultSubmitted;
    text = g_resultText;
    ReleaseSRWLockExclusive(&g_resultLock);
    InterlockedExchange(&g_resultReady, 0);

    {
        AcquireSRWLockExclusive(&g_textLock);
        g_lastText = submitted ? text : std::string();
        ReleaseSRWLockExclusive(&g_textLock);
    }

    struct GamepadTextInputDismissed_t {
        bool m_bSubmitted;
        uint32_t m_unSubmittedText;
    };
    static_assert(sizeof(GamepadTextInputDismissed_t) == 8, "bad size");
    GamepadTextInputDismissed_t data = {};
    data.m_bSubmitted = submitted;
    data.m_unSubmittedText = submitted ? static_cast<uint32_t>(text.size()) : 0;

    void** vt = *reinterpret_cast<void***>(g_cb714);
    void* slot0 = vt ? vt[0] : nullptr;
    Log("DISPATCH GamepadTextInputDismissed cb=%p vt=%p slot0=%p submitted=%d bytes=%zu inNMS=%d",
        g_cb714, static_cast<void*>(vt), slot0, submitted ? 1 : 0, text.size(),
        slot0 && IsInMainModule(slot0) ? 1 : 0);
    if (!slot0 || !IsInMainModule(slot0)) {
        Log("DISPATCH aborted: slot0 is not inside NMS.exe");
        return;
    }
    auto run = reinterpret_cast<void (*)(void*, void*)>(slot0);
    run(g_cb714, &data);
    Log("DISPATCH done");
}

// ---- exported forwards (the exact 11 imports of NMS.exe) ----
extern "C" {

__declspec(dllexport) bool SteamAPI_Init() {
    auto fn = Orig<bool (*)()>("SteamAPI_Init");
    bool r = fn ? fn() : false;
    Log("SteamAPI_Init() -> %d", r ? 1 : 0);
    return r;
}

__declspec(dllexport) void SteamAPI_Shutdown() {
    auto fn = Orig<void (*)()>("SteamAPI_Shutdown");
    Log("SteamAPI_Shutdown()");
    if (fn) fn();
}

__declspec(dllexport) bool SteamAPI_RestartAppIfNecessary(uint32_t appId) {
    auto fn = Orig<bool (*)(uint32_t)>("SteamAPI_RestartAppIfNecessary");
    bool r = fn ? fn(appId) : false;
    Log("SteamAPI_RestartAppIfNecessary(%u) -> %d", appId, r ? 1 : 0);
    return r;
}

__declspec(dllexport) void SteamAPI_RunCallbacks() {
    auto fn = Orig<void (*)()>("SteamAPI_RunCallbacks");
    if (fn) fn();
    DrainPendingDismissal();
    static LONG count = 0;
    LONG c = InterlockedIncrement(&count);
    if (c == 1 || c % 3600 == 0) Log("SteamAPI_RunCallbacks #%ld", c);
}

__declspec(dllexport) HSteamUser SteamAPI_GetHSteamUser() {
    auto fn = Orig<HSteamUser (*)()>("SteamAPI_GetHSteamUser");
    return fn ? fn() : 0;
}

__declspec(dllexport) void SteamAPI_RegisterCallback(void* cb, int id) {
    Log("SteamAPI_RegisterCallback(cb=%p, id=%d)", cb, id);
    if (id == 714) {
        g_cb714 = cb;
        void** vt = *reinterpret_cast<void***>(cb);
        void* s0 = vt ? vt[0] : nullptr;
        void* s1 = vt ? vt[1] : nullptr;
        void* s2 = vt ? vt[2] : nullptr;
        Log("CAPTURED GamepadTextInputDismissed cb=%p vt=%p slots=%p %p %p inNMS=%d",
            cb, static_cast<void*>(vt), s0, s1, s2, s0 && IsInMainModule(s0) ? 1 : 0);
    }
    auto fn = Orig<void (*)(void*, int)>("SteamAPI_RegisterCallback");
    if (fn) fn(cb, id);
}

__declspec(dllexport) void SteamAPI_UnregisterCallback(void* cb) {
    Log("SteamAPI_UnregisterCallback(cb=%p)", cb);
    if (cb == g_cb714) g_cb714 = nullptr;
    auto fn = Orig<void (*)(void*)>("SteamAPI_UnregisterCallback");
    if (fn) fn(cb);
}

__declspec(dllexport) void SteamAPI_RegisterCallResult(void* cb, uint64_t call) {
    static LONG count = 0;
    LONG c = InterlockedIncrement(&count);
    if (c <= 20 || c % 500 == 0)
        Log("SteamAPI_RegisterCallResult(cb=%p, call=%llu) #%ld", cb, (unsigned long long)call, c);
    auto fn = Orig<void (*)(void*, uint64_t)>("SteamAPI_RegisterCallResult");
    if (fn) fn(cb, call);
}

__declspec(dllexport) void SteamAPI_UnregisterCallResult(void* cb, uint64_t call) {
    Log("SteamAPI_UnregisterCallResult(cb=%p, call=%llu)", cb, (unsigned long long)call);
    auto fn = Orig<void (*)(void*, uint64_t)>("SteamAPI_UnregisterCallResult");
    if (fn) fn(cb, call);
}

__declspec(dllexport) void* SteamInternal_ContextInit(void* ctx) {
    auto fn = Orig<void* (*)(void*)>("SteamInternal_ContextInit");
    void* r = fn ? fn(ctx) : nullptr;
    LONG c = InterlockedIncrement(&g_ctxCount);
    if (c <= 3 || c % 100000 == 0)
        Log("SteamInternal_ContextInit(%p) -> %p #%ld", ctx, r, c);
    return r;
}

__declspec(dllexport) void* SteamInternal_FindOrCreateUserInterface(HSteamUser user, const char* version) {
    auto fn = Orig<void* (*)(HSteamUser, const char*)>("SteamInternal_FindOrCreateUserInterface");
    void* r = fn ? fn(user, version) : nullptr;
    Log("SteamInternal_FindOrCreateUserInterface(user=%d, ver=\"%s\") -> %p",
        (int)user, version ? version : "(null)", r);
    if (r && version && strcmp(version, "SteamUtils010") == 0) PatchUtils(r);
    return r;
}

}
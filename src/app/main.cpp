// Tom Clancy's Splinter Cell: Double Agent (Xbox), statically recompiled
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <timeapi.h>
#include <backends/imgui_impl_win32.h>
#else
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <backends/imgui_impl_sdl3.h>
#include <sys/resource.h>
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#import <Cocoa/Cocoa.h>
#endif

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cwchar>
#include <thread>

#include <imgui.h>

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "image_symbols.h"
#include "kernel/kernel.h"

#ifdef _WIN32
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
#endif

namespace app {

namespace {

#ifdef _WIN32
HWND g_hwnd = nullptr;
constexpr UINT kApplyDisplay = WM_APP + 1, kOpenWindow = WM_APP + 2;
bool g_mode_changed = false;
std::wstring g_mode_device;  // the monitor whose mode was changed
bool g_apply_posted = false;  // a display apply is waiting in the queue or running

// crash dump in CrashDumps beside the exe
LONG WINAPI WriteCrashDump(EXCEPTION_POINTERS* e) {
    // the dump holds no guest memory: the guest registers and return addresses go to the log
    if (kernel::Thread* t = kernel::Current(); e && t && t->ctx) {
        auto* r = e->ExceptionRecord;
        LOGE("exception %08lX at %p on guest thread %X", r->ExceptionCode, r->ExceptionAddress, t->id);
        if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
            uint64_t va = r->ExceptionInformation[1], g = uint64_t(reinterpret_cast<uintptr_t>(kernel::g_base));
            LOGE("  %s %llX, guest %08llX", r->ExceptionInformation[0] ? "write" : "read", (unsigned long long)va,
                 (unsigned long long)(va - g));
        }
        kernel::LogGuestStack(*t->ctx, 256);
    }
    auto dir = DataDir() / "CrashDumps";
    CreateDirectoryW(dir.c_str(), nullptr);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[64];
    swprintf(name, 64, L"SCDA.%04u.%02u.%02u_%02u.%02u.%02u.dmp", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    HANDLE f = CreateFileW((dir / name).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;
    MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), e, FALSE};
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                      MINIDUMP_TYPE(MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithThreadInfo |
                                    MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory),
                      e ? &info : nullptr, nullptr, nullptr);
    CloseHandle(f);
    logx::Flush();
    return EXCEPTION_CONTINUE_SEARCH;
}

#ifdef NDEBUG
constexpr bool kDumpKey = false;
#else
constexpr bool kDumpKey = true;
#endif

void DumpThreads() {
    gpu::g_capture = true;
    for (auto& t : kernel::Threads())
        if (t->ctx) {
            LOGI("thread %X", t->id);
            kernel::LogGuestStack(*t->ctx, 32);
        }
}

// window thread only
void ApplyNow() {
    const Settings& s = Get();
    std::string monitor;
    {
        std::lock_guard lock(UiLock());
        monitor = s.monitor;
    }
    HMONITOR mon = static_cast<HMONITOR>(MonitorHandle(monitor));
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (g_mode_changed) {
        ChangeDisplaySettingsExW(g_mode_device.c_str(), nullptr, nullptr, 0, nullptr);
        g_mode_changed = false;
    }
    GetMonitorInfoW(mon, &mi);
    Mode out = Output();
    LONG_PTR visible = IsWindowVisible(g_hwnd) ? WS_VISIBLE : 0;  // a window held back for a splash or handover stays so
    DEVMODEW desktop{};
    desktop.dmSize = sizeof(desktop);
    EnumDisplaySettingsW(mi.szDevice, ENUM_REGISTRY_SETTINGS, &desktop);
    // the desktop's own size is no mode change: switching to it anyway blanks the screen for nothing
    if (s.display_mode == DisplayMode::Fullscreen && s.width && s.height &&
        (s.width != desktop.dmPelsWidth || s.height != desktop.dmPelsHeight)) {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        dm.dmPelsWidth = s.width;
        dm.dmPelsHeight = s.height;
        dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
        g_mode_changed = ChangeDisplaySettingsExW(mi.szDevice, &dm, nullptr, CDS_FULLSCREEN, nullptr) == DISP_CHANGE_SUCCESSFUL;
        g_mode_device = mi.szDevice;
        GetMonitorInfoW(mon, &mi);
    }
    if (s.display_mode == DisplayMode::Windowed) {
        RECT r{0, 0, LONG(out.width), LONG(out.height)};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        int w = r.right - r.left, h = r.bottom - r.top;
        const RECT& a = mi.rcWork;
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | visible);
        SetWindowPos(g_hwnd, HWND_NOTOPMOST, a.left + (a.right - a.left - w) / 2, a.top + (a.bottom - a.top - h) / 2, w, h,
                     SWP_FRAMECHANGED);
    } else {
        const RECT& m = mi.rcMonitor;
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, WS_POPUP | visible);
        SetWindowPos(g_hwnd, HWND_NOTOPMOST, m.left, m.top, m.right - m.left, m.bottom - m.top, SWP_FRAMECHANGED);
    }
    g_display_applied++;
}

// a key went down: the settings panel and hide HUD binds
bool HostKey(WPARAM wp, LPARAM lp) {
    if (lp & (1 << 30) || ConsoleOpen()) return false;  // bit 30: a repeat of a held key
    if (input::HostKey(input::KeyEvent::SettingsPanel, int(wp))) ToggleMenu();
    else if (!MenuOpen() && input::HostKey(input::KeyEvent::HideHud, int(wp))) g_hide_hud = !g_hide_hud;
    else return false;
    return true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    {
        std::lock_guard lock(UiLock());
        if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
    }
    switch (msg) {
        case WM_KEYDOWN:
            if (HostKey(wp, lp)) return 0;
            if (kDumpKey && wp == VK_F3) DumpThreads();
            if (wp == VK_ESCAPE && MenuOpen() && !Capturing()) ToggleMenu();
            else if (wp == VK_ESCAPE && ConsoleOpen()) ToggleConsole();
            if (input::ConsoleKey(int(wp)) && !MenuOpen() && !(lp & (1 << 30))) ToggleConsole();
            return 0;
        case WM_CHAR:
            input::OnChar(unsigned(wp));
            break;
        case WM_INPUT:
            input::OnRawInput(reinterpret_cast<void*>(lp));
            break;
        case WM_LBUTTONDOWN:
            input::OnPress();
            break;
        case WM_LBUTTONUP:
            input::OnClick();
            break;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) {
                input::ReleaseCursor();
                // a changed display mode steps aside for the desktop, minimized, and comes back on return; not while
                // handing over to the next instance, which runs in the same mode
                if (g_mode_changed && !kernel::g_relaunching) {
                    ChangeDisplaySettingsExW(g_mode_device.c_str(), nullptr, nullptr, 0, nullptr);
                    g_mode_changed = false;
                    ShowWindow(hwnd, SW_MINIMIZE);
                }
            } else if (Get().display_mode == DisplayMode::Fullscreen && Get().width && Get().height && !g_mode_changed &&
                       IsWindowVisible(hwnd) && !g_apply_posted) {
                // once: restoring the window activates it again from inside the apply, and a second apply switched
                // the mode back and forth, after which presents were not taken for a while (the game stood still
                // until the next alt-tab)
                g_apply_posted = true;
                PostMessageW(hwnd, kApplyDisplay, 0, 0);
            }
            break;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                // the cursor does nothing in a synced co-op session (mouse.cpp), so none is shown
                // none while a drag turns the OPSAT map; the button's release asks before its state reads up
                bool pointing = (input::InMenu() || input::Pointer()) && !input::Movie() && input::KeyboardActive() &&
                                !input::NetSession() && (!input::MapDrag() || HIWORD(lp) == WM_RBUTTONUP);
                SetCursor(MenuOpen() || ConsoleOpen() ? LoadCursorW(nullptr, MAKEINTRESOURCEW(32512))
                          : pointing                  ? static_cast<HCURSOR>(MenuCursor())
                                                      : nullptr);
                return TRUE;
            }
            break;
        case kApplyDisplay:
            if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
            ApplyNow();
            g_apply_posted = false;
            return 0;
        case kOpenWindow:
            CloseSplash();
            if (wp) ApplyNow();
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            return 0;
        case WM_SYSKEYDOWN:
            // F10 comes this way on its own; with Alt held the key is Windows' (Alt+F4 closes, whatever is bound)
            if (!(lp & (1 << 29)) && HostKey(wp, lp)) return 0;
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
#endif

}  // namespace

std::filesystem::path DataDir() {
#if defined(__APPLE__)
    // an installed app bundle is not ours to write into: saves, settings and caches go to Application Support
    auto dir = ExeDir();
    if (dir.filename() == "MacOS" && dir.parent_path().filename() == "Contents" &&
        dir.parent_path().parent_path().extension() == ".app") {
        if (const char* home = std::getenv("HOME")) {
            std::filesystem::path support = std::filesystem::path(home) / "Library" / "Application Support" / "SCDA";
            std::error_code ec;
            std::filesystem::create_directories(support, ec);
            return support;
        }
    }
#endif
    return ExeDir();
}

std::filesystem::path ExeDir() {
#ifdef _WIN32
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return std::filesystem::path(p).parent_path();
#elif defined(__APPLE__)
    char path[1024] = {};
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        return std::filesystem::canonical(path).parent_path();
    }
    return std::filesystem::current_path();
#else
    char path[1024] = {};
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len > 0) return std::filesystem::path(std::string(path, len)).parent_path();
    return std::filesystem::current_path();
#endif
}

#ifdef _WIN32
int Instance() {
    static const int n = [] {
        const wchar_t* p = std::wcsstr(GetCommandLineW(), L"--instance");
        return p ? std::max(0, std::min(8, _wtoi(p + 10))) : 0;
    }();
    return n;
}

void* Window() { return g_hwnd; }

std::recursive_mutex& UiLock() {
    static std::recursive_mutex m;
    return m;
}

void ApplyDisplay() { PostMessageW(g_hwnd, kApplyDisplay, 0, 0); }
void OpenGameWindow(bool apply) { SendMessageW(g_hwnd, kOpenWindow, apply, 0); }
void RequestClose() { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); }

// the game window's monitor's refresh rate, read again each second (a mode change, a move to another monitor); 0
// when unreadable
uint32_t RefreshRate() {
    static uint32_t rate = 0;
    static ULONGLONG next = 0;
    if (ULONGLONG now = GetTickCount64(); now >= next) {
        next = now + 1000;
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        rate = GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi) &&
                       EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1
                   ? mode.dmDisplayFrequency
                   : 0;
    }
    return rate;
}

void Pace() {
    using clock = std::chrono::steady_clock;
    static clock::time_point next = clock::now();
    uint32_t cap = Get().fps_cap;
    // a few under the refresh rate, so a variable refresh display stays in its range and vsync never waits a whole
    // refresh
    constexpr uint32_t kRefreshMargin = 3;
    if (cap == kFpsCapRefresh) cap = RefreshRate() > kRefreshMargin ? RefreshRate() - kRefreshMargin : 0;
    auto now = clock::now();
    if (!cap) {
        next = now;
        return;
    }
    auto period = std::chrono::nanoseconds(1000000000ull / cap);
    next += period;
    if (next < now - period) next = now;
    while (clock::now() + std::chrono::milliseconds(2) < next) Sleep(1);
    while (clock::now() < next) std::this_thread::yield();
}

}  // namespace app

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    SetUnhandledExceptionFilter(app::WriteCrashDump);
    std::signal(SIGABRT, [](int) { app::WriteCrashDump(nullptr); });
    timeBeginPeriod(1);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // one log a session: a relaunch (front end to mission, story to versus) carries on its predecessor's
    bool relaunched = std::wcsstr(GetCommandLineW(), L"--relaunched") != nullptr;
    std::string log = app::Instance() > 1 ? "scda-" + std::to_string(app::Instance()) : "scda";
    logx::Init((app::DataDir() / "logs").string(), log, relaunched);
    app::Load();
    if (relaunched && !logx::kVerbose) {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        LOGR("Relaunched: %ls", std::filesystem::path(exe).filename().c_str());
    } else {
        app::LogSystem();
    }

    app::GameFiles game;
    if (!app::FindGame(game)) return 1;

    bool splash = app::OpenSplash(), handover = app::StartHandover();

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = app::WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = L"SCDA";
    RegisterClassExW(&wc);
    app::g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Tom Clancy's Splinter Cell: Double Agent", WS_POPUP, 0, 0, 1280, 720,
                                  nullptr, nullptr, inst, nullptr);
    // behind a splash the window stays hidden until the game's first frame; handed over, it is set up hidden
    if (handover) {
        app::ApplyNow();
    } else if (!splash) {
        app::ApplyNow();
        ShowWindow(app::g_hwnd, SW_SHOW);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplWin32_Init(app::g_hwnd);

    input::Init(app::g_hwnd);
    input::LoadBindings();
    std::wstring hdd = app::Instance() > 1 ? L"hdd" + std::to_wstring(app::Instance()) : L"hdd";
    if (app::Instance()) SetWindowTextW(app::g_hwnd, (L"Tom Clancy's Splinter Cell: Double Agent [" + std::to_wstring(app::Instance()) + L"]").c_str());
    if (!kernel::Boot(std::move(game.xbe), IMAGE_XBE, game.dir.string(), (app::DataDir() / hdd).string())) {
        MessageBoxW(app::g_hwnd, L"The game failed to start, see the log in the logs folder.", L"Splinter Cell Double Agent",
                    MB_ICONERROR);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (app::g_mode_changed) ChangeDisplaySettingsW(nullptr, 0);
    gpu::SavePipelineCache();
    logx::Flush();
    kernel::Shutdown(0);
}
#else
void* g_window = nullptr;

int Instance() { return 0; }
void* Window() { return g_window; }

std::recursive_mutex& UiLock() {
    static std::recursive_mutex m;
    return m;
}

constexpr Uint32 kEventApplyDisplay = SDL_EVENT_USER + 1;
constexpr Uint32 kEventOpenWindow = SDL_EVENT_USER + 2;

static void DoApplyDisplay() {
    if (!g_window) return;
    const Settings& s = Get();
    if (s.display_mode == DisplayMode::Fullscreen) {
        SDL_SetWindowFullscreen(static_cast<SDL_Window*>(g_window), true);
    } else {
        SDL_SetWindowFullscreen(static_cast<SDL_Window*>(g_window), false);
        Mode out = Output();
        SDL_SetWindowSize(static_cast<SDL_Window*>(g_window), out.width, out.height);
    }
    g_display_applied++;
}

static void DoOpenWindow(bool apply) {
    if (g_window) {
        if (apply) DoApplyDisplay();
        SDL_ShowWindow(static_cast<SDL_Window*>(g_window));
#ifdef __APPLE__
        @autoreleasepool {
            [NSApp activateIgnoringOtherApps:YES];
            SDL_PropertiesID props = SDL_GetWindowProperties(static_cast<SDL_Window*>(g_window));
            NSWindow* nswin = (__bridge NSWindow*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
            if (nswin) {
                [nswin makeKeyAndOrderFront:nil];
            }
        }
#endif
    }
}

void ApplyDisplay() {
    SDL_Event e{};
    e.type = kEventApplyDisplay;
    SDL_PushEvent(&e);
}

void OpenGameWindow(bool apply) {
    SDL_Event e{};
    e.type = kEventOpenWindow;
    e.user.code = apply ? 1 : 0;
    SDL_PushEvent(&e);
}

void RequestClose() {
    SDL_Event e;
    e.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&e);
}

uint32_t RefreshRate() {
    if (g_window) {
        SDL_DisplayID disp = SDL_GetDisplayForWindow(static_cast<SDL_Window*>(g_window));
        if (disp) {
            const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(disp);
            if (dm && dm->refresh_rate > 1) return uint32_t(dm->refresh_rate);
        }
    }
    return 60;
}

void Pace() {
    using clock = std::chrono::steady_clock;
    static clock::time_point next = clock::now();
    uint32_t cap = Get().fps_cap;
    constexpr uint32_t kRefreshMargin = 3;
    if (cap == kFpsCapRefresh) cap = RefreshRate() > kRefreshMargin ? RefreshRate() - kRefreshMargin : 0;
    auto now = clock::now();
    if (!cap) {
        next = now;
        return;
    }
    auto period = std::chrono::nanoseconds(1000000000ull / cap);
    next += period;
    if (next < now - period) next = now;
    while (clock::now() + std::chrono::milliseconds(2) < next) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    while (clock::now() < next) std::this_thread::yield();
}

}  // namespace app

#if defined(__APPLE__)
namespace {
// the Vulkan loader would otherwise read Homebrew's ICD manifest and load MoltenVK from /opt/homebrew; point it at
// the manifest shipped beside the bundled libMoltenVK.dylib instead, unless the environment already names a driver
void UseBundledVulkanDriver() {
    std::error_code ec;
    auto icd = app::ExeDir().parent_path() / "Resources" / "vulkan" / "icd.d" / "MoltenVK_icd.json";
    if (!std::filesystem::exists(icd, ec)) return;
    if (!std::getenv("VK_DRIVER_FILES")) setenv("VK_DRIVER_FILES", icd.string().c_str(), 1);
    if (!std::getenv("VK_ICD_FILENAMES")) setenv("VK_ICD_FILENAMES", icd.string().c_str(), 1);
}
}  // namespace
#endif

int main(int argc, char** argv) {
#ifdef __APPLE__
    // MoltenVK's default fast math breaks the bit exact positions the EQUAL depth passes need (blocky squares and
    // hatching on lit surfaces). Set before the Vulkan instance exists; a value already in the environment wins.
    setenv("MVK_CONFIG_FAST_MATH_ENABLED", "0", 0);
    UseBundledVulkanDriver();
#endif
#if !defined(_WIN32)
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        rl.rlim_cur = std::min<rlim_t>(rl.rlim_max, 10240);
        setrlimit(RLIMIT_NOFILE, &rl);
    }
#endif
    std::signal(SIGABRT, [](int) { logx::Flush(); });
    std::signal(SIGSEGV, [](int) {
        if (kernel::Thread* t = kernel::Current(); t && t->ctx) {
            LOGE("SIGSEGV on guest thread %X", t->id);
            kernel::LogGuestStack(*t->ctx, 256);
        }
        logx::Flush();
        std::exit(1);
    });

    bool relaunched = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--relaunched") == 0) relaunched = true;
    }
    std::string log = app::Instance() > 1 ? "scda-" + std::to_string(app::Instance()) : "scda";
    logx::Init((app::DataDir() / "logs").string(), log, relaunched);
    app::Load();
    app::LogSystem();

    app::GameFiles game;
    if (!app::FindGame(game)) return 1;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        LOGE("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    app::g_window = SDL_CreateWindow("Tom Clancy's Splinter Cell: Double Agent", 1280, 720,
                                     SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!app::g_window) {
        LOGE("SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }

#ifdef __APPLE__
    @autoreleasepool {
        [NSApp activateIgnoringOtherApps:YES];
        SDL_PropertiesID props = SDL_GetWindowProperties(static_cast<SDL_Window*>(app::g_window));
        NSWindow* nswin = (__bridge NSWindow*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
        if (nswin) {
            [nswin makeKeyAndOrderFront:nil];
            if (nswin.contentView) {
                nswin.contentView.wantsLayer = YES;
            }
        }
    }
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplSDL3_InitForVulkan(static_cast<SDL_Window*>(app::g_window));
    io.PlatformSetImeDataFn = nullptr;

    input::Init(app::g_window);
    input::LoadBindings();

    std::string hdd = app::Instance() > 1 ? "hdd" + std::to_string(app::Instance()) : "hdd";
    if (!kernel::Boot(std::move(game.xbe), IMAGE_XBE, game.dir.string(), (app::DataDir() / hdd).string())) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Splinter Cell Double Agent",
                                 "The game failed to start, see the log in the logs folder.",
                                 static_cast<SDL_Window*>(app::g_window));
        return 1;
    }

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            {
                std::lock_guard lock(app::UiLock());
                if (ImGui::GetCurrentContext()) {
                    ImGui_ImplSDL3_ProcessEvent(&event);
                }
            }
            input::ProcessEvent(event);
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == app::kEventApplyDisplay) {
                app::DoApplyDisplay();
            } else if (event.type == app::kEventOpenWindow) {
                app::DoOpenWindow(event.user.code != 0);
            }
        }

        bool want_relative = input::Active() && !input::InMenu() && !input::Pointer() && !app::MenuOpen() && !app::ConsoleOpen();
        static bool cur_relative = false;
        if (want_relative != cur_relative) {
            SDL_SetWindowRelativeMouseMode(static_cast<SDL_Window*>(app::g_window), want_relative);
            cur_relative = want_relative;
        }

        SDL_Delay(2);
    }

    gpu::SavePipelineCache();
    logx::Flush();
    kernel::Shutdown(0);
    SDL_DestroyWindow(static_cast<SDL_Window*>(app::g_window));
    SDL_Quit();
    return 0;
}
#endif

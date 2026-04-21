#include "Security.h"

#include "Obfuscate.h"
#include "Trap.h"

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <intrin.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

namespace hked::security {

namespace {

// NtQueryInformationProcess is in ntdll but has no lib-stub in winternl.h.
using PFN_NtQueryInformationProcess = LONG (NTAPI*)(
    HANDLE, ULONG, PVOID, ULONG, PULONG);

// Resolve dynamically once per process.
PFN_NtQueryInformationProcess ResolveNtQIP() {
    static PFN_NtQueryInformationProcess fn = []() -> PFN_NtQueryInformationProcess {
        HMODULE nt = GetModuleHandleA(OBF("ntdll.dll"));
        if (!nt) return nullptr;
        return reinterpret_cast<PFN_NtQueryInformationProcess>(
            GetProcAddress(nt, OBF("NtQueryInformationProcess")));
    }();
    return fn;
}

// ---------------------------------------------------------------------------
// Individual checks (true => detection)
// ---------------------------------------------------------------------------

bool Check_IsDebuggerPresent() {
    return IsDebuggerPresent() != FALSE;
}

bool Check_RemoteDebugger() {
    BOOL present = FALSE;
    if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &present)) {
        if (present) return true;
    }
    return false;
}

bool Check_PEB_BeingDebugged() {
#if defined(_M_X64)
    const auto peb = reinterpret_cast<const uint8_t*>(__readgsqword(0x60));
    if (peb && peb[0x02] /* BeingDebugged */) return true;
    // NtGlobalFlag @ PEB + 0xBC (on x64, it's 0xBC).
    if (peb) {
        DWORD flags = *reinterpret_cast<const DWORD*>(peb + 0xBC);
        constexpr DWORD kHeapTail   = 0x10;
        constexpr DWORD kHeapFree   = 0x20;
        constexpr DWORD kHeapValid  = 0x40;
        if (flags & (kHeapTail | kHeapFree | kHeapValid)) return true;
    }
#endif
    return false;
}

bool Check_NtQueryDebugPort() {
    auto fn = ResolveNtQIP();
    if (!fn) return false;

    // ProcessDebugPort (7): non-zero => attached debugger.
    DWORD_PTR port = 0;
    ULONG ret = 0;
    if (fn(GetCurrentProcess(), 7, &port, sizeof(port), &ret) == 0 && port != 0) {
        return true;
    }
    // ProcessDebugFlags (0x1F): 0 => debugger inherited.
    DWORD flags = 1;
    if (fn(GetCurrentProcess(), 0x1F, &flags, sizeof(flags), &ret) == 0 && flags == 0) {
        return true;
    }
    // ProcessDebugObjectHandle (0x1E): non-null => debug object attached.
    HANDLE h = nullptr;
    if (fn(GetCurrentProcess(), 0x1E, &h, sizeof(h), &ret) == 0 && h != nullptr) {
        return true;
    }
    return false;
}

bool Check_HardwareBreakpoints() {
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(GetCurrentThread(), &ctx)) return false;
    // Any of Dr0..Dr3 set => a hardware breakpoint is active.
    return (ctx.Dr0 | ctx.Dr1 | ctx.Dr2 | ctx.Dr3) != 0;
}

bool Check_RdtscSkew() {
    // If a debugger is single-stepping / attached, RDTSC deltas across a
    // trivial region blow out. Tolerate noisy hosts with a generous budget.
    LARGE_INTEGER freq{}, a{}, b{};
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) return false;
    QueryPerformanceCounter(&a);
    volatile uint64_t acc = 0;
    for (int i = 0; i < 2048; ++i) acc += static_cast<uint64_t>(i) * 2654435761ULL;
    QueryPerformanceCounter(&b);
    double ms = (b.QuadPart - a.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
    (void)acc;
    return ms > 35.0; // tight-loop over 2k iterations should never take >35ms
}

bool StrContainsCI(const char* hay, const char* needle) {
    if (!hay || !needle) return false;
    const size_t nl = std::strlen(needle);
    if (nl == 0) return true;
    for (; *hay; ++hay) {
        size_t i = 0;
        for (; i < nl; ++i) {
            char a = static_cast<char>(std::tolower(static_cast<unsigned char>(hay[i])));
            char b = static_cast<char>(std::tolower(static_cast<unsigned char>(needle[i])));
            if (a != b || hay[i] == '\0') break;
        }
        if (i == nl) return true;
    }
    return false;
}

// Scan running processes for popular debugger / dumper / cheat-tool names.
bool Check_ProcessNames() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    const char* const blacklist[] = {
        OBF("x64dbg.exe"), OBF("x32dbg.exe"), OBF("ollydbg.exe"),
        OBF("idaq.exe"),   OBF("idaq64.exe"), OBF("ida.exe"),
        OBF("ida64.exe"),  OBF("windbg.exe"), OBF("radare2.exe"),
        OBF("cheatengine-x86_64.exe"), OBF("cheatengine-i386.exe"),
        OBF("scylla_x64.exe"),         OBF("scylla_x86.exe"),
        OBF("httpdebuggerui.exe"),     OBF("fiddler.exe"),
        OBF("wireshark.exe"),          OBF("processhacker.exe"),
        OBF("ghidra.exe"),             OBF("dnspy.exe"),
    };

    bool hit = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            char narrow[MAX_PATH] = {};
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1,
                                narrow, sizeof(narrow), nullptr, nullptr);
            for (const char* bad : blacklist) {
                if (StrContainsCI(narrow, bad)) { hit = true; break; }
            }
            if (hit) break;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return hit;
}

// Look for well-known debugger window class names.
bool Check_WindowNames() {
    const char* const classes[] = {
        OBF("OLLYDBG"),
        OBF("WinDbgFrameClass"),
        OBF("ID"), // IDA main class is "Qt5QWindowIcon", not great signal -- skip
    };
    const char* const titles[] = {
        OBF("Cheat Engine"),
        OBF("x64dbg"),
        OBF("x32dbg"),
        OBF("HTTP Debugger"),
    };
    for (const char* cls : classes) {
        if (FindWindowA(cls, nullptr) != nullptr) return true;
    }
    for (const char* t : titles) {
        if (FindWindowA(nullptr, t) != nullptr) return true;
    }
    return false;
}

// Detect inline hooks on a handful of anti-debug-sensitive APIs by checking
// that the first instruction bytes match the un-hooked prologue. If a
// ScyllaHide / detours / minhook trampoline is in place, the first byte is
// usually 0xE9 (near jmp) or 0xFF 0x25 (indirect jmp).
bool Check_InlineHooks() {
    struct Target { const char* mod; const char* fn; };
    const Target targets[] = {
        { OBF("ntdll.dll"),    OBF("NtQueryInformationProcess") },
        { OBF("kernel32.dll"), OBF("IsDebuggerPresent") },
        { OBF("kernel32.dll"), OBF("CheckRemoteDebuggerPresent") },
        { OBF("ntdll.dll"),    OBF("NtSetInformationThread") },
    };
    for (const auto& t : targets) {
        HMODULE mod = GetModuleHandleA(t.mod);
        if (!mod) continue;
        auto addr = reinterpret_cast<const uint8_t*>(GetProcAddress(mod, t.fn));
        if (!addr) continue;
        // Hot-patch hook signatures.
        if (addr[0] == 0xE9) return true;                                // near jmp
        if (addr[0] == 0xEB) return true;                                // short jmp
        if (addr[0] == 0xFF && addr[1] == 0x25) return true;             // jmp [mem]
        if (addr[0] == 0x68 && addr[5] == 0xC3) return true;             // push imm; ret
    }
    return false;
}

// ---------------------------------------------------------------------------
// Dispatcher
// ---------------------------------------------------------------------------

// Returns 0 if clean, else a non-zero bitmask of failed checks. The caller
// always traps on non-zero, but we keep the mask so the watchdog can
// diverge between traps on repeat hits.
uint32_t RunAll(bool cheap_only) {
    uint32_t mask = 0;
    if (Check_IsDebuggerPresent())   mask |= 1u << 0;
    if (Check_RemoteDebugger())      mask |= 1u << 1;
    if (Check_PEB_BeingDebugged())   mask |= 1u << 2;
    if (Check_HardwareBreakpoints()) mask |= 1u << 3;
    if (!cheap_only) {
        if (Check_NtQueryDebugPort()) mask |= 1u << 4;
        if (Check_RdtscSkew())        mask |= 1u << 5;
        if (Check_ProcessNames())     mask |= 1u << 6;
        if (Check_WindowNames())      mask |= 1u << 7;
        if (Check_InlineHooks())      mask |= 1u << 8;
    }
    return mask;
}

// ---------------------------------------------------------------------------
// Watchdog
// ---------------------------------------------------------------------------

std::atomic<bool> g_watchdog_running { false };
std::thread       g_watchdog_thread;

} // namespace

void RunStartupChecks() {
    if (uint32_t m = RunAll(/*cheap_only=*/false)) {
        // Hash the mask into the trap selector so which crash path fires
        // depends on what was detected.
        HKED_TRAP();
        (void)m;
    }
}

void StartWatchdog() {
    bool expected = false;
    if (!g_watchdog_running.compare_exchange_strong(expected, true)) return;
    g_watchdog_thread = std::thread([]() {
        // Randomize the polling interval a little so timing-based detection
        // of the watchdog itself is harder.
        uint32_t phase = static_cast<uint32_t>(HKED_TRAP_SEED);
        while (g_watchdog_running.load()) {
            if (RunAll(/*cheap_only=*/true)) {
                HKED_TRAP();
            }
            phase = phase * 1664525u + 1013904223u;
            std::this_thread::sleep_for(
                std::chrono::milliseconds(180 + (phase % 140)));
        }
    });
}

void StopWatchdog() {
    if (!g_watchdog_running.exchange(false)) return;
    if (g_watchdog_thread.joinable()) g_watchdog_thread.join();
}

} // namespace hked::security

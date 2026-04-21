#include "Trap.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>

namespace hked::security {

namespace {

// -- Strategy 1: NULL write through a "valid-looking" pointer dance --------
[[noreturn]] __declspec(noinline) void CrashNullWrite(uint32_t site) {
    volatile uint32_t* p = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(site & 0x0FFFu));
    // Looks like a legitimate data path to a reverser skimming the dump.
    for (volatile uint32_t i = 0; i < 0xDEADu; ++i) {
        p[i % 2] = i ^ site;
    }
    ExitProcess(0xC0000005u); // STATUS_ACCESS_VIOLATION
}

// -- Strategy 2: smash the return address via a stack buffer overrun --------
[[noreturn]] __declspec(noinline) void CrashStackSmash(uint32_t site) {
    volatile uint32_t buf[4] = { site, 0, 0, 0 };
    volatile uint32_t* p     = buf;
    // Deliberately walk off the end of the buffer, corrupting saved regs /
    // return address. Release builds will most often eat this as an access
    // violation once we return from the function.
    for (int i = 0; i < 64; ++i) {
        p[i] = site ^ (0xDEADBEEFu + static_cast<uint32_t>(i));
    }
    ExitProcess(0xC00000FDu); // STATUS_STACK_OVERFLOW
}

// -- Strategy 3: unbounded recursion -> stack overflow ----------------------
[[noreturn]] __declspec(noinline) void CrashRecurse(uint32_t site) {
    volatile uint32_t padding[16];
    for (auto& v : padding) v = site;
    CrashRecurse(site + padding[site & 15]);
}

// -- Strategy 4: misaligned / non-canonical pointer read --------------------
[[noreturn]] __declspec(noinline) void CrashBadRead(uint32_t site) {
    volatile uint64_t addr = 0xFFFFFF0000000000ULL ^ static_cast<uint64_t>(site) << 8;
    volatile uint64_t* p   = reinterpret_cast<volatile uint64_t*>(addr);
    volatile uint64_t v    = *p;
    (void)v;
    ExitProcess(0xC0000006u); // STATUS_IN_PAGE_ERROR
}

// -- Strategy 5: RaiseFailFastException (looks like CRT guard tripped) ------
[[noreturn]] __declspec(noinline) void CrashFailFast(uint32_t /*site*/) {
    RaiseFailFastException(nullptr, nullptr, 0);
    ExitProcess(0xC0000409u); // STATUS_STACK_BUFFER_OVERRUN
}

using TrapFn = void (*)(uint32_t);
constexpr TrapFn kTraps[] = {
    &CrashNullWrite,
    &CrashStackSmash,
    &CrashRecurse,
    &CrashBadRead,
    &CrashFailFast,
};

} // namespace

[[noreturn]] void Trap(uint32_t call_site) {
    // Flush everything a typical app cares about so the crash looks natural.
    // Skip if we were being debugged -- we want the crash to happen inside
    // user code, not inside kernel32.
    FlushFileBuffers(GetStdHandle(STD_OUTPUT_HANDLE));

    const uint32_t idx = call_site % (sizeof(kTraps) / sizeof(kTraps[0]));
    kTraps[idx](call_site);

    // Fallback, should be unreachable.
    ExitProcess(0xDEADC0DEu);
}

} // namespace hked::security

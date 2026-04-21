// Trap.h -- deliberately destructive "oops we crashed" routines used when a
// tamper check fires. The goal is to make the crash look organic, land far
// from the check that triggered it, and vary between builds / call sites so
// a cracker cannot pattern-match on a single signature.
//
// All routines are [[noreturn]]; they never return to the caller.

#pragma once

#include <cstdint>

namespace hked::security {

// Picks one of several divergent crash strategies based on a seed mixed
// with __LINE__ and the per-build HKED_TRAP_SEED. The selected routine
// corrupts stack / writes to bogus memory / recurses, so the crash address
// on the dump is different per call site and per build.
[[noreturn]] void Trap(uint32_t call_site);

} // namespace hked::security

#include "ObfKey.generated.h"

// Convenience: invoke at the current source line with a unique tag.
#define HKED_TRAP()                                                           \
    ::hked::security::Trap(                                                   \
        static_cast<uint32_t>(                                                \
            (HKED_TRAP_SEED ^                                                 \
             (static_cast<uint32_t>(__LINE__) * 0x85EBCA77u) ^                \
             (static_cast<uint32_t>(__COUNTER__) * 0xC2B2AE3Du))))

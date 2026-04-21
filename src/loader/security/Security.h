// Security.h -- anti-debug / anti-tamper surface used by the loader.
//
// Call RunStartupChecks() once before the splash UI goes up; it runs every
// check synchronously and triggers a Trap() on the first positive. Call
// StartWatchdog() to spawn a background thread that re-runs the cheap
// checks a few times per second so a late-attach debugger doesn't sail
// through.
//
// All checks are "best-effort": they detect common off-the-shelf tooling
// (x64dbg, OllyDbg, IDA, Cheat Engine, ScyllaHide, Scylla, HTTPDebugger,
// Wireshark-style inline hooks). A motivated reverser with a custom
// kernel-mode debugger will still get through. Use VMProtect / Themida /
// Enigma on top of this if you need more.

#pragma once

namespace hked::security {

// Full battery of checks. On detection, invokes Trap() and never returns.
void RunStartupChecks();

// Kicks off a background thread running the cheap subset on a timer.
// Idempotent: calling twice is a no-op.
void StartWatchdog();

// Stops the watchdog (call before tearing down the process).
void StopWatchdog();

} // namespace hked::security

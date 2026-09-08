#pragma once

#include "shim/common.h"

namespace shim::hooks::x86 {

// Calls a game function that takes its first two arguments in ECX/EDX and
// leaves the remaining stack arguments for the caller to release.
//
// MSVC x86 has no keyword for this convention. __fastcall matches the register
// half but gets the cleanup backwards: it makes the callee release the tail, so
// a __fastcall declaration emits a call site with no cleanup against a callee
// that performs none either. The tail leaks, and the first frame that pops
// callee-saved registers then restores them from the wrong slots. That is what
// broke the 1.1.5 bring-up: the AppMain factory leaked 12 bytes, so BringUp()
// returned with EDI holding a local address instead of the version profile.
//
// The release is computed from EBP, not from the pushed count, so this is also
// correct against a callee-cleaned target: either the callee popped the tail
// and the release is a no-op, or it did not and the release removes it. A call
// site whose convention has not been confirmed on the wire is therefore safe to
// route through here.
void* CallRegisterCallerClean(const void* function, void* ecx_argument,
                              void* edx_argument,
                              const void* const* stack_arguments,
                              size_t stack_argument_count) noexcept;

}  // namespace shim::hooks::x86

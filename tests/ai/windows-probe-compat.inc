// Included before campaign-balance-probe.inc when run-campaign-balance.py builds the probe on
// Windows (clang-cl with the MSVC library). The shipped game never includes it.

// Network probes include ENet after this shim. Finish SDK socket declarations while
// FAR/NEAR still expand to their pointer-qualifier aliases.
#include <winsock2.h>

// <windows.h> (minwindef.h) defines these 16-bit pointer qualifiers away; the probes use them as names.
#undef near
#undef far

#include "robustlist-iterator-traits.inc"

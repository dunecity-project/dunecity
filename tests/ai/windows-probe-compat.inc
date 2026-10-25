// Included before campaign-balance-probe.inc when run-campaign-balance.py builds the probe on
// Windows (clang-cl with the MSVC library). The shipped game never includes it.

// Network probes include ENet after this shim. Finish SDK socket declarations while
// FAR/NEAR still expand to their pointer-qualifier aliases.
#include <winsock2.h>

// <windows.h> (minwindef.h) defines these 16-bit pointer qualifiers away; the probes use them as names.
#undef near
#undef far

// The MSVC library's std::find reads iterator_traits, which RobustList's iterators do not declare
// (libc++ does not need them). Prefix increment only: their postfix forms return void.
namespace std {
template<typename T>
struct iterator_traits<RobustListIterator<T>> {
    using iterator_category = input_iterator_tag;
    using value_type = T;
    using difference_type = ptrdiff_t;
    using pointer = T*;
    using reference = T&;
};

template<typename T>
struct iterator_traits<RobustListConstIterator<T>> {
    using iterator_category = input_iterator_tag;
    using value_type = T;
    using difference_type = ptrdiff_t;
    using pointer = const T*;
    using reference = const T&;
};
}

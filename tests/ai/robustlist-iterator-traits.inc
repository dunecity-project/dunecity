// Included before campaign-balance-probe.inc when run-campaign-balance.py builds the probe on
// Windows or Linux. The shipped game never includes it.

// The MSVC library's and libstdc++'s std::find read iterator_traits, which RobustList's iterators
// do not declare (libc++ does not need them). Prefix increment only: their postfix forms return void.
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

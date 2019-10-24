#pragma once

// Helper to use the int type of enums.
template <typename E>
constexpr typename std::underlying_type<E>::type typed(E e) noexcept {
    return static_cast<typename std::underlying_type<E>::type>(e);
}

// The % operator in C is actually the "remainder". To cycle through
// enum values we need the actual mod operator.
inline int mod(int a, int b) noexcept
{
    int r = a % b;
    return r < 0 ? r + b : r;
}


// Add integer to enum.
template <typename E>
constexpr E typed_add(E e, int i) noexcept {
    return static_cast<E>(static_cast<typename std::underlying_type<E>::type>(e) + i);
}

// Add integer to enum and mod it.
template <typename E>
inline E typed_add_mod(E e, int i, E max_value) noexcept {
    return static_cast<E>(
        mod(static_cast<typename std::underlying_type<E>::type>(e) + i, static_cast<typename std::underlying_type<E>::type>(max_value)));
}
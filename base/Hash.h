/*
 * Copyright (c) 2019-2023 WangBin <wbsecg1 at gmail.com>
 */
#pragma once
#include <string_view>

// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
// https://github.com/Kronuz/constexpr-phf/blob/master/hashes.hh
namespace detail
{
    template <typename T, T prime, T offset>
    struct fnv1ah {
        static constexpr T hash(const char *p, size_t len, T seed = offset) {
            T h = seed;
            for (size_t i = 0; i < len; ++i)
                h = (h ^ static_cast<unsigned char>(p[i])) * prime;
            return h;
        }

        template <size_t N>
        static constexpr T hash(const char(&s)[N], T seed = offset) {
            return hash(s, N - 1, seed);
        }

        static T hash(std::string_view s, T seed = offset) {
            return hash(s.data(), s.size(), seed);
        }

        template <typename... Args>
        constexpr T operator()(Args&&... args) const {
            return hash(std::forward<Args>(args)...);
        }
    };

    using fnv1ah32 = fnv1ah<uint32_t, 0x1000193UL, 2166136261UL>;
    using fnv1ah64 = fnv1ah<uint64_t, 0x100000001b3ULL, 14695981039346656037ULL>;

    // FNV-1a 32bit hashing algorithm.
    constexpr uint32_t fnv1a_32(char const* s, size_t count)
    {
        constexpr uint32_t Prime = 16777619u;
        constexpr uint32_t Seed = 2166136261u;
        return ((count > 1 ? fnv1a_32(s, count - 1) : Seed) ^ s[count - 1]) * Prime;
    }

    constexpr uint32_t fnv1a_32(std::string_view s)
    {
        return fnv1a_32(s.data(), s.size());
    }
}    // namespace detail

constexpr uint32_t operator"" _svh(char const* s, size_t count)
{
    return detail::fnv1ah32::hash(s, count);
}

constexpr uint32_t operator"" _svh2(char const* s, size_t count)
{
    return detail::fnv1a_32(s, count);
}

static_assert("test"_svh == "test"_svh2);

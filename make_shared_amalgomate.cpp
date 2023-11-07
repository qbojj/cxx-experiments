#include <cstdint>
#include <memory>
#include <utility>
#include <span>

namespace detail {

    constexpr std::size_t align_up(std::size_t val, std::size_t alignment)
    {
        return (val + alignment - 1) & ~(alignment - 1);
    }

    template <typename T>
    constexpr T* align_up(T* val, std::size_t alignment)
    {
        return reinterpret_cast<T*>((reinterpret_cast<std::intptr_t>(val) + alignment - 1) & ~(alignment - 1));
    }

    template <typename... T>
    struct amalgamate;

    template <>
    struct amalgamate<> {
        static constexpr std::size_t additional_alignment = 1;
        static constexpr std::size_t array_vals = 0;

        static std::size_t get_additional_size()
        {
            return 0;
        }

        void setup(std::byte*) { }

        auto get_as_tuple(auto ptr)
        {
            return std::make_tuple();
        }
    };

    template <typename T, typename... Ts>
    struct amalgamate<T, Ts...> : amalgamate<Ts...> {
        using amalgamate<Ts...>::additional_alignment;
        using amalgamate<Ts...>::array_vals;
        using amalgamate<Ts...>::get_additional_size;
        using amalgamate<Ts...>::setup;

        auto get_as_tuple(auto ptr)
        {
            return std::tuple_cat(std::make_tuple(std::shared_ptr<T>(ptr, &data)), amalgamate<Ts...>::get_as_tuple(ptr));
        }

        T data;
    };

    template <typename U, typename... Ts>
    struct amalgamate<U[], Ts...> : amalgamate<Ts...> {
        constexpr ~amalgamate() { std::destroy(data.begin(), data.end()); };

        static constexpr std::size_t additional_alignment = std::max(alignof(U), amalgamate<Ts...>::additional_alignment);
        static constexpr std::size_t array_vals = 1 + amalgamate<Ts...>::array_vals;

        template <typename T, typename... CntT>
        static std::size_t get_additional_size(T cnt, CntT... rest)
        {
            static_assert(sizeof...(CntT) + 1 == array_vals, "number of extents must equal to number of T[] types");
            return sizeof(U) * cnt + align_up(amalgamate<Ts...>::get_additional_size(rest...), amalgamate<Ts...>::additional_alignment);
        }

        template <typename T, typename... CntT>
        void setup(std::byte* ptr, T cnt, CntT... rest)
        {
            ptr = align_up(ptr, alignof(U));
            data = std::span(reinterpret_cast<U*>(ptr), cnt);
            std::uninitialized_default_construct_n(data.begin(), cnt);
            amalgamate<Ts...>::setup(ptr + sizeof(U) * cnt);
        }

        auto get_as_tuple(auto ptr)
        {
            return std::tuple_cat(std::make_tuple(std::shared_ptr<const std::span<U>>(ptr, &data)), amalgamate<Ts...>::get_as_tuple(ptr));
        }

        std::span<U> data;
    };

}

// makes any amalgamate of structures with maximmum of 2 allocations
template <typename... T>
auto make_shared_amalgomate(auto... counts)
{
    using amalg = detail::amalgamate<T...>;
    struct amalg_storage {
        std::unique_ptr<std::byte[]> storage;
        amalg a;
    };
    std::size_t storage_size = amalg::get_additional_size(counts...) + amalg::additional_alignment - 1;

    auto ptr = std::make_shared<amalg_storage>();
    ptr->storage = storage_size > 0 ? std::make_unique<std::byte[]>(storage_size) : nullptr;

    ptr->a.setup(ptr->storage.get(), counts...);

    return ptr->a.get_as_tuple(ptr);
}

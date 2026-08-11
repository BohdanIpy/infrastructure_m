module;

#include <cstddef>
#include <limits>
#include <new>
#include <utility>
#include <openssl/crypto.h>

export module SecurityCoreLibrary:CryptoAllocator;

export namespace security_core::crypto_allocator
{
    template<typename T>
    class CryptoAllocator
    {
    public:
        using value_type = T;
        using pointer = T*;
        using const_pointer = const T*;
        using difference_type = std::ptrdiff_t;
        using size_type = std::size_t;

        constexpr CryptoAllocator() noexcept = default;

        template <typename U>
        constexpr CryptoAllocator(const CryptoAllocator<U>&) noexcept {}

        [[nodiscard]] pointer allocate(std::size_t n) {
            if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
                throw std::bad_array_new_length{};
            }

            void* memory = OPENSSL_secure_malloc(n * sizeof(T));
            if (!memory) {
                throw std::bad_alloc{};
            }
            return static_cast<T*>(memory);
        }

        void deallocate(T* p, std::size_t n) noexcept {
            if (p) {
                OPENSSL_secure_clear_free(reinterpret_cast<void*>(p), n * sizeof(T));
            }
        }

        template <typename U>
        bool operator==(const CryptoAllocator<U>&) const noexcept { return true; }

        template <typename U>
        bool operator!=(const CryptoAllocator<U>&) const noexcept { return false; }
    };
}

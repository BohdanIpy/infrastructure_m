module;

#include <vector>
#include <memory>
#include <openssl/bio.h>
#include <openssl/provider.h>
#include <openssl/types.h>

export module SecurityCoreLibrary:CommonTypes;

import :CryptoAllocator;

export namespace security_core::common_types {

    using namespace security_core::crypto_allocator;

    enum class CryptoMode : uint8_t
    {
        DecryptMode = 0,
        EncryptMode
    };

    inline void BioFree(BIO *bio)
    {
        if (bio)
        {
            BIO_free_all(bio);
        }
    }

    inline auto clearFunction = [](unsigned char* ptr) {
        OPENSSL_free(ptr);
    };
    using HexStrPtr = std::unique_ptr<unsigned char, decltype(clearFunction)>;
    using UniqueBIOPtr = std::unique_ptr<BIO, decltype(&BioFree)>;
    using OsslProviderPtr = std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)>;

    template<typename CharT, typename CharTraits, typename Allocator>
    using BasicCryptoString = std::basic_string<CharT, CharTraits, Allocator>;

    using AsciiCryptoString = BasicCryptoString<char, std::char_traits<char>, CryptoAllocator<char>>;

    template<typename T>
    using CryptoVector = std::vector<T, CryptoAllocator<T>>;

}
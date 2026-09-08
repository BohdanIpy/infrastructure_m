module;

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <openssl/evp.h>
#include <vector>

export module SecurityCoreLibrary:KeyParser;

import :CommonTypes;

using security_core::common_types::CryptoVector;
using security_core::common_types::AsciiCryptoString;

export namespace security_core::key_parer
{
    class KeyParser
    {
    public:

        static bool IsHex(const AsciiCryptoString& str)
        {
            return !str.empty() &&
                   (str.length() % 2 == 0) &&
                   std::all_of(str.begin(), str.end(), [](unsigned char c) {
                       return std::isxdigit(c);
                   });
        }

        static CryptoVector<unsigned char> HexToBytes(const AsciiCryptoString& hex)
        {
            if (!IsHex(hex)) {
                throw std::invalid_argument("Input string is not valid hexadecimal format.");
            }

            long outLen = 0;
            unsigned char* buf = OPENSSL_hexstr2buf(hex.c_str(), &outLen);
            if (!buf) {
                throw std::runtime_error("Failed to parse hex string!");
            }

            CryptoVector<unsigned char> bytes(buf, buf + outLen);
            OPENSSL_free(buf);
            return bytes;
        }

        static void ResolveKeyAndIv(
            const AsciiCryptoString& keyOrPassword,
            const AsciiCryptoString& salt,
            const AsciiCryptoString& iv,
            const size_t requiredKeyLen,
            const size_t requiredIvLen,
            CryptoVector<unsigned char>& outKey,
            CryptoVector<unsigned char>& outIv
        )
        {
            // --------------------------------------------------------------------
            // PATH 1: Direct Hex Key & Direct Hex IV
            // --------------------------------------------------------------------
            if (IsHex(keyOrPassword) && keyOrPassword.length() == requiredKeyLen * 2 &&
                IsHex(iv)            && iv.length() == requiredIvLen * 2)
            {
                outKey = HexToBytes(keyOrPassword);
                outIv  = HexToBytes(iv);
                return;
            }

            // Prepare Salt bytes
            CryptoVector<unsigned char> rawSalt;
            if (IsHex(salt)) {
                rawSalt = HexToBytes(salt);
            } else {
                rawSalt.assign(salt.begin(), salt.end());
            }

            // --------------------------------------------------------------------
            // PATH 2: Password + Explicit IV Provided
            // (Matches: openssl enc -pbkdf2 -iv <hex>)
            // --------------------------------------------------------------------
            if (!iv.empty())
            {
                // 1. Parse the explicit IV directly
                if (IsHex(iv)) {
                    outIv = HexToBytes(iv);
                } else {
                    outIv.assign(iv.begin(), iv.end());
                }

                // Ensure IV matches required byte length (pad or truncate if needed)
                if (outIv.size() < requiredIvLen) {
                    outIv.resize(requiredIvLen, 0x00);
                } else if (outIv.size() > requiredIvLen) {
                    outIv.resize(requiredIvLen);
                }

                // 2. Derive ONLY the Key length via PBKDF2
                outKey.resize(requiredKeyLen);
                if (PKCS5_PBKDF2_HMAC(
                        keyOrPassword.c_str(),
                        static_cast<int>(keyOrPassword.length()),
                        rawSalt.data(),
                        static_cast<int>(rawSalt.size()),
                        100000, // Matches `-iter 100000`
                        EVP_sha256(),
                        static_cast<int>(requiredKeyLen),
                        outKey.data()
                    ) != 1)
                {
                    throw std::runtime_error("PBKDF2 key derivation failed!");
                }

                return;
            }

            // --------------------------------------------------------------------
            // PATH 3: Password + NO IV Provided
            // (Matches default: openssl enc -pbkdf2)
            // Derive BOTH Key and IV together from PBKDF2
            // --------------------------------------------------------------------
            const size_t totalLen = requiredKeyLen + requiredIvLen;
            CryptoVector<unsigned char> derivedBytes(totalLen);

            if (PKCS5_PBKDF2_HMAC(
                    keyOrPassword.c_str(),
                    static_cast<int>(keyOrPassword.length()),
                    rawSalt.data(),
                    static_cast<int>(rawSalt.size()),
                    100000,
                    EVP_sha256(),
                    static_cast<int>(totalLen),
                    derivedBytes.data()
                ) != 1)
            {
                throw std::runtime_error("PBKDF2 derivation failed!");
            }

            // Split derived bytes: Key first, IV second
            outKey.assign(derivedBytes.begin(), derivedBytes.begin() + requiredKeyLen);
            outIv.assign(derivedBytes.begin() + requiredKeyLen, derivedBytes.end());
        }
    };
}

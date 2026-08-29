module;

#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/provider.h>
#include <memory>
#include <queue>
#include <exception>
#include <stack>

export module SecurityCoreLibrary:BIOw;

import :CommonTypes;
import :KeyParser;

using namespace security_core::common_types;
using security_core::key_parer::KeyParser;

export namespace security_core::biow
{
    class IBioDecryptProcessor
    {
    public:

        virtual ~IBioDecryptProcessor() = default;

        virtual void operator()(BIO* bio) = 0;
    };

    class IBioEncryptProcessor
    {
    public:
        virtual ~IBioEncryptProcessor() = default;

        virtual void operator()(BIO* bio) = 0;
    };

    class BioConsoleEncryptProvider : public IBioEncryptProcessor
    {
    public:
        void operator()(BIO* bio) override;
    };

    class BioConsoleDecryptProvider : public IBioDecryptProcessor
    {
    public:

        void operator()(BIO* bio) override;
    };

    class BioLink
    {
        UniqueBIOPtr m_bio;
        std::function<void(BIO*)> m_processor;
    public:

        BioLink(const BioLink&) = delete;
        BioLink& operator=(const BioLink&) = delete;

        BioLink(BioLink&& rhs) noexcept : m_bio{std::move(rhs.m_bio)},
            m_processor(std::move(rhs.m_processor)) {}

        BioLink& operator=(BioLink&& rhs) noexcept;

        BioLink(BIO *bio, std::function<void(BIO*)> processor) : m_bio{bio, BioFree}, m_processor{std::move(processor)}
        {}

        void apply_processor() const;

        [[nodiscard]]
        BIO* get() const;

        [[nodiscard]]
        BIO* release();
    };

    class BioChain
    {
        std::stack<BioLink> m_linkStack;

        std::optional<BioLink> PopTop();

    public:

        BioChain() = default;

        BioChain& AddBioPtr(BioLink bio_ptr);

        BioChain& AddBio(BIO *bio, std::function<void(BIO*)> processor = nullptr);

        UniqueBIOPtr BuildChain();
    };

    // From the top to nested bio, e.g.: cipher => b64 => cipher => buffer => fd
    inline BIO* FindCipherBioInChain(BIO* chain_head, std::size_t positionToFind) {
        std::size_t pos = 1;
        BIO* current = chain_head;
        while (current != nullptr) {
            if (BIO_method_type(current) == BIO_TYPE_CIPHER) {
                if (positionToFind == pos)
                {
                    return current;
                }
                pos++;
            }
            current = BIO_next(current);
        }
        return nullptr;
    }

    enum class BioSeekability {
        None,       // Sockets, pipelines with Stream Ciphers / HMAC
        ForwardOnly,// Can simulate seek by reading & discarding
        Full        // File BIOs or Memory BIOs
    };

    enum class BioMode {
        None      = 0,
        ReadOnly  = 1 << 0,
        WriteOnly = 1 << 1,
        ReadWrite = ReadOnly | WriteOnly
    };

    inline BioSeekability QueryBioSeekability(BIO* bio) {
        if (!bio) return BioSeekability::None;

        // Try querying the current position or probing seek capability
        long pos = BIO_tell(bio);
        if (pos >= 0) {
            // Attempt a 0-offset relative seek to verify if seek actually works
            if (BIO_seek(bio, pos) >= 0) {
                return BioSeekability::Full;
            }
        }

        // If random seek isn't supported natively, we can still emulate forward seeking
        return BioSeekability::ForwardOnly;
    }

    inline BioMode QueryBioMode(BIO* bio) {
        if (!bio) return BioMode::None;

        // 1. Check explicit OpenSSL flags
        int flags = BIO_test_flags(bio, BIO_FLAGS_READ | BIO_FLAGS_WRITE);

        bool can_read  = !(flags & BIO_FLAGS_WRITE); // If not explicitly write-only
        bool can_write = !(flags & BIO_FLAGS_READ);  // If not explicitly read-only

        // 2. Fallback: Inspect BIO method type for built-in defaults
        int bio_type = BIO_method_type(bio);

        switch (bio_type & BIO_TYPE_START) {
        case BIO_TYPE_MEM:
            // Memory BIOs are generally Read/Write unless marked read-only (e.g. BIO_new_mem_buf)
            if (BIO_test_flags(bio, BIO_FLAGS_MEM_RDONLY)) {
                return BioMode::ReadOnly;
            }
            return BioMode::ReadWrite;

        case BIO_TYPE_FILE:
            // File BIOs store access modes internally in their control state
            break;
        }

        if (can_read && can_write) return BioMode::ReadWrite;
        if (can_read)              return BioMode::ReadOnly;
        if (can_write)             return BioMode::WriteOnly;

        return BioMode::None;
    }
}

// module :private;

export namespace security_core::biow
{
    using security_core::common_types::UniqueBIOPtr;
    using security_core::common_types::BioFree;
    using security_core::common_types::CryptoMode;

    std::optional<BioLink> BioChain::PopTop()
    {
        if (m_linkStack.empty())
            return std::nullopt;
        auto elem =  std::move(m_linkStack.top());
        m_linkStack.pop();
        return elem;
    }

    BioChain& BioChain::AddBioPtr(BioLink bio_ptr)
    {
        m_linkStack.push(std::move(bio_ptr));
        return *this;
    }

    BioChain& BioChain::AddBio(BIO * bio, std::function<void(BIO*)> processor)
    {
        m_linkStack.emplace(bio, processor);
        return *this;
    }

    UniqueBIOPtr BioChain::BuildChain()
    {
        auto firstOpt = PopTop();
        if (!firstOpt.has_value())
            return {nullptr, BioFree};
        auto first = std::move(firstOpt.value());

        try
        {
            first.apply_processor();
        }
        catch (const std::exception& e)
        {
            std::cerr << e.what() << std::endl;
            return {nullptr, BioFree};
        }
        catch (...)
        {
            std::cerr << "Non standard exception" << std::endl;
            return {nullptr, BioFree};
        }

        UniqueBIOPtr head{first.release(), BioFree};

        while (!m_linkStack.empty())
        {
            auto nextLink = std::move(m_linkStack.top());
            m_linkStack.pop();

            try
            {
                nextLink.apply_processor();
            }
            catch (const std::exception& e)
            {
                std::cerr << e.what() << std::endl;
                return {nullptr, BioFree};
            }
            catch (...)
            {
                std::cerr << "Non standard exception" << std::endl;
                return {nullptr, BioFree};
            }

            UniqueBIOPtr rawNext{nextLink.release(), BioFree};
            if (!BIO_push(rawNext.get(), head.get()))
            {
                return {nullptr, BioFree};
            }
            (void)head.release();
            head = std::move(rawNext);
        }
        return head;
    }

    void setBioMode(BIO* bio, CryptoMode mode)
    {
        std::string key;
        std::string salt;
        std::string iv;

        std::cout << "[+] Enter Password OR Hex Key: ";
        std::getline(std::cin, key);

        std::cout << "[+] Enter Hex Salt: ";
        std::getline(std::cin, salt);

        std::cout << "[+] Enter IV: ";
        std::getline(std::cin, iv);

        const EVP_CIPHER* cipher = EVP_bf_cbc();
        if (!cipher) {
            throw std::runtime_error{"Cipher non-existent or legacy provider not loaded!"};
        }

        auto keyNeeded = static_cast<std::size_t>(EVP_CIPHER_key_length(cipher)); // 16
        auto ivNeeded  = static_cast<std::size_t>(EVP_CIPHER_iv_length(cipher));  // 8

        std::vector<unsigned char> keyBytes;
        std::vector<unsigned char> ivBytes;

        KeyParser::ResolveKeyAndIv(
            key, salt, iv,
            keyNeeded, ivNeeded,
            keyBytes, ivBytes
        );

        if (BIO_set_cipher(bio, cipher, keyBytes.data(), ivBytes.data(), static_cast<int>(mode)) != 1)
        {
            throw std::runtime_error{"Failed to set cipher configuration on BIO!"};
        }

        std::cout << "[+] Cipher configured successfully!\n";
    }

    void BioConsoleDecryptProvider::operator()(BIO* bio)
    {
        setBioMode(bio, CryptoMode::DecryptMode);
    }

    void BioConsoleEncryptProvider::operator()(BIO* bio)
    {
        setBioMode(bio, CryptoMode::EncryptMode);
    }

    BioLink& BioLink::operator=(BioLink&& rhs) noexcept
    {
        if (this != &rhs)
        {
            this->m_bio = std::move(rhs.m_bio);
            this->m_processor = std::move(rhs.m_processor);
        }
        return *this;
    }

    void BioLink::apply_processor() const
    {
        if (m_processor && m_bio)
        {
            m_processor(m_bio.get());
        }
    }

    BIO* BioLink::get() const
    {
        return m_bio.get();
    }

    BIO* BioLink::release()
    {
        return m_bio.release();
    }
}


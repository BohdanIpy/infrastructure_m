#include <iostream>
#include <unordered_map>
#include <memory>
#include <concepts>
#include <numeric>
#include <cassert>

import SecurityCoreLibrary;

using security_core::crypto_allocator::CryptoAllocator;
using security_core::common_types::AsciiCryptoString;

template <typename T>
concept IsContainer = requires(T& container)
{
    { container.begin() } -> std::input_or_output_iterator;
    { container.end()   } -> std::input_or_output_iterator;
};

template<typename T, typename Alloc = std::allocator<T>>
class RingBuffer
{
public:

    struct NodeBase
    {
        NodeBase* next;
        NodeBase* prev;
    };

    struct Node : NodeBase
    {
        T value;
    };
private:
    using NodeAlloc = std::allocator_traits<Alloc>::template rebind_alloc<Node>;
    using Traits = std::allocator_traits<NodeAlloc>;

    NodeBase m_head;
    Alloc m_alloc;
    NodeAlloc nAlloc;

public:

    struct RingIterator
    {
        using difference_type   = std::ptrdiff_t;
        using value_type        = std::remove_cv_t<T>;
        using pointer           = value_type*;
        using reference         = value_type&;
        using iterator_category = std::bidirectional_iterator_tag;

        NodeBase* node{nullptr};
        NodeBase* head{nullptr};

        RingIterator() = default;
        explicit RingIterator(NodeBase* h, NodeBase* n) : node(n), head(h) {}

        reference operator*() const {
            return static_cast<Node*>(node)->value;
        }

        pointer operator->() const {
            return std::addressof(static_cast<Node*>(node)->value);
        }

        RingIterator& operator++() {
            node = node->next;
            if (node == head) {
                node = head->next;
            }
            return *this;
        }

        RingIterator operator++(int) {
            RingIterator tmp(*this);
            ++(*this);
            return tmp;
        }

        RingIterator& operator--() {
            if (node == head->next) {
                node = head->prev;
            } else {
                node = node->prev;
            }
            return *this;
        }

        RingIterator operator--(int) {
            RingIterator tmp(*this);
            --(*this);
            return tmp;
        }

        bool operator==(const RingIterator& rhs) const { return node == rhs.node; }
        bool operator!=(const RingIterator& rhs) const { return node != rhs.node; }
    };

    void AddValue(const T& value)
    {
        Node* newNode = Traits::allocate(nAlloc, 1);

        try
        {
            Traits::construct(nAlloc, std::addressof(newNode->value), value);
        }catch (...)
        {
            if (newNode)
            {
                Traits::deallocate(nAlloc, newNode, 1);
            }
            throw;
        }

        newNode->next = &m_head;
        newNode->prev = m_head.prev;
        m_head.prev->next = newNode;
        m_head.prev = newNode;
    }

    RingBuffer() : RingBuffer(Alloc()) {}

    RingBuffer(std::initializer_list<T> values, Alloc a = Alloc()) : RingBuffer(a)
    {
        for (const auto& value : values)
        {
            AddValue(value);
        }
    }

    explicit RingBuffer(Alloc a) : m_alloc(a), nAlloc{m_alloc}
    {
        m_head.next = &m_head;
        m_head.prev = &m_head;
    }

    RingBuffer(RingBuffer&& other) noexcept
    : m_alloc(std::move(other.m_alloc)), nAlloc(std::move(other.nAlloc))
    {
        if (other.m_head.next == &other.m_head) {
            m_head.next = &m_head;
            m_head.prev = &m_head;
        } else {
            m_head.next = other.m_head.next;
            m_head.prev = other.m_head.prev;
            m_head.next->prev = &m_head;
            m_head.prev->next = &m_head;

            other.m_head.next = &other.m_head;
            other.m_head.prev = &other.m_head;
        }
    }

    RingIterator begin() { return RingIterator(&m_head, m_head.next); }
    RingIterator end()   { return RingIterator(&m_head, &m_head); }

    ~RingBuffer()
    {
        for (auto n = m_head.next; n && n != &m_head; )
        {
            auto* temp = n->next;
            Node* realNode = static_cast<Node*>(n);
            Traits::destroy(nAlloc, realNode);
            Traits::deallocate(nAlloc, realNode, 1);
            n = temp;
        }
    }
};

constexpr std::size_t ModInverse(std::size_t a, std::size_t m)
{
    a = a % m;
    for (std::size_t x = 1; x < m; ++x)
    {
        if ((a * x) % m == 1)
            return x;
    }
    return 0;
}

struct AlfabetRing
{
    static RingBuffer<char, CryptoAllocator<char>> MakeAlphabetRing(char start, char end)
    {
        RingBuffer<char, CryptoAllocator<char>> ring;
        for (char c = start; c <= end; ++c)
        {
            ring.AddValue(c);
        }
        return ring;
    }

    static RingBuffer<char, CryptoAllocator<char>> upperCase;
    static RingBuffer<char, CryptoAllocator<char>> lowerCase;
};

RingBuffer<char, CryptoAllocator<char>> AlfabetRing::upperCase = MakeAlphabetRing('A', 'Z');
RingBuffer<char, CryptoAllocator<char>> AlfabetRing::lowerCase = MakeAlphabetRing('a', 'z');

struct CesarCiphers
{
    template<typename T>
        requires IsContainer<T>
    static AsciiCryptoString CipherData(AlfabetRing& ring, const T& plaintext, std::size_t key)
    {
        AsciiCryptoString result;
        std::size_t shift = key % 26;

        for (const auto& c : plaintext)
        {
            RingBuffer<char, CryptoAllocator<char>>* to_search = nullptr;
            char sub{0};

            if (std::isupper(static_cast<unsigned char>(c)))
            {
                to_search = &ring.upperCase;
                sub = 'A';
            }
            else if (std::islower(static_cast<unsigned char>(c)))
            {
                to_search = &ring.lowerCase;
                sub = 'a';
            }
            else
            {
                result += c;
                continue;
            }

            auto it = to_search->begin();
            std::advance(it, (c - sub) + shift);
            result += *it;
        }

        return result;
    }

    template<typename T>
        requires IsContainer<T>
    static AsciiCryptoString DecipherData(AlfabetRing& ring, const T& ciphertext, std::size_t key)
    {
        return CipherData(ring, ciphertext, 26 - (key % 26));
    }
};

struct AffineCiphers
{
    template<typename T>
        requires IsContainer<T>
    static AsciiCryptoString CipherData(AlfabetRing& ring, const T& plaintext, std::size_t a, std::size_t b)
    {
        assert(std::gcd(a, 26) == 1 && "'a' must be coprime to 26!");
        AsciiCryptoString result;

        for (const auto& c : plaintext)
        {
            RingBuffer<char, CryptoAllocator<char>>* to_search = nullptr;
            char sub{0};

            if (std::isupper(static_cast<unsigned char>(c)))
            {
                to_search = &ring.upperCase;
                sub = 'A';
            }
            else if (std::islower(static_cast<unsigned char>(c)))
            {
                to_search = &ring.lowerCase;
                sub = 'a';
            }
            else
            {
                result += c;
                continue;
            }

            std::size_t x = c - sub;
            std::size_t targetIndex = (a * x + b) % 26;

            auto it = to_search->begin();
            std::advance(it, targetIndex);
            result += *it;
        }

        return result;
    }

    template<typename T>
        requires IsContainer<T>
    static AsciiCryptoString DecipherData(AlfabetRing& ring, const T& ciphertext, std::size_t a, std::size_t b)
    {
        assert(std::gcd(a, 26) == 1 && "'a' must be coprime to 26!");
        std::size_t a_inv = ModInverse(a, 26);
        assert(a_inv != 0 && "Modular inverse does not exist!");

        AsciiCryptoString result;

        for (const auto& c : ciphertext)
        {
            RingBuffer<char, CryptoAllocator<char>>* to_search = nullptr;
            char sub{0};

            if (std::isupper(static_cast<unsigned char>(c)))
            {
                to_search = &ring.upperCase;
                sub = 'A';
            }
            else if (std::islower(static_cast<unsigned char>(c)))
            {
                to_search = &ring.lowerCase;
                sub = 'a';
            }
            else
            {
                result += c;
                continue;
            }

            std::size_t y = c - sub;
            // D(y) = a^-1 * (y - b) mod 26 using positive modular arithmetic
            std::size_t targetIndex = (a_inv * (y + 26 - (b % 26))) % 26;

            auto it = to_search->begin();
            std::advance(it, targetIndex);
            result += *it;
        }

        return result;
    }
};

static AlfabetRing ring;

int main(int argc, char* argv[])
{
    std::string secretMessage = "Hello, World! RingBuffer Shift 13.";

    std::size_t shiftKey = 13;
    {
        std::cout << "--- Caesar Cipher ---\n";
        std::cout << "Original:  " << secretMessage << "\n";

        AsciiCryptoString cesar_enc = CesarCiphers::CipherData(ring, secretMessage, shiftKey);
        std::cout << "Encrypted: " << cesar_enc << "\n";

        AsciiCryptoString cesar_dec = CesarCiphers::DecipherData(ring, cesar_enc, shiftKey);
        std::cout << "Decrypted: " << cesar_dec << "\n\n";
    }
    std::size_t a = 5;
    std::size_t b = 8;

    std::cout << "--- Affine Cipher (a=" << a << ", b=" << b << ") ---\n";
    std::cout << "Original:  " << secretMessage << "\n";

    AsciiCryptoString affine_enc = AffineCiphers::CipherData(ring, secretMessage, a, b);
    std::cout << "Encrypted: " << affine_enc << "\n";

    AsciiCryptoString affine_dec = AffineCiphers::DecipherData(ring, affine_enc, a, b);
    std::cout << "Decrypted: " << affine_dec << "\n";

    return 0;
}
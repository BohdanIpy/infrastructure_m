// #include <gtest/gtest.h>
//
// #include <streambuf>
// #include <string>
//
//
// #include <openssl/bio.h>
//
// using namespace std::literals;
//
// import SecurityCoreLibrary;
// using security_core::common_types::UniqueBIOPtr;
// using security_core::common_types::BioFree;
// using security_core::biow::BioLink;
// using security_core::biow::BioChain;
//
// class BIOMemoryFixture : public ::testing::Test
// {
// protected:
//     UniqueBIOPtr m_chain;
//
//     static std::string data; // "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()_+-=[]{}|;:',.<>/?";
//
//     BIOMemoryFixture() : m_chain{nullptr, BioFree} {}
//
//     void SetUp() override
//     {
//         UniqueBIOPtr uniqueBIOPtr{BIO_new(BIO_s_mem()), BioFree};
//         BioLink link{uniqueBIOPtr.release(), [](BIO *bio)
//         {
//             std::size_t write_bytes;
//             int err = BIO_write_ex(bio, data.c_str(), data.size(), &write_bytes);
//             if (err <= 0 || write_bytes != data.size())
//             {
//                 std::abort();
//             }
//         }};
//         m_chain = BioChain{}.AddBioPtr(std::move(link)).BuildChain();
//     }
//
//     void TearDown() override
//     {
//         m_chain.reset();
//     }
// };
// std::string BIOMemoryFixture::data = "ABCD EFGH IJKLM NOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()_+-=[]{}|;:',.<>/?";
//
// class BIOMemoryFixtureWide : public ::testing::Test
// {
// protected:
//     UniqueBIOPtr m_chain;
//
//     static std::wstring data; // L"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
//
//     BIOMemoryFixtureWide() : m_chain{nullptr, BioFree} {}
//
//     void SetUp() override
//     {
//         UniqueBIOPtr uniqueBIOPtr{BIO_new(BIO_s_mem()), BioFree};
//         BioLink link{uniqueBIOPtr.release(), [](BIO *bio)
//         {
//             std::size_t write_bytes;
//             std::size_t wr = data.size() * sizeof(wchar_t);
//             int err = BIO_write_ex(bio, data.c_str(), wr, &write_bytes);
//             if (err <= 0 || write_bytes != wr)
//             {
//                 std::abort();
//             }
//         }};
//         m_chain = BioChain{}.AddBioPtr(std::move(link)).BuildChain();
//     }
//
//     void TearDown() override
//     {
//         m_chain.reset();
//     }
// };
// std::wstring BIOMemoryFixtureWide::data = L"A B CDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
//
//
// TEST_F(BIOMemoryFixture, partialRead)
// {
//     security_core::stream_biow::basic_input_bio_stream<char> in{std::move(m_chain), 15};
//     std::istream stream{&in};
//
//     ASSERT_TRUE(stream.good()) << "The fail bit or other not good bit is specified";
//     ASSERT_TRUE(stream.tellg() == 0) << "The starting position is not on [0] char";
//
//     std::string part1, part2;
//     stream >> part1 >> part2;
//     ASSERT_STREQ(part1.c_str(), "ABCD");
//     ASSERT_STREQ(part2.c_str(), "EFGH");
//     std::string part3;
//     part3.resize(5);
//
//     // get the space
//     stream.get();
//
//     stream.readsome(part3.data(), part3.size());
//     ASSERT_STREQ(part3.c_str(), "IJKLM");
//
//     ASSERT_TRUE(stream.good());
//     ASSERT_FALSE(stream.eof());
//     ASSERT_FALSE(stream.fail());
// }
//
// TEST_F(BIOMemoryFixture, fullRead)
// {
//     security_core::stream_biow::basic_input_bio_stream<char> in{std::move(m_chain), 15};
//     std::istream stream{&in};
//     std::noskipws(stream);
//     std::string read_data{std::istream_iterator<char>{stream}, std::istream_iterator<char>{} };
//     ASSERT_EQ(read_data, data);
//
//     ASSERT_FALSE(stream.good());
//     ASSERT_TRUE(stream.eof());
//     ASSERT_TRUE(stream.fail());
// }
//
// TEST_F(BIOMemoryFixture, seekoff)
// {
//     security_core::stream_biow::basic_input_bio_stream<char> in{std::move(m_chain), 15};
//     std::istream stream{&in};
//
//     EXPECT_EQ(stream.tellg(), 0);
//     EXPECT_EQ(stream.get(), 'A');
//     EXPECT_EQ(stream.tellg(), 1);
//
//     stream.seekg(4, std::ios_base::cur);
//     EXPECT_TRUE(stream.good());
//     EXPECT_EQ(stream.tellg(), 5);
//     EXPECT_EQ(stream.get(), 'E');
//
//     stream.seekg(-5, std::ios_base::end);
//     EXPECT_TRUE(stream.good());
//     //EXPECT_EQ(stream.tellg(), 5);
//     std::string msg = std::to_string( data.length());
//     std::cout << msg << std::endl;
//     char c = std::char_traits<char>::to_char_type(stream.get());
//     EXPECT_EQ(c, '.');
// }
//
// TEST_F(BIOMemoryFixtureWide, partialRead)
// {
//     security_core::stream_biow::basic_input_bio_stream<wchar_t> in{std::move(m_chain), 15};
//     std::wistream stream{&in};
//
//     ASSERT_TRUE(stream.good()) << "The fail bit or other not good bit is specified";
//     ASSERT_TRUE(stream.tellg() == 0) << "The starting position is not on [0] char";
//
//     std::wstring part1, part2;
//     stream >> part1 >> part2;
//     ASSERT_STREQ(part1.c_str(), L"A");
//     ASSERT_STREQ(part2.c_str(), L"B");
//     std::wstring part3;
//     part3.resize(5);
//
//     // get the space
//     stream.get();
//
//     stream.readsome(part3.data(), part3.size());
//     ASSERT_STREQ(part3.c_str(), L"CDEFG");
//
//     ASSERT_TRUE(stream.good());
//     ASSERT_FALSE(stream.eof());
//     ASSERT_FALSE(stream.fail());
//
// }
//
// TEST_F(BIOMemoryFixtureWide, fullRead)
// {
//     security_core::stream_biow::basic_input_bio_stream<wchar_t> in{std::move(m_chain), 15};
//     std::wistream stream{&in};
//     std::noskipws(stream);
//
//     std::wstring read_data{
//         std::istreambuf_iterator<wchar_t>{stream},
//         std::istreambuf_iterator<wchar_t>{}
//     };
//
//     auto ch = stream.get();
//
//     ASSERT_EQ(read_data, data);
//
//     ASSERT_FALSE(stream.good());
//     ASSERT_TRUE(stream.eof());
//     ASSERT_TRUE(stream.fail());
// }
//
// TEST_F(BIOMemoryFixtureWide, seekpos)
// {
//     security_core::stream_biow::basic_input_bio_stream<wchar_t> in{std::move(m_chain), 15};
//     std::wistream stream{&in};
//
//     EXPECT_EQ(stream.tellg(), 0);
//     EXPECT_EQ(stream.get(), L'A');
//     EXPECT_EQ(stream.tellg(), 1);
//
//     stream.seekg(4, std::ios_base::cur);
//     EXPECT_TRUE(stream.good());
//     EXPECT_EQ(stream.tellg(), 5);
//     EXPECT_EQ(stream.get(), L'D');
//
//     stream.seekg(-5, std::ios_base::end);
//     EXPECT_TRUE(stream.good());
//
//     wchar_t c = std::char_traits<wchar_t>::to_char_type(stream.get());
//     EXPECT_EQ(c, L'5');
// }

#include <gtest/gtest.h>

#include <iostream>
#include <streambuf>
#include <string>

#include <openssl/bio.h>

using namespace std::literals;

import SecurityCoreLibrary;
using security_core::common_types::BioFree;
using security_core::common_types::UniqueBIOPtr;
using security_core::biow::BioChain;
using security_core::biow::BioLink;

// ============================================================================
// NARROW STREAM FIXTURE & TESTS (char)
// ============================================================================

class BIOMemoryFixture : public ::testing::Test
{
protected:
    UniqueBIOPtr m_chain;

    static std::string data;

    BIOMemoryFixture() : m_chain{nullptr, BioFree} {}

    void SetUp() override
    {
        UniqueBIOPtr uniqueBIOPtr{BIO_new(BIO_s_mem()), BioFree};
        BioLink link{uniqueBIOPtr.release(), [](BIO *bio)
        {
            std::size_t write_bytes;
            int err = BIO_write_ex(bio, data.c_str(), data.size(), &write_bytes);
            if (err <= 0 || write_bytes != data.size())
            {
                std::abort();
            }
        }};
        m_chain = BioChain{}.AddBioPtr(std::move(link)).BuildChain();
    }

    void TearDown() override
    {
        m_chain.reset();
    }
};

std::string BIOMemoryFixture::data =
    "ABCD EFGH IJKLM NOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()_+-=[]{}|;:',.<>/?";

TEST_F(BIOMemoryFixture, partialRead)
{
    security_core::stream_biow::basic_input_bio_stream<char> in{std::move(m_chain), 15};
    std::istream stream{&in};

    ASSERT_TRUE(stream.good()) << "The fail bit or other not good bit is specified";
    ASSERT_TRUE(stream.tellg() == 0) << "The starting position is not on [0] char";

    std::string part1, part2;
    stream >> part1 >> part2;
    ASSERT_STREQ(part1.c_str(), "ABCD");
    ASSERT_STREQ(part2.c_str(), "EFGH");

    std::string part3;
    part3.resize(5);

    // Consume the delimiter space
    stream.get();

    stream.readsome(part3.data(), part3.size());
    ASSERT_STREQ(part3.c_str(), "IJKLM");

    ASSERT_TRUE(stream.good());
    ASSERT_FALSE(stream.eof());
    ASSERT_FALSE(stream.fail());
}

TEST_F(BIOMemoryFixture, fullRead)
{
    security_core::stream_biow::basic_input_bio_stream<char> in{std::move(m_chain), 15};
    std::istream stream{&in};
    std::noskipws(stream);

    std::string read_data{
        std::istream_iterator<char>{stream},
        std::istream_iterator<char>{}
    };
    ASSERT_EQ(read_data, data);

    ASSERT_FALSE(stream.good());
    ASSERT_TRUE(stream.eof());
    ASSERT_TRUE(stream.fail());
}

TEST_F(BIOMemoryFixture, seekoff)
{
    security_core::stream_biow::basic_input_bio_stream<char> in{std::move(m_chain), 15};
    std::istream stream{&in};

    EXPECT_EQ(stream.tellg(), 0);
    EXPECT_EQ(stream.get(), 'A');
    EXPECT_EQ(stream.tellg(), 1);

    stream.seekg(4, std::ios_base::cur);
    EXPECT_TRUE(stream.good());
    EXPECT_EQ(stream.tellg(), 5);
    EXPECT_EQ(stream.get(), 'E');

    stream.seekg(-5, std::ios_base::end);
    EXPECT_TRUE(stream.good());

    char c = std::char_traits<char>::to_char_type(stream.get());
    EXPECT_EQ(c, '.');
}

// ============================================================================
// WIDE STREAM FIXTURE & TESTS (wchar_t)
// ============================================================================

class BIOMemoryFixtureWide : public ::testing::Test
{
protected:
    UniqueBIOPtr m_chain;

    static std::wstring data;

    BIOMemoryFixtureWide() : m_chain{nullptr, BioFree} {}

    void SetUp() override
    {
        UniqueBIOPtr uniqueBIOPtr{BIO_new(BIO_s_mem()), BioFree};
        BioLink link{uniqueBIOPtr.release(), [](BIO *bio)
        {
            std::size_t write_bytes = 0;
            std::size_t total_bytes = data.size() * sizeof(wchar_t);
            int err = BIO_write_ex(bio, data.c_str(), total_bytes, &write_bytes);
            if (err <= 0 || write_bytes != total_bytes)
            {
                std::abort();
            }
        }};
        m_chain = BioChain{}.AddBioPtr(std::move(link)).BuildChain();
    }

    void TearDown() override
    {
        m_chain.reset();
    }
};

std::wstring BIOMemoryFixtureWide::data = L"A B CDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

TEST_F(BIOMemoryFixtureWide, partialRead)
{
    security_core::stream_biow::basic_input_bio_stream<wchar_t> in{std::move(m_chain), 15};
    std::wistream stream{&in};

    ASSERT_TRUE(stream.good()) << "The fail bit or other not good bit is specified";
    ASSERT_TRUE(stream.tellg() == 0) << "The starting position is not on [0] char";

    std::wstring part1, part2;
    stream >> part1 >> part2;
    ASSERT_STREQ(part1.c_str(), L"A");
    ASSERT_STREQ(part2.c_str(), L"B");

    std::wstring part3;
    part3.resize(5);

    // Consume the delimiter space
    stream.get();

    stream.readsome(part3.data(), part3.size());
    ASSERT_STREQ(part3.c_str(), L"CDEFG");

    ASSERT_TRUE(stream.good());
    ASSERT_FALSE(stream.eof());
    ASSERT_FALSE(stream.fail());
}

TEST_F(BIOMemoryFixtureWide, fullRead)
{
    security_core::stream_biow::basic_input_bio_stream<wchar_t> in{std::move(m_chain), 15};
    std::wistream stream{&in};
    std::noskipws(stream);

    std::wstring read_data{
        std::istreambuf_iterator<wchar_t>{stream},
        std::istreambuf_iterator<wchar_t>{}
    };

    // Force stream state check to register EOF bit on wistream
    auto ch = stream.get();
    EXPECT_EQ(ch, std::char_traits<wchar_t>::eof());

    ASSERT_EQ(read_data, data);

    ASSERT_FALSE(stream.good());
    ASSERT_TRUE(stream.eof());
    ASSERT_TRUE(stream.fail());
}

TEST_F(BIOMemoryFixtureWide, seekpos)
{
    security_core::stream_biow::basic_input_bio_stream<wchar_t> in{std::move(m_chain), 15};
    std::wistream stream{&in};

    EXPECT_EQ(stream.tellg(), 0);
    EXPECT_EQ(stream.get(), L'A');
    EXPECT_EQ(stream.tellg(), 1);

    stream.seekg(4, std::ios_base::cur);
    EXPECT_TRUE(stream.good());
    EXPECT_EQ(stream.tellg(), 5);
    EXPECT_EQ(stream.get(), L'D');

    stream.seekg(-5, std::ios_base::end);
    EXPECT_TRUE(stream.good());

    wchar_t c = std::char_traits<wchar_t>::to_char_type(stream.get());
    EXPECT_EQ(c, L'5');
}
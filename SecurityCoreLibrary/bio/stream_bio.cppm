module;

#include <string>
#include <vector>
#include <array>

#include <openssl/bio.h>

export module SecurityCoreLibrary:StreamBIOw;

import :CommonTypes;
import :BIOw;

using security_core::common_types::UniqueBIOPtr;
using security_core::biow::BioSeekability;
using security_core::biow::BioMode;

export namespace security_core::stream_biow
{
    template<typename CharT, typename TraitsType = std::char_traits<CharT>>
    class basic_input_bio_stream : public std::basic_streambuf<CharT, TraitsType>
    {
    public:
        using traits_type = TraitsType;
        using int_type    = typename traits_type::int_type;
        using char_type   = CharT;
        using pos_type    = typename traits_type::pos_type;
        using off_type    = typename traits_type::off_type;

    private:
        UniqueBIOPtr m_bio;
        BioSeekability m_mode;
        // size in the CharT(e.g. can be 12 wchar_t, so the total buffer would be sizeof(wchar_t) * bufferSize)
        std::size_t m_bufferSize;
        std::vector<char_type> m_buffer;

        // @brief:  in sizeof(CharT)
        pos_type GetAbsolutePosition() const noexcept {
            if (!m_bio) return pos_type(off_type(-1));

            long bio_raw_bytes = BIO_tell(m_bio.get());
            if (bio_raw_bytes < 0) return pos_type(off_type(-1));

            // Convert raw BIO bytes to CharT element count
            off_type bio_element_pos = static_cast<off_type>(bio_raw_bytes / sizeof(char_type));

            off_type unread_elements = static_cast<off_type>(
                (this->gptr() && this->egptr()) ? (this->egptr() - this->gptr()) : 0
            );

            return pos_type(bio_element_pos - unread_elements);
        }

        // in sizeof(CharT)
        pos_type GetTotalSize() const noexcept
        {
            if (!m_bio) return pos_type(off_type(-1));

            // already read bytes
            long bio_raw_pos = BIO_tell(m_bio.get());
            off_type bio_pos = static_cast<off_type>(bio_raw_pos / sizeof(char_type));

            // remain bytes
            long remained_raw_length = BIO_ctrl(m_bio.get(), BIO_CTRL_INFO, 0, nullptr);
            off_type remained_length = static_cast<off_type>(remained_raw_length / sizeof(char_type));
            return pos_type{bio_pos + remained_length};
        }

        bool EmulateForwardSkip(off_type chars_to_skip) {
            if (chars_to_skip < 0) {
                return false; // Cannot skip backward in forward-only mode
            }
            if (chars_to_skip == 0) {
                return true;
            }

            if (this->gptr() && this->egptr() && this->gptr() < this->egptr()) {
                off_type in_buffer_chars = static_cast<off_type>(this->egptr() - this->gptr());

                if (chars_to_skip <= in_buffer_chars) {
                    this->setg(this->eback(), this->gptr() + chars_to_skip, this->egptr());
                    return true;
                }

                // Exhaust the C++ buffer and reduce remaining skip count
                chars_to_skip -= in_buffer_chars;
                this->setg(nullptr, nullptr, nullptr);
            }

            std::size_t bytes_remaining = static_cast<std::size_t>(chars_to_skip) * sizeof(char_type);

            // Ensure the dummy chunk buffer is element-aligned
            constexpr std::size_t kMaxChunkElements = 4096 / sizeof(char_type);
            constexpr std::size_t kChunkSizeBytes = kMaxChunkElements * sizeof(char_type);

            alignas(char_type) std::array<uint8_t, kChunkSizeBytes> dummy;

            while (bytes_remaining > 0) {
                std::size_t chunk_bytes = std::min(bytes_remaining, dummy.size());
                std::size_t bytes_read = 0;

                int res = BIO_read_ex(m_bio.get(), dummy.data(), chunk_bytes, &bytes_read);

                if (res <= 0 || bytes_read == 0) {
                    return false;
                }

                bytes_remaining -= bytes_read;
            }

            return true;
        }

    public:
        explicit basic_input_bio_stream(UniqueBIOPtr bio, std::size_t buffer_size = 64000)
            : m_bio(std::move(bio)),
              m_bufferSize(buffer_size)
        {
            m_mode = biow::QueryBioSeekability(m_bio.get());
            m_buffer.resize(m_bufferSize);
            this->setg(nullptr, nullptr, nullptr);
        }

        basic_input_bio_stream(const basic_input_bio_stream&) = delete;
        basic_input_bio_stream& operator=(const basic_input_bio_stream&) = delete;

        basic_input_bio_stream(basic_input_bio_stream&&) noexcept = default;
        basic_input_bio_stream& operator=(basic_input_bio_stream&&) noexcept = default;

    protected:

        // in sizeof(CharT)
        std::streamsize showmanyc() override
        {
            if (!m_bio) {
                return -1;
            }

            if (m_mode == BioSeekability::None) {
                return 0;
            }

            std::streamsize cpp_buffered_elements = 0;
            if (this->gptr() && this->egptr() && this->gptr() < this->egptr()) {
                cpp_buffered_elements = static_cast<std::streamsize>(this->egptr() - this->gptr());
            }

            std::size_t bio_pending_bytes = BIO_pending(m_bio.get());
            std::streamsize bio_pending_elements = static_cast<std::streamsize>(
                bio_pending_bytes / sizeof(char_type)
            );

            std::streamsize total_available = cpp_buffered_elements + bio_pending_elements;

            if (total_available == 0) {
                if (BIO_eof(m_bio.get())) {
                    return -1;
                }
                return 0;
            }

            return total_available;
        }

        int_type underflow() override
        {
            if (this->gptr() < this->egptr()) {
                return traits_type::to_int_type(*this->gptr());
            }

            if (!m_bio) return traits_type::eof();

            std::size_t bytes_read = 0;
            std::size_t bytes_to_read = m_bufferSize * sizeof(char_type);

            int err = BIO_read_ex(m_bio.get(), m_buffer.data(), bytes_to_read, &bytes_read);

            std::size_t chars_read = bytes_read / sizeof(char_type);
            if (chars_read > 0) {
                char_type* ptr = m_buffer.data();
                this->setg(ptr, ptr, ptr + chars_read);
                return traits_type::to_int_type(*this->gptr());
            }

            return traits_type::eof();
        }

        std::streamsize xsgetn(char_type *s, std::streamsize n) override
        {
            std::streamsize loaded = 0;

            if (this->gptr() < this->egptr()) {
                std::streamsize available = this->egptr() - this->gptr();
                std::streamsize can_load = std::min(available, n);
                traits_type::copy(s, this->gptr(), can_load);
                this->gbump(static_cast<int>(can_load));
                loaded += can_load;
            }

            std::streamsize left_to_load = n - loaded;
            if (left_to_load == 0) return loaded;

            if (static_cast<std::size_t>(left_to_load) >= m_bufferSize) {
                std::size_t bytes_read = 0;
                std::size_t bytes_to_read = static_cast<std::size_t>(left_to_load) * sizeof(char_type);

                int err = BIO_read_ex(m_bio.get(), s + loaded, bytes_to_read, &bytes_read);
                std::size_t chars_read = bytes_read / sizeof(char_type);

                loaded += static_cast<std::streamsize>(chars_read);
                this->setg(nullptr, nullptr, nullptr);
            }
            else {
                std::size_t bytes_read = 0;
                std::size_t bytes_to_read = m_bufferSize * sizeof(char_type);

                int err = BIO_read_ex(m_bio.get(), m_buffer.data(), bytes_to_read, &bytes_read);
                std::size_t chars_read = bytes_read / sizeof(char_type);

                if (chars_read == 0) return loaded;

                char_type* ptr = m_buffer.data();
                this->setg(ptr, ptr, ptr + chars_read);

                std::streamsize copy_count = std::min(left_to_load, static_cast<std::streamsize>(chars_read));
                traits_type::copy(s + loaded, this->gptr(), copy_count);
                loaded += copy_count;
                this->gbump(static_cast<int>(copy_count));
            }
            return loaded;
        }

        pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which) override
        {
            if (!(which & std::ios_base::in) || !m_bio) {
                return pos_type(off_type(-1));
            }

            if (dir == std::ios_base::cur && this->gptr() != nullptr) {
                char_type* target_gptr = this->gptr() + off;

                if (target_gptr >= this->eback() && target_gptr <= this->egptr()) {
                    this->setg(this->eback(), target_gptr, this->egptr());
                    return GetAbsolutePosition();
                }
            }

            // in sizeof(CharT)
            off_type target_absolute_pos = 0;

            if (dir == std::ios_base::cur) {
                pos_type current_pos = GetAbsolutePosition();
                if (current_pos == pos_type(off_type(-1))) return pos_type(off_type(-1));
                target_absolute_pos = off_type(current_pos) + off;
            } else if (dir == std::ios_base::beg) {
                target_absolute_pos = off;
            } else if (dir == std::ios_base::end) {
                if (m_mode != BioSeekability::Full) {
                    return pos_type(off_type(-1));
                }
                long total_len = GetTotalSize();
                if (total_len < 0) return pos_type(off_type(-1));
                target_absolute_pos = static_cast<off_type>(total_len) + off;
            }

            if (target_absolute_pos < 0) {
                return pos_type(off_type(-1));
            }

            if (m_mode == BioSeekability::Full) {
                if (BIO_seek(m_bio.get(), static_cast<long>(target_absolute_pos) * sizeof(CharT)) >= 0) {
                    this->setg(nullptr, nullptr, nullptr);
                    return pos_type(target_absolute_pos);
                }
            }

            pos_type current_pos = GetAbsolutePosition();
            if (current_pos != pos_type(off_type(-1)) && target_absolute_pos >= off_type(current_pos)) {
                off_type part_to_skip = target_absolute_pos - off_type(current_pos);
                if (EmulateForwardSkip(part_to_skip)) {
                    return GetAbsolutePosition();
                }
            }

            return pos_type(off_type(-1));
        }

        pos_type seekpos(pos_type sp, std::ios_base::openmode which = std::ios_base::in) override
        {
            return seekoff(off_type(sp), std::ios_base::beg, which);
        }
    };
}
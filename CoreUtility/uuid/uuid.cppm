module;

#include <cstdint>
#include <array>
#include <stdexcept>
#include <optional>
#include <string>
#include <random>
#include <format>
#include <cstring>

export module CoreUtility:UUID;

export namespace core_utility::uuid
{
    struct UUID
    {
        std::array<std::uint8_t, 16> data{};

        UUID() = default;

        // Explicit constructor from string view (throws on invalid format)
        explicit UUID(std::string_view str)
        {
            auto result = FromString(str);
            if (!result)
            {
                throw std::invalid_argument("Invalid UUID string format");
            }
            *this = *result;
        }

        [[nodiscard]] static std::optional<UUID> FromString(std::string_view str) noexcept
        {
            // UUID format: "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" (36 chars)
            if (str.size() != 36) return std::nullopt;

            // Check hyphen positions
            if (str[8] != '-' || str[13] != '-' || str[18] != '-' || str[23] != '-')
            {
                return std::nullopt;
            }

            UUID uuid;
            size_t byte_idx = 0;

            auto hex_to_nibble = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };

            for (size_t i = 0; i < str.size(); ++i)
            {
                if (i == 8 || i == 13 || i == 18 || i == 23) continue;

                int high = hex_to_nibble(str[i]);
                int low = hex_to_nibble(str[++i]);

                if (high == -1 || low == -1) return std::nullopt;

                uuid.data[byte_idx++] = static_cast<std::uint8_t>((high << 4) | low);
            }

            return uuid;
        }

        [[nodiscard]] static UUID GenerateV4()
        {
            UUID uuid;

            thread_local static std::mt19937_64 engine([]{
                std::random_device rd;
                return rd();
            }());
            thread_local static std::uniform_int_distribution<std::uint64_t> dist;

            std::uint64_t low = dist(engine);
            std::uint64_t high = dist(engine);

            std::memcpy(uuid.data.data(), &low, 8);
            std::memcpy(uuid.data.data() + 8, &high, 8);

            uuid.data[6] = (uuid.data[6] & 0x0F) | 0x40; // Version 4
            uuid.data[8] = (uuid.data[8] & 0x3F) | 0x80; // Variant 1

            return uuid;
        }

        [[nodiscard]] std::string ToString() const
        {
            return std::format(
                "{:02x}{:02x}{:02x}{:02x}-"
                "{:02x}{:02x}-"
                "{:02x}{:02x}-"
                "{:02x}{:02x}-"
                "{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
                data[0], data[1], data[2], data[3],
                data[4], data[5],
                data[6], data[7],
                data[8], data[9],
                data[10], data[11], data[12], data[13], data[14], data[15]
            );
        }

        auto operator<=>(const UUID&) const = default;
    };
}

template <>
struct std::hash<core_utility::uuid::UUID>
{
    std::size_t operator()(const core_utility::uuid::UUID& uuid) const noexcept
    {
        auto pack64 = [&uuid](size_t offset) -> uint64_t {
            uint64_t val = 0;
            for (size_t i = 0; i < 8; ++i) {
                val |= static_cast<uint64_t>(uuid.data[offset + i]) << (i * 8);
            }
            return val;
        };

        const uint64_t high = pack64(0);
        const uint64_t low = pack64(8);

        const std::size_t h1 = std::hash<uint64_t>{}(high);
        const std::size_t h2 = std::hash<uint64_t>{}(low);

        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

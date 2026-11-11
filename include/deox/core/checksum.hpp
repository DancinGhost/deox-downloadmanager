#pragma once

#include <span>
#include <string>
#include <string_view>
#include <cstdint>
#include <array>

namespace deox::core {

class Sha256 {
public:
    Sha256() noexcept;
    void reset() noexcept;

    void update(std::span<const uint8_t> data) noexcept;
    void update(std::string_view data) noexcept;

    [[nodiscard]] std::string finalize() noexcept;
    [[nodiscard]] std::array<uint8_t, 32> finalize_raw() noexcept;

private:
    void transform(const uint8_t block[64]) noexcept;

    std::array<uint32_t, 8> state_{};
    uint64_t bit_count_{0};
    std::array<uint8_t, 64> buffer_{};
    size_t buffer_len_{0};
};

// Pure function to compute SHA-256 of in-memory data
[[nodiscard]] std::string compute_sha256(std::span<const uint8_t> data) noexcept;
[[nodiscard]] std::string compute_sha256(std::string_view data) noexcept;

// Pure verification function comparing computed hex with expected hex (case-insensitive)
[[nodiscard]] bool verify_sha256(std::string_view computed_hex, std::string_view expected_hex) noexcept;

} // namespace deox::core

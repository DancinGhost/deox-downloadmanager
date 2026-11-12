// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace deoxdm::core {

// Streaming SHA-256 (FIPS 180-4)
class Sha256 {
public:
    Sha256() noexcept;
    void reset() noexcept;

    void update(std::span<const uint8_t> data) noexcept;
    void update(std::string_view data) noexcept;

    // Both return the digest and reset the hasher for reuse.
    [[nodiscard]] std::string finalize() noexcept;
    [[nodiscard]] std::array<uint8_t, 32> finalize_raw() noexcept;

private:
    void transform(const uint8_t block[64]) noexcept;

    std::array<uint32_t, 8> state_{};
    uint64_t bit_count_{0};
    std::array<uint8_t, 64> buffer_{};
    size_t buffer_len_{0};
};

// SHA-256 of in-memory data as 64 lowercase hex characters
[[nodiscard]] std::string compute_sha256(std::span<const uint8_t> data) noexcept;
[[nodiscard]] std::string compute_sha256(std::string_view data) noexcept;

// True if the text (ignoring surrounding whitespace) is 64 hex characters
[[nodiscard]] bool is_valid_sha256_hex(std::string_view hex) noexcept;

// Compares two SHA-256 hex digests, ignoring case and surrounding whitespace
[[nodiscard]] bool verify_sha256(std::string_view computed_hex, std::string_view expected_hex) noexcept;

} // namespace deoxdm::core

// SPDX-License-Identifier: MIT
#include "deoxdm/core/checksum.hpp"
#include <algorithm>
#include <cstring>

namespace deoxdm::core {

namespace {

[[nodiscard]] constexpr uint32_t rotr(uint32_t x, uint32_t n) noexcept {
    return (x >> n) | (x << (32 - n));
}

[[nodiscard]] constexpr uint32_t ch(uint32_t x, uint32_t y, uint32_t z) noexcept {
    return (x & y) ^ (~x & z);
}

[[nodiscard]] constexpr uint32_t maj(uint32_t x, uint32_t y, uint32_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
}

[[nodiscard]] constexpr uint32_t Sigma0(uint32_t x) noexcept {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

[[nodiscard]] constexpr uint32_t Sigma1(uint32_t x) noexcept {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

[[nodiscard]] constexpr uint32_t sigma0(uint32_t x) noexcept {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

[[nodiscard]] constexpr uint32_t sigma1(uint32_t x) noexcept {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

constexpr std::array<uint32_t, 64> K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

} // namespace

Sha256::Sha256() noexcept {
    reset();
}

void Sha256::reset() noexcept {
    state_[0] = 0x6a09e667;
    state_[1] = 0xbb67ae85;
    state_[2] = 0x3c6ef372;
    state_[3] = 0xa54ff53a;
    state_[4] = 0x510e527f;
    state_[5] = 0x9b05688c;
    state_[6] = 0x1f83d9ab;
    state_[7] = 0x5be0cd19;
    bit_count_ = 0;
    buffer_len_ = 0;
    buffer_.fill(0);
}

void Sha256::transform(const uint8_t block[64]) noexcept {
    std::array<uint32_t, 64> w{};

    for (size_t i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               (static_cast<uint32_t>(block[i * 4 + 3]));
    }

    for (size_t i = 16; i < 64; ++i) {
        w[i] = sigma1(w[i - 2]) + w[i - 7] + sigma0(w[i - 15]) + w[i - 16];
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];

    for (size_t i = 0; i < 64; ++i) {
        const uint32_t t1 = h + Sigma1(e) + ch(e, f, g) + K[i] + w[i];
        const uint32_t t2 = Sigma0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(std::span<const uint8_t> data) noexcept {
    if (data.empty()) {
        return;
    }

    size_t offset = 0;
    const size_t len = data.size();

    bit_count_ += static_cast<uint64_t>(len) * 8;

    if (buffer_len_ > 0) {
        const size_t needed = 64 - buffer_len_;
        const size_t to_copy = std::min(len, needed);
        std::memcpy(buffer_.data() + buffer_len_, data.data(), to_copy);
        buffer_len_ += to_copy;
        offset += to_copy;

        if (buffer_len_ == 64) {
            transform(buffer_.data());
            buffer_len_ = 0;
        }
    }

    while (offset + 64 <= len) {
        transform(data.data() + offset);
        offset += 64;
    }

    if (offset < len) {
        const size_t rem = len - offset;
        std::memcpy(buffer_.data(), data.data() + offset, rem);
        buffer_len_ = rem;
    }
}

void Sha256::update(std::string_view data) noexcept {
    update(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data.data()), data.size()));
}

std::array<uint8_t, 32> Sha256::finalize_raw() noexcept {
    // Pad message
    buffer_[buffer_len_++] = 0x80;

    if (buffer_len_ > 56) {
        std::memset(buffer_.data() + buffer_len_, 0, 64 - buffer_len_);
        transform(buffer_.data());
        buffer_len_ = 0;
    }

    std::memset(buffer_.data() + buffer_len_, 0, 56 - buffer_len_);

    // Append 64-bit length in big-endian
    for (size_t i = 0; i < 8; ++i) {
        buffer_[56 + i] = static_cast<uint8_t>((bit_count_ >> ((7 - i) * 8)) & 0xFF);
    }

    transform(buffer_.data());

    std::array<uint8_t, 32> hash{};
    for (size_t i = 0; i < 8; ++i) {
        hash[i * 4 + 0] = static_cast<uint8_t>((state_[i] >> 24) & 0xFF);
        hash[i * 4 + 1] = static_cast<uint8_t>((state_[i] >> 16) & 0xFF);
        hash[i * 4 + 2] = static_cast<uint8_t>((state_[i] >> 8) & 0xFF);
        hash[i * 4 + 3] = static_cast<uint8_t>(state_[i] & 0xFF);
    }

    reset();
    return hash;
}

std::string Sha256::finalize() noexcept {
    constexpr std::string_view digits = "0123456789abcdef";
    const auto raw = finalize_raw();
    std::string hex;
    hex.reserve(64);
    for (const uint8_t b : raw) {
        hex.push_back(digits[b >> 4]);
        hex.push_back(digits[b & 0x0F]);
    }
    return hex;
}

std::string compute_sha256(std::span<const uint8_t> data) noexcept {
    Sha256 hasher;
    hasher.update(data);
    return hasher.finalize();
}

std::string compute_sha256(std::string_view data) noexcept {
    Sha256 hasher;
    hasher.update(data);
    return hasher.finalize();
}

namespace {

constexpr std::string_view trim_hex(std::string_view s) noexcept {
    constexpr auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return s;
}

constexpr bool is_hex_digit(char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

constexpr char lower_hex(char c) noexcept {
    return (c >= 'A' && c <= 'F') ? static_cast<char>(c - 'A' + 'a') : c;
}

} // namespace

bool is_valid_sha256_hex(std::string_view hex) noexcept {
    hex = trim_hex(hex);
    return hex.size() == 64 && std::ranges::all_of(hex, is_hex_digit);
}

bool verify_sha256(std::string_view computed_hex, std::string_view expected_hex) noexcept {
    computed_hex = trim_hex(computed_hex);
    expected_hex = trim_hex(expected_hex);

    if (!is_valid_sha256_hex(computed_hex) || !is_valid_sha256_hex(expected_hex)) {
        return false;
    }

    return std::ranges::equal(computed_hex, expected_hex, [](char a, char b) {
        return lower_hex(a) == lower_hex(b);
    });
}

} // namespace deoxdm::core

/*
  hashlib.cpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

     * Neither the name of the copyright holder nor the names of its
       contributors may be used to endorse or promote products derived from
       this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  Its BLAKE2b and BLAKE2s implementation is derived from Markku-Juhani O. Saarinen's BLAKE2 reference implementation
  (RFC 7693, Appendix C; https://github.com/mjosaarinen/blake2_mjosref), dedicated to the public domain under CC0 1.0
  Universal. Changes: translated to C++ (a template for both variants), with BLAKE2's salt and personalization added
  (see runtime/python/NOTICE).
*/

// The module hashlib: MD5, SHA-1, SHA-2, SHA-3 (and SHAKE), BLAKE2b and BLAKE2s (their reference algorithms), and pbkdf2_hmac

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            std::uint32_t rotl32(std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
            std::uint32_t rotr32(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
            std::uint64_t rotl64(std::uint64_t x, int n) { return n == 0 ? x : (x << n) | (x >> (64 - n)); }
            std::uint64_t rotr64(std::uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

            // a hash algorithm's state
            struct hasher
            {
                virtual ~hasher() = default;
                virtual std::unique_ptr<hasher> clone() const = 0;
                virtual void update(unsigned char const* aData, std::size_t aSize) = 0;
                virtual std::string digest(std::size_t aLength) const = 0;    // (aLength: SHAKE's)
                virtual std::size_t digest_size() const = 0;
                virtual std::size_t block_size() const = 0;
            };

            // (a Merkle-Damgard hash: MD5, SHA-1 and SHA-2: its block function)
            template <typename Derived, std::size_t BlockSize, std::size_t LengthBytes, bool BigEndian>
            struct block_hasher : hasher
            {
                std::array<unsigned char, BlockSize> buffer{};
                std::size_t buffered = 0u;
                std::uint64_t length = 0u;     ///< (bytes)
                void update(unsigned char const* aData, std::size_t aSize) override
                {
                    length += aSize;
                    while (aSize > 0u)
                    {
                        auto const n = std::min(aSize, BlockSize - buffered);
                        std::memcpy(buffer.data() + buffered, aData, n);
                        buffered += n;
                        aData += n;
                        aSize -= n;
                        if (buffered == BlockSize)
                        {
                            static_cast<Derived*>(this)->block(buffer.data());
                            buffered = 0u;
                        }
                    }
                }
                Derived finished() const
                {
                    Derived copy = *static_cast<Derived const*>(this);
                    auto const bits = length * 8u;
                    unsigned char const one = 0x80u;
                    copy.update(&one, 1u);
                    unsigned char const zero = 0u;
                    while (copy.buffered != BlockSize - LengthBytes)
                        copy.update(&zero, 1u);
                    unsigned char lengthBytes[LengthBytes]{};
                    for (std::size_t i = 0u; i < 8u; ++i)
                    {
                        auto const byte = static_cast<unsigned char>(bits >> (8u * i));
                        if (BigEndian)
                            lengthBytes[LengthBytes - 1u - i] = byte;
                        else
                            lengthBytes[i] = byte;
                    }
                    copy.update(lengthBytes, LengthBytes);
                    return copy;
                }
                std::size_t block_size() const override { return BlockSize; }
            };

            struct md5 : block_hasher<md5, 64u, 8u, false>
            {
                std::array<std::uint32_t, 4> h{ 0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u };
                void block(unsigned char const* p)
                {
                    static std::uint32_t const k[64] = {
                        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
                        0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
                        0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942,
                        0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
                        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
                        0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
                    static int const r[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
                    std::uint32_t w[16];
                    for (int i = 0; i < 16; ++i)
                        w[i] = static_cast<std::uint32_t>(p[4 * i]) | (static_cast<std::uint32_t>(p[4 * i + 1]) << 8) | (static_cast<std::uint32_t>(p[4 * i + 2]) << 16) |
                            (static_cast<std::uint32_t>(p[4 * i + 3]) << 24);
                    auto a = h[0], b = h[1], c = h[2], d = h[3];
                    for (int i = 0; i < 64; ++i)
                    {
                        std::uint32_t f;
                        int g;
                        if (i < 16) { f = (b & c) | (~b & d); g = i; }
                        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
                        else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
                        else { f = c ^ (b | ~d); g = (7 * i) % 16; }
                        auto const temp = d;
                        d = c;
                        c = b;
                        b = b + rotl32(a + f + k[i] + w[g], r[i]);
                        a = temp;
                    }
                    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
                }
                std::unique_ptr<hasher> clone() const override { return std::make_unique<md5>(*this); }
                std::string digest(std::size_t) const override
                {
                    auto const f = finished();
                    std::string result;
                    for (auto v : f.h)
                        for (int i = 0; i < 4; ++i)
                            result += static_cast<char>(v >> (8 * i));
                    return result;
                }
                std::size_t digest_size() const override { return 16u; }
            };

            struct sha1 : block_hasher<sha1, 64u, 8u, true>
            {
                std::array<std::uint32_t, 5> h{ 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
                void block(unsigned char const* p)
                {
                    std::uint32_t w[80];
                    for (int i = 0; i < 16; ++i)
                        w[i] = (static_cast<std::uint32_t>(p[4 * i]) << 24) | (static_cast<std::uint32_t>(p[4 * i + 1]) << 16) |
                            (static_cast<std::uint32_t>(p[4 * i + 2]) << 8) | static_cast<std::uint32_t>(p[4 * i + 3]);
                    for (int i = 16; i < 80; ++i)
                        w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
                    auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
                    for (int i = 0; i < 80; ++i)
                    {
                        std::uint32_t f, k;
                        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
                        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
                        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
                        else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
                        auto const temp = rotl32(a, 5) + f + e + k + w[i];
                        e = d; d = c; c = rotl32(b, 30); b = a; a = temp;
                    }
                    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
                }
                std::unique_ptr<hasher> clone() const override { return std::make_unique<sha1>(*this); }
                std::string digest(std::size_t) const override
                {
                    auto const f = finished();
                    std::string result;
                    for (auto v : f.h)
                        for (int i = 3; i >= 0; --i)
                            result += static_cast<char>(v >> (8 * i));
                    return result;
                }
                std::size_t digest_size() const override { return 20u; }
            };

            struct sha256 : block_hasher<sha256, 64u, 8u, true>
            {
                std::array<std::uint32_t, 8> h;
                std::size_t size;
                explicit sha256(bool a224) : size{ a224 ? 28u : 32u }
                {
                    if (a224)
                        h = { 0xc1059ed8u, 0x367cd507u, 0x3070dd17u, 0xf70e5939u, 0xffc00b31u, 0x68581511u, 0x64f98fa7u, 0xbefa4fa4u };
                    else
                        h = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
                }
                void block(unsigned char const* p)
                {
                    static std::uint32_t const k[64] = {
                        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
                        0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
                        0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
                        0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
                        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
                        0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
                    std::uint32_t w[64];
                    for (int i = 0; i < 16; ++i)
                        w[i] = (static_cast<std::uint32_t>(p[4 * i]) << 24) | (static_cast<std::uint32_t>(p[4 * i + 1]) << 16) |
                            (static_cast<std::uint32_t>(p[4 * i + 2]) << 8) | static_cast<std::uint32_t>(p[4 * i + 3]);
                    for (int i = 16; i < 64; ++i)
                    {
                        auto const s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
                        auto const s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
                        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
                    }
                    auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
                    for (int i = 0; i < 64; ++i)
                    {
                        auto const S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
                        auto const ch = (e & f) ^ (~e & g);
                        auto const t1 = hh + S1 + ch + k[i] + w[i];
                        auto const S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
                        auto const maj = (a & b) ^ (a & c) ^ (b & c);
                        auto const t2 = S0 + maj;
                        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
                    }
                    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
                }
                std::unique_ptr<hasher> clone() const override { return std::make_unique<sha256>(*this); }
                std::string digest(std::size_t) const override
                {
                    auto const f = finished();
                    std::string result;
                    for (auto v : f.h)
                        for (int i = 3; i >= 0; --i)
                            result += static_cast<char>(v >> (8 * i));
                    return result.substr(0u, size);
                }
                std::size_t digest_size() const override { return size; }
            };

            struct sha512 : block_hasher<sha512, 128u, 16u, true>
            {
                std::array<std::uint64_t, 8> h;
                std::size_t size;
                explicit sha512(std::size_t aSize) : size{ aSize }
                {
                    if (aSize == 48u)
                        h = { 0xcbbb9d5dc1059ed8ull, 0x629a292a367cd507ull, 0x9159015a3070dd17ull, 0x152fecd8f70e5939ull, 0x67332667ffc00b31ull,
                            0x8eb44a8768581511ull, 0xdb0c2e0d64f98fa7ull, 0x47b5481dbefa4fa4ull };
                    else
                        h = { 0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull, 0x510e527fade682d1ull,
                            0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull };
                }
                void block(unsigned char const* p)
                {
                    static std::uint64_t const k[80] = {
                        0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull, 0x3956c25bf348b538ull, 0x59f111f1b605d019ull,
                        0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull, 0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
                        0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull, 0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull,
                        0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull, 0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
                        0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull, 0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull,
                        0x06ca6351e003826full, 0x142929670a0e6e70ull, 0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
                        0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull, 0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull,
                        0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull, 0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
                        0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull, 0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull,
                        0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull, 0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
                        0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull, 0xca273eceea26619cull, 0xd186b8c721c0c207ull,
                        0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull, 0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
                        0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull, 0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull,
                        0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull };
                    std::uint64_t w[80];
                    for (int i = 0; i < 16; ++i)
                    {
                        w[i] = 0u;
                        for (int j = 0; j < 8; ++j)
                            w[i] = (w[i] << 8) | p[8 * i + j];
                    }
                    for (int i = 16; i < 80; ++i)
                    {
                        auto const s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
                        auto const s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
                        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
                    }
                    auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
                    for (int i = 0; i < 80; ++i)
                    {
                        auto const S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
                        auto const ch = (e & f) ^ (~e & g);
                        auto const t1 = hh + S1 + ch + k[i] + w[i];
                        auto const S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
                        auto const maj = (a & b) ^ (a & c) ^ (b & c);
                        auto const t2 = S0 + maj;
                        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
                    }
                    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
                }
                std::unique_ptr<hasher> clone() const override { return std::make_unique<sha512>(*this); }
                std::string digest(std::size_t) const override
                {
                    auto const f = finished();
                    std::string result;
                    for (auto v : f.h)
                        for (int i = 7; i >= 0; --i)
                            result += static_cast<char>(v >> (8 * i));
                    return result.substr(0u, size);
                }
                std::size_t digest_size() const override { return size; }
            };

            // (Keccak: SHA-3 and SHAKE)
            struct sha3 : hasher
            {
                std::array<std::uint64_t, 25> state{};
                std::size_t rate;
                std::size_t size;           ///< (0: SHAKE)
                unsigned char suffix;
                std::array<unsigned char, 200> buffer{};
                std::size_t buffered = 0u;
                sha3(std::size_t aRate, std::size_t aSize, unsigned char aSuffix) : rate{ aRate }, size{ aSize }, suffix{ aSuffix } {}
                static void keccak(std::array<std::uint64_t, 25>& a)
                {
                    static std::uint64_t const rc[24] = {
                        0x0000000000000001ull, 0x0000000000008082ull, 0x800000000000808aull, 0x8000000080008000ull, 0x000000000000808bull, 0x0000000080000001ull,
                        0x8000000080008081ull, 0x8000000000008009ull, 0x000000000000008aull, 0x0000000000000088ull, 0x0000000080008009ull, 0x000000008000000aull,
                        0x000000008000808bull, 0x800000000000008bull, 0x8000000000008089ull, 0x8000000000008003ull, 0x8000000000008002ull, 0x8000000000000080ull,
                        0x000000000000800aull, 0x800000008000000aull, 0x8000000080008081ull, 0x8000000000008080ull, 0x0000000080000001ull, 0x8000000080008008ull };
                    static int const rotations[25] = { 0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43, 25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14 };
                    for (int round = 0; round < 24; ++round)
                    {
                        std::uint64_t c[5], d[5];
                        for (int x = 0; x < 5; ++x)
                            c[x] = a[x] ^ a[x + 5] ^ a[x + 10] ^ a[x + 15] ^ a[x + 20];
                        for (int x = 0; x < 5; ++x)
                            d[x] = c[(x + 4) % 5] ^ rotl64(c[(x + 1) % 5], 1);
                        for (int i = 0; i < 25; ++i)
                            a[i] ^= d[i % 5];
                        std::uint64_t b[25];
                        for (int x = 0; x < 5; ++x)
                            for (int y = 0; y < 5; ++y)
                                b[y + 5 * ((2 * x + 3 * y) % 5)] = rotl64(a[x + 5 * y], rotations[x + 5 * y]);
                        for (int x = 0; x < 5; ++x)
                            for (int y = 0; y < 5; ++y)
                                a[x + 5 * y] = b[x + 5 * y] ^ (~b[(x + 1) % 5 + 5 * y] & b[(x + 2) % 5 + 5 * y]);
                        a[0] ^= rc[round];
                    }
                }
                void absorb_block(unsigned char const* p)
                {
                    for (std::size_t i = 0u; i < rate / 8u; ++i)
                    {
                        std::uint64_t lane = 0u;
                        for (int j = 7; j >= 0; --j)
                            lane = (lane << 8) | p[8u * i + static_cast<std::size_t>(j)];
                        state[i] ^= lane;
                    }
                    keccak(state);
                }
                void update(unsigned char const* aData, std::size_t aSize) override
                {
                    while (aSize > 0u)
                    {
                        auto const n = std::min(aSize, rate - buffered);
                        std::memcpy(buffer.data() + buffered, aData, n);
                        buffered += n;
                        aData += n;
                        aSize -= n;
                        if (buffered == rate)
                        {
                            absorb_block(buffer.data());
                            buffered = 0u;
                        }
                    }
                }
                std::unique_ptr<hasher> clone() const override { return std::make_unique<sha3>(*this); }
                std::string digest(std::size_t aLength) const override
                {
                    auto copy = *this;
                    std::fill(copy.buffer.begin() + static_cast<std::ptrdiff_t>(copy.buffered), copy.buffer.begin() + static_cast<std::ptrdiff_t>(copy.rate), 0u);
                    copy.buffer[copy.buffered] ^= suffix;
                    copy.buffer[copy.rate - 1u] ^= 0x80u;
                    copy.absorb_block(copy.buffer.data());
                    auto const length = size != 0u ? size : aLength;
                    std::string result;
                    while (result.size() < length)
                    {
                        for (std::size_t i = 0u; i < copy.rate && result.size() < length; ++i)
                            result += static_cast<char>(copy.state[i / 8u] >> (8u * (i % 8u)));
                        if (result.size() < length)
                            keccak(copy.state);
                    }
                    return result;
                }
                std::size_t digest_size() const override { return size; }
                std::size_t block_size() const override { return rate; }
            };

            // (BLAKE2b (64-bit words) and BLAKE2s (32-bit words))
            template <typename Word, std::size_t Rounds>
            struct blake2 : hasher
            {
                static constexpr std::size_t BlockBytes = 16u * sizeof(Word);
                std::array<Word, 8> h;
                std::array<unsigned char, BlockBytes> buffer{};
                std::size_t buffered = 0u;
                Word t0 = 0u, t1 = 0u;
                std::size_t size;
                std::string key;
                static constexpr std::array<Word, 8> iv()
                {
                    if constexpr (sizeof(Word) == 8u)
                        return { 0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull, 0x510e527fade682d1ull,
                            0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull };
                    else
                        return { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
                }
                blake2(std::size_t aSize, std::string const& aKey, std::string const& aSalt, std::string const& aPerson) : size{ aSize }, key{ aKey }
                {
                    h = iv();
                    h[0] ^= static_cast<Word>(0x01010000u ^ (aKey.size() << 8) ^ aSize);
                    auto const mix = [&](std::size_t aWord, std::string const& aBytes)
                    {
                        for (std::size_t i = 0u; i < aBytes.size(); ++i)
                            h[aWord + i / sizeof(Word)] ^= static_cast<Word>(static_cast<unsigned char>(aBytes[i])) << (8u * (i % sizeof(Word)));
                    };
                    mix(4u, aSalt);
                    mix(6u, aPerson);
                    if (!aKey.empty())
                    {
                        std::string block = aKey;
                        block.resize(BlockBytes, '\0');
                        update(reinterpret_cast<unsigned char const*>(block.data()), block.size());
                    }
                }
                static Word rotr(Word x, int n) { return (x >> n) | (x << (8 * sizeof(Word) - static_cast<std::size_t>(n))); }
                void compress(unsigned char const* p, bool aLast)
                {
                    static unsigned char const sigma[12][16] = {
                        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
                        { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 }, { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
                        { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 }, { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
                        { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 }, { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
                        { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 }, { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
                        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 } };
                    constexpr int r1 = sizeof(Word) == 8u ? 32 : 16, r2 = sizeof(Word) == 8u ? 24 : 12, r3 = sizeof(Word) == 8u ? 16 : 8, r4 = sizeof(Word) == 8u ? 63 : 7;
                    Word m[16];
                    for (std::size_t i = 0u; i < 16u; ++i)
                    {
                        m[i] = 0u;
                        for (std::size_t j = sizeof(Word); j-- > 0u;)
                            m[i] = static_cast<Word>((m[i] << 8) | p[i * sizeof(Word) + j]);
                    }
                    Word v[16];
                    auto const init = iv();
                    for (int i = 0; i < 8; ++i)
                    {
                        v[i] = h[i];
                        v[i + 8] = init[i];
                    }
                    v[12] ^= t0;
                    v[13] ^= t1;
                    if (aLast)
                        v[14] = static_cast<Word>(~v[14]);
                    auto const g = [&](int a, int b, int c, int d, Word x, Word y)
                    {
                        v[a] = v[a] + v[b] + x; v[d] = rotr(v[d] ^ v[a], r1);
                        v[c] = v[c] + v[d]; v[b] = rotr(v[b] ^ v[c], r2);
                        v[a] = v[a] + v[b] + y; v[d] = rotr(v[d] ^ v[a], r3);
                        v[c] = v[c] + v[d]; v[b] = rotr(v[b] ^ v[c], r4);
                    };
                    for (std::size_t r = 0u; r < Rounds; ++r)
                    {
                        auto const* s = sigma[r];
                        g(0, 4, 8, 12, m[s[0]], m[s[1]]); g(1, 5, 9, 13, m[s[2]], m[s[3]]); g(2, 6, 10, 14, m[s[4]], m[s[5]]); g(3, 7, 11, 15, m[s[6]], m[s[7]]);
                        g(0, 5, 10, 15, m[s[8]], m[s[9]]); g(1, 6, 11, 12, m[s[10]], m[s[11]]); g(2, 7, 8, 13, m[s[12]], m[s[13]]); g(3, 4, 9, 14, m[s[14]], m[s[15]]);
                    }
                    for (int i = 0; i < 8; ++i)
                        h[i] ^= v[i] ^ v[i + 8];
                }
                void increment(std::size_t aBytes)
                {
                    t0 = static_cast<Word>(t0 + aBytes);
                    if (t0 < aBytes)
                        ++t1;
                }
                void update(unsigned char const* aData, std::size_t aSize) override
                {
                    while (aSize > 0u)
                    {
                        if (buffered == BlockBytes)
                        {
                            increment(BlockBytes);
                            compress(buffer.data(), false);
                            buffered = 0u;
                        }
                        auto const n = std::min(aSize, BlockBytes - buffered);
                        std::memcpy(buffer.data() + buffered, aData, n);
                        buffered += n;
                        aData += n;
                        aSize -= n;
                    }
                }
                std::unique_ptr<hasher> clone() const override { return std::make_unique<blake2>(*this); }
                std::string digest(std::size_t) const override
                {
                    auto copy = *this;
                    copy.increment(copy.buffered);
                    std::fill(copy.buffer.begin() + static_cast<std::ptrdiff_t>(copy.buffered), copy.buffer.end(), 0u);
                    copy.compress(copy.buffer.data(), true);
                    std::string result;
                    for (auto w : copy.h)
                        for (std::size_t i = 0u; i < sizeof(Word); ++i)
                            result += static_cast<char>(w >> (8u * i));
                    return result.substr(0u, size);
                }
                std::size_t digest_size() const override { return size; }
                std::size_t block_size() const override { return BlockBytes; }
            };

            using blake2b = blake2<std::uint64_t, 12u>;
            using blake2s = blake2<std::uint32_t, 10u>;

            std::unique_ptr<hasher> make_hasher(std::string const& aName)
            {
                if (aName == "md5") return std::make_unique<md5>();
                if (aName == "sha1") return std::make_unique<sha1>();
                if (aName == "sha224") return std::make_unique<sha256>(true);
                if (aName == "sha256") return std::make_unique<sha256>(false);
                if (aName == "sha384") return std::make_unique<sha512>(48u);
                if (aName == "sha512") return std::make_unique<sha512>(64u);
                if (aName == "sha3_224") return std::make_unique<sha3>(144u, 28u, 0x06u);
                if (aName == "sha3_256") return std::make_unique<sha3>(136u, 32u, 0x06u);
                if (aName == "sha3_384") return std::make_unique<sha3>(104u, 48u, 0x06u);
                if (aName == "sha3_512") return std::make_unique<sha3>(72u, 64u, 0x06u);
                if (aName == "shake_128") return std::make_unique<sha3>(168u, 0u, 0x1Fu);
                if (aName == "shake_256") return std::make_unique<sha3>(136u, 0u, 0x1Fu);
                if (aName == "blake2b") return std::make_unique<blake2b>(64u, "", "", "");
                if (aName == "blake2s") return std::make_unique<blake2s>(32u, "", "", "");
                return nullptr;
            }

            // --- hash objects

            struct hash_object
            {
                header h;
                std::string name;
                std::unique_ptr<hasher> state;
            };

            neos_py_type make_hash_type();
            neos_py_type sHashType = make_hash_type();
            hash_object& as_hash(object* aObject) { return *reinterpret_cast<hash_object*>(aObject); }

            std::string hex_of(std::string const& aBytes)
            {
                static char const sDigits[] = "0123456789abcdef";
                std::string result;
                for (unsigned char c : aBytes)
                {
                    result += sDigits[c >> 4];
                    result += sDigits[c & 15u];
                }
                return result;
            }

            bool data_argument(object* aData, std::string_view& aResult)
            {
                if (kind_of(aData) == NEOS_PY_STR)
                {
                    error("TypeError", "Strings must be encoded before hashing");
                    return false;
                }
                if (!bytes_like(aData, aResult))
                {
                    error("TypeError", std::string{ "object supporting the buffer API required" });
                    return false;
                }
                return true;
            }

            bool is_shake(hash_object const& aHash) { return aHash.name.starts_with("shake_"); }

            neos_py_type make_hash_type()
            {
                auto type = make_type("_hashlib.HASH", "HASH", retain_object, release_object<hash_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    return make_str("<" + as_hash(aSelf).name + " _hashlib.HASH object @ " + object_address(aSelf) + ">");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto& hash = as_hash(aSelf);
                    std::string_view const name{ aName };
                    if (name == "name")
                        return make_str(hash.name);
                    if (name == "digest_size")
                        return make_int(static_cast<long long>(hash.state->digest_size()));
                    if (name == "block_size")
                        return make_int(static_cast<long long>(hash.state->block_size()));
                    static char const* const sMethods[] = { "update", "digest", "hexdigest", "copy" };
                    for (std::size_t i = 0u; i < std::size(sMethods); ++i)
                        if (name == sMethods[i])
                            return make_function(sMethods[i], [](neos_py_context*, void* aData, object* aArgs, object* aKwargs) -> object*
                            {
                                auto const& args = as<tuple_object>(aArgs)->items;
                                auto& hash = as_hash(args[0]);
                                std::vector<object*> const rest{ args.begin() + 1, args.end() };
                                switch (reinterpret_cast<std::uintptr_t>(aData))
                                {
                                case 0:
                                    {
                                        std::vector<object*> a;
                                        if (!parse_arguments("update", rest, aKwargs, { "obj" }, 1u, 1u, a))
                                            return nullptr;
                                        std::string_view data;
                                        if (!data_argument(a[0], data))
                                            return nullptr;
                                        hash.state->update(reinterpret_cast<unsigned char const*>(data.data()), data.size());
                                        retain(none());
                                        return none();
                                    }
                                case 1: case 2:
                                    {
                                        std::size_t length = 0u;
                                        if (is_shake(hash))
                                        {
                                            std::vector<object*> a;
                                            if (!parse_arguments(reinterpret_cast<std::uintptr_t>(aData) == 1u ? "digest" : "hexdigest", rest, aKwargs, { "length" }, 1u, 0u, a))
                                                return nullptr;
                                            long long n = 0;
                                            if (!to_index(a[0], n))
                                                return nullptr;
                                            if (n < 0)
                                                return error("ValueError", "negative digest length");
                                            length = static_cast<std::size_t>(n);
                                        }
                                        else if (!rest.empty())
                                            return error("TypeError", std::string{ reinterpret_cast<std::uintptr_t>(aData) == 1u ? "digest" : "hexdigest" } +
                                                "() takes no arguments (" + std::to_string(rest.size()) + " given)");
                                        auto const digest = hash.state->digest(length);
                                        return reinterpret_cast<std::uintptr_t>(aData) == 1u ? make_bytes(digest) : make_str(hex_of(digest));
                                    }
                                default:
                                    {
                                        auto const copy = new hash_object{ { { &sHashType.base } }, hash.name, hash.state->clone() };
                                        return &copy->h.base;
                                    }
                                }
                            }, reinterpret_cast<void*>(i), aSelf);
                    return error("AttributeError", std::string{ "'_hashlib.HASH' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            object* new_hash(std::string const& aName, std::unique_ptr<hasher> aState, object* aData)
            {
                ref result{ &(new hash_object{ { { &sHashType.base } }, aName, std::move(aState) })->h.base };
                if (aData != nullptr)
                {
                    std::string_view data;
                    if (!data_argument(aData, data))
                        return nullptr;
                    as_hash(result.get()).state->update(reinterpret_cast<unsigned char const*>(data.data()), data.size());
                }
                return result.release_ownership();
            }

            struct constructor_entry { char const* name; };
            constructor_entry const sConstructors[] = { { "md5" }, { "sha1" }, { "sha224" }, { "sha256" }, { "sha384" }, { "sha512" }, { "sha3_224" }, { "sha3_256" },
                { "sha3_384" }, { "sha3_512" }, { "shake_128" }, { "shake_256" } };

            object* call_constructor(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& entry = *static_cast<constructor_entry const*>(aData);
                std::vector<object*> a;
                if (!parse_arguments(entry.name, as<tuple_object>(aArgs)->items, aKwargs, { "data", "usedforsecurity", "string" }, 0u, 0u, a))
                    return nullptr;
                return new_hash(entry.name, make_hasher(entry.name), a[0] != nullptr ? a[0] : a[2]);
            }

            object* blake(std::vector<object*> const& aArgs, object* aKwargs, bool aB)
            {
                std::vector<object*> a;
                if (!parse_arguments(aB ? "blake2b" : "blake2s", aArgs, aKwargs, { "data", "digest_size", "key", "salt", "person", "fanout", "depth", "leaf_size",
                    "node_offset", "node_depth", "inner_size", "last_node", "usedforsecurity" }, 0u, 1u, a))
                    return nullptr;
                std::size_t const maxSize = aB ? 64u : 32u;
                long long size = static_cast<long long>(maxSize);
                if (a[1] != nullptr && !to_index(a[1], size))
                    return nullptr;
                if (size < 1 || size > static_cast<long long>(maxSize))
                    return error("ValueError", "digest_size must be between 1 and " + std::to_string(maxSize) + " bytes");
                std::string parts[3];
                for (int i = 0; i < 3; ++i)
                    if (a[2 + i] != nullptr)
                    {
                        std::string_view b;
                        if (!bytes_like(a[2 + i], b))
                            return error("TypeError", "a bytes-like object is required");
                        parts[i] = std::string{ b };
                    }
                if (parts[0].size() > maxSize)
                    return error("ValueError", "maximum key length is " + std::to_string(maxSize) + " bytes");
                if (parts[1].size() > maxSize / 4u)
                    return error("ValueError", "maximum salt length is " + std::to_string(maxSize / 4u) + " bytes");
                if (parts[2].size() > maxSize / 4u)
                    return error("ValueError", "maximum person length is " + std::to_string(maxSize / 4u) + " bytes");
                std::unique_ptr<hasher> state;
                if (aB)
                    state = std::make_unique<blake2b>(static_cast<std::size_t>(size), parts[0], parts[1], parts[2]);
                else
                    state = std::make_unique<blake2s>(static_cast<std::size_t>(size), parts[0], parts[1], parts[2]);
                return new_hash(aB ? "blake2b" : "blake2s", std::move(state), a[0]);
            }

            std::string hmac(std::string const& aName, std::string aKey, std::string const& aMessage)
            {
                auto inner = make_hasher(aName);
                auto const blockSize = inner->block_size();
                if (aKey.size() > blockSize)
                {
                    auto h = make_hasher(aName);
                    h->update(reinterpret_cast<unsigned char const*>(aKey.data()), aKey.size());
                    aKey = h->digest(0u);
                }
                aKey.resize(blockSize, '\0');
                std::string ipad = aKey, opad = aKey;
                for (std::size_t i = 0u; i < blockSize; ++i)
                {
                    ipad[i] = static_cast<char>(ipad[i] ^ 0x36);
                    opad[i] = static_cast<char>(opad[i] ^ 0x5c);
                }
                inner->update(reinterpret_cast<unsigned char const*>(ipad.data()), ipad.size());
                inner->update(reinterpret_cast<unsigned char const*>(aMessage.data()), aMessage.size());
                auto const innerDigest = inner->digest(0u);
                auto outer = make_hasher(aName);
                outer->update(reinterpret_cast<unsigned char const*>(opad.data()), opad.size());
                outer->update(reinterpret_cast<unsigned char const*>(innerDigest.data()), innerDigest.size());
                return outer->digest(0u);
            }

            builtin_entry const sHashlibFunctions[] =
            {
                { "new", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("new", aArgs, aKwargs, { "name", "data", "usedforsecurity", "string" }, 1u, 0u, a))
                            return nullptr;
                        std::string name;
                        if (!str_of(a[0], name))
                            return nullptr;
                        for (auto& c : name)
                            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        if (name == "sha-256") name = "sha256";
                        if (name == "blake2b" || name == "blake2s")
                            return blake(a[1] != nullptr ? std::vector<object*>{ a[1] } : std::vector<object*>{}, nullptr, name == "blake2b");
                        auto state = make_hasher(name);
                        if (!state)
                            return error("ValueError", "unsupported hash type " + name);
                        return new_hash(name, std::move(state), a[1] != nullptr ? a[1] : a[3]);
                    } },
                { "blake2b", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return blake(aArgs, aKwargs, true); } },
                { "blake2s", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return blake(aArgs, aKwargs, false); } },
                { "pbkdf2_hmac", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("pbkdf2_hmac", aArgs, aKwargs, { "hash_name", "password", "salt", "iterations", "dklen" }, 4u, 0u, a))
                            return nullptr;
                        std::string name;
                        std::string_view password, salt;
                        long long iterations = 0, dklen = -1;
                        if (!str_of(a[0], name) || !bytes_like(a[1], password) || !bytes_like(a[2], salt) || !to_index(a[3], iterations))
                            return failed() ? nullptr : error("TypeError", "a bytes-like object is required");
                        if (!make_hasher(name) || name.starts_with("shake"))
                            return error("ValueError", "unsupported hash type " + name);
                        auto const digestSize = make_hasher(name)->digest_size();
                        if (a[4] != nullptr && kind_of(a[4]) != NEOS_PY_NONE && !to_index(a[4], dklen))
                            return nullptr;
                        if (dklen < 0)
                            dklen = static_cast<long long>(digestSize);
                        if (iterations < 1)
                            return error("ValueError", "iteration value must be greater than 0.");
                        if (dklen < 1)
                            return error("ValueError", "key length must be greater than 0.");
                        std::string result;
                        for (std::uint32_t block = 1u; static_cast<long long>(result.size()) < dklen; ++block)
                        {
                            std::string message{ salt };
                            for (int i = 3; i >= 0; --i)
                                message += static_cast<char>(block >> (8 * i));
                            auto u = hmac(name, std::string{ password }, message);
                            auto t = u;
                            for (long long i = 1; i < iterations; ++i)
                            {
                                u = hmac(name, std::string{ password }, u);
                                for (std::size_t j = 0u; j < t.size(); ++j)
                                    t[j] = static_cast<char>(t[j] ^ u[j]);
                            }
                            result += t;
                        }
                        return make_bytes(result.substr(0u, static_cast<std::size_t>(dklen)));
                    } }
            };

            bool hashlib_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& c : sConstructors)
                    aAttributes.emplace_back(c.name, make_function(std::string{ "openssl_" } + c.name, call_constructor, const_cast<constructor_entry*>(&c)));
                for (auto const& f : sHashlibFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                std::vector<object*> names;
                for (auto n : { "blake2b", "blake2s", "md5", "sha1", "sha224", "sha256", "sha384", "sha3_224", "sha3_256", "sha3_384", "sha3_512", "sha512",
                    "shake_128", "shake_256" })
                    names.push_back(make_str(n));
                ref const list{ make_list(std::move(names)) };
                aAttributes.emplace_back("algorithms_guaranteed", b_frozenset({ list.get() }, nullptr));
                aAttributes.emplace_back("algorithms_available", b_frozenset({ list.get() }, nullptr));
                return true;
            }

            inline bool const sHashlib = register_module("hashlib", hashlib_module);
        }
    }
}

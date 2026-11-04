/*
  lib_string.cpp

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
*/

// The C language's runtime: <string.h> (7.26) (and POSIX's strnlen, strtok_r, strcasecmp, strncasecmp), and <ctype.h> (7.4) in the
// "C" locale

#include <cerrno>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include "core.hpp"

namespace neos::c_runtime
{
    namespace
    {
        inline char* p(std::int64_t aAddress) { return at<char>(static_cast<address>(aAddress)); }
        inline std::int64_t a(void const* aPointer) { return static_cast<std::int64_t>(address_of(aPointer)); }

        std::int64_t c_memcpy(std::int64_t aTo, std::int64_t aFrom, std::int64_t aCount)
        {
            if (aCount != 0)
                std::memmove(p(aTo), p(aFrom), static_cast<std::size_t>(aCount));
            return aTo;
        }

        std::int64_t c_memccpy(std::int64_t aTo, std::int64_t aFrom, std::int32_t aCharacter, std::int64_t aCount)
        {
            auto* to = p(aTo);
            auto const* from = p(aFrom);
            for (std::int64_t i = 0; i < aCount; ++i)
            {
                to[i] = from[i];
                if (static_cast<unsigned char>(from[i]) == static_cast<unsigned char>(aCharacter))
                    return aTo + i + 1;
            }
            return 0;
        }

        std::int64_t c_strcpy(std::int64_t aTo, std::int64_t aFrom)
        {
            std::memmove(p(aTo), p(aFrom), std::strlen(p(aFrom)) + 1u);
            return aTo;
        }

        std::int64_t c_strncpy(std::int64_t aTo, std::int64_t aFrom, std::int64_t aCount)
        {
            auto const n = static_cast<std::size_t>(aCount);
            auto const length = strnlen(p(aFrom), n);
            std::memmove(p(aTo), p(aFrom), length);
            if (length < n)
                std::memset(p(aTo) + length, 0, n - length);
            return aTo;
        }

        std::int64_t c_strdup(std::int64_t aString)
        {
            return static_cast<std::int64_t>(copy_string(p(aString)));
        }

        std::int64_t c_strndup(std::int64_t aString, std::int64_t aCount)
        {
            auto const length = strnlen(p(aString), static_cast<std::size_t>(aCount));
            return static_cast<std::int64_t>(copy_string(std::string{ p(aString), length }));
        }

        std::int64_t c_strcat(std::int64_t aTo, std::int64_t aFrom)
        {
            auto const length = std::strlen(p(aTo));
            std::memmove(p(aTo) + length, p(aFrom), std::strlen(p(aFrom)) + 1u);
            return aTo;
        }

        std::int64_t c_strncat(std::int64_t aTo, std::int64_t aFrom, std::int64_t aCount)
        {
            auto const length = std::strlen(p(aTo));
            auto const n = strnlen(p(aFrom), static_cast<std::size_t>(aCount));
            std::memmove(p(aTo) + length, p(aFrom), n);
            p(aTo)[length + n] = '\0';
            return aTo;
        }

        std::int32_t c_memcmp(std::int64_t aA, std::int64_t aB, std::int64_t aCount)
        {
            return aCount == 0 ? 0 : std::memcmp(p(aA), p(aB), static_cast<std::size_t>(aCount));
        }

        std::int32_t c_strcmp(std::int64_t aA, std::int64_t aB) { return std::strcmp(p(aA), p(aB)); }
        std::int32_t c_strncmp(std::int64_t aA, std::int64_t aB, std::int64_t aCount) { return std::strncmp(p(aA), p(aB), static_cast<std::size_t>(aCount)); }

        std::int64_t c_strxfrm(std::int64_t aTo, std::int64_t aFrom, std::int64_t aCount)
        {
            auto const length = std::strlen(p(aFrom));
            if (static_cast<std::size_t>(aCount) > length)
                std::memcpy(p(aTo), p(aFrom), length + 1u);
            return static_cast<std::int64_t>(length);
        }

        std::int64_t c_memchr(std::int64_t aString, std::int32_t aCharacter, std::int64_t aCount)
        {
            if (aCount == 0)
                return 0;
            return a(std::memchr(p(aString), aCharacter, static_cast<std::size_t>(aCount)));
        }

        std::int64_t c_strchr(std::int64_t aString, std::int32_t aCharacter) { return a(std::strchr(p(aString), aCharacter)); }
        std::int64_t c_strrchr(std::int64_t aString, std::int32_t aCharacter) { return a(std::strrchr(p(aString), aCharacter)); }
        std::int64_t c_strcspn(std::int64_t aString, std::int64_t aReject) { return static_cast<std::int64_t>(std::strcspn(p(aString), p(aReject))); }
        std::int64_t c_strspn(std::int64_t aString, std::int64_t aAccept) { return static_cast<std::int64_t>(std::strspn(p(aString), p(aAccept))); }
        std::int64_t c_strpbrk(std::int64_t aString, std::int64_t aAccept) { return a(std::strpbrk(p(aString), p(aAccept))); }
        std::int64_t c_strstr(std::int64_t aString, std::int64_t aFind) { return a(std::strstr(p(aString), p(aFind))); }

        std::int64_t c_strtok_r(std::int64_t aString, std::int64_t aDelimiters, std::int64_t aSave)
        {
            auto& save = *at<std::uint64_t>(static_cast<address>(aSave));
            char* s = aString != 0 ? p(aString) : (save != 0u ? at<char>(save) : nullptr);
            if (s == nullptr)
                return 0;
            s += std::strspn(s, p(aDelimiters));
            if (*s == '\0')
            {
                save = 0u;
                return 0;
            }
            char* end = s + std::strcspn(s, p(aDelimiters));
            if (*end != '\0')
            {
                *end = '\0';
                save = address_of(end + 1);
            }
            else
                save = 0u;
            return a(s);
        }

        std::int64_t c_strtok(std::int64_t aString, std::int64_t aDelimiters)
        {
            static thread_local address sSave = 0u;     // (each thread's)
            if (sSave == 0u)
                sSave = allocate(8u);
            return c_strtok_r(aString, aDelimiters, static_cast<std::int64_t>(sSave));
        }

        std::int64_t c_memset(std::int64_t aTo, std::int32_t aCharacter, std::int64_t aCount)
        {
            if (aCount != 0)
                std::memset(p(aTo), aCharacter, static_cast<std::size_t>(aCount));
            return aTo;
        }

        std::int64_t c_strerror(std::int32_t aError)
        {
            static std::map<int, address> sMessages;
            static std::mutex sMutex;
            std::lock_guard lock{ sMutex };
            auto& message = sMessages[aError];
            if (message == 0u)
                message = copy_string(std::strerror(aError));
            return static_cast<std::int64_t>(message);
        }

        std::int64_t c_strlen(std::int64_t aString) { return static_cast<std::int64_t>(std::strlen(p(aString))); }
        std::int64_t c_strnlen(std::int64_t aString, std::int64_t aCount) { return static_cast<std::int64_t>(strnlen(p(aString), static_cast<std::size_t>(aCount))); }

        int lower(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; }

        std::int32_t c_strcasecmp(std::int64_t aA, std::int64_t aB)
        {
            auto const* x = reinterpret_cast<unsigned char const*>(p(aA));
            auto const* y = reinterpret_cast<unsigned char const*>(p(aB));
            for (;; ++x, ++y)
            {
                int const d = lower(*x) - lower(*y);
                if (d != 0 || *x == 0u)
                    return d;
            }
        }

        std::int32_t c_strncasecmp(std::int64_t aA, std::int64_t aB, std::int64_t aCount)
        {
            auto const* x = reinterpret_cast<unsigned char const*>(p(aA));
            auto const* y = reinterpret_cast<unsigned char const*>(p(aB));
            for (std::int64_t i = 0; i < aCount; ++i, ++x, ++y)
            {
                int const d = lower(*x) - lower(*y);
                if (d != 0 || *x == 0u)
                    return d;
            }
            return 0;
        }

        // --- <ctype.h> ("C" locale: the ASCII characters' classes; EOF and other values are in none)

        enum : std::uint8_t { Upper = 1, Lower = 2, Digit = 4, Space = 8, Punct = 16, Control = 32, Blank = 64, Hex = 128 };

        std::uint8_t classes(std::int32_t c)
        {
            if (c < 0 || c > 127)
                return 0u;
            std::uint8_t r = 0u;
            if (c >= 'A' && c <= 'Z') r |= Upper;
            if (c >= 'a' && c <= 'z') r |= Lower;
            if (c >= '0' && c <= '9') r |= Digit;
            if (c == ' ' || (c >= '\t' && c <= '\r')) r |= Space;
            if (c < 32 || c == 127) r |= Control;
            if (c == ' ' || c == '\t') r |= Blank;
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) r |= Hex;
            if (c > 32 && c < 127 && !((r & (Upper | Lower | Digit)) != 0u)) r |= Punct;
            return r;
        }

        std::int32_t c_isalnum(std::int32_t c) { return (classes(c) & (Upper | Lower | Digit)) != 0u; }
        std::int32_t c_isalpha(std::int32_t c) { return (classes(c) & (Upper | Lower)) != 0u; }
        std::int32_t c_isblank(std::int32_t c) { return (classes(c) & Blank) != 0u; }
        std::int32_t c_iscntrl(std::int32_t c) { return (classes(c) & Control) != 0u; }
        std::int32_t c_isdigit(std::int32_t c) { return (classes(c) & Digit) != 0u; }
        std::int32_t c_isgraph(std::int32_t c) { return c > 32 && c < 127; }
        std::int32_t c_islower(std::int32_t c) { return (classes(c) & Lower) != 0u; }
        std::int32_t c_isprint(std::int32_t c) { return c >= 32 && c < 127; }
        std::int32_t c_ispunct(std::int32_t c) { return (classes(c) & Punct) != 0u; }
        std::int32_t c_isspace(std::int32_t c) { return (classes(c) & Space) != 0u; }
        std::int32_t c_isupper(std::int32_t c) { return (classes(c) & Upper) != 0u; }
        std::int32_t c_isxdigit(std::int32_t c) { return (classes(c) & Hex) != 0u; }
        std::int32_t c_tolower(std::int32_t c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; }
        std::int32_t c_toupper(std::int32_t c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; }

        function_table const sStringFunctions
        {
            library("memcpy", NEOS_RT_FUNCTION(c_memcpy)), library("memccpy", NEOS_RT_FUNCTION(c_memccpy)),
            library("memmove", NEOS_RT_FUNCTION(c_memcpy)), library("strcpy", NEOS_RT_FUNCTION(c_strcpy)),
            library("strncpy", NEOS_RT_FUNCTION(c_strncpy)), library("strdup", NEOS_RT_FUNCTION(c_strdup)),
            library("strndup", NEOS_RT_FUNCTION(c_strndup)), library("strcat", NEOS_RT_FUNCTION(c_strcat)),
            library("strncat", NEOS_RT_FUNCTION(c_strncat)), library("memcmp", NEOS_RT_FUNCTION(c_memcmp)),
            library("strcmp", NEOS_RT_FUNCTION(c_strcmp)), library("strcoll", NEOS_RT_FUNCTION(c_strcmp)),
            library("strncmp", NEOS_RT_FUNCTION(c_strncmp)), library("strxfrm", NEOS_RT_FUNCTION(c_strxfrm)),
            library("memchr", NEOS_RT_FUNCTION(c_memchr)), library("strchr", NEOS_RT_FUNCTION(c_strchr)),
            library("strcspn", NEOS_RT_FUNCTION(c_strcspn)), library("strpbrk", NEOS_RT_FUNCTION(c_strpbrk)),
            library("strrchr", NEOS_RT_FUNCTION(c_strrchr)), library("strspn", NEOS_RT_FUNCTION(c_strspn)),
            library("strstr", NEOS_RT_FUNCTION(c_strstr)), library("strtok", NEOS_RT_FUNCTION(c_strtok)),
            library("memset", NEOS_RT_FUNCTION(c_memset)), library("memset_explicit", NEOS_RT_FUNCTION(c_memset)),
            library("strerror", NEOS_RT_FUNCTION(c_strerror)), library("strlen", NEOS_RT_FUNCTION(c_strlen)),
            library("strnlen", NEOS_RT_FUNCTION(c_strnlen)), library("strtok_r", NEOS_RT_FUNCTION(c_strtok_r)),
            library("strcasecmp", NEOS_RT_FUNCTION(c_strcasecmp)), library("strncasecmp", NEOS_RT_FUNCTION(c_strncasecmp)),
            library("isalnum", NEOS_RT_FUNCTION(c_isalnum)), library("isalpha", NEOS_RT_FUNCTION(c_isalpha)),
            library("isblank", NEOS_RT_FUNCTION(c_isblank)), library("iscntrl", NEOS_RT_FUNCTION(c_iscntrl)),
            library("isdigit", NEOS_RT_FUNCTION(c_isdigit)), library("isgraph", NEOS_RT_FUNCTION(c_isgraph)),
            library("islower", NEOS_RT_FUNCTION(c_islower)), library("isprint", NEOS_RT_FUNCTION(c_isprint)),
            library("ispunct", NEOS_RT_FUNCTION(c_ispunct)), library("isspace", NEOS_RT_FUNCTION(c_isspace)),
            library("isupper", NEOS_RT_FUNCTION(c_isupper)), library("isxdigit", NEOS_RT_FUNCTION(c_isxdigit)),
            library("tolower", NEOS_RT_FUNCTION(c_tolower)), library("toupper", NEOS_RT_FUNCTION(c_toupper))
        };
    }
}

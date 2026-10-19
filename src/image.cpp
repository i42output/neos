/*
  image.cpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <neos/neos.hpp>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <neolib/file/file.hpp>
#include <neos/image.hpp>

namespace neos
{
    namespace image
    {
        namespace
        {
#if defined(_WIN32)
            char const ExecutableExtension[] = ".exe";
#else
            char const ExecutableExtension[] = "";
#endif

            // the trailer (the last bytes of an image; little endian): magic, the text's offset and size, flags and the
            // format's version
            constexpr char Magic[8] = { 'n', 'e', 'o', 's', '.', 'i', 'm', 'g' };
            constexpr std::uint32_t Version = 1u;
            constexpr std::uint32_t FlagRestricted = 1u << 0u;
            constexpr std::size_t TrailerSize = 8u + 8u + 8u + 4u + 4u;

            struct trailer
            {
                std::uint64_t textOffset = 0u;
                std::uint64_t textSize = 0u;
                std::uint32_t flags = 0u;
            };

            void put(std::vector<char>& aBuffer, std::uint64_t aValue, std::size_t aBytes)
            {
                for (std::size_t i = 0u; i < aBytes; ++i)
                    aBuffer.push_back(static_cast<char>((aValue >> (i * 8u)) & 0xFFu));
            }

            std::uint64_t get(char const* aBytes, std::size_t aCount)
            {
                std::uint64_t result = 0u;
                for (std::size_t i = 0u; i < aCount; ++i)
                    result |= static_cast<std::uint64_t>(static_cast<unsigned char>(aBytes[i])) << (i * 8u);
                return result;
            }

            std::vector<char> encode(trailer const& aTrailer)
            {
                std::vector<char> result{ std::begin(Magic), std::end(Magic) };
                put(result, aTrailer.textOffset, 8u);
                put(result, aTrailer.textSize, 8u);
                put(result, aTrailer.flags, 4u);
                put(result, Version, 4u);
                return result;
            }

            // the trailer of an image (none if the file is not one)
            std::optional<trailer> trailer_of(std::istream& aFile, std::uint64_t aFileSize)
            {
                if (aFileSize < TrailerSize)
                    return {};
                char bytes[TrailerSize];
                aFile.seekg(static_cast<std::streamoff>(aFileSize - TrailerSize));
                if (!aFile.read(bytes, TrailerSize) || std::memcmp(bytes, Magic, sizeof(Magic)) != 0)
                    return {};
                if (get(bytes + 28u, 4u) != Version)
                    throw std::runtime_error("unsupported image version");
                trailer const result{ get(bytes + 8u, 8u), get(bytes + 16u, 8u), static_cast<std::uint32_t>(get(bytes + 24u, 4u)) };
                if (result.textOffset > aFileSize - TrailerSize || result.textSize > aFileSize - TrailerSize - result.textOffset)
                    throw std::runtime_error("invalid image");
                return result;
            }
        }

        std::string stub_path()
        {
            return (std::filesystem::path{ neolib::program_directory() } / (std::string{ "neos.stub" } + ExecutableExtension)).string();
        }

        std::string compiler_stub_path(std::string const& aLanguage)
        {
            return (std::filesystem::path{ neolib::program_directory() } / ("neos." + aLanguage + ".compiler.stub" + ExecutableExtension)).string();
        }

        std::string write(std::string const& aImagePath, text const& aText, bool aRestricted, std::string const& aStubPath)
        {
            if (aText.empty())
                throw std::runtime_error("no text");
            std::filesystem::path const imagePath{ aImagePath };
            std::ifstream stub{ aStubPath, std::ios::binary };
            if (!stub)
                throw std::runtime_error("failed to open stub '" + aStubPath + "'");
            auto const stubFileSize = static_cast<std::uint64_t>(std::filesystem::file_size(aStubPath));
            // (if the stub is itself an image only its stub is used)
            auto const existing = trailer_of(stub, stubFileSize);
            auto const stubSize = existing ? existing->textOffset : stubFileSize;
            std::vector<char> bytes(static_cast<std::size_t>(stubSize));
            stub.seekg(0);
            if (!stub.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
                throw std::runtime_error("failed to read stub '" + aStubPath + "'");
            std::error_code ec;
            if (std::filesystem::equivalent(imagePath, aStubPath, ec))
                throw std::runtime_error("the image would replace the stub '" + aStubPath + "'");
            bytes.insert(bytes.end(), reinterpret_cast<char const*>(aText.data()), reinterpret_cast<char const*>(aText.data()) + aText.size());
            auto const trailerBytes = encode(trailer{ stubSize, static_cast<std::uint64_t>(aText.size()), aRestricted ? FlagRestricted : 0u });
            bytes.insert(bytes.end(), trailerBytes.begin(), trailerBytes.end());
            {
                std::ofstream output{ imagePath, std::ios::binary | std::ios::trunc };
                if (!output || !output.write(bytes.data(), static_cast<std::streamsize>(bytes.size())) || !output.flush())
                    throw std::runtime_error("failed to write image '" + imagePath.string() + "'");
            }
            // (executable as the stub is)
            std::filesystem::permissions(imagePath, std::filesystem::status(aStubPath).permissions());
            return imagePath.string();
        }

        std::optional<contents> read(std::string const& aPath)
        {
            std::ifstream file{ aPath, std::ios::binary };
            if (!file)
                throw std::runtime_error("failed to open '" + aPath + "'");
            auto const t = trailer_of(file, static_cast<std::uint64_t>(std::filesystem::file_size(aPath)));
            if (!t)
                return {};
            contents result{ text(static_cast<std::size_t>(t->textSize)), (t->flags & FlagRestricted) != 0u };
            file.seekg(static_cast<std::streamoff>(t->textOffset));
            if (!file.read(reinterpret_cast<char*>(result.text.data()), static_cast<std::streamsize>(result.text.size())))
                throw std::runtime_error("failed to read '" + aPath + "'");
            return result;
        }
    }
}

/*
  type.hpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

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

#pragma once

#include <neos/neos.hpp>
#include <unordered_map>
#include <neolib/core/variant.hpp>
#include <neolib/core/vector.hpp>
#include <neolib/core/optional.hpp>
#include <neonumeric/integer.hpp>
#include <neonumeric/real.hpp>
#include <neos/language/symbols.hpp>

namespace neos
{
    namespace language
    {
        enum class type : std::uint32_t
        {
            UNKNOWN = 0x00000000,

            Void = 0x00000001,
            Boolean = 0x00000002,
            U8 = 0x00000003,
            U16 = 0x00000004,
            U32 = 0x00000005,
            U64 = 0x00000006,
            I8 = 0x00000007,
            I16 = 0x00000008,
            I32 = 0x00000009,
            I64 = 0x0000000A,
            F32 = 0x0000000B,
            F64 = 0x0000000C,
            Ibig = 0x0000000D,
            Fbig = 0x0000000E,
            String = 0x0000000F,
            Pointer = 0x00000010,
            Composite = 0x00000011
        };

        inline std::string type_name(language::type t)
        {
            using namespace language;
            switch (t)
            {
            case type::UNKNOWN:   return "unknown";
            case type::Void:      return "void";
            case type::Boolean:   return "bool";
            case type::U8:        return "u8";
            case type::U16:       return "u16";
            case type::U32:       return "u32";
            case type::U64:       return "u64";
            case type::I8:        return "i8";
            case type::I16:       return "i16";
            case type::I32:       return "i32";
            case type::I64:       return "i64";
            case type::F32:       return "f32";
            case type::F64:       return "f64";
            case type::Ibig:      return "ibig";
            case type::Fbig:      return "fbig";
            case type::String:    return "string";
            case type::Pointer:   return "*";
            case type::Composite: return "composite";
            default:              return "unknown";
            }
        }

        inline type type_from_name(std::string const& aName)
        {
            static const std::unordered_map<std::string, type> map = {
                { "void",      type::Void },
                { "bool",      type::Boolean },
                { "u8",        type::U8 },
                { "u16",       type::U16 },
                { "u32",       type::U32 },
                { "u64",       type::U64 },
                { "i8",        type::I8 },
                { "i16",       type::I16 },
                { "i32",       type::I32 },
                { "i64",       type::I64 },
                { "f32",       type::F32 },
                { "f64",       type::F64 },
                { "ibig",      type::Ibig },
                { "fbig",      type::Fbig },
                { "string",    type::String },
                { "*",         type::Pointer },
                { "composite", type::Composite },
                { "unknown",   type::UNKNOWN }
            };
            return map.at(aName);
        }

        using _void = std::monostate;
        using boolean = bool;
        using u8 = std::uint8_t;
        using u16 = std::uint16_t;
        using u32 = std::uint32_t;
        using u64 = std::uint64_t;
        using i8 = std::int8_t;
        using i16 = int16_t;
        using i32 = std::int32_t;
        using i64 = std::int64_t;
        using f32 = float;
        using f64 = double;
        using ibig = neonumeric::integer<0u>;
        using fbig = neonumeric::real<0u>;
        using string = neolib::string;

        static_assert(sizeof(f32) == 4);
        static_assert(sizeof(f64) == 8);

        struct pointer_type;
        struct composite_type;

        template<typename T>
        struct i_data
        {
            using abstract_type = i_data;
            using type = T;

            virtual neolib::i_optional<type> const& value() const = 0;
            virtual neolib::i_optional<type>& value() = 0;
            virtual neolib::i_optional<language::symbol_table::iterator> const& symbol() const = 0;
            virtual neolib::i_optional<language::symbol_table::iterator>& symbol() = 0;
        };

        template<typename T>
        struct data : i_data<typename neolib::abstract_t<T>>
        {
            using abstract_type = i_data<typename neolib::abstract_t<T>>;
            using type = T;

            neolib::optional<type> v;
            neolib::optional<language::symbol_table::iterator> s;

            neolib::optional<type> const& value() const final { return v; }
            neolib::optional<type>& value() final { return v; }
            neolib::optional<language::symbol_table::iterator> const& symbol() const final { return s; }
            neolib::optional<language::symbol_table::iterator>& symbol() final { return s; }

            data() {}
            data(abstract_type const& other) :
                v{ other.value() },
                s{ other.symbol() } {}
        };

        struct i_pointer_type;
        struct i_composite_type;

        using i_data_type = neolib::i_variant<
            i_data<_void>,
            i_data<boolean>,
            i_data<u8>,
            i_data<u16>,
            i_data<u32>,
            i_data<u64>,
            i_data<i8>,
            i_data<i16>,
            i_data<i32>,
            i_data<i64>,
            i_data<f32>,
            i_data<f64>,
            i_data<neolib::abstract_t<ibig>>,
            i_data<neolib::abstract_t<fbig>>,
            i_data<neolib::abstract_t<string>>,
            i_data<neolib::i_ref_ptr<i_pointer_type>>,
            i_data<neolib::i_ref_ptr<i_composite_type>>>;

        struct i_pointer_type : neolib::i_reference_counted
        {
            using abstract_type = i_pointer_type;

            virtual type base_type() const = 0;
            virtual neolib::i_ref_ptr<i_pointer_type> const& pointee() const = 0;
            virtual neolib::i_ref_ptr<i_pointer_type>& pointee() = 0;
            virtual void to_string(neolib::i_string& aResult) const = 0;

            std::string to_string() const
            {
                neolib::string result;
                to_string(result);
                return result.to_std_string();
            }
        };

        struct i_composite_type : neolib::i_reference_counted
        {
            using abstract_type = i_composite_type;

            virtual neolib::i_vector<i_data_type> const& contents() const = 0;
            virtual neolib::i_vector<i_data_type>& contents() = 0;
        };

        struct pointer_type;
        struct composite_type;

        using data_type = neolib::variant<
            data<_void>,
            data<boolean>,
            data<u8>,
            data<u16>,
            data<u32>,
            data<u64>,
            data<i8>,
            data<i16>,
            data<i32>,
            data<i64>,
            data<f32>,
            data<f64>,
            data<ibig>,
            data<fbig>,
            data<string>,
            data<neolib::ref_ptr<i_pointer_type>>,
            data<neolib::ref_ptr<i_composite_type>>>;

        struct pointer_type : neolib::reference_counted<i_pointer_type>
        {
            language::type base;
            neolib::ref_ptr<i_pointer_type> nested;

            pointer_type(language::type aBase) :
                base{ aBase }, nested{} {}

            pointer_type(neolib::ref_ptr<i_pointer_type> aNested) :
                base{ language::type::Pointer }, nested{ std::move(aNested) } {}

            type base_type() const override { return base; }
            neolib::ref_ptr<i_pointer_type> const& pointee() const override { return nested; }
            neolib::ref_ptr<i_pointer_type>& pointee() override { return nested; }

            using i_pointer_type::to_string;
            void to_string(neolib::i_string& aResult) const override
            {
                std::string result;
                if (base != language::type::Pointer || !nested)
                    result = type_name(base);
                result = nested->to_string() + "*";
                aResult = neolib::string{ result };
            }
        };

        struct composite_type : neolib::reference_counted<i_composite_type>, neolib::vector<data_type>
        {
            neolib::vector<data_type> const& contents() const final
            {
                return *this;
            }
            neolib::vector<data_type>& contents() final
            {
                return *this;
            }
        };

        template <typename T> inline constexpr type type_to_enum_v = type::UNKNOWN;
        template <> inline constexpr type type_to_enum_v<void> = type::Void;
        template <> inline constexpr type type_to_enum_v<bool> = type::Boolean;
        template <> inline constexpr type type_to_enum_v<std::uint8_t> = type::U8;
        template <> inline constexpr type type_to_enum_v<std::uint16_t> = type::U16;
        template <> inline constexpr type type_to_enum_v<std::uint32_t> = type::U32;
        template <> inline constexpr type type_to_enum_v<std::uint64_t> = type::U64;
        template <> inline constexpr type type_to_enum_v<std::int8_t> = type::I8;
        template <> inline constexpr type type_to_enum_v<std::int16_t> = type::I16;
        template <> inline constexpr type type_to_enum_v<std::int32_t> = type::I32;
        template <> inline constexpr type type_to_enum_v<std::int64_t> = type::I64;
        template <> inline constexpr type type_to_enum_v<float> = type::F32;
        template <> inline constexpr type type_to_enum_v<double> = type::F64;
        template <> inline constexpr type type_to_enum_v<ibig> = type::Ibig;
        //template <> inline constexpr type type_to_enum_v<neolib::abstract_t<ibig>> = type::Ibig;
        template <> inline constexpr type type_to_enum_v<fbig> = type::Fbig;
        //template <> inline constexpr type type_to_enum_v<neolib::abstract_t<fbig>> = type::Fbig;
        template <> inline constexpr type type_to_enum_v<string> = type::String;
        template <> inline constexpr type type_to_enum_v<neolib::abstract_t<string>> = type::String;
        template <> inline constexpr type type_to_enum_v<pointer_type> = type::Pointer;
        template <> inline constexpr type type_to_enum_v<neolib::ref_ptr<i_pointer_type>> = type::Pointer;
        template <> inline constexpr type type_to_enum_v<composite_type> = type::Composite;
        template <> inline constexpr type type_to_enum_v<neolib::ref_ptr<i_composite_type>> = type::Composite;

        inline std::ostream& operator<<(std::ostream& stream, data_type const& data)
        {
            std::ostringstream result;
            std::visit([&](auto const& d)
                {
                    using vt = std::decay_t<decltype(d)>;
                    if constexpr (std::is_same_v<vt, std::monostate>)
                        result << "";
                    else
                    {
                        if (!d.v.has_value())
                            result << "";
                        else
                        {
                            if constexpr (std::is_scalar_v<vt> ||
                                std::is_same_v<vt, ibig> ||
                                std::is_same_v<vt, fbig>)
                                result << d.v.value();
                            else
                                result << "*";
                        }
                    }
                }, data);
            stream << "[" << neolib::to_escaped_string(result.str(), 32u, true) << "]";
            return stream;
        }
   }
}
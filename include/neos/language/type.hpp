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
            Array = 0x00000011,
            Composite = 0x00000012,
            Function = 0x00000013
        };

        inline std::string type_name(type t)
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
            case type::Pointer:   return "pointer";
            case type::Array:     return "array";
            case type::Composite: return "composite";
            case type::Function:  return "function";
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
                { "pointer",   type::Pointer },
                { "array",     type::Array },
                { "composite", type::Composite },
                { "function",  type::Function },
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
        using reference = void*;

        static_assert(sizeof(f32) == 4);
        static_assert(sizeof(f64) == 8);

        struct i_default_type_descriptor { using abstract_type = i_default_type_descriptor; };
        struct default_type_descriptor : i_default_type_descriptor {};

        template<typename T, typename D = i_default_type_descriptor>
        struct i_data
        {
            using abstract_type = i_data;
            using type = T;
            using type_descriptor = D;

            virtual neolib::i_optional<type_descriptor> const& descriptor() const = 0;
            virtual neolib::i_optional<type> const& value() const = 0;
            virtual neolib::i_optional<type>& value() = 0;
            virtual neolib::i_optional<symbol_table_pointer> const& symbol() const = 0;
            virtual neolib::i_optional<symbol_table_pointer>& symbol() = 0;
        };

        template<typename T, typename D = default_type_descriptor>
        struct data : i_data<typename neolib::abstract_t<T>, neolib::abstract_t<D>>
        {
            using abstract_type = i_data<typename neolib::abstract_t<T>, neolib::abstract_t<D>>;
            using type = T;
            using type_descriptor = D;

            neolib::optional<type_descriptor> d;
            neolib::optional<type> v;
            neolib::optional<symbol_table_pointer> s;

            neolib::optional<type_descriptor> const& descriptor() const final { return d; }
            neolib::optional<type> const& value() const final { return v; }
            neolib::optional<type>& value() final { return v; }
            neolib::optional<symbol_table_pointer> const& symbol() const final { return s; }
            neolib::optional<symbol_table_pointer>& symbol() final { return s; }

            data() {}
            data(abstract_type const& other) :
                v{ other.value() },
                s{ other.symbol() } {}
        };

        struct i_pointer_descriptor : neolib::i_reference_counted
        {
            using abstract_type = i_pointer_descriptor;

            virtual type base_type() const = 0;
            virtual neolib::i_ref_ptr<i_pointer_descriptor> const& pointee() const = 0;
            virtual neolib::i_ref_ptr<i_pointer_descriptor>& pointee() = 0;
            virtual void to_string(neolib::i_string& aResult) const = 0;

            std::string to_string() const
            {
                neolib::string result;
                to_string(result);
                return result.to_std_string();
            }
        };

        struct pointer_descriptor : neolib::reference_counted<i_pointer_descriptor>
        {
            type base = {};
            neolib::ref_ptr<i_pointer_descriptor> nested;

            pointer_descriptor(type aBase) :
                base{ aBase }, nested{} {}
            pointer_descriptor(neolib::ref_ptr<i_pointer_descriptor> aNested) :
                base{ type::Pointer }, nested{ std::move(aNested) } {}
            pointer_descriptor(i_pointer_descriptor const& other) :
                base{ type::Pointer }, nested{ other.pointee() } {}

            type base_type() const final { return base; }
            neolib::ref_ptr<i_pointer_descriptor> const& pointee() const final { return nested; }
            neolib::ref_ptr<i_pointer_descriptor>& pointee() final { return nested; }

            using i_pointer_descriptor::to_string;
            void to_string(neolib::i_string& aResult) const final
            {
                std::string result;
                if (base != type::Pointer || !nested)
                    result = type_name(base);
                result = nested->to_string() + "*";
                aResult = result;
            }
        };

        struct i_array_descriptor : neolib::i_reference_counted
        {
            using abstract_type = i_array_descriptor;

            virtual type element_type() const = 0;
            virtual neolib::i_vector<std::size_t> const& extents() const = 0;
            virtual void to_string(neolib::i_string& out) const = 0;

            std::string to_string() const
            {
                neolib::string tmp;
                to_string(tmp);
                return tmp.to_std_string();
            }
        };

        struct array_descriptor : neolib::reference_counted<i_array_descriptor>
        {
            type elemType = {};
            neolib::vector<std::size_t> dims;

            array_descriptor(type e, std::vector<std::size_t> const& xs)
                : elemType{ e }, dims{ xs } {}
            array_descriptor(i_array_descriptor const& other)
                : elemType{ other.element_type() }, dims{ other.extents() } {}

            type element_type() const final { return elemType; }
            neolib::i_vector<std::size_t> const& extents() const final { return dims; }

            using i_array_descriptor::to_string;
            void to_string(neolib::i_string& out) const final
            {
                std::string s = type_name(elemType);
                for (auto x : dims) s += "[" + std::to_string(x) + "]";
                out = neolib::string{ s };
            }
        };

        struct i_array_type;
        struct i_composite_type;
        struct i_function_type;

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
            i_data<reference, i_pointer_descriptor>,
            i_data<neolib::i_ref_ptr<i_array_type>, i_array_descriptor>,
            i_data<neolib::i_ref_ptr<i_composite_type>>,
            i_data<neolib::i_ref_ptr<i_function_type>>>;

        struct i_array_type : neolib::i_reference_counted
        {
            using abstract_type = i_array_type;

            virtual neolib::i_vector<i_data_type> const& contents() const = 0;
            virtual neolib::i_vector<i_data_type>& contents() = 0;
        };

        struct i_composite_type : neolib::i_reference_counted
        {
            using abstract_type = i_composite_type;

            virtual neolib::i_vector<i_data_type> const& contents() const = 0;
            virtual neolib::i_vector<i_data_type>& contents() = 0;
        };

        struct i_function_parameter
        {
            virtual neolib::i_string const& parameter_name() const = 0;
            virtual void set_parameter_name(neolib::i_string_view const& aParameterName) = 0;
            virtual type parameter_type() const = 0;
            virtual void set_parameter_type(type aParameterType) = 0;
        };

        using i_function_parameters = neolib::i_vector<i_function_parameter>;

        struct i_function_type : neolib::i_reference_counted
        {
            using abstract_type = i_function_type;

            virtual type return_type() const = 0;
            virtual void set_return_type(type aReturnType) = 0;

            virtual i_function_parameters const& parameters() const = 0;
            virtual i_function_parameters& parameters() = 0;

            virtual void to_string(neolib::i_string& aResult) const = 0;

            std::string to_string() const
            {
                neolib::string result;
                to_string(result);
                return result.to_std_string();
            }
        };

        struct composite_type;
        struct function_type;

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
            data<reference, pointer_descriptor>,
            data<neolib::ref_ptr<i_array_type>, array_descriptor>,
            data<neolib::ref_ptr<i_composite_type>>,
            data<neolib::ref_ptr<i_function_type>>>;

        struct array_type : neolib::reference_counted<i_array_type>, neolib::vector<data_type>
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

        struct function_parameter : i_function_parameter
        {
            using abstract_type = i_function_parameter;

            neolib::string parameterName;
            type parameterType = type::UNKNOWN;

            neolib::i_string const& parameter_name() const final
            {
                return parameterName;
            }
            void set_parameter_name(neolib::i_string_view const& aParameterName) final
            {
                parameterName = aParameterName;
            }
            type parameter_type() const final
            {
                return parameterType;
            }
            void set_parameter_type(type aParameterType) final
            {
                parameterType = aParameterType;
            }

            function_parameter() = default;
            function_parameter(i_function_parameter const& other) :
                parameterName{ other.parameter_name() }, parameterType{ other.parameter_type() }
            {}
        };

        using function_parameters = neolib::vector<function_parameter>;

        struct function_type : neolib::reference_counted<i_function_type>
        {
            type returnType = type::UNKNOWN;
            function_parameters parameterList;

            function_type() = default;
            function_type(type aReturnType, function_parameters aParameters) :
                returnType{ aReturnType }, parameterList{ std::move(aParameters) } {}

            type return_type() const final { return returnType; }
            void set_return_type(type aReturnType) final { returnType = aReturnType; }

            i_function_parameters const& parameters() const final { return parameterList; }
            i_function_parameters& parameters() final { return parameterList; }

            using i_function_type::to_string;
            void to_string(neolib::i_string& aResult) const final
            {
                std::string result;
                result += type_name(returnType);
                result += "(";
                for (std::size_t i = 0; i < parameterList.size(); ++i)
                {
                    if (i > 0) result += ", ";
                    result += type_name(parameterList[i].parameter_type());
                    result += " ";
                    result += parameterList[i].parameter_name();
                }
                result += ")";
                aResult = result;
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
        template <> inline constexpr type type_to_enum_v<reference> = type::Pointer;
        template <> inline constexpr type type_to_enum_v<array_type> = type::Array;
        template <> inline constexpr type type_to_enum_v<neolib::ref_ptr<i_array_type>> = type::Array;
        template <> inline constexpr type type_to_enum_v<composite_type> = type::Composite;
        template <> inline constexpr type type_to_enum_v<neolib::ref_ptr<i_composite_type>> = type::Composite;
        template <> inline constexpr type type_to_enum_v<function_type> = type::Function;
        template <> inline constexpr type type_to_enum_v<neolib::ref_ptr<i_function_type>> = type::Function;

        inline std::ostream& operator<<(std::ostream& aStream, data_type const& aData)
        {
            std::ostringstream oss;
            std::visit([&](auto const& d)
                {
                    using vt = std::decay_t<decltype(d)>;
                    if constexpr (std::is_same_v<vt, std::monostate>)
                        ; /* empty */
                    else if constexpr (std::is_same_v<vt, data<reference, i_pointer_descriptor>>)
                    {
                        if (d.v.has_value())
                        {
                            auto const& pd = d.d.value();
                            oss << pd.to_string();
                            oss << "@" << d.v.value();
                        }
                    }
                    else if constexpr (std::is_same_v<vt, data<array_type, array_descriptor>>)
                    {
                        if (d.v.has_value())
                        {
                            auto const& ad = d.d.value();
                            // 1) shape e.g. "i32[3][4]"
                            oss << ad.to_string();
                            // 2) contents via the composite inside ad.second()
                            //    ad.second() is a ref_ptr<i_composite_type>
                            auto const& elems = d.v.value()->contents();
                            oss << "{";
                            for (std::size_t i = 0; i < elems.size(); ++i)
                            {
                                if (i) oss << ", ";
                                oss << elems[i];  // recursive call to operator<<
                            }
                            oss << "}";
                        }
                    }
                    else if constexpr (std::is_same_v<vt, data<neolib::ref_ptr<i_function_type>>>)
                    {
                        if (d.v.has_value())
                            oss << (**d.v).to_string();
                    }
                    else if constexpr (std::is_scalar_v<typename vt::type> ||
                        std::is_same_v<typename vt::type, ibig> ||
                        std::is_same_v<typename vt::type, fbig>)
                    {
                        if (d.v.has_value())
                            oss << d.v.value();
                    }
                    else
                        oss << "*";
                }, aData);

            aStream << "["
                << neolib::to_escaped_string(oss.str(), 32u, true)
                << "]";
            return aStream;
        }
   }
}
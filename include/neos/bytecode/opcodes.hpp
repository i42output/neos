/*
  opcodes.hpp

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
#include <variant>
#include <unordered_map>
#include <neos/bytecode/exceptions.hpp>

namespace neos
{
    namespace bytecode
    {
        // Define the opcode enum (core + extended)
        enum class opcode : std::uint_fast32_t
        {
            // Core (WASM 1.0) opcodes (1 byte)
            Unreachable, Nop, Block, Loop, If, Else, End, Br, BrIf, BrTable,
            Return, Call, CallIndirect, Drop, Select, GetLocal, SetLocal, TeeLocal,
            GetGlobal, SetGlobal, I32Load, I64Load, F32Load, F64Load,
            I32Load8S, I32Load8U, I32Load16S, I32Load16U,
            I64Load8S, I64Load8U, I64Load16S, I64Load16U, I64Load32S, I64Load32U,
            I32Store, I64Store, F32Store, F64Store, I32Store8, I32Store16,
            I64Store8, I64Store16, I64Store32,
            CurrentMemory, GrowMemory, I32Const, I64Const, F32Const, F64Const,
            I32Eqz, I32Eq, I32Ne, I32LtS, I32LtU, I32GtS, I32GtU,
            I32LeS, I32LeU, I32GeS, I32GeU, I64Eqz, I64Eq, I64Ne, I64LtS,
            I64LtU, I64GtS, I64GtU, I64LeS, I64LeU, I64GeS, I64GeU,
            F32Eq, F32Ne, F32Lt, F32Gt, F32Le, F32Ge,
            F64Eq, F64Ne, F64Lt, F64Gt, F64Le, F64Ge,
            I32Clz, I32Ctz, I32Popcnt, I32Add, I32Sub, I32Mul,
            I32DivS, I32DivU, I32RemS, I32RemU, I32And, I32Or, I32Xor,
            I32Shl, I32ShrS, I32ShrU, I32Rotl, I32Rotr,
            I64Clz, I64Ctz, I64Popcnt, I64Add, I64Sub, I64Mul,
            I64DivS, I64DivU, I64RemS, I64RemU, I64And, I64Or, I64Xor,
            I64Shl, I64ShrS, I64ShrU, I64Rotl, I64Rotr,
            F32Abs, F32Neg, F32Ceil, F32Floor, F32Trunc, F32Nearest, F32Sqrt,
            F64Abs, F64Neg, F64Ceil, F64Floor, F64Trunc, F64Nearest, F64Sqrt,
            I32WrapI64, I32TruncF32S, I32TruncF32U, I32TruncF64S, I32TruncF64U,
            I64ExtendI32S, I64ExtendI32U, I64TruncF32S, I64TruncF32U,
            I64TruncF64S, I64TruncF64U, F32ConvertI32S, F32ConvertI32U,
            F32ConvertI64S, F32ConvertI64U, F32DemoteF64, F64ConvertI32S,
            F64ConvertI32U, F64ConvertI64S, F64ConvertI64U, F64PromoteF32,
            I32ReinterpretF32, I64ReinterpretF64, F32ReinterpretI32, F64ReinterpretI64,

            // Extended opcodes (WASM 2.0 proposals)

            // Bulk Memory Operations (prefix 0xFC) -> 2 bytes
            MemoryInit, DataDrop, MemoryCopy, MemoryFill, TableInit, ElemDrop, TableCopy,

            // Reference Types (prefix 0xFB) -> 2 bytes
            RefNull, RefIsNull, RefFunc,

            // Threads (prefix 0xFE) -> 2 bytes
            AtomicNotify, I32AtomicWait, I64AtomicWait, AtomicFence,

            // SIMD (prefix 0xFD) -> 2 bytes
            V128Load, V128Store, V128Const,
            I8x16Shuffle, I8x16Swizzle, I8x16Splat,
            I16x8Splat, I32x4Splat, I64x2Splat,
            F32x4Splat, F64x2Splat,
            I8x16ExtractLaneS, I8x16ExtractLaneU, I8x16ReplaceLane,
            I16x8ExtractLane, I16x8ReplaceLane,
            I32x4ExtractLane, I32x4ReplaceLane,
            I64x2ExtractLane, I64x2ReplaceLane,
            F32x4ExtractLane, F32x4ReplaceLane,
            F64x2ExtractLane, F64x2ReplaceLane,
            I8x16Eq, I8x16Ne, I8x16LtS, I8x16LtU,
            I8x16GtS, I8x16GtU, I8x16LeS, I8x16LeU,
            I8x16GeS, I8x16GeU,
            I16x8Eq, I16x8Ne, I16x8LtS, I16x8LtU,
            I16x8GtS, I16x8GtU, I16x8LeS, I16x8LeU,
            I16x8GeS, I16x8GeU,
            I32x4Eq, I32x4Ne, I32x4LtS, I32x4LtU,
            I32x4GtS, I32x4GtU, I32x4LeS, I32x4LeU,
            I32x4GeS, I32x4GeU,
            F32x4Eq, F32x4Ne, F32x4Lt, F32x4Gt, F32x4Le, F32x4Ge,
            F64x2Eq, F64x2Ne, F64x2Lt, F64x2Gt, F64x2Le, F64x2Ge,
            I8x16Abs, I8x16Neg, I8x16Popcnt, I8x16AllTrue, I8x16Bitmask,
            I8x16NarrowI16x8S, I8x16NarrowI16x8U,
            I8x16Shl, I8x16ShrS, I8x16ShrU, I8x16Add,
            I8x16AddSatS, I8x16AddSatU, I8x16Sub,
            I8x16SubSatS, I8x16SubSatU, I8x16MinS, I8x16MinU,
            I8x16MaxS, I8x16MaxU, I8x16AvgrU,
            I16x8ExtMulLowI8x16S, I16x8ExtMulHighI8x16S,
            I16x8ExtMulLowI8x16U, I16x8ExtMulHighI8x16U,
            I16x8Abs, I16x8Neg, I16x8Q15MulrSatS, I16x8Add, I16x8Sub,
            I16x8Mul, I16x8AddSatS, I16x8AddSatU, I16x8SubSatS,
            I16x8SubSatU, I16x8MinS, I16x8MinU, I16x8MaxS, I16x8MaxU,
            I16x8AvgrU, I16x8ExtAddPairwiseI8x16S, I16x8ExtAddPairwiseI8x16U,
            I32x4ExtMulLowI16x8S, I32x4ExtMulHighI16x8S,
            I32x4ExtMulLowI16x8U, I32x4ExtMulHighI16x8U,
            I32x4Add, I32x4Sub, I32x4Mul, I32x4MinS,
            I32x4MinU, I32x4MaxS, I32x4MaxU,
            I64x2Add, I64x2Sub, I64x2Mul,
            I64x2ExtMulLowI32x4S, I64x2ExtMulHighI32x4S,
            I64x2ExtMulLowI32x4U, I64x2ExtMulHighI32x4U,
            F32x4Neg, F32x4Add, F32x4Sub, F32x4Mul,
            F32x4Div, F32x4Min, F32x4Max, F32x4Pmin, F32x4Pmax,
            F64x2Neg, F64x2Add, F64x2Sub, F64x2Mul,
            F64x2Div, F64x2Min, F64x2Max, F64x2Pmin, F64x2Pmax,
            V128And, V128AndNot, V128Or, V128Xor,
            V128Bitselect, V128AnyTrue
        };

        struct op1 { std::uint8_t b1; constexpr std::strong_ordering operator<=>(op1 const&) const noexcept = default; };
        struct op2 { std::uint8_t b1, b2; constexpr std::strong_ordering operator<=>(op2 const&) const noexcept = default; };
        using encoding = std::variant<op1, op2>;

        struct encoding_hash
        {
            std::uint32_t operator()(encoding const& encoding) const
            {
                if (std::holds_alternative<op1>(encoding))
                    return static_cast<std::uint32_t>(std::get<op1>(encoding).b1);
                else if (std::holds_alternative<op2>(encoding))
                    return static_cast<std::uint32_t>(std::get<op2>(encoding).b1) +
                        (static_cast<std::uint32_t>(std::get<op2>(encoding).b2) << 8u);
                throw exceptions::logic_error();
            }
        };

        struct opcode_entry
        {
            opcode opcode;
            encoding encoding;

            operator std::pair<const decltype(opcode), decltype(encoding)>() const
            {
                return std::make_pair(opcode, encoding);
            }

            operator std::pair<const decltype(encoding), decltype(opcode)>() const
            {
                return std::make_pair(encoding, opcode);
            }
        };

        inline std::vector<opcode_entry> const& opcodes()
        {
            static std::vector<opcode_entry> const sOpCodes = 
            {
                // Core opcodes (op1: 1 byte)
                { opcode::Unreachable,     op1{ 0x00 } },
                { opcode::Nop,             op1{ 0x01 } },
                { opcode::Block,           op1{ 0x02 } },
                { opcode::Loop,            op1{ 0x03 } },
                { opcode::If,              op1{ 0x04 } },
                { opcode::Else,            op1{ 0x05 } },
                { opcode::End,             op1{ 0x0B } },
                { opcode::Br,              op1{ 0x0C } },
                { opcode::BrIf,            op1{ 0x0D } },
                { opcode::BrTable,         op1{ 0x0E } },
                { opcode::Return,          op1{ 0x0F } },
                { opcode::Call,            op1{ 0x10 } },
                { opcode::CallIndirect,    op1{ 0x11 } },
                { opcode::Drop,            op1{ 0x1A } },
                { opcode::Select,          op1{ 0x1B } },
                { opcode::GetLocal,        op1{ 0x20 } },
                { opcode::SetLocal,        op1{ 0x21 } },
                { opcode::TeeLocal,        op1{ 0x22 } },
                { opcode::GetGlobal,       op1{ 0x23 } },
                { opcode::SetGlobal,       op1{ 0x24 } },
                { opcode::I32Load,         op1{ 0x28 } },
                { opcode::I64Load,         op1{ 0x29 } },
                { opcode::F32Load,         op1{ 0x2A } },
                { opcode::F64Load,         op1{ 0x2B } },
                { opcode::I32Load8S,       op1{ 0x2C } },
                { opcode::I32Load8U,       op1{ 0x2D } },
                { opcode::I32Load16S,      op1{ 0x2E } },
                { opcode::I32Load16U,      op1{ 0x2F } },
                { opcode::I64Load8S,       op1{ 0x30 } },
                { opcode::I64Load8U,       op1{ 0x31 } },
                { opcode::I64Load16S,      op1{ 0x32 } },
                { opcode::I64Load16U,      op1{ 0x33 } },
                { opcode::I64Load32S,      op1{ 0x34 } },
                { opcode::I64Load32U,      op1{ 0x35 } },
                { opcode::I32Store,        op1{ 0x36 } },
                { opcode::I64Store,        op1{ 0x37 } },
                { opcode::F32Store,        op1{ 0x38 } },
                { opcode::F64Store,        op1{ 0x39 } },
                { opcode::I32Store8,       op1{ 0x3A } },
                { opcode::I32Store16,      op1{ 0x3B } },
                { opcode::I64Store8,       op1{ 0x3C } },
                { opcode::I64Store16,      op1{ 0x3D } },
                { opcode::I64Store32,      op1{ 0x3E } },
                { opcode::CurrentMemory,   op1{ 0x3F } },
                { opcode::GrowMemory,      op1{ 0x40 } },
                { opcode::I32Const,        op1{ 0x41 } },
                { opcode::I64Const,        op1{ 0x42 } },
                { opcode::F32Const,        op1{ 0x43 } },
                { opcode::F64Const,        op1{ 0x44 } },
                { opcode::I32Eqz,          op1{ 0x45 } },
                { opcode::I32Eq,           op1{ 0x46 } },
                { opcode::I32Ne,           op1{ 0x47 } },
                { opcode::I32LtS,          op1{ 0x48 } },
                { opcode::I32LtU,          op1{ 0x49 } },
                { opcode::I32GtS,          op1{ 0x4A } },
                { opcode::I32GtU,          op1{ 0x4B } },
                { opcode::I32LeS,          op1{ 0x4C } },
                { opcode::I32LeU,          op1{ 0x4D } },
                { opcode::I32GeS,          op1{ 0x4E } },
                { opcode::I32GeU,          op1{ 0x4F } },
                { opcode::I64Eqz,          op1{ 0x50 } },
                { opcode::I64Eq,           op1{ 0x51 } },
                { opcode::I64Ne,           op1{ 0x52 } },
                { opcode::I64LtS,          op1{ 0x53 } },
                { opcode::I64LtU,          op1{ 0x54 } },
                { opcode::I64GtS,          op1{ 0x55 } },
                { opcode::I64GtU,          op1{ 0x56 } },
                { opcode::I64LeS,          op1{ 0x57 } },
                { opcode::I64LeU,          op1{ 0x58 } },
                { opcode::I64GeS,          op1{ 0x59 } },
                { opcode::I64GeU,          op1{ 0x5A } },
                { opcode::F32Eq,           op1{ 0x5B } },
                { opcode::F32Ne,           op1{ 0x5C } },
                { opcode::F32Lt,           op1{ 0x5D } },
                { opcode::F32Gt,           op1{ 0x5E } },
                { opcode::F32Le,           op1{ 0x5F } },
                { opcode::F32Ge,           op1{ 0x60 } },
                { opcode::F64Eq,           op1{ 0x61 } },
                { opcode::F64Ne,           op1{ 0x62 } },
                { opcode::F64Lt,           op1{ 0x63 } },
                { opcode::F64Gt,           op1{ 0x64 } },
                { opcode::F64Le,           op1{ 0x65 } },
                { opcode::F64Ge,           op1{ 0x66 } },
                { opcode::I32Clz,          op1{ 0x67 } },
                { opcode::I32Ctz,          op1{ 0x68 } },
                { opcode::I32Popcnt,       op1{ 0x69 } },
                { opcode::I32Add,          op1{ 0x6A } },
                { opcode::I32Sub,          op1{ 0x6B } },
                { opcode::I32Mul,          op1{ 0x6C } },
                { opcode::I32DivS,         op1{ 0x6D } },
                { opcode::I32DivU,         op1{ 0x6E } },
                { opcode::I32RemS,         op1{ 0x6F } },
                { opcode::I32RemU,         op1{ 0x70 } },
                { opcode::I32And,          op1{ 0x71 } },
                { opcode::I32Or,           op1{ 0x72 } },
                { opcode::I32Xor,          op1{ 0x73 } },
                { opcode::I32Shl,          op1{ 0x74 } },
                { opcode::I32ShrS,         op1{ 0x75 } },
                { opcode::I32ShrU,         op1{ 0x76 } },
                { opcode::I32Rotl,         op1{ 0x77 } },
                { opcode::I32Rotr,         op1{ 0x78 } },
                { opcode::I64Clz,          op1{ 0x79 } },
                { opcode::I64Ctz,          op1{ 0x7A } },
                { opcode::I64Popcnt,       op1{ 0x7B } },
                { opcode::I64Add,          op1{ 0x7C } },
                { opcode::I64Sub,          op1{ 0x7D } },
                { opcode::I64Mul,          op1{ 0x7E } },
                { opcode::I64DivS,         op1{ 0x7F } },
                { opcode::I64DivU,         op1{ 0x80 } },
                { opcode::I64RemS,         op1{ 0x81 } },
                { opcode::I64RemU,         op1{ 0x82 } },
                { opcode::I64And,          op1{ 0x83 } },
                { opcode::I64Or,           op1{ 0x84 } },
                { opcode::I64Xor,          op1{ 0x85 } },
                { opcode::I64Shl,          op1{ 0x86 } },
                { opcode::I64ShrS,         op1{ 0x87 } },
                { opcode::I64ShrU,         op1{ 0x88 } },
                { opcode::I64Rotl,         op1{ 0x89 } },
                { opcode::I64Rotr,         op1{ 0x8A } },
                { opcode::F32Abs,          op1{ 0x8B } },
                { opcode::F32Neg,          op1{ 0x8C } },
                { opcode::F32Ceil,         op1{ 0x8D } },
                { opcode::F32Floor,        op1{ 0x8E } },
                { opcode::F32Trunc,        op1{ 0x8F } },
                { opcode::F32Nearest,      op1{ 0x90 } },
                { opcode::F32Sqrt,         op1{ 0x91 } },
                { opcode::F64Abs,          op1{ 0x92 } },
                { opcode::F64Neg,          op1{ 0x93 } },
                { opcode::F64Ceil,         op1{ 0x94 } },
                { opcode::F64Floor,        op1{ 0x95 } },
                { opcode::F64Trunc,        op1{ 0x96 } },
                { opcode::F64Nearest,      op1{ 0x97 } },
                { opcode::F64Sqrt,         op1{ 0x98 } },
                { opcode::I32WrapI64,      op1{ 0x99 } },
                { opcode::I32TruncF32S,    op1{ 0x9A } },
                { opcode::I32TruncF32U,    op1{ 0x9B } },
                { opcode::I32TruncF64S,    op1{ 0x9C } },
                { opcode::I32TruncF64U,    op1{ 0x9D } },
                { opcode::I64ExtendI32S,   op1{ 0x9E } },
                { opcode::I64ExtendI32U,   op1{ 0x9F } },
                { opcode::I64TruncF32S,    op1{ 0xA0 } },
                { opcode::I64TruncF32U,    op1{ 0xA1 } },
                { opcode::I64TruncF64S,    op1{ 0xA2 } },
                { opcode::I64TruncF64U,    op1{ 0xA3 } },
                { opcode::F32ConvertI32S,  op1{ 0xA4 } },
                { opcode::F32ConvertI32U,  op1{ 0xA5 } },
                { opcode::F32ConvertI64S,  op1{ 0xA6 } },
                { opcode::F32ConvertI64U,  op1{ 0xA7 } },
                { opcode::F32DemoteF64,    op1{ 0xA8 } },
                { opcode::F64ConvertI32S,  op1{ 0xA9 } },
                { opcode::F64ConvertI32U,  op1{ 0xAA } },
                { opcode::F64ConvertI64S,  op1{ 0xAB } },
                { opcode::F64ConvertI64U,  op1{ 0xAC } },
                { opcode::F64PromoteF32,   op1{ 0xAD } },
                { opcode::I32ReinterpretF32, op1{ 0xAE } },
                { opcode::I64ReinterpretF64, op1{ 0xAF } },
                { opcode::F32ReinterpretI32, op1{ 0xB0 } },
                { opcode::F64ReinterpretI64, op1{ 0xB1 } },

                // Extended opcodes (2-byte encodings)
                // Bulk Memory Operations (prefix 0xFC)
                { opcode::MemoryInit,   op2{ 0xFC, 0x00 } },
                { opcode::DataDrop,     op2{ 0xFC, 0x01 } },
                { opcode::MemoryCopy,   op2{ 0xFC, 0x02 } },
                { opcode::MemoryFill,   op2{ 0xFC, 0x03 } },
                { opcode::TableInit,    op2{ 0xFC, 0x04 } },
                { opcode::ElemDrop,     op2{ 0xFC, 0x05 } },
                { opcode::TableCopy,    op2{ 0xFC, 0x06 } },

                // Reference Types (prefix 0xFB)
                { opcode::RefNull,      op2{ 0xFB, 0x00 } },
                { opcode::RefIsNull,    op2{ 0xFB, 0x01 } },
                { opcode::RefFunc,      op2{ 0xFB, 0x02 } },

                // Threads (prefix 0xFE)
                { opcode::AtomicNotify, op2{ 0xFE, 0x00 } },
                { opcode::I32AtomicWait, op2{ 0xFE, 0x01 } },
                { opcode::I64AtomicWait, op2{ 0xFE, 0x02 } },
                { opcode::AtomicFence,   op2{ 0xFE, 0x03 } },

                // SIMD (prefix 0xFD)
                { opcode::V128Load,          op2{ 0xFD, 0x00 } },
                { opcode::V128Store,         op2{ 0xFD, 0x01 } },
                { opcode::V128Const,         op2{ 0xFD, 0x02 } },
                { opcode::I8x16Shuffle,      op2{ 0xFD, 0x03 } },
                { opcode::I8x16Swizzle,      op2{ 0xFD, 0x04 } },
                { opcode::I8x16Splat,        op2{ 0xFD, 0x05 } },
                { opcode::I16x8Splat,        op2{ 0xFD, 0x06 } },
                { opcode::I32x4Splat,        op2{ 0xFD, 0x07 } },
                { opcode::I64x2Splat,        op2{ 0xFD, 0x08 } },
                { opcode::F32x4Splat,        op2{ 0xFD, 0x09 } },
                { opcode::F64x2Splat,        op2{ 0xFD, 0x0A } },
                { opcode::I8x16ExtractLaneS, op2{ 0xFD, 0x0B } },
                { opcode::I8x16ExtractLaneU, op2{ 0xFD, 0x0C } },
                { opcode::I8x16ReplaceLane,  op2{ 0xFD, 0x0D } },
                { opcode::I16x8ExtractLane,  op2{ 0xFD, 0x0E } },
                { opcode::I16x8ReplaceLane,  op2{ 0xFD, 0x0F } },
                { opcode::I32x4ExtractLane,  op2{ 0xFD, 0x10 } },
                { opcode::I32x4ReplaceLane,  op2{ 0xFD, 0x11 } },
                { opcode::I64x2ExtractLane,  op2{ 0xFD, 0x12 } },
                { opcode::I64x2ReplaceLane,  op2{ 0xFD, 0x13 } },
                { opcode::F32x4ExtractLane,  op2{ 0xFD, 0x14 } },
                { opcode::F32x4ReplaceLane,  op2{ 0xFD, 0x15 } },
                { opcode::F64x2ExtractLane,  op2{ 0xFD, 0x16 } },
                { opcode::F64x2ReplaceLane,  op2{ 0xFD, 0x17 } },
                { opcode::I8x16Eq,           op2{ 0xFD, 0x18 } },
                { opcode::I8x16Ne,           op2{ 0xFD, 0x19 } },
                { opcode::I8x16LtS,          op2{ 0xFD, 0x1A } },
                { opcode::I8x16LtU,          op2{ 0xFD, 0x1B } },
                { opcode::I8x16GtS,          op2{ 0xFD, 0x1C } },
                { opcode::I8x16GtU,          op2{ 0xFD, 0x1D } },
                { opcode::I8x16LeS,          op2{ 0xFD, 0x1E } },
                { opcode::I8x16LeU,          op2{ 0xFD, 0x1F } },
                { opcode::I8x16GeS,          op2{ 0xFD, 0x20 } },
                { opcode::I8x16GeU,          op2{ 0xFD, 0x21 } },
                { opcode::I16x8Eq,           op2{ 0xFD, 0x22 } },
                { opcode::I16x8Ne,           op2{ 0xFD, 0x23 } },
                { opcode::I16x8LtS,          op2{ 0xFD, 0x24 } },
                { opcode::I16x8LtU,          op2{ 0xFD, 0x25 } },
                { opcode::I16x8GtS,          op2{ 0xFD, 0x26 } },
                { opcode::I16x8GtU,          op2{ 0xFD, 0x27 } },
                { opcode::I16x8LeS,          op2{ 0xFD, 0x28 } },
                { opcode::I16x8LeU,          op2{ 0xFD, 0x29 } },
                { opcode::I16x8GeS,          op2{ 0xFD, 0x2A } },
                { opcode::I16x8GeU,          op2{ 0xFD, 0x2B } },
                { opcode::I32x4Eq,           op2{ 0xFD, 0x2C } },
                { opcode::I32x4Ne,           op2{ 0xFD, 0x2D } },
                { opcode::I32x4LtS,          op2{ 0xFD, 0x2E } },
                { opcode::I32x4LtU,          op2{ 0xFD, 0x2F } },
                { opcode::I32x4GtS,          op2{ 0xFD, 0x30 } },
                { opcode::I32x4GtU,          op2{ 0xFD, 0x31 } },
                { opcode::I32x4LeS,          op2{ 0xFD, 0x32 } },
                { opcode::I32x4LeU,          op2{ 0xFD, 0x33 } },
                { opcode::I32x4GeS,          op2{ 0xFD, 0x34 } },
                { opcode::I32x4GeU,          op2{ 0xFD, 0x35 } },
                { opcode::F32x4Eq,           op2{ 0xFD, 0x36 } },
                { opcode::F32x4Ne,           op2{ 0xFD, 0x37 } },
                { opcode::F32x4Lt,           op2{ 0xFD, 0x38 } },
                { opcode::F32x4Gt,           op2{ 0xFD, 0x39 } },
                { opcode::F32x4Le,           op2{ 0xFD, 0x3A } },
                { opcode::F32x4Ge,           op2{ 0xFD, 0x3B } },
                { opcode::F64x2Eq,           op2{ 0xFD, 0x3C } },
                { opcode::F64x2Ne,           op2{ 0xFD, 0x3D } },
                { opcode::F64x2Lt,           op2{ 0xFD, 0x3E } },
                { opcode::F64x2Gt,           op2{ 0xFD, 0x3F } },
                { opcode::F64x2Le,           op2{ 0xFD, 0x40 } },
                { opcode::F64x2Ge,           op2{ 0xFD, 0x41 } },
                { opcode::I8x16Abs,          op2{ 0xFD, 0x42 } },
                { opcode::I8x16Neg,          op2{ 0xFD, 0x43 } },
                { opcode::I8x16Popcnt,       op2{ 0xFD, 0x44 } },
                { opcode::I8x16AllTrue,      op2{ 0xFD, 0x45 } },
                { opcode::I8x16Bitmask,      op2{ 0xFD, 0x46 } },
                { opcode::I8x16NarrowI16x8S, op2{ 0xFD, 0x47 } },
                { opcode::I8x16NarrowI16x8U, op2{ 0xFD, 0x48 } },
                { opcode::I8x16Shl,          op2{ 0xFD, 0x49 } },
                { opcode::I8x16ShrS,         op2{ 0xFD, 0x4A } },
                { opcode::I8x16ShrU,         op2{ 0xFD, 0x4B } },
                { opcode::I8x16Add,          op2{ 0xFD, 0x4C } },
                { opcode::I8x16AddSatS,      op2{ 0xFD, 0x4D } },
                { opcode::I8x16AddSatU,      op2{ 0xFD, 0x4E } },
                { opcode::I8x16Sub,          op2{ 0xFD, 0x4F } },
                { opcode::I8x16SubSatS,      op2{ 0xFD, 0x50 } },
                { opcode::I8x16SubSatU,      op2{ 0xFD, 0x51 } },
                { opcode::I8x16MinS,         op2{ 0xFD, 0x52 } },
                { opcode::I8x16MinU,         op2{ 0xFD, 0x53 } },
                { opcode::I8x16MaxS,         op2{ 0xFD, 0x54 } },
                { opcode::I8x16MaxU,         op2{ 0xFD, 0x55 } },
                { opcode::I8x16AvgrU,        op2{ 0xFD, 0x56 } },
                { opcode::I16x8ExtMulLowI8x16S,  op2{ 0xFD, 0x57 } },
                { opcode::I16x8ExtMulHighI8x16S, op2{ 0xFD, 0x58 } },
                { opcode::I16x8ExtMulLowI8x16U,  op2{ 0xFD, 0x59 } },
                { opcode::I16x8ExtMulHighI8x16U, op2{ 0xFD, 0x5A } },
                { opcode::I16x8Abs,          op2{ 0xFD, 0x5B } },
                { opcode::I16x8Neg,          op2{ 0xFD, 0x5C } },
                { opcode::I16x8Q15MulrSatS,  op2{ 0xFD, 0x5D } },
                { opcode::I16x8Add,          op2{ 0xFD, 0x5E } },
                { opcode::I16x8Sub,          op2{ 0xFD, 0x5F } },
                { opcode::I16x8Mul,          op2{ 0xFD, 0x60 } },
                { opcode::I16x8AddSatS,      op2{ 0xFD, 0x61 } },
                { opcode::I16x8AddSatU,      op2{ 0xFD, 0x62 } },
                { opcode::I16x8SubSatS,      op2{ 0xFD, 0x63 } },
                { opcode::I16x8SubSatU,      op2{ 0xFD, 0x64 } },
                { opcode::I16x8MinS,         op2{ 0xFD, 0x65 } },
                { opcode::I16x8MinU,         op2{ 0xFD, 0x66 } },
                { opcode::I16x8MaxS,         op2{ 0xFD, 0x67 } },
                { opcode::I16x8MaxU,         op2{ 0xFD, 0x68 } },
                { opcode::I16x8AvgrU,        op2{ 0xFD, 0x69 } },
                { opcode::I16x8ExtAddPairwiseI8x16S, op2{ 0xFD, 0x6A } },
                { opcode::I16x8ExtAddPairwiseI8x16U, op2{ 0xFD, 0x6B } },
                { opcode::I32x4ExtMulLowI16x8S, op2{ 0xFD, 0x6C } },
                { opcode::I32x4ExtMulHighI16x8S, op2{ 0xFD, 0x6D } },
                { opcode::I32x4ExtMulLowI16x8U, op2{ 0xFD, 0x6E } },
                { opcode::I32x4ExtMulHighI16x8U, op2{ 0xFD, 0x6F } },
                { opcode::I32x4Add,          op2{ 0xFD, 0x70 } },
                { opcode::I32x4Sub,          op2{ 0xFD, 0x71 } },
                { opcode::I32x4Mul,          op2{ 0xFD, 0x72 } },
                { opcode::I32x4MinS,         op2{ 0xFD, 0x73 } },
                { opcode::I32x4MinU,         op2{ 0xFD, 0x74 } },
                { opcode::I32x4MaxS,         op2{ 0xFD, 0x75 } },
                { opcode::I32x4MaxU,         op2{ 0xFD, 0x76 } },
                { opcode::I64x2Add,          op2{ 0xFD, 0x77 } },
                { opcode::I64x2Sub,          op2{ 0xFD, 0x78 } },
                { opcode::I64x2Mul,          op2{ 0xFD, 0x79 } },
                { opcode::I64x2ExtMulLowI32x4S, op2{ 0xFD, 0x7A } },
                { opcode::I64x2ExtMulHighI32x4S, op2{ 0xFD, 0x7B } },
                { opcode::I64x2ExtMulLowI32x4U, op2{ 0xFD, 0x7C } },
                { opcode::I64x2ExtMulHighI32x4U, op2{ 0xFD, 0x7D } },
                { opcode::F32x4Neg,          op2{ 0xFD, 0x7E } },
                { opcode::F32x4Add,          op2{ 0xFD, 0x7F } },
                { opcode::F32x4Sub,          op2{ 0xFD, 0x80 } },
                { opcode::F32x4Mul,          op2{ 0xFD, 0x81 } },
                { opcode::F32x4Div,          op2{ 0xFD, 0x82 } },
                { opcode::F32x4Min,          op2{ 0xFD, 0x83 } },
                { opcode::F32x4Max,          op2{ 0xFD, 0x84 } },
                { opcode::F32x4Pmin,         op2{ 0xFD, 0x85 } },
                { opcode::F32x4Pmax,         op2{ 0xFD, 0x86 } },
                { opcode::F64x2Neg,          op2{ 0xFD, 0x87 } },
                { opcode::F64x2Add,          op2{ 0xFD, 0x88 } },
                { opcode::F64x2Sub,          op2{ 0xFD, 0x89 } },
                { opcode::F64x2Mul,          op2{ 0xFD, 0x8A } },
                { opcode::F64x2Div,          op2{ 0xFD, 0x8B } },
                { opcode::F64x2Min,          op2{ 0xFD, 0x8C } },
                { opcode::F64x2Max,          op2{ 0xFD, 0x8D } },
                { opcode::F64x2Pmin,         op2{ 0xFD, 0x8E } },
                { opcode::F64x2Pmax,         op2{ 0xFD, 0x8F } },
                { opcode::V128And,           op2{ 0xFD, 0x91 } },
                { opcode::V128AndNot,        op2{ 0xFD, 0x92 } },
                { opcode::V128Or,            op2{ 0xFD, 0x93 } },
                { opcode::V128Xor,           op2{ 0xFD, 0x94 } },
                { opcode::V128Bitselect,     op2{ 0xFD, 0x95 } },
                { opcode::V128AnyTrue,       op2{ 0xFD, 0x96 } }
            };
            return sOpCodes;
        };

        inline std::unordered_map<opcode, encoding> const& opcode_dictionary()
        {
            static std::unordered_map<opcode, encoding> const sOpcodeDictionary{ opcodes().begin(), opcodes().end() };
            return sOpcodeDictionary;
        }
                
        inline std::unordered_map<encoding, opcode, encoding_hash> const& inverse_opcode_dictionary()
        {
            static std::unordered_map<encoding, opcode, encoding_hash> const sInverseOpcodeDictionary{ opcodes().begin(), opcodes().end() };
            return sInverseOpcodeDictionary;
        }
    }
}
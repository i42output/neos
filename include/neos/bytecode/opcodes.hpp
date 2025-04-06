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

/*
  The opcode tables in this file have been generated from Chromium
  V8 source code ("v8\src\wasm\wasm-opcodes.h")
  Copyright 2006-2011, the V8 project authors (see LICENSE and 
  LICENSE.V8 files in this file's directory.
*/

#pragma once

#include <neos/neos.hpp>
#include <variant>
#include <unordered_map>
#include <boost/container_hash/hash.hpp>
#include <neos/bytecode/exceptions.hpp>

namespace neos
{
    namespace bytecode
    {
        // Define the opcode enum (core + extended)
        enum class opcode 
        {
            // Control expressions and blocks.
            Unreachable = 0x00, Nop = 0x01, Block = 0x02, Loop = 0x03, If = 0x04, Else = 0x05, Try = 0x06, Catch = 0x07,
            Throw = 0x08, Rethrow = 0x09, TryTable = 0x1f, ThrowRef = 0x0a, End = 0x0b, Br = 0x0c, BrIf = 0x0d, BrTable = 0x0e,
            Return = 0x0f, Delegate = 0x18, CatchAll = 0x19, BrOnNull = 0xd5, BrOnNonNull = 0xd6,
            NopForTestingUnsupportedInLiftoff = 0x16,
            // Constants, locals, globals, calls, etc.
            CallFunction = 0x10, CallIndirect = 0x11, ReturnCall = 0x12, ReturnCallIndirect = 0x13, CallRef = 0x14,
            ReturnCallRef = 0x15, Drop = 0x1a, Select = 0x1b, SelectWithType = 0x1c, LocalGet = 0x20, LocalSet = 0x21,
            LocalTee = 0x22, GlobalGet = 0x23, GlobalSet = 0x24, TableGet = 0x25, TableSet = 0x26, I32Const = 0x41,
            I64Const = 0x42, F32Const = 0x43, F64Const = 0x44, RefNull = 0xd0, RefIsNull = 0xd1, RefFunc = 0xd2,
            RefAsNonNull = 0xd4, RefEq = 0xd3,
            // Load memory expressions.
            I32LoadMem = 0x28, I64LoadMem = 0x29, F32LoadMem = 0x2a, F64LoadMem = 0x2b, I32LoadMem8S = 0x2c,
            I32LoadMem8U = 0x2d, I32LoadMem16S = 0x2e, I32LoadMem16U = 0x2f, I64LoadMem8S = 0x30, I64LoadMem8U = 0x31,
            I64LoadMem16S = 0x32, I64LoadMem16U = 0x33, I64LoadMem32S = 0x34, I64LoadMem32U = 0x35, F32LoadMemF16 = 0xfc30,
            // Store memory expressions.
            I32StoreMem = 0x36, I64StoreMem = 0x37, F32StoreMem = 0x38, F64StoreMem = 0x39, I32StoreMem8 = 0x3a,
            I32StoreMem16 = 0x3b, I64StoreMem8 = 0x3c, I64StoreMem16 = 0x3d, I64StoreMem32 = 0x3e, F32StoreMemF16 = 0xfc31,
            // Miscellaneous memory expressions
            MemorySize = 0x3f, MemoryGrow = 0x40,
            // Expressions with signatures.
            // Opcodes that can also be used in constant expressions (via the 'extended
            // constant expressions' proposal.
            I32Add = 0x6a, I32Sub = 0x6b, I32Mul = 0x6c, I64Add = 0x7c, I64Sub = 0x7d, I64Mul = 0x7e, I32Eqz = 0x45,
            I32Eq = 0x46, I32Ne = 0x47, I32LtS = 0x48, I32LtU = 0x49, I32GtS = 0x4a, I32GtU = 0x4b, I32LeS = 0x4c,
            I32LeU = 0x4d, I32GeS = 0x4e, I32GeU = 0x4f, I64Eqz = 0x50, I64Eq = 0x51, I64Ne = 0x52, I64LtS = 0x53,
            I64LtU = 0x54, I64GtS = 0x55, I64GtU = 0x56, I64LeS = 0x57, I64LeU = 0x58, I64GeS = 0x59, I64GeU = 0x5a,
            F32Eq = 0x5b, F32Ne = 0x5c, F32Lt = 0x5d, F32Gt = 0x5e, F32Le = 0x5f, F32Ge = 0x60, F64Eq = 0x61, F64Ne = 0x62,
            F64Lt = 0x63, F64Gt = 0x64, F64Le = 0x65, F64Ge = 0x66, I32Clz = 0x67, I32Ctz = 0x68, I32Popcnt = 0x69,
            I32DivS = 0x6d, I32DivU = 0x6e, I32RemS = 0x6f, I32RemU = 0x70, I32And = 0x71, I32Ior = 0x72, I32Xor = 0x73,
            I32Shl = 0x74, I32ShrS = 0x75, I32ShrU = 0x76, I32Rol = 0x77, I32Ror = 0x78, I64Clz = 0x79, I64Ctz = 0x7a,
            I64Popcnt = 0x7b, I64DivS = 0x7f, I64DivU = 0x80, I64RemS = 0x81, I64RemU = 0x82, I64And = 0x83, I64Ior = 0x84,
            I64Xor = 0x85, I64Shl = 0x86, I64ShrS = 0x87, I64ShrU = 0x88, I64Rol = 0x89, I64Ror = 0x8a, F32Abs = 0x8b,
            F32Neg = 0x8c, F32Ceil = 0x8d, F32Floor = 0x8e, F32Trunc = 0x8f, F32NearestInt = 0x90, F32Sqrt = 0x91,
            F32Add = 0x92, F32Sub = 0x93, F32Mul = 0x94, F32Div = 0x95, F32Min = 0x96, F32Max = 0x97, F32CopySign = 0x98,
            F64Abs = 0x99, F64Neg = 0x9a, F64Ceil = 0x9b, F64Floor = 0x9c, F64Trunc = 0x9d, F64NearestInt = 0x9e,
            F64Sqrt = 0x9f, F64Add = 0xa0, F64Sub = 0xa1, F64Mul = 0xa2, F64Div = 0xa3, F64Min = 0xa4, F64Max = 0xa5,
            F64CopySign = 0xa6, I32ConvertI64 = 0xa7, I32SConvertF32 = 0xa8, I32UConvertF32 = 0xa9, I32SConvertF64 = 0xaa,
            I32UConvertF64 = 0xab, I64SConvertI32 = 0xac, I64UConvertI32 = 0xad, I64SConvertF32 = 0xae, I64UConvertF32 = 0xaf,
            I64SConvertF64 = 0xb0, I64UConvertF64 = 0xb1, F32SConvertI32 = 0xb2, F32UConvertI32 = 0xb3, F32SConvertI64 = 0xb4,
            F32UConvertI64 = 0xb5, F32ConvertF64 = 0xb6, F64SConvertI32 = 0xb7, F64UConvertI32 = 0xb8, F64SConvertI64 = 0xb9,
            F64UConvertI64 = 0xba, F64ConvertF32 = 0xbb, I32ReinterpretF32 = 0xbc, I64ReinterpretF64 = 0xbd,
            F32ReinterpretI32 = 0xbe, F64ReinterpretI64 = 0xbf, I32SExtendI8 = 0xc0, I32SExtendI16 = 0xc1, I64SExtendI8 = 0xc2,
            I64SExtendI16 = 0xc3, I64SExtendI32 = 0xc4, ContNew = 0xe0, ContBind = 0xe1, Suspend = 0xe2, Resume = 0xe3,
            ResumeThrow = 0xe4, Switch = 0xe5,
            // For compatibility with Asm.js.
            // These opcodes are not spec'ed (or visible externally; the idea is
            // to use unused ranges for internal purposes.
            F64Acos = 0xfa3c, F64Asin = 0xfa3d, F64Atan = 0xfa3e, F64Cos = 0xfa3f, F64Sin = 0xfa40, F64Tan = 0xfa41,
            F64Exp = 0xfa42, F64Log = 0xfa43, F64Atan2 = 0xfa44, F64Pow = 0xfa45, F64Mod = 0xfa46, I32AsmjsDivS = 0xfa47,
            I32AsmjsDivU = 0xfa48, I32AsmjsRemS = 0xfa49, I32AsmjsRemU = 0xfa4a, I32AsmjsLoadMem8S = 0xfa4b,
            I32AsmjsLoadMem8U = 0xfa4c, I32AsmjsLoadMem16S = 0xfa4d, I32AsmjsLoadMem16U = 0xfa4e, I32AsmjsLoadMem = 0xfa4f,
            F32AsmjsLoadMem = 0xfa50, F64AsmjsLoadMem = 0xfa51, I32AsmjsStoreMem8 = 0xfa52, I32AsmjsStoreMem16 = 0xfa53,
            I32AsmjsStoreMem = 0xfa54, F32AsmjsStoreMem = 0xfa55, F64AsmjsStoreMem = 0xfa56, I32AsmjsSConvertF32 = 0xfa57,
            I32AsmjsUConvertF32 = 0xfa58, I32AsmjsSConvertF64 = 0xfa59, I32AsmjsUConvertF64 = 0xfa5a, S128LoadMem = 0xfd00,
            S128Load8x8S = 0xfd01, S128Load8x8U = 0xfd02, S128Load16x4S = 0xfd03, S128Load16x4U = 0xfd04,
            S128Load32x2S = 0xfd05, S128Load32x2U = 0xfd06, S128Load8Splat = 0xfd07, S128Load16Splat = 0xfd08,
            S128Load32Splat = 0xfd09, S128Load64Splat = 0xfd0a, S128StoreMem = 0xfd0b, S128Load32Zero = 0xfd5c,
            S128Load64Zero = 0xfd5d, S128Load8Lane = 0xfd54, S128Load16Lane = 0xfd55, S128Load32Lane = 0xfd56,
            S128Load64Lane = 0xfd57, S128Store8Lane = 0xfd58, S128Store16Lane = 0xfd59, S128Store32Lane = 0xfd5a,
            S128Store64Lane = 0xfd5b, S128Const = 0xfd0c, I8x16Shuffle = 0xfd0d, I8x16Swizzle = 0xfd0e, I8x16Splat = 0xfd0f,
            I16x8Splat = 0xfd10, I32x4Splat = 0xfd11, I64x2Splat = 0xfd12, F32x4Splat = 0xfd13, F64x2Splat = 0xfd14,
            I8x16Eq = 0xfd23, I8x16Ne = 0xfd24, I8x16LtS = 0xfd25, I8x16LtU = 0xfd26, I8x16GtS = 0xfd27, I8x16GtU = 0xfd28,
            I8x16LeS = 0xfd29, I8x16LeU = 0xfd2a, I8x16GeS = 0xfd2b, I8x16GeU = 0xfd2c, I16x8Eq = 0xfd2d, I16x8Ne = 0xfd2e,
            I16x8LtS = 0xfd2f, I16x8LtU = 0xfd30, I16x8GtS = 0xfd31, I16x8GtU = 0xfd32, I16x8LeS = 0xfd33, I16x8LeU = 0xfd34,
            I16x8GeS = 0xfd35, I16x8GeU = 0xfd36, I32x4Eq = 0xfd37, I32x4Ne = 0xfd38, I32x4LtS = 0xfd39, I32x4LtU = 0xfd3a,
            I32x4GtS = 0xfd3b, I32x4GtU = 0xfd3c, I32x4LeS = 0xfd3d, I32x4LeU = 0xfd3e, I32x4GeS = 0xfd3f, I32x4GeU = 0xfd40,
            F32x4Eq = 0xfd41, F32x4Ne = 0xfd42, F32x4Lt = 0xfd43, F32x4Gt = 0xfd44, F32x4Le = 0xfd45, F32x4Ge = 0xfd46,
            F64x2Eq = 0xfd47, F64x2Ne = 0xfd48, F64x2Lt = 0xfd49, F64x2Gt = 0xfd4a, F64x2Le = 0xfd4b, F64x2Ge = 0xfd4c,
            S128Not = 0xfd4d, S128And = 0xfd4e, S128AndNot = 0xfd4f, S128Or = 0xfd50, S128Xor = 0xfd51, S128Select = 0xfd52,
            V128AnyTrue = 0xfd53, F32x4DemoteF64x2Zero = 0xfd5e, F64x2PromoteLowF32x4 = 0xfd5f, I8x16Abs = 0xfd60,
            I8x16Neg = 0xfd61, I8x16Popcnt = 0xfd62, I8x16AllTrue = 0xfd63, I8x16BitMask = 0xfd64, I8x16SConvertI16x8 = 0xfd65,
            I8x16UConvertI16x8 = 0xfd66, F32x4Ceil = 0xfd67, F32x4Floor = 0xfd68, F32x4Trunc = 0xfd69, F32x4NearestInt = 0xfd6a,
            I8x16Shl = 0xfd6b, I8x16ShrS = 0xfd6c, I8x16ShrU = 0xfd6d, I8x16Add = 0xfd6e, I8x16AddSatS = 0xfd6f,
            I8x16AddSatU = 0xfd70, I8x16Sub = 0xfd71, I8x16SubSatS = 0xfd72, I8x16SubSatU = 0xfd73, F64x2Ceil = 0xfd74,
            F64x2Floor = 0xfd75, I8x16MinS = 0xfd76, I8x16MinU = 0xfd77, I8x16MaxS = 0xfd78, I8x16MaxU = 0xfd79,
            F64x2Trunc = 0xfd7a, I8x16RoundingAverageU = 0xfd7b, I16x8ExtAddPairwiseI8x16S = 0xfd7c,
            I16x8ExtAddPairwiseI8x16U = 0xfd7d, I32x4ExtAddPairwiseI16x8S = 0xfd7e, I32x4ExtAddPairwiseI16x8U = 0xfd7f,
            I16x8Abs = 0xfd80, I16x8Neg = 0xfd81, I16x8Q15MulRSatS = 0xfd82, I16x8AllTrue = 0xfd83, I16x8BitMask = 0xfd84,
            I16x8SConvertI32x4 = 0xfd85, I16x8UConvertI32x4 = 0xfd86, I16x8SConvertI8x16Low = 0xfd87,
            I16x8SConvertI8x16High = 0xfd88, I16x8UConvertI8x16Low = 0xfd89, I16x8UConvertI8x16High = 0xfd8a, I16x8Shl = 0xfd8b,
            I16x8ShrS = 0xfd8c, I16x8ShrU = 0xfd8d, I16x8Add = 0xfd8e, I16x8AddSatS = 0xfd8f, I16x8AddSatU = 0xfd90,
            I16x8Sub = 0xfd91, I16x8SubSatS = 0xfd92, I16x8SubSatU = 0xfd93, F64x2NearestInt = 0xfd94, I16x8Mul = 0xfd95,
            I16x8MinS = 0xfd96, I16x8MinU = 0xfd97, I16x8MaxS = 0xfd98, I16x8MaxU = 0xfd99, I16x8RoundingAverageU = 0xfd9b,
            I16x8ExtMulLowI8x16S = 0xfd9c, I16x8ExtMulHighI8x16S = 0xfd9d, I16x8ExtMulLowI8x16U = 0xfd9e,
            I16x8ExtMulHighI8x16U = 0xfd9f, I32x4Abs = 0xfda0, I32x4Neg = 0xfda1, I32x4AllTrue = 0xfda3, I32x4BitMask = 0xfda4,
            I32x4SConvertI16x8Low = 0xfda7, I32x4SConvertI16x8High = 0xfda8, I32x4UConvertI16x8Low = 0xfda9,
            I32x4UConvertI16x8High = 0xfdaa, I32x4Shl = 0xfdab, I32x4ShrS = 0xfdac, I32x4ShrU = 0xfdad, I32x4Add = 0xfdae,
            I32x4Sub = 0xfdb1, I32x4Mul = 0xfdb5, I32x4MinS = 0xfdb6, I32x4MinU = 0xfdb7, I32x4MaxS = 0xfdb8,
            I32x4MaxU = 0xfdb9, I32x4DotI16x8S = 0xfdba, I32x4ExtMulLowI16x8S = 0xfdbc, I32x4ExtMulHighI16x8S = 0xfdbd,
            I32x4ExtMulLowI16x8U = 0xfdbe, I32x4ExtMulHighI16x8U = 0xfdbf, I64x2Abs = 0xfdc0, I64x2Neg = 0xfdc1,
            I64x2AllTrue = 0xfdc3, I64x2BitMask = 0xfdc4, I64x2SConvertI32x4Low = 0xfdc7, I64x2SConvertI32x4High = 0xfdc8,
            I64x2UConvertI32x4Low = 0xfdc9, I64x2UConvertI32x4High = 0xfdca, I64x2Shl = 0xfdcb, I64x2ShrS = 0xfdcc,
            I64x2ShrU = 0xfdcd, I64x2Add = 0xfdce, I64x2Sub = 0xfdd1, I64x2Mul = 0xfdd5, I64x2Eq = 0xfdd6, I64x2Ne = 0xfdd7,
            I64x2LtS = 0xfdd8, I64x2GtS = 0xfdd9, I64x2LeS = 0xfdda, I64x2GeS = 0xfddb, I64x2ExtMulLowI32x4S = 0xfddc,
            I64x2ExtMulHighI32x4S = 0xfddd, I64x2ExtMulLowI32x4U = 0xfdde, I64x2ExtMulHighI32x4U = 0xfddf, F32x4Abs = 0xfde0,
            F32x4Neg = 0xfde1, F32x4Sqrt = 0xfde3, F32x4Add = 0xfde4, F32x4Sub = 0xfde5, F32x4Mul = 0xfde6, F32x4Div = 0xfde7,
            F32x4Min = 0xfde8, F32x4Max = 0xfde9, F32x4Pmin = 0xfdea, F32x4Pmax = 0xfdeb, F64x2Abs = 0xfdec, F64x2Neg = 0xfded,
            F64x2Sqrt = 0xfdef, F64x2Add = 0xfdf0, F64x2Sub = 0xfdf1, F64x2Mul = 0xfdf2, F64x2Div = 0xfdf3, F64x2Min = 0xfdf4,
            F64x2Max = 0xfdf5, F64x2Pmin = 0xfdf6, F64x2Pmax = 0xfdf7, I32x4SConvertF32x4 = 0xfdf8, I32x4UConvertF32x4 = 0xfdf9,
            F32x4SConvertI32x4 = 0xfdfa, F32x4UConvertI32x4 = 0xfdfb, I32x4TruncSatF64x2SZero = 0xfdfc,
            I32x4TruncSatF64x2UZero = 0xfdfd, F64x2ConvertLowI32x4S = 0xfdfe, F64x2ConvertLowI32x4U = 0xfdff,
            I8x16RelaxedSwizzle = 0xfd100, I32x4RelaxedTruncF32x4S = 0xfd101, I32x4RelaxedTruncF32x4U = 0xfd102,
            I32x4RelaxedTruncF64x2SZero = 0xfd103, I32x4RelaxedTruncF64x2UZero = 0xfd104, F32x4Qfma = 0xfd105,
            F32x4Qfms = 0xfd106, F64x2Qfma = 0xfd107, F64x2Qfms = 0xfd108, I8x16RelaxedLaneSelect = 0xfd109,
            I16x8RelaxedLaneSelect = 0xfd10a, I32x4RelaxedLaneSelect = 0xfd10b, I64x2RelaxedLaneSelect = 0xfd10c,
            F32x4RelaxedMin = 0xfd10d, F32x4RelaxedMax = 0xfd10e, F64x2RelaxedMin = 0xfd10f, F64x2RelaxedMax = 0xfd110,
            I16x8RelaxedQ15MulRS = 0xfd111, I16x8DotI8x16I7x16S = 0xfd112, I32x4DotI8x16I7x16AddS = 0xfd113,
            F16x8Splat = 0xfd120, F16x8Abs = 0xfd130, F16x8Neg = 0xfd131, F16x8Sqrt = 0xfd132, F16x8Ceil = 0xfd133,
            F16x8Floor = 0xfd134, F16x8Trunc = 0xfd135, F16x8NearestInt = 0xfd136, F16x8Eq = 0xfd137, F16x8Ne = 0xfd138,
            F16x8Lt = 0xfd139, F16x8Gt = 0xfd13a, F16x8Le = 0xfd13b, F16x8Ge = 0xfd13c, F16x8Add = 0xfd13d, F16x8Sub = 0xfd13e,
            F16x8Mul = 0xfd13f, F16x8Div = 0xfd140, F16x8Min = 0xfd141, F16x8Max = 0xfd142, F16x8Pmin = 0xfd143,
            F16x8Pmax = 0xfd144, I16x8SConvertF16x8 = 0xfd145, I16x8UConvertF16x8 = 0xfd146, F16x8SConvertI16x8 = 0xfd147,
            F16x8UConvertI16x8 = 0xfd148, F16x8DemoteF32x4Zero = 0xfd149, F16x8DemoteF64x2Zero = 0xfd14a,
            F32x4PromoteLowF16x8 = 0xfd14b, F16x8Qfma = 0xfd14e, F16x8Qfms = 0xfd14f, I8x16ExtractLaneS = 0xfd15,
            I8x16ExtractLaneU = 0xfd16, I16x8ExtractLaneS = 0xfd18, I16x8ExtractLaneU = 0xfd19, I32x4ExtractLane = 0xfd1b,
            I64x2ExtractLane = 0xfd1d, F32x4ExtractLane = 0xfd1f, F64x2ExtractLane = 0xfd21, F16x8ExtractLane = 0xfd121,
            I8x16ReplaceLane = 0xfd17, I16x8ReplaceLane = 0xfd1a, I32x4ReplaceLane = 0xfd1c, I64x2ReplaceLane = 0xfd1e,
            F32x4ReplaceLane = 0xfd20, F64x2ReplaceLane = 0xfd22, F16x8ReplaceLane = 0xfd122, I32SConvertSatF32 = 0xfc00,
            I32UConvertSatF32 = 0xfc01, I32SConvertSatF64 = 0xfc02, I32UConvertSatF64 = 0xfc03, I64SConvertSatF32 = 0xfc04,
            I64UConvertSatF32 = 0xfc05, I64SConvertSatF64 = 0xfc06, I64UConvertSatF64 = 0xfc07, DataDrop = 0xfc09,
            TableInit = 0xfc0c, ElemDrop = 0xfc0d, TableCopy = 0xfc0e, TableSize = 0xfc10, MemoryInit = 0xfc08,
            MemoryCopy = 0xfc0a, MemoryFill = 0xfc0b,
            /* TableGrow is polymorphic in the first parameter. */
            /* It's whatever the table type is. */
            TableGrow = 0xfc0f,
            /* TableFill is polymorphic in the second parameter. */
            /* It's whatever the table type is. */
            TableFill = 0xfc11,
            // kExprName, binary, signature for memory32, wat name, signature for memory64.
            AtomicNotify = 0xfe00, I32AtomicWait = 0xfe01, I64AtomicWait = 0xfe02, I32AtomicLoad = 0xfe10,
            I64AtomicLoad = 0xfe11, I32AtomicLoad8U = 0xfe12, I32AtomicLoad16U = 0xfe13, I64AtomicLoad8U = 0xfe14,
            I64AtomicLoad16U = 0xfe15, I64AtomicLoad32U = 0xfe16, I32AtomicStore = 0xfe17, I64AtomicStore = 0xfe18,
            I32AtomicStore8U = 0xfe19, I32AtomicStore16U = 0xfe1a, I64AtomicStore8U = 0xfe1b, I64AtomicStore16U = 0xfe1c,
            I64AtomicStore32U = 0xfe1d, I32AtomicAdd = 0xfe1e, I64AtomicAdd = 0xfe1f, I32AtomicAdd8U = 0xfe20,
            I32AtomicAdd16U = 0xfe21, I64AtomicAdd8U = 0xfe22, I64AtomicAdd16U = 0xfe23, I64AtomicAdd32U = 0xfe24,
            I32AtomicSub = 0xfe25, I64AtomicSub = 0xfe26, I32AtomicSub8U = 0xfe27, I32AtomicSub16U = 0xfe28,
            I64AtomicSub8U = 0xfe29, I64AtomicSub16U = 0xfe2a, I64AtomicSub32U = 0xfe2b, I32AtomicAnd = 0xfe2c,
            I64AtomicAnd = 0xfe2d, I32AtomicAnd8U = 0xfe2e, I32AtomicAnd16U = 0xfe2f, I64AtomicAnd8U = 0xfe30,
            I64AtomicAnd16U = 0xfe31, I64AtomicAnd32U = 0xfe32, I32AtomicOr = 0xfe33, I64AtomicOr = 0xfe34,
            I32AtomicOr8U = 0xfe35, I32AtomicOr16U = 0xfe36, I64AtomicOr8U = 0xfe37, I64AtomicOr16U = 0xfe38,
            I64AtomicOr32U = 0xfe39, I32AtomicXor = 0xfe3a, I64AtomicXor = 0xfe3b, I32AtomicXor8U = 0xfe3c,
            I32AtomicXor16U = 0xfe3d, I64AtomicXor8U = 0xfe3e, I64AtomicXor16U = 0xfe3f, I64AtomicXor32U = 0xfe40,
            I32AtomicExchange = 0xfe41, I64AtomicExchange = 0xfe42, I32AtomicExchange8U = 0xfe43, I32AtomicExchange16U = 0xfe44,
            I64AtomicExchange8U = 0xfe45, I64AtomicExchange16U = 0xfe46, I64AtomicExchange32U = 0xfe47,
            I32AtomicCompareExchange = 0xfe48, I64AtomicCompareExchange = 0xfe49, I32AtomicCompareExchange8U = 0xfe4a,
            I32AtomicCompareExchange16U = 0xfe4b, I64AtomicCompareExchange8U = 0xfe4c, I64AtomicCompareExchange16U = 0xfe4d,
            I64AtomicCompareExchange32U = 0xfe4e,
            /* AtomicFence does not target a particular linear memory. */
            AtomicFence = 0xfe03, StructNew = 0xfb00, StructNewDefault = 0xfb01, StructGet = 0xfb02, StructGetS = 0xfb03,
            StructGetU = 0xfb04, StructSet = 0xfb05, ArrayNew = 0xfb06, ArrayNewDefault = 0xfb07, ArrayNewFixed = 0xfb08,
            ArrayNewData = 0xfb09, ArrayNewElem = 0xfb0a, ArrayGet = 0xfb0b, ArrayGetS = 0xfb0c, ArrayGetU = 0xfb0d,
            ArraySet = 0xfb0e, ArrayLen = 0xfb0f, ArrayFill = 0xfb10, ArrayCopy = 0xfb11, ArrayInitData = 0xfb12,
            ArrayInitElem = 0xfb13, RefTest = 0xfb14, RefTestNull = 0xfb15, RefCast = 0xfb16, RefCastNull = 0xfb17,
            BrOnCast = 0xfb18, BrOnCastFail = 0xfb19, AnyConvertExtern = 0xfb1a, ExternConvertAny = 0xfb1b, RefI31 = 0xfb1c,
            I31GetS = 0xfb1d, I31GetU = 0xfb1e,
            /* Custom Descriptors proposal */
            RefGetDesc = 0xfb22, RefCastDesc = 0xfb23, RefCastDescNull = 0xfb24, BrOnCastDesc = 0xfb25,
            BrOnCastDescFail = 0xfb26, RefCastNop = 0xfb4c,
            /* Stringref proposal. */
            StringNewUtf8 = 0xfb80, StringNewWtf16 = 0xfb81, StringConst = 0xfb82, StringMeasureUtf8 = 0xfb83,
            StringMeasureWtf8 = 0xfb84, StringMeasureWtf16 = 0xfb85, StringEncodeUtf8 = 0xfb86, StringEncodeWtf16 = 0xfb87,
            StringConcat = 0xfb88, StringEq = 0xfb89, StringIsUSVSequence = 0xfb8a, StringNewLossyUtf8 = 0xfb8b,
            StringNewWtf8 = 0xfb8c, StringEncodeLossyUtf8 = 0xfb8d, StringEncodeWtf8 = 0xfb8e, StringNewUtf8Try = 0xfb8f,
            StringAsWtf8 = 0xfb90, StringViewWtf8Advance = 0xfb91, StringViewWtf8EncodeUtf8 = 0xfb92,
            StringViewWtf8Slice = 0xfb93, StringViewWtf8EncodeLossyUtf8 = 0xfb94, StringViewWtf8EncodeWtf8 = 0xfb95,
            StringAsWtf16 = 0xfb98, StringViewWtf16Length = 0xfb99, StringViewWtf16GetCodeunit = 0xfb9a,
            StringViewWtf16Encode = 0xfb9b, StringViewWtf16Slice = 0xfb9c, StringAsIter = 0xfba0, StringViewIterNext = 0xfba1,
            StringViewIterAdvance = 0xfba2, StringViewIterRewind = 0xfba3, StringViewIterSlice = 0xfba4, StringCompare = 0xfba8,
            StringFromCodePoint = 0xfba9, StringHash = 0xfbaa, StringNewUtf8Array = 0xfbb0, StringNewWtf16Array = 0xfbb1,
            StringEncodeUtf8Array = 0xfbb2, StringEncodeWtf16Array = 0xfbb3, StringNewLossyUtf8Array = 0xfbb4,
            StringNewWtf8Array = 0xfbb5, StringEncodeLossyUtf8Array = 0xfbb6, StringEncodeWtf8Array = 0xfbb7,
            StringNewUtf8ArrayTry = 0xfbb8,
        };

        using leb128_1 = std::array<std::uint8_t, 1>;
        using leb128_2 = std::array<std::uint8_t, 2>;
        using leb128_3 = std::array<std::uint8_t, 3>;
        using leb128 = std::variant<leb128_1, leb128_2, leb128_3>;

        inline leb128 LEB128(std::uint32_t aValue)
        {
            // todo
            return leb128{};
        }

        struct op1 { std::uint8_t b; constexpr std::strong_ordering operator<=>(op1 const&) const noexcept = default; };
        struct op2 { std::uint8_t p; leb128 e; constexpr std::strong_ordering operator<=>(op2 const&) const noexcept = default; };
        using encoding = std::variant<op1, op2>;

        struct encoding_hash
        {
            std::uint32_t operator()(encoding const& encoding) const
            {
                if (std::holds_alternative<op1>(encoding))
                    return static_cast<std::uint32_t>(std::get<op1>(encoding).b);
                else if (std::holds_alternative<op2>(encoding))
                    return static_cast<std::uint32_t>(std::get<op2>(encoding).p) +
                        (std::visit([](auto const& leb) -> std::uint32_t 
                            { return static_cast<std::uint32_t>(boost::hash<std::decay_t<decltype(leb)>>{}(leb)); },
                            std::get<op2>(encoding).e) << 8u);
                throw exceptions::logic_error();
            }
        };

        namespace type
        {
            struct Void {};
            using I32 = std::int32_t;
            using I64 = std::int64_t;
            using F32 = float;
            using F64 = double;
            struct AnyRef {}; ///< todo
            struct FuncRef {}; ///< todo
            struct EqRef {}; ///< todo
            struct S128 {}; ///< todo
        }

        namespace sig
        {
            using _ = std::tuple<>;
            using d_d = std::tuple<type::F64, type::F64>;
            using d_dd = std::tuple<type::F64, type::F64, type::F64>;
            using d_f = std::tuple<type::F64, type::F32>;
            using d_i = std::tuple<type::F64, type::I32>;
            using d_id = std::tuple<type::F64, type::I32, type::F64>;
            using d_l = std::tuple<type::F64, type::I64>;
            using f_d = std::tuple<type::F32, type::F64>;
            using f_f = std::tuple<type::F32, type::F32>;
            using f_ff = std::tuple<type::F32, type::F32, type::F32>;
            using f_i = std::tuple<type::F32, type::I32>;
            using f_if = std::tuple<type::F32, type::I32, type::F32>;
            using f_l = std::tuple<type::F32, type::I64>;
            using i_a = std::tuple<type::I32, type::AnyRef>;
            using i_ci = std::tuple<type::I32, type::FuncRef, type::I32>;
            using i_d = std::tuple<type::I32, type::F64>;
            using i_dd = std::tuple<type::I32, type::F64, type::F64>;
            using i_f = std::tuple<type::I32, type::F32>;
            using i_ff = std::tuple<type::I32, type::F32, type::F32>;
            using i_i = std::tuple<type::I32, type::I32>;
            using i_ii = std::tuple<type::I32, type::I32, type::I32>;
            using i_iii = std::tuple<type::I32, type::I32, type::I32, type::I32>;
            using i_iil = std::tuple<type::I32, type::I32, type::I32, type::I64>;
            using i_ill = std::tuple<type::I32, type::I32, type::I64, type::I64>;
            using i_l = std::tuple<type::I32, type::I64>;
            using i_li = std::tuple<type::I32, type::I64, type::I32>;
            using i_lii = std::tuple<type::I32, type::I64, type::I32, type::I32>;
            using i_lil = std::tuple<type::I32, type::I64, type::I32, type::I64>;
            using i_lll = std::tuple<type::I32, type::I64, type::I64, type::I64>;
            using i_ll = std::tuple<type::I32, type::I64, type::I64>;
            using i_qq = std::tuple<type::I32, type::EqRef, type::EqRef>;
            using i_v = std::tuple<type::I32>;
            using l_d = std::tuple<type::I64, type::F64>;
            using l_f = std::tuple<type::I64, type::F32>;
            using l_i = std::tuple<type::I64, type::I32>;
            using l_il = std::tuple<type::I64, type::I32, type::I64>;
            using l_ill = std::tuple<type::I64, type::I32, type::I64, type::I64>;
            using l_l = std::tuple<type::I64, type::I64>;
            using l_ll = std::tuple<type::I64, type::I64, type::I64>;
            using l_lll = std::tuple<type::I64, type::I64, type::I64, type::I64>;
            using v_id = std::tuple<type::Void, type::I32, type::F64>;
            using v_if = std::tuple<type::Void, type::I32, type::F32>;
            using v_i = std::tuple<type::Void, type::I32>;
            using v_ii = std::tuple<type::Void, type::I32, type::I32>;
            using v_iii = std::tuple<type::Void, type::I32, type::I32, type::I32>;
            using v_il = std::tuple<type::Void, type::I32, type::I64>;
            using v_li = std::tuple<type::Void, type::I64, type::I32>;
            using v_ll = std::tuple<type::Void, type::I64, type::I64>;
            using v_v = std::tuple<type::Void>;
            using s_s = std::tuple<type::S128, type::S128>;
            using s_f = std::tuple<type::S128, type::F32>;
            using s_d = std::tuple<type::S128, type::F64>;
            using s_ss = std::tuple<type::S128, type::S128, type::S128>;
            using s_i = std::tuple<type::S128, type::I32>;
            using s_l = std::tuple<type::S128, type::I64>;
            using s_si = std::tuple<type::S128, type::S128, type::I32>;
            using i_s = std::tuple<type::I32, type::S128>;
            using v_is = std::tuple<type::Void, type::I32, type::S128>;
            using s_sss = std::tuple<type::S128, type::S128, type::S128, type::S128>;
            using s_is = std::tuple<type::S128, type::I32, type::S128>;

            using set = std::variant<
                _, d_d, d_dd, d_f, d_i, d_id, d_l, f_d, f_f, f_ff, f_i, f_if, f_l,
                i_a, i_ci, i_d, i_dd, i_f, i_ff, i_i, i_ii, i_iii, i_iil, i_ill,
                i_l, i_li, i_lii, i_lil, i_lll, i_ll, i_qq, i_v, l_d, l_f, l_i,
                l_il, l_ill, l_l, l_ll, l_lll, v_id, v_if, v_i, v_ii, v_iii,
                v_il, v_li, v_ll, v_v, s_s, s_f, s_d, s_ss, s_i, s_l, s_si,
                i_s, v_is, s_sss, s_is
            >;
        }

        inline constexpr auto _ = sig::_{};
        inline constexpr auto d_d = sig::d_dd{};
        inline constexpr auto d_dd = sig::d_dd{};
        inline constexpr auto d_f = sig::d_f{};
        inline constexpr auto d_i = sig::d_i{};
        inline constexpr auto d_id = sig::d_id{};
        inline constexpr auto d_l = sig::d_l{};
        inline constexpr auto f_d = sig::f_d{};
        inline constexpr auto f_f = sig::f_f{};
        inline constexpr auto f_ff = sig::f_ff{};
        inline constexpr auto f_i = sig::f_i{};
        inline constexpr auto f_if = sig::f_if{};
        inline constexpr auto f_l = sig::f_l{};
        inline constexpr auto i_a = sig::i_a{};
        inline constexpr auto i_ci = sig::i_ci{};
        inline constexpr auto i_d = sig::i_d{};
        inline constexpr auto i_dd = sig::i_dd{};
        inline constexpr auto i_f = sig::i_f{};
        inline constexpr auto i_ff = sig::i_ff{};
        inline constexpr auto i_i = sig::i_i{};
        inline constexpr auto i_ii = sig::i_ii{};
        inline constexpr auto i_iii = sig::i_iii{};
        inline constexpr auto i_iil = sig::i_iil{};
        inline constexpr auto i_ill = sig::i_ill{};
        inline constexpr auto i_l = sig::i_l{};
        inline constexpr auto i_li = sig::i_li{};
        inline constexpr auto i_lii = sig::i_lii{};
        inline constexpr auto i_lil = sig::i_lil{};
        inline constexpr auto i_lll = sig::i_lll{};
        inline constexpr auto i_ll = sig::i_ll{};
        inline constexpr auto i_qq = sig::i_qq{};
        inline constexpr auto i_v = sig::i_v{};
        inline constexpr auto l_d = sig::l_d{};
        inline constexpr auto l_f = sig::l_f{};
        inline constexpr auto l_i = sig::l_i{};
        inline constexpr auto l_il = sig::l_il{};
        inline constexpr auto l_ill = sig::l_ill{};
        inline constexpr auto l_l = sig::l_l{};
        inline constexpr auto l_ll = sig::l_ll{};
        inline constexpr auto l_lll = sig::l_lll{};
        inline constexpr auto v_id = sig::v_id{};
        inline constexpr auto v_if = sig::v_if{};
        inline constexpr auto v_i = sig::v_i{};
        inline constexpr auto v_ii = sig::v_ii{};
        inline constexpr auto v_iii = sig::v_iii{};
        inline constexpr auto v_il = sig::v_il{};
        inline constexpr auto v_li = sig::v_li{};
        inline constexpr auto v_ll = sig::v_ll{};
        inline constexpr auto v_v = sig::v_v{};
        inline constexpr auto s_s = sig::s_s{};
        inline constexpr auto s_f = sig::s_f{};
        inline constexpr auto s_d = sig::s_d{};
        inline constexpr auto s_ss = sig::s_ss{};
        inline constexpr auto s_i = sig::s_i{};
        inline constexpr auto s_l = sig::s_l{};
        inline constexpr auto s_si = sig::s_si{};
        inline constexpr auto i_s = sig::i_s{};
        inline constexpr auto v_is = sig::v_is{};
        inline constexpr auto s_sss = sig::s_sss{};
        inline constexpr auto s_is = sig::s_is{};

        struct opcode_entry
        {
            opcode opcode;
            encoding encoding;
            sig::set sig;
            std::string op;
            std::optional<sig::set> sig64 = {};

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
                // Control expressions and blocks.
                { opcode::Unreachable, op1{ 0x00 }, _, "unreachable" },
                { opcode::Nop, op1{ 0x01 }, _, "nop" },
                { opcode::Block, op1{ 0x02 }, _, "block" },
                { opcode::Loop, op1{ 0x03 }, _, "loop" },
                { opcode::If, op1{ 0x04 }, _, "if" },
                { opcode::Else, op1{ 0x05 }, _, "else" },
                { opcode::Try, op1{ 0x06 }, _, "try" },
                { opcode::Catch, op1{ 0x07 }, _, "catch" },
                { opcode::Throw, op1{ 0x08 }, _, "throw" },
                { opcode::Rethrow, op1{ 0x09 }, _, "rethrow" },
                { opcode::TryTable, op1{ 0x1F }, _, "try_table" },
                { opcode::ThrowRef, op1{ 0x0A }, _, "throw_ref" },
                { opcode::End, op1{ 0x0B }, _, "end" },
                { opcode::Br, op1{ 0x0C }, _, "br" },
                { opcode::BrIf, op1{ 0x0D }, _, "br_if" },
                { opcode::BrTable, op1{ 0x0E }, _, "br_table" },
                { opcode::Return, op1{ 0x0F }, _, "return" },
                { opcode::Delegate, op1{ 0x18 }, _, "delegate" },
                { opcode::CatchAll, op1{ 0x19 }, _, "catch_all" },
                { opcode::BrOnNull, op1{ 0xD5 }, _, "br_on_null" },
                { opcode::BrOnNonNull, op1{ 0xD6 }, _, "br_on_non_null" },
                { opcode::NopForTestingUnsupportedInLiftoff, op1{ 0x16 }, _, "nop_for_testing" },
                // Constants, locals, globals, calls, etc.
                { opcode::CallFunction, op1{ 0x10 }, _, "call" },
                { opcode::CallIndirect, op1{ 0x11 }, _, "call_indirect" },
                { opcode::ReturnCall, op1{ 0x12 }, _, "return_call" },
                { opcode::ReturnCallIndirect, op1{ 0x13 }, _, "return_call_indirect" },
                { opcode::CallRef, op1{ 0x14 }, _, "call_ref" },
                { opcode::ReturnCallRef, op1{ 0x15 }, _, "return_call_ref" },
                { opcode::Drop, op1{ 0x1A }, _, "drop" },
                { opcode::Select, op1{ 0x1B }, _, "select" },
                { opcode::SelectWithType, op1{ 0x1C }, _, "select" },
                { opcode::LocalGet, op1{ 0x20 }, _, "local.get" },
                { opcode::LocalSet, op1{ 0x21 }, _, "local.set" },
                { opcode::LocalTee, op1{ 0x22 }, _, "local.tee" },
                { opcode::GlobalGet, op1{ 0x23 }, _, "global.get" },
                { opcode::GlobalSet, op1{ 0x24 }, _, "global.set" },
                { opcode::TableGet, op1{ 0x25 }, _, "table.get" },
                { opcode::TableSet, op1{ 0x26 }, _, "table.set" },
                { opcode::I32Const, op1{ 0x41 }, _, "i32.const" },
                { opcode::I64Const, op1{ 0x42 }, _, "i64.const" },
                { opcode::F32Const, op1{ 0x43 }, _, "f32.const" },
                { opcode::F64Const, op1{ 0x44 }, _, "f64.const" },
                { opcode::RefNull, op1{ 0xD0 }, _, "ref.null" },
                { opcode::RefIsNull, op1{ 0xD1 }, _, "ref.is_null" },
                { opcode::RefFunc, op1{ 0xD2 }, _, "ref.func" },
                { opcode::RefAsNonNull, op1{ 0xD4 }, _, "ref.as_non_null" },
                { opcode::RefEq, op1{ 0xD3 }, _, "ref.eq" },
                // Load memory expressions.
                { opcode::I32LoadMem, op1{ 0x28 }, i_i, "i32.load" },
                { opcode::I64LoadMem, op1{ 0x29 }, l_i, "i64.load" },
                { opcode::F32LoadMem, op1{ 0x2A }, f_i, "f32.load" },
                { opcode::F64LoadMem, op1{ 0x2B }, d_i, "f64.load" },
                { opcode::I32LoadMem8S, op1{ 0x2C }, i_i, "i32.load8_s" },
                { opcode::I32LoadMem8U, op1{ 0x2D }, i_i, "i32.load8_u" },
                { opcode::I32LoadMem16S, op1{ 0x2E }, i_i, "i32.load16_s" },
                { opcode::I32LoadMem16U, op1{ 0x2F }, i_i, "i32.load16_u" },
                { opcode::I64LoadMem8S, op1{ 0x30 }, l_i, "i64.load8_s" },
                { opcode::I64LoadMem8U, op1{ 0x31 }, l_i, "i64.load8_u" },
                { opcode::I64LoadMem16S, op1{ 0x32 }, l_i, "i64.load16_s" },
                { opcode::I64LoadMem16U, op1{ 0x33 }, l_i, "i64.load16_u" },
                { opcode::I64LoadMem32S, op1{ 0x34 }, l_i, "i64.load32_s" },
                { opcode::I64LoadMem32U, op1{ 0x35 }, l_i, "i64.load32_u" },
                { opcode::F32LoadMemF16, op2{ 0xFC, LEB128(0x30) }, f_i, "f32.load_f16" },
                // Store memory expressions.
                { opcode::I32StoreMem, op1{ 0x36 }, v_ii, "i32.store" },
                { opcode::I64StoreMem, op1{ 0x37 }, v_il, "i64.store" },
                { opcode::F32StoreMem, op1{ 0x38 }, v_if, "f32.store" },
                { opcode::F64StoreMem, op1{ 0x39 }, v_id, "f64.store" },
                { opcode::I32StoreMem8, op1{ 0x3A }, v_ii, "i32.store8" },
                { opcode::I32StoreMem16, op1{ 0x3B }, v_ii, "i32.store16" },
                { opcode::I64StoreMem8, op1{ 0x3C }, v_il, "i64.store8" },
                { opcode::I64StoreMem16, op1{ 0x3D }, v_il, "i64.store16" },
                { opcode::I64StoreMem32, op1{ 0x3E }, v_il, "i64.store32" },
                { opcode::F32StoreMemF16, op2{ 0xFC, LEB128(0x31) }, v_if, "f32.store_f16" },
                // Miscellaneous memory expressions
                { opcode::MemorySize, op1{ 0x3F }, i_v, "memory.size" },
                { opcode::MemoryGrow, op1{ 0x40 }, i_i, "memory.grow" },
                // Expressions with signatures.
                // Opcodes that can also be used in constant expressions (via the 'extended
                // constant expressions' proposal.
                { opcode::I32Add, op1{ 0x6A }, i_ii, "i32.add" },
                { opcode::I32Sub, op1{ 0x6B }, i_ii, "i32.sub" },
                { opcode::I32Mul, op1{ 0x6C }, i_ii, "i32.mul" },
                { opcode::I64Add, op1{ 0x7C }, l_ll, "i64.add" },
                { opcode::I64Sub, op1{ 0x7D }, l_ll, "i64.sub" },
                { opcode::I64Mul, op1{ 0x7E }, l_ll, "i64.mul" },
                { opcode::I32Eqz, op1{ 0x45 }, i_i, "i32.eqz" },
                { opcode::I32Eq, op1{ 0x46 }, i_ii, "i32.eq" },
                { opcode::I32Ne, op1{ 0x47 }, i_ii, "i32.ne" },
                { opcode::I32LtS, op1{ 0x48 }, i_ii, "i32.lt_s" },
                { opcode::I32LtU, op1{ 0x49 }, i_ii, "i32.lt_u" },
                { opcode::I32GtS, op1{ 0x4A }, i_ii, "i32.gt_s" },
                { opcode::I32GtU, op1{ 0x4B }, i_ii, "i32.gt_u" },
                { opcode::I32LeS, op1{ 0x4C }, i_ii, "i32.le_s" },
                { opcode::I32LeU, op1{ 0x4D }, i_ii, "i32.le_u" },
                { opcode::I32GeS, op1{ 0x4E }, i_ii, "i32.ge_s" },
                { opcode::I32GeU, op1{ 0x4F }, i_ii, "i32.ge_u" },
                { opcode::I64Eqz, op1{ 0x50 }, i_l, "i64.eqz" },
                { opcode::I64Eq, op1{ 0x51 }, i_ll, "i64.eq" },
                { opcode::I64Ne, op1{ 0x52 }, i_ll, "i64.ne" },
                { opcode::I64LtS, op1{ 0x53 }, i_ll, "i64.lt_s" },
                { opcode::I64LtU, op1{ 0x54 }, i_ll, "i64.lt_u" },
                { opcode::I64GtS, op1{ 0x55 }, i_ll, "i64.gt_s" },
                { opcode::I64GtU, op1{ 0x56 }, i_ll, "i64.gt_u" },
                { opcode::I64LeS, op1{ 0x57 }, i_ll, "i64.le_s" },
                { opcode::I64LeU, op1{ 0x58 }, i_ll, "i64.le_u" },
                { opcode::I64GeS, op1{ 0x59 }, i_ll, "i64.ge_s" },
                { opcode::I64GeU, op1{ 0x5A }, i_ll, "i64.ge_u" },
                { opcode::F32Eq, op1{ 0x5B }, i_ff, "f32.eq" },
                { opcode::F32Ne, op1{ 0x5C }, i_ff, "f32.ne" },
                { opcode::F32Lt, op1{ 0x5D }, i_ff, "f32.lt" },
                { opcode::F32Gt, op1{ 0x5E }, i_ff, "f32.gt" },
                { opcode::F32Le, op1{ 0x5F }, i_ff, "f32.le" },
                { opcode::F32Ge, op1{ 0x60 }, i_ff, "f32.ge" },
                { opcode::F64Eq, op1{ 0x61 }, i_dd, "f64.eq" },
                { opcode::F64Ne, op1{ 0x62 }, i_dd, "f64.ne" },
                { opcode::F64Lt, op1{ 0x63 }, i_dd, "f64.lt" },
                { opcode::F64Gt, op1{ 0x64 }, i_dd, "f64.gt" },
                { opcode::F64Le, op1{ 0x65 }, i_dd, "f64.le" },
                { opcode::F64Ge, op1{ 0x66 }, i_dd, "f64.ge" },
                { opcode::I32Clz, op1{ 0x67 }, i_i, "i32.clz" },
                { opcode::I32Ctz, op1{ 0x68 }, i_i, "i32.ctz" },
                { opcode::I32Popcnt, op1{ 0x69 }, i_i, "i32.popcnt" },
                { opcode::I32DivS, op1{ 0x6D }, i_ii, "i32.div_s" },
                { opcode::I32DivU, op1{ 0x6E }, i_ii, "i32.div_u" },
                { opcode::I32RemS, op1{ 0x6F }, i_ii, "i32.rem_s" },
                { opcode::I32RemU, op1{ 0x70 }, i_ii, "i32.rem_u" },
                { opcode::I32And, op1{ 0x71 }, i_ii, "i32.and" },
                { opcode::I32Ior, op1{ 0x72 }, i_ii, "i32.or" },
                { opcode::I32Xor, op1{ 0x73 }, i_ii, "i32.xor" },
                { opcode::I32Shl, op1{ 0x74 }, i_ii, "i32.shl" },
                { opcode::I32ShrS, op1{ 0x75 }, i_ii, "i32.shr_s" },
                { opcode::I32ShrU, op1{ 0x76 }, i_ii, "i32.shr_u" },
                { opcode::I32Rol, op1{ 0x77 }, i_ii, "i32.rotl" },
                { opcode::I32Ror, op1{ 0x78 }, i_ii, "i32.rotr" },
                { opcode::I64Clz, op1{ 0x79 }, l_l, "i64.clz" },
                { opcode::I64Ctz, op1{ 0x7A }, l_l, "i64.ctz" },
                { opcode::I64Popcnt, op1{ 0x7B }, l_l, "i64.popcnt" },
                { opcode::I64DivS, op1{ 0x7F }, l_ll, "i64.div_s" },
                { opcode::I64DivU, op1{ 0x80 }, l_ll, "i64.div_u" },
                { opcode::I64RemS, op1{ 0x81 }, l_ll, "i64.rem_s" },
                { opcode::I64RemU, op1{ 0x82 }, l_ll, "i64.rem_u" },
                { opcode::I64And, op1{ 0x83 }, l_ll, "i64.and" },
                { opcode::I64Ior, op1{ 0x84 }, l_ll, "i64.or" },
                { opcode::I64Xor, op1{ 0x85 }, l_ll, "i64.xor" },
                { opcode::I64Shl, op1{ 0x86 }, l_ll, "i64.shl" },
                { opcode::I64ShrS, op1{ 0x87 }, l_ll, "i64.shr_s" },
                { opcode::I64ShrU, op1{ 0x88 }, l_ll, "i64.shr_u" },
                { opcode::I64Rol, op1{ 0x89 }, l_ll, "i64.rotl" },
                { opcode::I64Ror, op1{ 0x8A }, l_ll, "i64.rotr" },
                { opcode::F32Abs, op1{ 0x8B }, f_f, "f32.abs" },
                { opcode::F32Neg, op1{ 0x8C }, f_f, "f32.neg" },
                { opcode::F32Ceil, op1{ 0x8D }, f_f, "f32.ceil" },
                { opcode::F32Floor, op1{ 0x8E }, f_f, "f32.floor" },
                { opcode::F32Trunc, op1{ 0x8F }, f_f, "f32.trunc" },
                { opcode::F32NearestInt, op1{ 0x90 }, f_f, "f32.nearest" },
                { opcode::F32Sqrt, op1{ 0x91 }, f_f, "f32.sqrt" },
                { opcode::F32Add, op1{ 0x92 }, f_ff, "f32.add" },
                { opcode::F32Sub, op1{ 0x93 }, f_ff, "f32.sub" },
                { opcode::F32Mul, op1{ 0x94 }, f_ff, "f32.mul" },
                { opcode::F32Div, op1{ 0x95 }, f_ff, "f32.div" },
                { opcode::F32Min, op1{ 0x96 }, f_ff, "f32.min" },
                { opcode::F32Max, op1{ 0x97 }, f_ff, "f32.max" },
                { opcode::F32CopySign, op1{ 0x98 }, f_ff, "f32.copysign" },
                { opcode::F64Abs, op1{ 0x99 }, d_d, "f64.abs" },
                { opcode::F64Neg, op1{ 0x9A }, d_d, "f64.neg" },
                { opcode::F64Ceil, op1{ 0x9B }, d_d, "f64.ceil" },
                { opcode::F64Floor, op1{ 0x9C }, d_d, "f64.floor" },
                { opcode::F64Trunc, op1{ 0x9D }, d_d, "f64.trunc" },
                { opcode::F64NearestInt, op1{ 0x9E }, d_d, "f64.nearest" },
                { opcode::F64Sqrt, op1{ 0x9F }, d_d, "f64.sqrt" },
                { opcode::F64Add, op1{ 0xA0 }, d_dd, "f64.add" },
                { opcode::F64Sub, op1{ 0xA1 }, d_dd, "f64.sub" },
                { opcode::F64Mul, op1{ 0xA2 }, d_dd, "f64.mul" },
                { opcode::F64Div, op1{ 0xA3 }, d_dd, "f64.div" },
                { opcode::F64Min, op1{ 0xA4 }, d_dd, "f64.min" },
                { opcode::F64Max, op1{ 0xA5 }, d_dd, "f64.max" },
                { opcode::F64CopySign, op1{ 0xA6 }, d_dd, "f64.copysign" },
                { opcode::I32ConvertI64, op1{ 0xA7 }, i_l, "i32.wrap_i64" },
                { opcode::I32SConvertF32, op1{ 0xA8 }, i_f, "i32.trunc_f32_s" },
                { opcode::I32UConvertF32, op1{ 0xA9 }, i_f, "i32.trunc_f32_u" },
                { opcode::I32SConvertF64, op1{ 0xAA }, i_d, "i32.trunc_f64_s" },
                { opcode::I32UConvertF64, op1{ 0xAB }, i_d, "i32.trunc_f64_u" },
                { opcode::I64SConvertI32, op1{ 0xAC }, l_i, "i64.extend_i32_s" },
                { opcode::I64UConvertI32, op1{ 0xAD }, l_i, "i64.extend_i32_u" },
                { opcode::I64SConvertF32, op1{ 0xAE }, l_f, "i64.trunc_f32_s" },
                { opcode::I64UConvertF32, op1{ 0xAF }, l_f, "i64.trunc_f32_u" },
                { opcode::I64SConvertF64, op1{ 0xB0 }, l_d, "i64.trunc_f64_s" },
                { opcode::I64UConvertF64, op1{ 0xB1 }, l_d, "i64.trunc_f64_u" },
                { opcode::F32SConvertI32, op1{ 0xB2 }, f_i, "f32.convert_i32_s" },
                { opcode::F32UConvertI32, op1{ 0xB3 }, f_i, "f32.convert_i32_u" },
                { opcode::F32SConvertI64, op1{ 0xB4 }, f_l, "f32.convert_i64_s" },
                { opcode::F32UConvertI64, op1{ 0xB5 }, f_l, "f32.convert_i64_u" },
                { opcode::F32ConvertF64, op1{ 0xB6 }, f_d, "f32.demote_f64" },
                { opcode::F64SConvertI32, op1{ 0xB7 }, d_i, "f64.convert_i32_s" },
                { opcode::F64UConvertI32, op1{ 0xB8 }, d_i, "f64.convert_i32_u" },
                { opcode::F64SConvertI64, op1{ 0xB9 }, d_l, "f64.convert_i64_s" },
                { opcode::F64UConvertI64, op1{ 0xBA }, d_l, "f64.convert_i64_u" },
                { opcode::F64ConvertF32, op1{ 0xBB }, d_f, "f64.promote_f32" },
                { opcode::I32ReinterpretF32, op1{ 0xBC }, i_f, "i32.reinterpret_f32" },
                { opcode::I64ReinterpretF64, op1{ 0xBD }, l_d, "i64.reinterpret_f64" },
                { opcode::F32ReinterpretI32, op1{ 0xBE }, f_i, "f32.reinterpret_i32" },
                { opcode::F64ReinterpretI64, op1{ 0xBF }, d_l, "f64.reinterpret_i64" },
                { opcode::I32SExtendI8, op1{ 0xC0 }, i_i, "i32.extend8_s" },
                { opcode::I32SExtendI16, op1{ 0xC1 }, i_i, "i32.extend16_s" },
                { opcode::I64SExtendI8, op1{ 0xC2 }, l_l, "i64.extend8_s" },
                { opcode::I64SExtendI16, op1{ 0xC3 }, l_l, "i64.extend16_s" },
                { opcode::I64SExtendI32, op1{ 0xC4 }, l_l, "i64.extend32_s" },
                { opcode::ContNew, op1{ 0xE0 }, _, "cont.new" },
                { opcode::ContBind, op1{ 0xE1 }, _, "cont.bind" },
                { opcode::Suspend, op1{ 0xE2 }, _, "suspend" },
                { opcode::Resume, op1{ 0xE3 }, _, "resume" },
                { opcode::ResumeThrow, op1{ 0xE4 }, _, "resume_throw" },
                { opcode::Switch, op1{ 0xE5 }, _, "switch" },
                { opcode::S128LoadMem, op2{ 0xFD, LEB128(0x00) }, s_i, "v128.load" },
                { opcode::S128Load8x8S, op2{ 0xFD, LEB128(0x01) }, s_i, "v128.load8x8_s" },
                { opcode::S128Load8x8U, op2{ 0xFD, LEB128(0x02) }, s_i, "v128.load8x8_u" },
                { opcode::S128Load16x4S, op2{ 0xFD, LEB128(0x03) }, s_i, "v128.load16x4_s" },
                { opcode::S128Load16x4U, op2{ 0xFD, LEB128(0x04) }, s_i, "v128.load16x4_u" },
                { opcode::S128Load32x2S, op2{ 0xFD, LEB128(0x05) }, s_i, "v128.load32x2_s" },
                { opcode::S128Load32x2U, op2{ 0xFD, LEB128(0x06) }, s_i, "v128.load32x2_u" },
                { opcode::S128Load8Splat, op2{ 0xFD, LEB128(0x07) }, s_i, "v128.load8_splat" },
                { opcode::S128Load16Splat, op2{ 0xFD, LEB128(0x08) }, s_i, "v128.load16_splat" },
                { opcode::S128Load32Splat, op2{ 0xFD, LEB128(0x09) }, s_i, "v128.load32_splat" },
                { opcode::S128Load64Splat, op2{ 0xFD, LEB128(0x0A) }, s_i, "v128.load64_splat" },
                { opcode::S128StoreMem, op2{ 0xFD, LEB128(0x0B) }, v_is, "v128.store" },
                { opcode::S128Load32Zero, op2{ 0xFD, LEB128(0x5C) }, s_i, "v128.load32_zero" },
                { opcode::S128Load64Zero, op2{ 0xFD, LEB128(0x5D) }, s_i, "v128.load64_zero" },
                { opcode::S128Load8Lane, op2{ 0xFD, LEB128(0x54) }, s_is, "v128.load8_lane" },
                { opcode::S128Load16Lane, op2{ 0xFD, LEB128(0x55) }, s_is, "v128.load16_lane" },
                { opcode::S128Load32Lane, op2{ 0xFD, LEB128(0x56) }, s_is, "v128.load32_lane" },
                { opcode::S128Load64Lane, op2{ 0xFD, LEB128(0x57) }, s_is, "v128.load64_lane" },
                { opcode::S128Store8Lane, op2{ 0xFD, LEB128(0x58) }, v_is, "v128.store8_lane" },
                { opcode::S128Store16Lane, op2{ 0xFD, LEB128(0x59) }, v_is, "v128.store16_lane" },
                { opcode::S128Store32Lane, op2{ 0xFD, LEB128(0x5A) }, v_is, "v128.store32_lane" },
                { opcode::S128Store64Lane, op2{ 0xFD, LEB128(0x5B) }, v_is, "v128.store64_lane" },
                { opcode::S128Const, op2{ 0xFD, LEB128(0x0C) }, _, "v128.const" },
                { opcode::I8x16Shuffle, op2{ 0xFD, LEB128(0x0D) }, s_ss, "i8x16.shuffle" },
                { opcode::I8x16Swizzle, op2{ 0xFD, LEB128(0x0E) }, s_ss, "i8x16.swizzle" },
                { opcode::I8x16Splat, op2{ 0xFD, LEB128(0x0F) }, s_i, "i8x16.splat" },
                { opcode::I16x8Splat, op2{ 0xFD, LEB128(0x10) }, s_i, "i16x8.splat" },
                { opcode::I32x4Splat, op2{ 0xFD, LEB128(0x11) }, s_i, "i32x4.splat" },
                { opcode::I64x2Splat, op2{ 0xFD, LEB128(0x12) }, s_l, "i64x2.splat" },
                { opcode::F32x4Splat, op2{ 0xFD, LEB128(0x13) }, s_f, "f32x4.splat" },
                { opcode::F64x2Splat, op2{ 0xFD, LEB128(0x14) }, s_d, "f64x2.splat" },
                { opcode::I8x16Eq, op2{ 0xFD, LEB128(0x23) }, s_ss, "i8x16.eq" },
                { opcode::I8x16Ne, op2{ 0xFD, LEB128(0x24) }, s_ss, "i8x16.ne" },
                { opcode::I8x16LtS, op2{ 0xFD, LEB128(0x25) }, s_ss, "i8x16.lt_s" },
                { opcode::I8x16LtU, op2{ 0xFD, LEB128(0x26) }, s_ss, "i8x16.lt_u" },
                { opcode::I8x16GtS, op2{ 0xFD, LEB128(0x27) }, s_ss, "i8x16.gt_s" },
                { opcode::I8x16GtU, op2{ 0xFD, LEB128(0x28) }, s_ss, "i8x16.gt_u" },
                { opcode::I8x16LeS, op2{ 0xFD, LEB128(0x29) }, s_ss, "i8x16.le_s" },
                { opcode::I8x16LeU, op2{ 0xFD, LEB128(0x2A) }, s_ss, "i8x16.le_u" },
                { opcode::I8x16GeS, op2{ 0xFD, LEB128(0x2B) }, s_ss, "i8x16.ge_s" },
                { opcode::I8x16GeU, op2{ 0xFD, LEB128(0x2C) }, s_ss, "i8x16.ge_u" },
                { opcode::I16x8Eq, op2{ 0xFD, LEB128(0x2D) }, s_ss, "i16x8.eq" },
                { opcode::I16x8Ne, op2{ 0xFD, LEB128(0x2E) }, s_ss, "i16x8.ne" },
                { opcode::I16x8LtS, op2{ 0xFD, LEB128(0x2F) }, s_ss, "i16x8.lt_s" },
                { opcode::I16x8LtU, op2{ 0xFD, LEB128(0x30) }, s_ss, "i16x8.lt_u" },
                { opcode::I16x8GtS, op2{ 0xFD, LEB128(0x31) }, s_ss, "i16x8.gt_s" },
                { opcode::I16x8GtU, op2{ 0xFD, LEB128(0x32) }, s_ss, "i16x8.gt_u" },
                { opcode::I16x8LeS, op2{ 0xFD, LEB128(0x33) }, s_ss, "i16x8.le_s" },
                { opcode::I16x8LeU, op2{ 0xFD, LEB128(0x34) }, s_ss, "i16x8.le_u" },
                { opcode::I16x8GeS, op2{ 0xFD, LEB128(0x35) }, s_ss, "i16x8.ge_s" },
                { opcode::I16x8GeU, op2{ 0xFD, LEB128(0x36) }, s_ss, "i16x8.ge_u" },
                { opcode::I32x4Eq, op2{ 0xFD, LEB128(0x37) }, s_ss, "i32x4.eq" },
                { opcode::I32x4Ne, op2{ 0xFD, LEB128(0x38) }, s_ss, "i32x4.ne" },
                { opcode::I32x4LtS, op2{ 0xFD, LEB128(0x39) }, s_ss, "i32x4.lt_s" },
                { opcode::I32x4LtU, op2{ 0xFD, LEB128(0x3A) }, s_ss, "i32x4.lt_u" },
                { opcode::I32x4GtS, op2{ 0xFD, LEB128(0x3B) }, s_ss, "i32x4.gt_s" },
                { opcode::I32x4GtU, op2{ 0xFD, LEB128(0x3C) }, s_ss, "i32x4.gt_u" },
                { opcode::I32x4LeS, op2{ 0xFD, LEB128(0x3D) }, s_ss, "i32x4.le_s" },
                { opcode::I32x4LeU, op2{ 0xFD, LEB128(0x3E) }, s_ss, "i32x4.le_u" },
                { opcode::I32x4GeS, op2{ 0xFD, LEB128(0x3F) }, s_ss, "i32x4.ge_s" },
                { opcode::I32x4GeU, op2{ 0xFD, LEB128(0x40) }, s_ss, "i32x4.ge_u" },
                { opcode::F32x4Eq, op2{ 0xFD, LEB128(0x41) }, s_ss, "f32x4.eq" },
                { opcode::F32x4Ne, op2{ 0xFD, LEB128(0x42) }, s_ss, "f32x4.ne" },
                { opcode::F32x4Lt, op2{ 0xFD, LEB128(0x43) }, s_ss, "f32x4.lt" },
                { opcode::F32x4Gt, op2{ 0xFD, LEB128(0x44) }, s_ss, "f32x4.gt" },
                { opcode::F32x4Le, op2{ 0xFD, LEB128(0x45) }, s_ss, "f32x4.le" },
                { opcode::F32x4Ge, op2{ 0xFD, LEB128(0x46) }, s_ss, "f32x4.ge" },
                { opcode::F64x2Eq, op2{ 0xFD, LEB128(0x47) }, s_ss, "f64x2.eq" },
                { opcode::F64x2Ne, op2{ 0xFD, LEB128(0x48) }, s_ss, "f64x2.ne" },
                { opcode::F64x2Lt, op2{ 0xFD, LEB128(0x49) }, s_ss, "f64x2.lt" },
                { opcode::F64x2Gt, op2{ 0xFD, LEB128(0x4A) }, s_ss, "f64x2.gt" },
                { opcode::F64x2Le, op2{ 0xFD, LEB128(0x4B) }, s_ss, "f64x2.le" },
                { opcode::F64x2Ge, op2{ 0xFD, LEB128(0x4C) }, s_ss, "f64x2.ge" },
                { opcode::S128Not, op2{ 0xFD, LEB128(0x4D) }, s_s, "v128.not" },
                { opcode::S128And, op2{ 0xFD, LEB128(0x4E) }, s_ss, "v128.and" },
                { opcode::S128AndNot, op2{ 0xFD, LEB128(0x4F) }, s_ss, "v128.andnot" },
                { opcode::S128Or, op2{ 0xFD, LEB128(0x50) }, s_ss, "v128.or" },
                { opcode::S128Xor, op2{ 0xFD, LEB128(0x51) }, s_ss, "v128.xor" },
                { opcode::S128Select, op2{ 0xFD, LEB128(0x52) }, s_sss, "v128.bitselect" },
                { opcode::V128AnyTrue, op2{ 0xFD, LEB128(0x53) }, i_s, "v128.any_true" },
                { opcode::F32x4DemoteF64x2Zero, op2{ 0xFD, LEB128(0x5E) }, s_s, "f32x4.demote_f64x2_zero" },
                { opcode::F64x2PromoteLowF32x4, op2{ 0xFD, LEB128(0x5F) }, s_s, "f64x2.promote_low_f32x4" },
                { opcode::I8x16Abs, op2{ 0xFD, LEB128(0x60) }, s_s, "i8x16.abs" },
                { opcode::I8x16Neg, op2{ 0xFD, LEB128(0x61) }, s_s, "i8x16.neg" },
                { opcode::I8x16Popcnt, op2{ 0xFD, LEB128(0x62) }, s_s, "i8x16.popcnt" },
                { opcode::I8x16AllTrue, op2{ 0xFD, LEB128(0x63) }, i_s, "i8x16.all_true" },
                { opcode::I8x16BitMask, op2{ 0xFD, LEB128(0x64) }, i_s, "i8x16.bitmask" },
                { opcode::I8x16SConvertI16x8, op2{ 0xFD, LEB128(0x65) }, s_ss, "i8x16.narrow_i16x8_s" },
                { opcode::I8x16UConvertI16x8, op2{ 0xFD, LEB128(0x66) }, s_ss, "i8x16.narrow_i16x8_u" },
                { opcode::F32x4Ceil, op2{ 0xFD, LEB128(0x67) }, s_s, "f32x4.ceil" },
                { opcode::F32x4Floor, op2{ 0xFD, LEB128(0x68) }, s_s, "f32x4.floor" },
                { opcode::F32x4Trunc, op2{ 0xFD, LEB128(0x69) }, s_s, "f32x4.trunc" },
                { opcode::F32x4NearestInt, op2{ 0xFD, LEB128(0x6A) }, s_s, "f32x4.nearest" },
                { opcode::I8x16Shl, op2{ 0xFD, LEB128(0x6B) }, s_si, "i8x16.shl" },
                { opcode::I8x16ShrS, op2{ 0xFD, LEB128(0x6C) }, s_si, "i8x16.shr_s" },
                { opcode::I8x16ShrU, op2{ 0xFD, LEB128(0x6D) }, s_si, "i8x16.shr_u" },
                { opcode::I8x16Add, op2{ 0xFD, LEB128(0x6E) }, s_ss, "i8x16.add" },
                { opcode::I8x16AddSatS, op2{ 0xFD, LEB128(0x6F) }, s_ss, "i8x16.add_sat_s" },
                { opcode::I8x16AddSatU, op2{ 0xFD, LEB128(0x70) }, s_ss, "i8x16.add_sat_u" },
                { opcode::I8x16Sub, op2{ 0xFD, LEB128(0x71) }, s_ss, "i8x16.sub" },
                { opcode::I8x16SubSatS, op2{ 0xFD, LEB128(0x72) }, s_ss, "i8x16.sub_sat_s" },
                { opcode::I8x16SubSatU, op2{ 0xFD, LEB128(0x73) }, s_ss, "i8x16.sub_sat_u" },
                { opcode::F64x2Ceil, op2{ 0xFD, LEB128(0x74) }, s_s, "f64x2.ceil" },
                { opcode::F64x2Floor, op2{ 0xFD, LEB128(0x75) }, s_s, "f64x2.floor" },
                { opcode::I8x16MinS, op2{ 0xFD, LEB128(0x76) }, s_ss, "i8x16.min_s" },
                { opcode::I8x16MinU, op2{ 0xFD, LEB128(0x77) }, s_ss, "i8x16.min_u" },
                { opcode::I8x16MaxS, op2{ 0xFD, LEB128(0x78) }, s_ss, "i8x16.max_s" },
                { opcode::I8x16MaxU, op2{ 0xFD, LEB128(0x79) }, s_ss, "i8x16.max_u" },
                { opcode::F64x2Trunc, op2{ 0xFD, LEB128(0x7A) }, s_s, "f64x2.trunc" },
                { opcode::I8x16RoundingAverageU, op2{ 0xFD, LEB128(0x7B) }, s_ss, "i8x16.avgr_u" },
                { opcode::I16x8ExtAddPairwiseI8x16S, op2{ 0xFD, LEB128(0x7C) }, s_s, "i16x8.extadd_pairwise_i8x16_s" },
                { opcode::I16x8ExtAddPairwiseI8x16U, op2{ 0xFD, LEB128(0x7D) }, s_s, "i16x8.extadd_pairwise_i8x16_u" },
                { opcode::I32x4ExtAddPairwiseI16x8S, op2{ 0xFD, LEB128(0x7E) }, s_s, "i32x4.extadd_pairwise_i16x8_s" },
                { opcode::I32x4ExtAddPairwiseI16x8U, op2{ 0xFD, LEB128(0x7F) }, s_s, "i32x4.extadd_pairwise_i16x8_u" },
                { opcode::I16x8Abs, op2{ 0xFD, LEB128(0x80) }, s_s, "i16x8.abs" },
                { opcode::I16x8Neg, op2{ 0xFD, LEB128(0x81) }, s_s, "i16x8.neg" },
                { opcode::I16x8Q15MulRSatS, op2{ 0xFD, LEB128(0x82) }, s_ss, "i16x8.q15mulr_sat_s" },
                { opcode::I16x8AllTrue, op2{ 0xFD, LEB128(0x83) }, i_s, "i16x8.all_true" },
                { opcode::I16x8BitMask, op2{ 0xFD, LEB128(0x84) }, i_s, "i16x8.bitmask" },
                { opcode::I16x8SConvertI32x4, op2{ 0xFD, LEB128(0x85) }, s_ss, "i16x8.narrow_i32x4_s" },
                { opcode::I16x8UConvertI32x4, op2{ 0xFD, LEB128(0x86) }, s_ss, "i16x8.narrow_i32x4_u" },
                { opcode::I16x8SConvertI8x16Low, op2{ 0xFD, LEB128(0x87) }, s_s, "i16x8.extend_low_i8x16_s" },
                { opcode::I16x8SConvertI8x16High, op2{ 0xFD, LEB128(0x88) }, s_s, "i16x8.extend_high_i8x16_s" },
                { opcode::I16x8UConvertI8x16Low, op2{ 0xFD, LEB128(0x89) }, s_s, "i16x8.extend_low_i8x16_u" },
                { opcode::I16x8UConvertI8x16High, op2{ 0xFD, LEB128(0x8A) }, s_s, "i16x8.extend_high_i8x16_u" },
                { opcode::I16x8Shl, op2{ 0xFD, LEB128(0x8B) }, s_si, "i16x8.shl" },
                { opcode::I16x8ShrS, op2{ 0xFD, LEB128(0x8C) }, s_si, "i16x8.shr_s" },
                { opcode::I16x8ShrU, op2{ 0xFD, LEB128(0x8D) }, s_si, "i16x8.shr_u" },
                { opcode::I16x8Add, op2{ 0xFD, LEB128(0x8E) }, s_ss, "i16x8.add" },
                { opcode::I16x8AddSatS, op2{ 0xFD, LEB128(0x8F) }, s_ss, "i16x8.add_sat_s" },
                { opcode::I16x8AddSatU, op2{ 0xFD, LEB128(0x90) }, s_ss, "i16x8.add_sat_u" },
                { opcode::I16x8Sub, op2{ 0xFD, LEB128(0x91) }, s_ss, "i16x8.sub" },
                { opcode::I16x8SubSatS, op2{ 0xFD, LEB128(0x92) }, s_ss, "i16x8.sub_sat_s" },
                { opcode::I16x8SubSatU, op2{ 0xFD, LEB128(0x93) }, s_ss, "i16x8.sub_sat_u" },
                { opcode::F64x2NearestInt, op2{ 0xFD, LEB128(0x94) }, s_s, "f64x2.nearest" },
                { opcode::I16x8Mul, op2{ 0xFD, LEB128(0x95) }, s_ss, "i16x8.mul" },
                { opcode::I16x8MinS, op2{ 0xFD, LEB128(0x96) }, s_ss, "i16x8.min_s" },
                { opcode::I16x8MinU, op2{ 0xFD, LEB128(0x97) }, s_ss, "i16x8.min_u" },
                { opcode::I16x8MaxS, op2{ 0xFD, LEB128(0x98) }, s_ss, "i16x8.max_s" },
                { opcode::I16x8MaxU, op2{ 0xFD, LEB128(0x99) }, s_ss, "i16x8.max_u" },
                { opcode::I16x8RoundingAverageU, op2{ 0xFD, LEB128(0x9B) }, s_ss, "i16x8.avgr_u" },
                { opcode::I16x8ExtMulLowI8x16S, op2{ 0xFD, LEB128(0x9C) }, s_ss, "i16x8.extmul_low_i8x16_s" },
                { opcode::I16x8ExtMulHighI8x16S, op2{ 0xFD, LEB128(0x9D) }, s_ss, "i16x8.extmul_high_i8x16_s" },
                { opcode::I16x8ExtMulLowI8x16U, op2{ 0xFD, LEB128(0x9E) }, s_ss, "i16x8.extmul_low_i8x16_u" },
                { opcode::I16x8ExtMulHighI8x16U, op2{ 0xFD, LEB128(0x9F) }, s_ss, "i16x8.extmul_high_i8x16_u" },
                { opcode::I32x4Abs, op2{ 0xFD, LEB128(0xA0) }, s_s, "i32x4.abs" },
                { opcode::I32x4Neg, op2{ 0xFD, LEB128(0xA1) }, s_s, "i32x4.neg" },
                { opcode::I32x4AllTrue, op2{ 0xFD, LEB128(0xA3) }, i_s, "i32x4.all_true" },
                { opcode::I32x4BitMask, op2{ 0xFD, LEB128(0xA4) }, i_s, "i32x4.bitmask" },
                { opcode::I32x4SConvertI16x8Low, op2{ 0xFD, LEB128(0xA7) }, s_s, "i32x4.extend_low_i16x8_s" },
                { opcode::I32x4SConvertI16x8High, op2{ 0xFD, LEB128(0xA8) }, s_s, "i32x4.extend_high_i16x8_s" },
                { opcode::I32x4UConvertI16x8Low, op2{ 0xFD, LEB128(0xA9) }, s_s, "i32x4.extend_low_i16x8_u" },
                { opcode::I32x4UConvertI16x8High, op2{ 0xFD, LEB128(0xAA) }, s_s, "i32x4.extend_high_i16x8_u" },
                { opcode::I32x4Shl, op2{ 0xFD, LEB128(0xAB) }, s_si, "i32x4.shl" },
                { opcode::I32x4ShrS, op2{ 0xFD, LEB128(0xAC) }, s_si, "i32x4.shr_s" },
                { opcode::I32x4ShrU, op2{ 0xFD, LEB128(0xAD) }, s_si, "i32x4.shr_u" },
                { opcode::I32x4Add, op2{ 0xFD, LEB128(0xAE) }, s_ss, "i32x4.add" },
                { opcode::I32x4Sub, op2{ 0xFD, LEB128(0xB1) }, s_ss, "i32x4.sub" },
                { opcode::I32x4Mul, op2{ 0xFD, LEB128(0xB5) }, s_ss, "i32x4.mul" },
                { opcode::I32x4MinS, op2{ 0xFD, LEB128(0xB6) }, s_ss, "i32x4.min_s" },
                { opcode::I32x4MinU, op2{ 0xFD, LEB128(0xB7) }, s_ss, "i32x4.min_u" },
                { opcode::I32x4MaxS, op2{ 0xFD, LEB128(0xB8) }, s_ss, "i32x4.max_s" },
                { opcode::I32x4MaxU, op2{ 0xFD, LEB128(0xB9) }, s_ss, "i32x4.max_u" },
                { opcode::I32x4DotI16x8S, op2{ 0xFD, LEB128(0xBA) }, s_ss, "i32x4.dot_i16x8_s" },
                { opcode::I32x4ExtMulLowI16x8S, op2{ 0xFD, LEB128(0xBC) }, s_ss, "i32x4.extmul_low_i16x8_s" },
                { opcode::I32x4ExtMulHighI16x8S, op2{ 0xFD, LEB128(0xBD) }, s_ss, "i32x4.extmul_high_i16x8_s" },
                { opcode::I32x4ExtMulLowI16x8U, op2{ 0xFD, LEB128(0xBE) }, s_ss, "i32x4.extmul_low_i16x8_u" },
                { opcode::I32x4ExtMulHighI16x8U, op2{ 0xFD, LEB128(0xBF) }, s_ss, "i32x4.extmul_high_i16x8_u" },
                { opcode::I64x2Abs, op2{ 0xFD, LEB128(0xC0) }, s_s, "i64x2.abs" },
                { opcode::I64x2Neg, op2{ 0xFD, LEB128(0xC1) }, s_s, "i64x2.neg" },
                { opcode::I64x2AllTrue, op2{ 0xFD, LEB128(0xC3) }, i_s, "i64x2.all_true" },
                { opcode::I64x2BitMask, op2{ 0xFD, LEB128(0xC4) }, i_s, "i64x2.bitmask" },
                { opcode::I64x2SConvertI32x4Low, op2{ 0xFD, LEB128(0xC7) }, s_s, "i64x2.extend_low_i32x4_s" },
                { opcode::I64x2SConvertI32x4High, op2{ 0xFD, LEB128(0xC8) }, s_s, "i64x2.extend_high_i32x4_s" },
                { opcode::I64x2UConvertI32x4Low, op2{ 0xFD, LEB128(0xC9) }, s_s, "i64x2.extend_low_i32x4_u" },
                { opcode::I64x2UConvertI32x4High, op2{ 0xFD, LEB128(0xCA) }, s_s, "i64x2.extend_high_i32x4_u" },
                { opcode::I64x2Shl, op2{ 0xFD, LEB128(0xCB) }, s_si, "i64x2.shl" },
                { opcode::I64x2ShrS, op2{ 0xFD, LEB128(0xCC) }, s_si, "i64x2.shr_s" },
                { opcode::I64x2ShrU, op2{ 0xFD, LEB128(0xCD) }, s_si, "i64x2.shr_u" },
                { opcode::I64x2Add, op2{ 0xFD, LEB128(0xCE) }, s_ss, "i64x2.add" },
                { opcode::I64x2Sub, op2{ 0xFD, LEB128(0xD1) }, s_ss, "i64x2.sub" },
                { opcode::I64x2Mul, op2{ 0xFD, LEB128(0xD5) }, s_ss, "i64x2.mul" },
                { opcode::I64x2Eq, op2{ 0xFD, LEB128(0xD6) }, s_ss, "i64x2.eq" },
                { opcode::I64x2Ne, op2{ 0xFD, LEB128(0xD7) }, s_ss, "i64x2.ne" },
                { opcode::I64x2LtS, op2{ 0xFD, LEB128(0xD8) }, s_ss, "i64x2.lt_s" },
                { opcode::I64x2GtS, op2{ 0xFD, LEB128(0xD9) }, s_ss, "i64x2.gt_s" },
                { opcode::I64x2LeS, op2{ 0xFD, LEB128(0xDA) }, s_ss, "i64x2.le_s" },
                { opcode::I64x2GeS, op2{ 0xFD, LEB128(0xDB) }, s_ss, "i64x2.ge_s" },
                { opcode::I64x2ExtMulLowI32x4S, op2{ 0xFD, LEB128(0xDC) }, s_ss, "i64x2.extmul_low_i32x4_s" },
                { opcode::I64x2ExtMulHighI32x4S, op2{ 0xFD, LEB128(0xDD) }, s_ss, "i64x2.extmul_high_i32x4_s" },
                { opcode::I64x2ExtMulLowI32x4U, op2{ 0xFD, LEB128(0xDE) }, s_ss, "i64x2.extmul_low_i32x4_u" },
                { opcode::I64x2ExtMulHighI32x4U, op2{ 0xFD, LEB128(0xDF) }, s_ss, "i64x2.extmul_high_i32x4_u" },
                { opcode::F32x4Abs, op2{ 0xFD, LEB128(0xE0) }, s_s, "f32x4.abs" },
                { opcode::F32x4Neg, op2{ 0xFD, LEB128(0xE1) }, s_s, "f32x4.neg" },
                { opcode::F32x4Sqrt, op2{ 0xFD, LEB128(0xE3) }, s_s, "f32x4.sqrt" },
                { opcode::F32x4Add, op2{ 0xFD, LEB128(0xE4) }, s_ss, "f32x4.add" },
                { opcode::F32x4Sub, op2{ 0xFD, LEB128(0xE5) }, s_ss, "f32x4.sub" },
                { opcode::F32x4Mul, op2{ 0xFD, LEB128(0xE6) }, s_ss, "f32x4.mul" },
                { opcode::F32x4Div, op2{ 0xFD, LEB128(0xE7) }, s_ss, "f32x4.div" },
                { opcode::F32x4Min, op2{ 0xFD, LEB128(0xE8) }, s_ss, "f32x4.min" },
                { opcode::F32x4Max, op2{ 0xFD, LEB128(0xE9) }, s_ss, "f32x4.max" },
                { opcode::F32x4Pmin, op2{ 0xFD, LEB128(0xEA) }, s_ss, "f32x4.pmin" },
                { opcode::F32x4Pmax, op2{ 0xFD, LEB128(0xEB) }, s_ss, "f32x4.pmax" },
                { opcode::F64x2Abs, op2{ 0xFD, LEB128(0xEC) }, s_s, "f64x2.abs" },
                { opcode::F64x2Neg, op2{ 0xFD, LEB128(0xED) }, s_s, "f64x2.neg" },
                { opcode::F64x2Sqrt, op2{ 0xFD, LEB128(0xEF) }, s_s, "f64x2.sqrt" },
                { opcode::F64x2Add, op2{ 0xFD, LEB128(0xF0) }, s_ss, "f64x2.add" },
                { opcode::F64x2Sub, op2{ 0xFD, LEB128(0xF1) }, s_ss, "f64x2.sub" },
                { opcode::F64x2Mul, op2{ 0xFD, LEB128(0xF2) }, s_ss, "f64x2.mul" },
                { opcode::F64x2Div, op2{ 0xFD, LEB128(0xF3) }, s_ss, "f64x2.div" },
                { opcode::F64x2Min, op2{ 0xFD, LEB128(0xF4) }, s_ss, "f64x2.min" },
                { opcode::F64x2Max, op2{ 0xFD, LEB128(0xF5) }, s_ss, "f64x2.max" },
                { opcode::F64x2Pmin, op2{ 0xFD, LEB128(0xF6) }, s_ss, "f64x2.pmin" },
                { opcode::F64x2Pmax, op2{ 0xFD, LEB128(0xF7) }, s_ss, "f64x2.pmax" },
                { opcode::I32x4SConvertF32x4, op2{ 0xFD, LEB128(0xF8) }, s_s, "i32x4.trunc_sat_f32x4_s" },
                { opcode::I32x4UConvertF32x4, op2{ 0xFD, LEB128(0xF9) }, s_s, "i32x4.trunc_sat_f32x4_u" },
                { opcode::F32x4SConvertI32x4, op2{ 0xFD, LEB128(0xFA) }, s_s, "f32x4.convert_i32x4_s" },
                { opcode::F32x4UConvertI32x4, op2{ 0xFD, LEB128(0xFB) }, s_s, "f32x4.convert_i32x4_u" },
                { opcode::I32x4TruncSatF64x2SZero, op2{ 0xFD, LEB128(0xFC) }, s_s, "i32x4.trunc_sat_f64x2_s_zero" },
                { opcode::I32x4TruncSatF64x2UZero, op2{ 0xFD, LEB128(0xFD) }, s_s, "i32x4.trunc_sat_f64x2_u_zero" },
                { opcode::F64x2ConvertLowI32x4S, op2{ 0xFD, LEB128(0xFE) }, s_s, "f64x2.convert_low_i32x4_s" },
                { opcode::F64x2ConvertLowI32x4U, op2{ 0xFD, LEB128(0xFF) }, s_s, "f64x2.convert_low_i32x4_u" },
                { opcode::I8x16RelaxedSwizzle, op2{ 0xFD, LEB128(0x100) }, s_ss, "i8x16.relaxed_swizzle" },
                { opcode::I32x4RelaxedTruncF32x4S, op2{ 0xFD, LEB128(0x101) }, s_s, "i32x4.relaxed_trunc_f32x4_s" },
                { opcode::I32x4RelaxedTruncF32x4U, op2{ 0xFD, LEB128(0x102) }, s_s, "i32x4.relaxed_trunc_f32x4_u" },
                { opcode::I32x4RelaxedTruncF64x2SZero, op2{ 0xFD, LEB128(0x103) }, s_s, "i32x4.relaxed_trunc_f64x2_s_zero" },
                { opcode::I32x4RelaxedTruncF64x2UZero, op2{ 0xFD, LEB128(0x104) }, s_s, "i32x4.relaxed_trunc_f64x2_u_zero" },
                { opcode::F32x4Qfma, op2{ 0xFD, LEB128(0x105) }, s_sss, "f32x4.qfma" },
                { opcode::F32x4Qfms, op2{ 0xFD, LEB128(0x106) }, s_sss, "f32x4.qfms" },
                { opcode::F64x2Qfma, op2{ 0xFD, LEB128(0x107) }, s_sss, "f64x2.qfma" },
                { opcode::F64x2Qfms, op2{ 0xFD, LEB128(0x108) }, s_sss, "f64x2.qfms" },
                { opcode::I8x16RelaxedLaneSelect, op2{ 0xFD, LEB128(0x109) }, s_sss, "i8x16.relaxed_laneselect" },
                { opcode::I16x8RelaxedLaneSelect, op2{ 0xFD, LEB128(0x10A) }, s_sss, "i16x8.relaxed_laneselect" },
                { opcode::I32x4RelaxedLaneSelect, op2{ 0xFD, LEB128(0x10B) }, s_sss, "i32x4.relaxed_laneselect" },
                { opcode::I64x2RelaxedLaneSelect, op2{ 0xFD, LEB128(0x10C) }, s_sss, "i64x2.relaxed_laneselect" },
                { opcode::F32x4RelaxedMin, op2{ 0xFD, LEB128(0x10D) }, s_ss, "f32x4.relaxed_min" },
                { opcode::F32x4RelaxedMax, op2{ 0xFD, LEB128(0x10E) }, s_ss, "f32x4.relaxed_max" },
                { opcode::F64x2RelaxedMin, op2{ 0xFD, LEB128(0x10F) }, s_ss, "f64x2.relaxed_min" },
                { opcode::F64x2RelaxedMax, op2{ 0xFD, LEB128(0x110) }, s_ss, "f64x2.relaxed_max" },
                { opcode::I16x8RelaxedQ15MulRS, op2{ 0xFD, LEB128(0x111) }, s_ss, "i16x8.relaxed_q15mulr_s" },
                { opcode::I16x8DotI8x16I7x16S, op2{ 0xFD, LEB128(0x112) }, s_ss, "i16x8.dot_i8x16_i7x16_s" },
                { opcode::I32x4DotI8x16I7x16AddS, op2{ 0xFD, LEB128(0x113) }, s_sss, "i32x4.dot_i8x16_i7x16_add_s" },
                { opcode::F16x8Splat, op2{ 0xFD, LEB128(0x120) }, s_f, "f16x8.splat" },
                { opcode::F16x8Abs, op2{ 0xFD, LEB128(0x130) }, s_s, "f16x8.abs" },
                { opcode::F16x8Neg, op2{ 0xFD, LEB128(0x131) }, s_s, "f16x8.neg" },
                { opcode::F16x8Sqrt, op2{ 0xFD, LEB128(0x132) }, s_s, "f16x8.sqrt" },
                { opcode::F16x8Ceil, op2{ 0xFD, LEB128(0x133) }, s_s, "f16x8.ceil" },
                { opcode::F16x8Floor, op2{ 0xFD, LEB128(0x134) }, s_s, "f16x8.floor" },
                { opcode::F16x8Trunc, op2{ 0xFD, LEB128(0x135) }, s_s, "f16x8.trunc" },
                { opcode::F16x8NearestInt, op2{ 0xFD, LEB128(0x136) }, s_s, "f16x8.nearest" },
                { opcode::F16x8Eq, op2{ 0xFD, LEB128(0x137) }, s_ss, "f16x8.eq" },
                { opcode::F16x8Ne, op2{ 0xFD, LEB128(0x138) }, s_ss, "f16x8.ne" },
                { opcode::F16x8Lt, op2{ 0xFD, LEB128(0x139) }, s_ss, "f16x8.lt" },
                { opcode::F16x8Gt, op2{ 0xFD, LEB128(0x13A) }, s_ss, "f16x8.gt" },
                { opcode::F16x8Le, op2{ 0xFD, LEB128(0x13B) }, s_ss, "f16x8.le" },
                { opcode::F16x8Ge, op2{ 0xFD, LEB128(0x13C) }, s_ss, "f16x8.ge" },
                { opcode::F16x8Add, op2{ 0xFD, LEB128(0x13D) }, s_ss, "f16x8.add" },
                { opcode::F16x8Sub, op2{ 0xFD, LEB128(0x13E) }, s_ss, "f16x8.sub" },
                { opcode::F16x8Mul, op2{ 0xFD, LEB128(0x13F) }, s_ss, "f16x8.mul" },
                { opcode::F16x8Div, op2{ 0xFD, LEB128(0x140) }, s_ss, "f16x8.div" },
                { opcode::F16x8Min, op2{ 0xFD, LEB128(0x141) }, s_ss, "f16x8.min" },
                { opcode::F16x8Max, op2{ 0xFD, LEB128(0x142) }, s_ss, "f16x8.max" },
                { opcode::F16x8Pmin, op2{ 0xFD, LEB128(0x143) }, s_ss, "f16x8.pmin" },
                { opcode::F16x8Pmax, op2{ 0xFD, LEB128(0x144) }, s_ss, "f16x8.pmax" },
                { opcode::I16x8SConvertF16x8, op2{ 0xFD, LEB128(0x145) }, s_s, "i16x8.trunc_sat_f16x8_s" },
                { opcode::I16x8UConvertF16x8, op2{ 0xFD, LEB128(0x146) }, s_s, "i16x8.trunc_sat_f16x8_u" },
                { opcode::F16x8SConvertI16x8, op2{ 0xFD, LEB128(0x147) }, s_s, "f16x8.convert_i16x8_s" },
                { opcode::F16x8UConvertI16x8, op2{ 0xFD, LEB128(0x148) }, s_s, "f16x8.convert_i16x8_u" },
                { opcode::F16x8DemoteF32x4Zero, op2{ 0xFD, LEB128(0x149) }, s_s, "f16x8.demote_f32x4_zero" },
                { opcode::F16x8DemoteF64x2Zero, op2{ 0xFD, LEB128(0x14A) }, s_s, "f16x8.demote_f64x2_zero" },
                { opcode::F32x4PromoteLowF16x8, op2{ 0xFD, LEB128(0x14B) }, s_s, "f32x4.promote_low_f16x8" },
                { opcode::F16x8Qfma, op2{ 0xFD, LEB128(0x14E) }, s_sss, "f16x8.madd" },
                { opcode::F16x8Qfms, op2{ 0xFD, LEB128(0x14F) }, s_sss, "f16x8.nmadd" },
                { opcode::I8x16ExtractLaneS, op2{ 0xFD, LEB128(0x15) }, _, "i8x16.extract_lane_s" },
                { opcode::I8x16ExtractLaneU, op2{ 0xFD, LEB128(0x16) }, _, "i8x16.extract_lane_u" },
                { opcode::I16x8ExtractLaneS, op2{ 0xFD, LEB128(0x18) }, _, "i16x8.extract_lane_s" },
                { opcode::I16x8ExtractLaneU, op2{ 0xFD, LEB128(0x19) }, _, "i16x8.extract_lane_u" },
                { opcode::I32x4ExtractLane, op2{ 0xFD, LEB128(0x1B) }, _, "i32x4.extract_lane" },
                { opcode::I64x2ExtractLane, op2{ 0xFD, LEB128(0x1D) }, _, "i64x2.extract_lane" },
                { opcode::F32x4ExtractLane, op2{ 0xFD, LEB128(0x1F) }, _, "f32x4.extract_lane" },
                { opcode::F64x2ExtractLane, op2{ 0xFD, LEB128(0x21) }, _, "f64x2.extract_lane" },
                { opcode::F16x8ExtractLane, op2{ 0xFD, LEB128(0x121) }, _, "f16x8.extract_lane" },
                { opcode::I8x16ReplaceLane, op2{ 0xFD, LEB128(0x17) }, _, "i8x16.replace_lane" },
                { opcode::I16x8ReplaceLane, op2{ 0xFD, LEB128(0x1A) }, _, "i16x8.replace_lane" },
                { opcode::I32x4ReplaceLane, op2{ 0xFD, LEB128(0x1C) }, _, "i32x4.replace_lane" },
                { opcode::I64x2ReplaceLane, op2{ 0xFD, LEB128(0x1E) }, _, "i64x2.replace_lane" },
                { opcode::F32x4ReplaceLane, op2{ 0xFD, LEB128(0x20) }, _, "f32x4.replace_lane" },
                { opcode::F64x2ReplaceLane, op2{ 0xFD, LEB128(0x22) }, _, "f64x2.replace_lane" },
                { opcode::F16x8ReplaceLane, op2{ 0xFD, LEB128(0x122) }, _, "f16x8.replace_lane" },
                { opcode::I32SConvertSatF32, op2{ 0xFC, LEB128(0x00) }, i_f, "i32.trunc_sat_f32_s" },
                { opcode::I32UConvertSatF32, op2{ 0xFC, LEB128(0x01) }, i_f, "i32.trunc_sat_f32_u" },
                { opcode::I32SConvertSatF64, op2{ 0xFC, LEB128(0x02) }, i_d, "i32.trunc_sat_f64_s" },
                { opcode::I32UConvertSatF64, op2{ 0xFC, LEB128(0x03) }, i_d, "i32.trunc_sat_f64_u" },
                { opcode::I64SConvertSatF32, op2{ 0xFC, LEB128(0x04) }, l_f, "i64.trunc_sat_f32_s" },
                { opcode::I64UConvertSatF32, op2{ 0xFC, LEB128(0x05) }, l_f, "i64.trunc_sat_f32_u" },
                { opcode::I64SConvertSatF64, op2{ 0xFC, LEB128(0x06) }, l_d, "i64.trunc_sat_f64_s" },
                { opcode::I64UConvertSatF64, op2{ 0xFC, LEB128(0x07) }, l_d, "i64.trunc_sat_f64_u" },
                { opcode::DataDrop, op2{ 0xFC, LEB128(0x09) }, v_v, "data.drop" },
                { opcode::TableInit, op2{ 0xFC, LEB128(0x0C) }, v_iii, "table.init" },
                { opcode::ElemDrop, op2{ 0xFC, LEB128(0x0D) }, v_v, "elem.drop" },
                { opcode::TableCopy, op2{ 0xFC, LEB128(0x0E) }, v_iii, "table.copy" },
                { opcode::TableSize, op2{ 0xFC, LEB128(0x10) }, i_v, "table.size" },
                { opcode::MemoryInit, op2{ 0xFC, LEB128(0x08) }, _, "memory.init" },
                { opcode::MemoryCopy, op2{ 0xFC, LEB128(0x0A) }, _, "memory.copy" },
                { opcode::MemoryFill, op2{ 0xFC, LEB128(0x0B) }, _, "memory.fill" },
                /* TableGrow is polymorphic in the first parameter. */
                /* It's whatever the table type is. */
                { opcode::TableGrow, op2{ 0xFC, LEB128(0x0F) }, _, "table.grow" },
                /* TableFill is polymorphic in the second parameter. */
                /* It's whatever the table type is. */
                { opcode::TableFill, op2{ 0xFC, LEB128(0x11) }, _, "table.fill" },
                // kExprName, binary, signature for memory32, wat name, signature for memory64.
                { opcode::AtomicNotify, op2{ 0xFE, LEB128(0x00) }, i_ii, "memory.atomic.notify", i_li },
                { opcode::I32AtomicWait, op2{ 0xFE, LEB128(0x01) }, i_iil, "memory.atomic.wait32", i_lil },
                { opcode::I64AtomicWait, op2{ 0xFE, LEB128(0x02) }, i_ill, "memory.atomic.wait64", i_lll },
                { opcode::I32AtomicLoad, op2{ 0xFE, LEB128(0x10) }, i_i, "i32.atomic.load", i_l },
                { opcode::I64AtomicLoad, op2{ 0xFE, LEB128(0x11) }, l_i, "i64.atomic.load", l_l },
                { opcode::I32AtomicLoad8U, op2{ 0xFE, LEB128(0x12) }, i_i, "i32.atomic.load8_u", i_l },
                { opcode::I32AtomicLoad16U, op2{ 0xFE, LEB128(0x13) }, i_i, "i32.atomic.load16_u", i_l },
                { opcode::I64AtomicLoad8U, op2{ 0xFE, LEB128(0x14) }, l_i, "i64.atomic.load8_u", l_l },
                { opcode::I64AtomicLoad16U, op2{ 0xFE, LEB128(0x15) }, l_i, "i64.atomic.load16_u", l_l },
                { opcode::I64AtomicLoad32U, op2{ 0xFE, LEB128(0x16) }, l_i, "i64.atomic.load32_u", l_l },
                { opcode::I32AtomicStore, op2{ 0xFE, LEB128(0x17) }, v_ii, "i32.atomic.store", v_li },
                { opcode::I64AtomicStore, op2{ 0xFE, LEB128(0x18) }, v_il, "i64.atomic.store", v_ll },
                { opcode::I32AtomicStore8U, op2{ 0xFE, LEB128(0x19) }, v_ii, "i32.atomic.store8", v_li },
                { opcode::I32AtomicStore16U, op2{ 0xFE, LEB128(0x1A) }, v_ii, "i32.atomic.store16", v_li },
                { opcode::I64AtomicStore8U, op2{ 0xFE, LEB128(0x1B) }, v_il, "i64.atomic.store8", v_ll },
                { opcode::I64AtomicStore16U, op2{ 0xFE, LEB128(0x1C) }, v_il, "i64.atomic.store16", v_ll },
                { opcode::I64AtomicStore32U, op2{ 0xFE, LEB128(0x1D) }, v_il, "i64.atomic.store32", v_ll },
                { opcode::I32AtomicAdd, op2{ 0xFE, LEB128(0x1E) }, i_ii, "i32.atomic.rmw.add", i_li },
                { opcode::I64AtomicAdd, op2{ 0xFE, LEB128(0x1F) }, l_il, "i64.atomic.rmw.add", l_ll },
                { opcode::I32AtomicAdd8U, op2{ 0xFE, LEB128(0x20) }, i_ii, "i32.atomic.rmw8.add_u", i_li },
                { opcode::I32AtomicAdd16U, op2{ 0xFE, LEB128(0x21) }, i_ii, "i32.atomic.rmw16.add_u", i_li },
                { opcode::I64AtomicAdd8U, op2{ 0xFE, LEB128(0x22) }, l_il, "i64.atomic.rmw8.add_u", l_ll },
                { opcode::I64AtomicAdd16U, op2{ 0xFE, LEB128(0x23) }, l_il, "i64.atomic.rmw16.add_u", l_ll },
                { opcode::I64AtomicAdd32U, op2{ 0xFE, LEB128(0x24) }, l_il, "i64.atomic.rmw32.add_u", l_ll },
                { opcode::I32AtomicSub, op2{ 0xFE, LEB128(0x25) }, i_ii, "i32.atomic.rmw.sub", i_li },
                { opcode::I64AtomicSub, op2{ 0xFE, LEB128(0x26) }, l_il, "i64.atomic.rmw.sub", l_ll },
                { opcode::I32AtomicSub8U, op2{ 0xFE, LEB128(0x27) }, i_ii, "i32.atomic.rmw8.sub_u", i_li },
                { opcode::I32AtomicSub16U, op2{ 0xFE, LEB128(0x28) }, i_ii, "i32.atomic.rmw16.sub_u", i_li },
                { opcode::I64AtomicSub8U, op2{ 0xFE, LEB128(0x29) }, l_il, "i64.atomic.rmw8.sub_u", l_ll },
                { opcode::I64AtomicSub16U, op2{ 0xFE, LEB128(0x2A) }, l_il, "i64.atomic.rmw16.sub_u", l_ll },
                { opcode::I64AtomicSub32U, op2{ 0xFE, LEB128(0x2B) }, l_il, "i64.atomic.rmw32.sub_u", l_ll },
                { opcode::I32AtomicAnd, op2{ 0xFE, LEB128(0x2C) }, i_ii, "i32.atomic.rmw.and", i_li },
                { opcode::I64AtomicAnd, op2{ 0xFE, LEB128(0x2D) }, l_il, "i64.atomic.rmw.and", l_ll },
                { opcode::I32AtomicAnd8U, op2{ 0xFE, LEB128(0x2E) }, i_ii, "i32.atomic.rmw8.and_u", i_li },
                { opcode::I32AtomicAnd16U, op2{ 0xFE, LEB128(0x2F) }, i_ii, "i32.atomic.rmw16.and_u", i_li },
                { opcode::I64AtomicAnd8U, op2{ 0xFE, LEB128(0x30) }, l_il, "i64.atomic.rmw8.and_u", l_ll },
                { opcode::I64AtomicAnd16U, op2{ 0xFE, LEB128(0x31) }, l_il, "i64.atomic.rmw16.and_u", l_ll },
                { opcode::I64AtomicAnd32U, op2{ 0xFE, LEB128(0x32) }, l_il, "i64.atomic.rmw32.and_u", l_ll },
                { opcode::I32AtomicOr, op2{ 0xFE, LEB128(0x33) }, i_ii, "i32.atomic.rmw.or", i_li },
                { opcode::I64AtomicOr, op2{ 0xFE, LEB128(0x34) }, l_il, "i64.atomic.rmw.or", l_ll },
                { opcode::I32AtomicOr8U, op2{ 0xFE, LEB128(0x35) }, i_ii, "i32.atomic.rmw8.or_u", i_li },
                { opcode::I32AtomicOr16U, op2{ 0xFE, LEB128(0x36) }, i_ii, "i32.atomic.rmw16.or_u", i_li },
                { opcode::I64AtomicOr8U, op2{ 0xFE, LEB128(0x37) }, l_il, "i64.atomic.rmw8.or_u", l_ll },
                { opcode::I64AtomicOr16U, op2{ 0xFE, LEB128(0x38) }, l_il, "i64.atomic.rmw16.or_u", l_ll },
                { opcode::I64AtomicOr32U, op2{ 0xFE, LEB128(0x39) }, l_il, "i64.atomic.rmw32.or_u", l_ll },
                { opcode::I32AtomicXor, op2{ 0xFE, LEB128(0x3A) }, i_ii, "i32.atomic.rmw.xor", i_li },
                { opcode::I64AtomicXor, op2{ 0xFE, LEB128(0x3B) }, l_il, "i64.atomic.rmw.xor", l_ll },
                { opcode::I32AtomicXor8U, op2{ 0xFE, LEB128(0x3C) }, i_ii, "i32.atomic.rmw8.xor_u", i_li },
                { opcode::I32AtomicXor16U, op2{ 0xFE, LEB128(0x3D) }, i_ii, "i32.atomic.rmw16.xor_u", i_li },
                { opcode::I64AtomicXor8U, op2{ 0xFE, LEB128(0x3E) }, l_il, "i64.atomic.rmw8.xor_u", l_ll },
                { opcode::I64AtomicXor16U, op2{ 0xFE, LEB128(0x3F) }, l_il, "i64.atomic.rmw16.xor_u", l_ll },
                { opcode::I64AtomicXor32U, op2{ 0xFE, LEB128(0x40) }, l_il, "i64.atomic.rmw32.xor_u", l_ll },
                { opcode::I32AtomicExchange, op2{ 0xFE, LEB128(0x41) }, i_ii, "i32.atomic.rmw.xchg", i_li },
                { opcode::I64AtomicExchange, op2{ 0xFE, LEB128(0x42) }, l_il, "i64.atomic.rmw.xchg", l_ll },
                { opcode::I32AtomicExchange8U, op2{ 0xFE, LEB128(0x43) }, i_ii, "i32.atomic.rmw8.xchg_u", i_li },
                { opcode::I32AtomicExchange16U, op2{ 0xFE, LEB128(0x44) }, i_ii, "i32.atomic.rmw16.xchg_u", i_li },
                { opcode::I64AtomicExchange8U, op2{ 0xFE, LEB128(0x45) }, l_il, "i64.atomic.rmw8.xchg_u", l_ll },
                { opcode::I64AtomicExchange16U, op2{ 0xFE, LEB128(0x46) }, l_il, "i64.atomic.rmw16.xchg_u", l_ll },
                { opcode::I64AtomicExchange32U, op2{ 0xFE, LEB128(0x47) }, l_il, "i64.atomic.rmw32.xchg_u", l_ll },
                { opcode::I32AtomicCompareExchange, op2{ 0xFE, LEB128(0x48) }, i_iii, "i32.atomic.rmw.cmpxchg", i_lii },
                { opcode::I64AtomicCompareExchange, op2{ 0xFE, LEB128(0x49) }, l_ill, "i64.atomic.rmw.cmpxchg", l_lll },
                { opcode::I32AtomicCompareExchange8U, op2{ 0xFE, LEB128(0x4A) }, i_iii, "i32.atomic.rmw8.cmpxchg_u", i_lii },
                { opcode::I32AtomicCompareExchange16U, op2{ 0xFE, LEB128(0x4B) }, i_iii, "i32.atomic.rmw16.cmpxchg_u", i_lii },
                { opcode::I64AtomicCompareExchange8U, op2{ 0xFE, LEB128(0x4C) }, l_ill, "i64.atomic.rmw8.cmpxchg_u", l_lll },
                { opcode::I64AtomicCompareExchange16U, op2{ 0xFE, LEB128(0x4D) }, l_ill, "i64.atomic.rmw16.cmpxchg_u", l_lll },
                { opcode::I64AtomicCompareExchange32U, op2{ 0xFE, LEB128(0x4E) }, l_ill, "i64.atomic.rmw32.cmpxchg_u", l_lll },
                /* AtomicFence does not target a particular linear memory. */
                { opcode::AtomicFence, op2{ 0xFE, LEB128(0x03) }, v_v, "atomic.fence", v_v },
                { opcode::StructNew, op2{ 0xFB, LEB128(0x00) }, _, "struct.new" },
                { opcode::StructNewDefault, op2{ 0xFB, LEB128(0x01) }, _, "struct.new_default" },
                { opcode::StructGet, op2{ 0xFB, LEB128(0x02) }, _, "struct.get" },
                { opcode::StructGetS, op2{ 0xFB, LEB128(0x03) }, _, "struct.get_s" },
                { opcode::StructGetU, op2{ 0xFB, LEB128(0x04) }, _, "struct.get_u" },
                { opcode::StructSet, op2{ 0xFB, LEB128(0x05) }, _, "struct.set" },
                { opcode::ArrayNew, op2{ 0xFB, LEB128(0x06) }, _, "array.new" },
                { opcode::ArrayNewDefault, op2{ 0xFB, LEB128(0x07) }, _, "array.new_default" },
                { opcode::ArrayNewFixed, op2{ 0xFB, LEB128(0x08) }, _, "array.new_fixed" },
                { opcode::ArrayNewData, op2{ 0xFB, LEB128(0x09) }, _, "array.new_data" },
                { opcode::ArrayNewElem, op2{ 0xFB, LEB128(0x0A) }, _, "array.new_elem" },
                { opcode::ArrayGet, op2{ 0xFB, LEB128(0x0B) }, _, "array.get" },
                { opcode::ArrayGetS, op2{ 0xFB, LEB128(0x0C) }, _, "array.get_s" },
                { opcode::ArrayGetU, op2{ 0xFB, LEB128(0x0D) }, _, "array.get_u" },
                { opcode::ArraySet, op2{ 0xFB, LEB128(0x0E) }, _, "array.set" },
                { opcode::ArrayLen, op2{ 0xFB, LEB128(0x0F) }, _, "array.len" },
                { opcode::ArrayFill, op2{ 0xFB, LEB128(0x10) }, _, "array.fill" },
                { opcode::ArrayCopy, op2{ 0xFB, LEB128(0x11) }, _, "array.copy" },
                { opcode::ArrayInitData, op2{ 0xFB, LEB128(0x12) }, _, "array.init_data" },
                { opcode::ArrayInitElem, op2{ 0xFB, LEB128(0x13) }, _, "array.init_elem" },
                { opcode::RefTest, op2{ 0xFB, LEB128(0x14) }, _, "ref.test" },
                { opcode::RefTestNull, op2{ 0xFB, LEB128(0x15) }, _, "ref.test null" },
                { opcode::RefCast, op2{ 0xFB, LEB128(0x16) }, _, "ref.cast" },
                { opcode::RefCastNull, op2{ 0xFB, LEB128(0x17) }, _, "ref.cast null" },
                { opcode::BrOnCast, op2{ 0xFB, LEB128(0x18) }, _, "br_on_cast" },
                { opcode::BrOnCastFail, op2{ 0xFB, LEB128(0x19) }, _, "br_on_cast_fail" },
                { opcode::AnyConvertExtern, op2{ 0xFB, LEB128(0x1A) }, _, "any.convert_extern" },
                { opcode::ExternConvertAny, op2{ 0xFB, LEB128(0x1B) }, _, "extern.convert_any" },
                { opcode::RefI31, op2{ 0xFB, LEB128(0x1C) }, _, "ref.i31" },
                { opcode::I31GetS, op2{ 0xFB, LEB128(0x1D) }, _, "i31.get_s" },
                { opcode::I31GetU, op2{ 0xFB, LEB128(0x1E) }, _, "i31.get_u" },
                /* Custom Descriptors proposal */
                { opcode::RefGetDesc, op2{ 0xFB, LEB128(0x22) }, _, "ref.get_desc" },
                { opcode::RefCastDesc, op2{ 0xFB, LEB128(0x23) }, _, "ref.cast_desc" },
                { opcode::RefCastDescNull, op2{ 0xFB, LEB128(0x24) }, _, "ref.cast_desc null" },
                { opcode::BrOnCastDesc, op2{ 0xFB, LEB128(0x25) }, _, "br_on_cast_desc" },
                { opcode::BrOnCastDescFail, op2{ 0xFB, LEB128(0x26) }, _, "br_on_cast_desc_fail" },
                { opcode::RefCastNop, op2{ 0xFB, LEB128(0x4C) }, _, "ref.cast_nop" },
                /* Stringref proposal. */
                { opcode::StringNewUtf8, op2{ 0xFB, LEB128(0x80) }, _, "string.new_utf8" },
                { opcode::StringNewWtf16, op2{ 0xFB, LEB128(0x81) }, _, "string.new_wtf16" },
                { opcode::StringConst, op2{ 0xFB, LEB128(0x82) }, _, "string.const" },
                { opcode::StringMeasureUtf8, op2{ 0xFB, LEB128(0x83) }, _, "string.measure_utf8" },
                { opcode::StringMeasureWtf8, op2{ 0xFB, LEB128(0x84) }, _, "string.measure_wtf8" },
                { opcode::StringMeasureWtf16, op2{ 0xFB, LEB128(0x85) }, _, "string.measure_wtf16" },
                { opcode::StringEncodeUtf8, op2{ 0xFB, LEB128(0x86) }, _, "string.encode_utf8" },
                { opcode::StringEncodeWtf16, op2{ 0xFB, LEB128(0x87) }, _, "string.encode_wtf16" },
                { opcode::StringConcat, op2{ 0xFB, LEB128(0x88) }, _, "string.concat" },
                { opcode::StringEq, op2{ 0xFB, LEB128(0x89) }, _, "string.eq" },
                { opcode::StringIsUSVSequence, op2{ 0xFB, LEB128(0x8A) }, _, "string.is_usv_sequence" },
                { opcode::StringNewLossyUtf8, op2{ 0xFB, LEB128(0x8B) }, _, "string.new_lossy_utf8" },
                { opcode::StringNewWtf8, op2{ 0xFB, LEB128(0x8C) }, _, "string.new_wtf8" },
                { opcode::StringEncodeLossyUtf8, op2{ 0xFB, LEB128(0x8D) }, _, "string.encode_lossy_utf8" },
                { opcode::StringEncodeWtf8, op2{ 0xFB, LEB128(0x8E) }, _, "string.encode_wtf8" },
                { opcode::StringNewUtf8Try, op2{ 0xFB, LEB128(0x8F) }, _, "string.new_utf8_try" },
                { opcode::StringAsWtf8, op2{ 0xFB, LEB128(0x90) }, _, "string.as_wtf8" },
                { opcode::StringViewWtf8Advance, op2{ 0xFB, LEB128(0x91) }, _, "stringview_wtf8.advance" },
                { opcode::StringViewWtf8EncodeUtf8, op2{ 0xFB, LEB128(0x92) }, _, "stringview_wtf8.encode_utf8" },
                { opcode::StringViewWtf8Slice, op2{ 0xFB, LEB128(0x93) }, _, "stringview_wtf8.slice" },
                { opcode::StringViewWtf8EncodeLossyUtf8, op2{ 0xFB, LEB128(0x94) }, _, "stringview_wtf8.encode_lossy_utf8" },
                { opcode::StringViewWtf8EncodeWtf8, op2{ 0xFB, LEB128(0x95) }, _, "stringview_wtf8.encode_wtf8" },
                { opcode::StringAsWtf16, op2{ 0xFB, LEB128(0x98) }, _, "string.as_wtf16" },
                { opcode::StringViewWtf16Length, op2{ 0xFB, LEB128(0x99) }, _, "stringview_wtf16.length" },
                { opcode::StringViewWtf16GetCodeunit, op2{ 0xFB, LEB128(0x9A) }, _, "stringview_wtf16.get_codeunit" },
                { opcode::StringViewWtf16Encode, op2{ 0xFB, LEB128(0x9B) }, _, "stringview_wtf16.encode" },
                { opcode::StringViewWtf16Slice, op2{ 0xFB, LEB128(0x9C) }, _, "stringview_wtf16.slice" },
                { opcode::StringAsIter, op2{ 0xFB, LEB128(0xA0) }, _, "string.as_iter" },
                { opcode::StringViewIterNext, op2{ 0xFB, LEB128(0xA1) }, _, "stringview_iter.next" },
                { opcode::StringViewIterAdvance, op2{ 0xFB, LEB128(0xA2) }, _, "stringview_iter.advance" },
                { opcode::StringViewIterRewind, op2{ 0xFB, LEB128(0xA3) }, _, "stringview_iter.rewind" },
                { opcode::StringViewIterSlice, op2{ 0xFB, LEB128(0xA4) }, _, "stringview_iter.slice" },
                { opcode::StringCompare, op2{ 0xFB, LEB128(0xA8) }, _, "string.compare" },
                { opcode::StringFromCodePoint, op2{ 0xFB, LEB128(0xA9) }, _, "string.from_code_point" },
                { opcode::StringHash, op2{ 0xFB, LEB128(0xAA) }, _, "string.hash" },
                { opcode::StringNewUtf8Array, op2{ 0xFB, LEB128(0xB0) }, _, "string.new_utf8_array" },
                { opcode::StringNewWtf16Array, op2{ 0xFB, LEB128(0xB1) }, _, "string.new_wtf16_array" },
                { opcode::StringEncodeUtf8Array, op2{ 0xFB, LEB128(0xB2) }, _, "string.encode_utf8_array" },
                { opcode::StringEncodeWtf16Array, op2{ 0xFB, LEB128(0xB3) }, _, "string.encode_wtf16_array" },
                { opcode::StringNewLossyUtf8Array, op2{ 0xFB, LEB128(0xB4) }, _, "string.new_lossy_utf8_array" },
                { opcode::StringNewWtf8Array, op2{ 0xFB, LEB128(0xB5) }, _, "string.new_wtf8_array" },
                { opcode::StringEncodeLossyUtf8Array, op2{ 0xFB, LEB128(0xB6) }, _, "string.encode_lossy_utf8_array" },
                { opcode::StringEncodeWtf8Array, op2{ 0xFB, LEB128(0xB7) }, _, "string.encode_wtf8_array" },
                { opcode::StringNewUtf8ArrayTry, op2{ 0xFB, LEB128(0xB8) }, _, "string.new_utf8_array_try" },
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
/*
  vm.cpp

  Copyright (c) 2019 Leigh Johnston.  All Rights Reserved.

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

#include "vm.hpp"
#include <sstream>

namespace neos
{
    namespace bytecode
    {
        namespace vm
        {
            namespace registers
            {
                thread_local data r[16];
                thread_local simd_128 x[16];
                thread_local simd_256 y[16];
                thread_local simd_512 z[16];
            }

            namespace
            {
                template <typename DataType>
                inline DataType read_r1(opcode aOpcode)
                {
                    return vm::read<DataType>(r1(aOpcode));
                }

                template <typename DataType>
                inline DataType read_r2(opcode aOpcode)
                {
                    return vm::read<DataType>(r2(aOpcode));
                }

                template <typename DataType>
                inline DataType& write_r1(opcode aOpcode)
                {
                    return vm::write<DataType>(r1(aOpcode));
                }

                template <typename DataType>
                inline DataType& write_r2(opcode aOpcode)
                {
                    return vm::write<DataType>(r2(aOpcode));
                }

                template <typename DataType>
                inline DataType immediate(opcode aOpcode, const std::byte* aText)
                {
                    switch (static_cast<opcode_type>(aOpcode & opcode_type::DATA_MASK))
                    {
                    case opcode_type::D8:
                    case opcode_type::D8 | opcode_type::Signed:
                        return static_cast<DataType>(aOpcode & opcode_type::D8_MASK);
                    default:
                        return *reinterpret_cast<const DataType*>(aText);
                    }
                }
            }

            namespace instruction
            {
                inline void B(opcode aOpcode, const std::byte* aText)
                {
                    if ((static_cast<opcode_type>(aOpcode & opcode_type::Immediate)) == opcode_type::Immediate)
                    {
                        switch (static_cast<opcode_type>(aOpcode & opcode_type::DATA_MASK))
                        {
                        case opcode_type::D8:
                            r<u64, reg::PC>() += immediate<u8>(aOpcode, aText);
                            break;
                        case opcode_type::D16:
                            r<u64, reg::PC>() += immediate<u16>(aOpcode, aText);
                            break;
                        case opcode_type::D32:
                            r<u64, reg::PC>() += immediate<u32>(aOpcode, aText);
                            break;
                        case opcode_type::D64:
                            r<u64, reg::PC>() = immediate<u64>(aOpcode, aText);
                            break;
                        case opcode_type::D8 | opcode_type::Signed:
                            r<u64, reg::PC>() += immediate<i8>(aOpcode, aText);
                            break;
                        case opcode_type::D16 | opcode_type::Signed:
                            r<u64, reg::PC>() += immediate<i16>(aOpcode, aText);
                            break;
                        case opcode_type::D32 | opcode_type::Signed:
                            r<u64, reg::PC>() += immediate<i32>(aOpcode, aText);
                            break;
                        case opcode_type::D64 | opcode_type::Signed:
                            r<u64, reg::PC>() = immediate<i64>(aOpcode, aText);
                            break;
                        }
                    }
                }
                inline uint32_t ADD(opcode aOpcode, const std::byte* aText)
                {
                    // todo: update flags based on result of arithmetic
                    if ((static_cast<opcode_type>(aOpcode & opcode_type::Immediate)) == opcode_type::Immediate)
                    {
                        switch (static_cast<opcode_type>(aOpcode & opcode_type::DATA_MASK))
                        {
                        case opcode_type::D8:
                            write_r1<u64>(aOpcode) += immediate<u8>(aOpcode, aText);
                            return 0u;
                        case opcode_type::D16:
                            write_r1<u64>(aOpcode) += immediate<u16>(aOpcode, aText);
                            return 2u;
                        case opcode_type::D32:
                            write_r1<u64>(aOpcode) += immediate<u32>(aOpcode, aText);
                            return 4u;
                        case opcode_type::D64:
                            write_r1<u64>(aOpcode) += immediate<u64>(aOpcode, aText);
                            return 8u;
                        case opcode_type::D8 | opcode_type::Signed:
                            write_r1<u64>(aOpcode) += immediate<i8>(aOpcode, aText);
                            return 0u;
                        case opcode_type::D16 | opcode_type::Signed:
                            write_r1<u64>(aOpcode) += immediate<i16>(aOpcode, aText);
                            return 2u;
                        case opcode_type::D32 | opcode_type::Signed:
                            write_r1<u64>(aOpcode) += immediate<i32>(aOpcode, aText);
                            return 4u;
                        case opcode_type::D64 | opcode_type::Signed:
                            write_r1<u64>(aOpcode) += immediate<i64>(aOpcode, aText);
                            return 8u;
                        }
                    }
                    throw exceptions::invalid_instruction();
                }
            }

            thread::thread(const text_t& aText) : 
                iText{ aText },
                iCount{ 0ull },
                iTerminate{ false },
                iResult{}
            {
                iNativeThread.emplace([this]()
                {
                    iResult = execute();
                });
            }

            bool thread::joinable() const
            {
                return iNativeThread->joinable();
            }

            void thread::join()
            {
                try
                {
                    iNativeThread->join();
                }
                catch (std::invalid_argument)
                {
                }
            }

            void thread::terminate()
            {
                iTerminate = true;
                join();
            }

            const std::chrono::steady_clock::time_point& thread::start_time() const
            {
                return iStartTime;
            }

            uint64_t thread::count() const
            {
                return iCountSample;
            }

            std::string thread::metrics() const
            {
                std::ostringstream oss;
                auto threadId = iNativeThread->get_id();
                oss << "[Thread " << threadId << "] Instruction count (approx): " << count() << std::endl;
                auto instructionsPerSecond = count() / (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time()).count() / 1000000.0);
                auto ghzFrequency = instructionsPerSecond / 1000000000.0;
                if (ghzFrequency >= 1.0)
                    oss << "[Thread " << threadId << "] Virtual CPU clock frequency: " << ghzFrequency << " GHz" << std::endl;
                else
                    oss << "[Thread " << threadId << "] Virtual CPU clock frequency: " << ghzFrequency * 1000.0 << " MHz" << std::endl;
                return oss.str();
            }

            i64 thread::result() const
            {
                return iResult;
            }

            i64 thread::execute()
            {
                iStartTime = std::chrono::steady_clock::now();
                if (iText.empty())
                    throw exceptions::no_text();
                for(;;)
                {
                    auto& pc = r<u64, reg::PC>();
                    bytecode::opcode const opcode = *reinterpret_cast<const bytecode::opcode*>(&iText[pc]);
                    pc += 4u;
                    bytecode::opcode const opcodeInstruction = (opcode & opcode_type::OPCODE_MASK);
                    switch (opcodeInstruction)
                    {
                    case bytecode::opcode::B:
                        instruction::B(opcode, &iText[pc]);
                        break;
                    case bytecode::opcode::ADD:
                        pc += instruction::ADD(opcode, &iText[pc]);
                        break;
                    }
                    if ((++iCount & 0x3FFFF) == 0)
                    {
                        if (iTerminate)
                            break;
                        iCountSample = iCount;
                    }
                }
                return r<i64, reg::R1>();
            }
        }
   }
}
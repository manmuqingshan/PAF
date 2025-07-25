/*
 * SPDX-FileCopyrightText: <text>Copyright 2021-2024 Arm Limited and/or its
 * affiliates <open-source-office@arm.com></text>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file is part of PAF, the Physical Attack Framework.
 */

#include "PAF/SCA/Power.h"
#include "PAF/ArchInfo.h"
#include "PAF/PAF.h"
#include "PAF/SCA/Dumper.h"
#include "PAF/SCA/NPArray.h"

#include "PAF/SCA/Noise.h"
#include "libtarmac/parser.hh"
#include "paf-unit-testing.h"

#include "gtest/gtest.h"

#include <array>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace testing;

using std::array;
using std::make_unique;
using std::map;
using std::pair;
using std::string;
using std::unique_ptr;
using std::vector;

using PAF::ArchInfo;
using PAF::MemoryAccess;
using PAF::ReferenceInstruction;
using PAF::RegisterAccess;
using PAF::SCA::CSVPowerDumper;
using PAF::SCA::InstrDumper;
using PAF::SCA::MemoryAccessesDumper;
using PAF::SCA::NoiseSource;
using PAF::SCA::NPArray;
using PAF::SCA::NPYPowerDumper;
using PAF::SCA::NPYRegBankDumper;
using PAF::SCA::PowerAnalysisConfig;
using PAF::SCA::PowerDumper;
using PAF::SCA::PowerTrace;
using PAF::SCA::PowerTraceConfig;
using PAF::SCA::RegBankDumper;
using PAF::SCA::TimingInfo;
using PAF::SCA::YAMLInstrDumper;
using PAF::SCA::YAMLMemoryAccessesDumper;
using PAF::SCA::YAMLTimingInfo;

class TestTimingInfo : public TimingInfo {
  public:
    TestTimingInfo() {}
    void save(std::ostream &os) const override {}
    [[nodiscard]] size_t minimum() const { return cmin; }
    [[nodiscard]] size_t maximum() const { return cmax; }
    [[nodiscard]] const vector<pair<Addr, unsigned>> &locations() const {
        return pcCycle;
    }
};

TEST(TimingInfo, Base) {

    TestTimingInfo TTI;
    EXPECT_EQ(TTI.minimum(), -1);
    EXPECT_EQ(TTI.maximum(), 0);
    EXPECT_TRUE(TTI.locations().empty());

    TTI.add(124, 2);
    TTI.add(128, 4);
    TTI.incr(4);
    TTI.add(132, 1);

    vector<pair<Addr, unsigned>> t1({{124, 0}, {128, 2}, {132, 10}});
    EXPECT_EQ(TTI.locations().size(), 3);
    EXPECT_EQ(TTI.locations(), t1);

    // Switch to next trace: check statistics have been computed and that the
    // first trace is remembered.
    TTI.nextTrace();
    EXPECT_EQ(TTI.minimum(), 11);
    EXPECT_EQ(TTI.maximum(), 11);
    EXPECT_EQ(TTI.locations().size(), 3);
    EXPECT_EQ(TTI.locations(), t1);

    // Now process a slightly different trace.
    TTI.add(124, 2);
    TTI.incr(2);
    TTI.add(132, 1);

    // The first trace should be remembered, and statistics updated.
    TTI.nextTrace();
    EXPECT_EQ(TTI.minimum(), 5);
    EXPECT_EQ(TTI.maximum(), 11);
    EXPECT_EQ(TTI.locations().size(), 3);
    EXPECT_EQ(TTI.locations(), t1);
}

// Create the test fixture for YAMLTimingInfo.
TEST_WITH_TEMP_FILE(YAMLTimingInfoF, "test-YAMLTimingInfo.yml.XXXXXX");

TEST_F(YAMLTimingInfoF, Base) {
    YAMLTimingInfo TI;

    TI.add(123, 2);
    TI.add(124, 1);
    TI.add(125, 1);
    TI.incr(4);
    TI.nextTrace();

    std::ostringstream s;
    TI.save(s);
    EXPECT_EQ(s.str(), "timing:\n  min: 8\n  ave: 8\n  max: 8\n  cycles: [ [ "
                       "0x7b, 0 ], [ 0x7c, 2 ], [ 0x7d, 3 ] ]\n");

    TI.saveToFile(getTemporaryFilename());
    EXPECT_TRUE(checkFileContent({
        // clang-format off
        "timing:",
        "  min: 8",
        "  ave: 8",
        "  max: 8",
        "  cycles: [ [ 0x7b, 0 ], [ 0x7c, 2 ], [ 0x7d, 3 ] ]"
        // clang-format on
    }));
}

// Create the test fixture for YAMLMemAccessesDumper.
TEST_WITH_TEMP_FILE(YAMLMemAccessesF, "test-YAMLMemAccesses.yml.XXXXXX");

TEST_F(YAMLMemAccessesF, Base) {
    std::ostringstream s;
    YAMLMemoryAccessesDumper MA1(s);

    EXPECT_EQ(s.str(), "memaccess:\n");

    // Check the trace separator is not emitted until something is dumped.
    MA1.nextTrace();
    EXPECT_EQ(s.str(), "memaccess:\n");
    MA1.dump(1234, {});
    EXPECT_EQ(s.str(), "memaccess:\n  - \n");
    MA1.dump(
        0x1234,
        {{MemoryAccess(4, 0x00021f5c, 0x00000003, MemoryAccess::Type::READ),
          MemoryAccess(4, 0x00021f60, 0x00021f64, MemoryAccess::Type::READ)}});
    EXPECT_EQ(s.str(), "memaccess:\n  - \n    - { pc: 0x1234, loads: "
                       "[[0x21f5c, 4, 0x3], [0x21f60, 4, 0x21f64]]}\n");
    MA1.dump(
        0x2345,
        {{MemoryAccess(2, 0x000abcdc, 0x00005678, MemoryAccess::Type::WRITE),
          MemoryAccess(2, 0x000abcde, 0x00001234, MemoryAccess::Type::WRITE)}});
    EXPECT_EQ(s.str(),
              "memaccess:\n  - \n    - { pc: 0x1234, loads: [[0x21f5c, 4, "
              "0x3], [0x21f60, 4, 0x21f64]]}\n    - { pc: 0x2345, stores: "
              "[[0xabcdc, 2, 0x5678], [0xabcde, 2, 0x1234]]}\n");

    YAMLMemoryAccessesDumper MA2(getTemporaryFilename());

    MA2.dump(1234, {});
    MA2.dump(
        0x1234,
        {{MemoryAccess(4, 0x00021f5c, 0x00000003, MemoryAccess::Type::READ),
          MemoryAccess(2, 0x000abcde, 0x00001234, MemoryAccess::Type::WRITE)}});
    MA2.nextTrace();
    MA2.dump(
        0x2345,
        {{MemoryAccess(2, 0x000abcdc, 0x00005678, MemoryAccess::Type::WRITE),
          MemoryAccess(4, 0x00021f60, 0x00021f64, MemoryAccess::Type::READ)}});
    MA2.flush();
    EXPECT_TRUE(checkFileContent({
        // clang-format off
        "memaccess:",
        "  - ",
        "    - { pc: 0x1234, loads: [[0x21f5c, 4, 0x3]], stores: [[0xabcde, 2, 0x1234]]}",
        "  - ",
        "    - { pc: 0x2345, loads: [[0x21f60, 4, 0x21f64]], stores: [[0xabcdc, 2, 0x5678]]}"
        // clang-format on
    }));
}

// Create the test fixture for YAMLInstrDumper.
TEST_WITH_TEMP_FILE(YAMLInstrDumperF, "test-YAMLInstrDumper.yml.XXXXXX");

TEST_F(YAMLInstrDumperF, Base) {
    const ReferenceInstruction I[2] = {
        // clang-format off
        {
            28, IE_EXECUTED, 0x08326, ARM, 32, 0xf8db0800, "ldr.w      r0,[r11,#2048]",
            {
                MemoryAccess(4, 0xf939b40, 0xdeadbeef, MemoryAccess::Type::READ)
            },
            {
                RegisterAccess("r0", 0xdeadbeef, RegisterAccess::Type::WRITE),
                RegisterAccess("r11", 0xf939340, RegisterAccess::Type::READ)
            }
        },
        {
            29, IE_EXECUTED, 0x0832a, THUMB, 16, 0x4408, "add      r0,r1",
            {},
            {
                RegisterAccess("r0", 0xdeadbef4, RegisterAccess::Type::WRITE),
                RegisterAccess("r1", 0x05, RegisterAccess::Type::READ)
            }
        },
        // clang-format on
    };

    const vector<uint64_t> regBankState[2] = {
        // clang-format off
        { 0, 1, 2, 3},
        { 4, 5, 6, 7},
        // clang-format on
    };

    std::ostringstream s;
    YAMLInstrDumper ID1(s);

    EXPECT_EQ(s.str(), "instr:\n");

    // Check the trace separator is not emitted until something is dumped.
    ID1.nextTrace();
    EXPECT_EQ(s.str(), "instr:\n");
    ID1.dump(I[0]);
    EXPECT_EQ(s.str(),
              "instr:\n  - \n    - { pc: 0x8326, opcode: 0xf8db0800, size: 32, "
              "executed: True, disassembly: \"ldr.w r0,[r11,#2048]\"}\n");
    ID1.dump(I[1]);
    EXPECT_EQ(s.str(),
              "instr:\n  - \n    - { pc: 0x8326, opcode: 0xf8db0800, size: 32, "
              "executed: True, disassembly: \"ldr.w r0,[r11,#2048]\"}\n    - { "
              "pc: 0x832a, opcode: 0x4408, size: 16, executed: True, "
              "disassembly: \"add r0,r1\"}\n");

    YAMLInstrDumper ID2(getTemporaryFilename());

    ID2.dump(I[0]);
    ID2.dump(I[1]);
    ID2.nextTrace();
    ID2.dump(I[0]);
    ID2.dump(I[1]);
    ID2.flush();
    EXPECT_TRUE(checkFileContent({
        // clang-format off
        "instr:",
        "  - ",
        "    - { pc: 0x8326, opcode: 0xf8db0800, size: 32, executed: True, disassembly: \"ldr.w r0,[r11,#2048]\"}",
        "    - { pc: 0x832a, opcode: 0x4408, size: 16, executed: True, disassembly: \"add r0,r1\"}",
        "  - ",
        "    - { pc: 0x8326, opcode: 0xf8db0800, size: 32, executed: True, disassembly: \"ldr.w r0,[r11,#2048]\"}",
        "    - { pc: 0x832a, opcode: 0x4408, size: 16, executed: True, disassembly: \"add r0,r1\"}"
        // clang-format on
    }));

    // Check Memory accesses can be dumped.
    s.str("");
    YAMLInstrDumper ID3(s, true, true, false);
    ID3.dump(I[0]);
    ID3.dump(I[1]);
    ID3.flush();
    EXPECT_EQ(s.str(),
              "instr:\n  - \n    - { pc: 0x8326, opcode: 0xf8db0800, size: 32, "
              "executed: True, disassembly: \"ldr.w r0,[r11,#2048]\", loads: "
              "[[0x261331776, 4, 0xdeadbeef]], stores: []}\n    - { "
              "pc: 0x832a, opcode: 0x4408, size: 16, executed: True, "
              "disassembly: \"add r0,r1\", loads: [], stores: []}\n");

    // Check reg bank state can be dumped.
    s.str("");
    YAMLInstrDumper ID4(s, true, false, true);
    ID4.dump(I[0]);
    ID4.dump(I[1]);
    ID4.flush();
    EXPECT_EQ(s.str(),
              "instr:\n  - \n    - { pc: 0x8326, opcode: 0xf8db0800, size: 32, "
              "executed: True, disassembly: \"ldr.w r0,[r11,#2048]\"}\n    - { "
              "pc: 0x832a, opcode: 0x4408, size: 16, executed: True, "
              "disassembly: \"add r0,r1\"}\n");
    s.str("");
    ID4.dump(I[0], regBankState[0]);
    ID4.dump(I[1], regBankState[1]);
    ID4.flush();
    EXPECT_EQ(s.str(),
              "    - { pc: 0x8326, opcode: 0xf8db0800, size: 32, "
              "executed: True, disassembly: \"ldr.w r0,[r11,#2048]\", regbank: "
              "[ 0x0, 0x1, 0x2, 0x3]}\n    - { "
              "pc: 0x832a, opcode: 0x4408, size: 16, executed: True, "
              "disassembly: \"add r0,r1\", regbank: [ 0x4, 0x5, 0x6, 0x7]}\n");

    // Check reg bank state is not dumped when not enabled.
    s.str("");
    YAMLInstrDumper ID5(s, true, false, false);
    ID5.dump(I[0], regBankState[0]);
    EXPECT_EQ(s.str(),
              "instr:\n  - \n    - { pc: 0x8326, opcode: 0xf8db0800, size: 32, "
              "executed: True, disassembly: \"ldr.w r0,[r11,#2048]\"}\n");

    // Check memory accesses and reg bank states can be dumped.
    s.str("");
    YAMLInstrDumper ID6(s, true, true, true);
    ID6.dump(I[0], regBankState[0]);
    ID6.dump(I[1], regBankState[1]);
    ID6.flush();
    EXPECT_EQ(s.str(),
              "instr:\n  - \n    - { pc: 0x8326, opcode: 0xf8db0800, size: 32, "
              "executed: True, disassembly: \"ldr.w r0,[r11,#2048]\", loads: "
              "[[0x261331776, 4, 0xdeadbeef]], stores: [], regbank: [ 0x0, "
              "0x1, 0x2, 0x3]}\n    - { "
              "pc: 0x832a, opcode: 0x4408, size: 16, executed: True, "
              "disassembly: \"add r0,r1\", loads: [], stores: [], regbank: [ "
              "0x4, 0x5, 0x6, 0x7]}\n");
}

struct PowerFields {
    double total;
    double pc;
    double instr;
    double oreg;
    double ireg;
    double addr;
    double data;
    const PAF::ReferenceInstruction *inst;
    PowerFields(double t, double p, double i, double oreg, double ireg,
                double a, double d, const PAF::ReferenceInstruction *I)
        : total(t), pc(p), instr(i), oreg(oreg), ireg(ireg), addr(a), data(d),
          inst(I) {}

    // Compare the power fields (and ignore the Instruction it refers to)
    bool operator==(const PowerFields &Other) const {
        // 2 ReferenceInstructions are the same if they are either null or point
        // to similar content (restricted here to the pc and the opcode)
        bool same_instr = inst == nullptr && Other.inst == nullptr;
        if (inst != nullptr && Other.inst != nullptr)
            same_instr = inst->pc == Other.inst->pc &&
                         inst->instruction == Other.inst->instruction;

        return same_instr && total == Other.total && pc == Other.pc &&
               instr == Other.instr && oreg == Other.oreg &&
               ireg == Other.ireg && addr == Other.addr && data == Other.data;
    }
    bool operator!=(const PowerFields &Other) const {
        return !(*this == Other);
    }

    static double noise(const PowerFields &RHS, const PowerFields &LHS) {
        return std::fabs(LHS.total - RHS.total) + std::fabs(LHS.pc - RHS.pc) +
               std::fabs(LHS.instr - RHS.instr) +
               std::fabs(LHS.oreg - RHS.oreg) + std::fabs(LHS.ireg - RHS.ireg) +
               std::fabs(LHS.addr - RHS.addr) + std::fabs(LHS.data - RHS.data);
    }
};

std::ostream &operator<<(std::ostream &os, const PowerFields &pf) {
    os << "PowerFields(";
    os << pf.total << ", ";
    os << pf.pc << ", ";
    os << pf.instr << ", ";
    os << pf.oreg << ", ";
    os << pf.ireg << ", ";
    os << pf.addr << ", ";
    os << pf.data << ", ";
    os << (uintptr_t)pf.inst << ")";
    return os;
}

// A mock for testing power dumps.
struct TestPowerDumper : public PowerDumper {

    TestPowerDumper() {}

    void dump(double t, double p, double i, double oreg, double ireg, double a,
              double d, const PAF::ReferenceInstruction *I) override {
        pwf.emplace_back(t, p, i, oreg, ireg, a, d, I);
    }

    void reset() { pwf.clear(); }

    vector<PowerFields> pwf;
};

// A mock for testing register bank traces.
class TestRegBankDumper : public RegBankDumper {
  public:
    TestRegBankDumper(bool enabled = false) : RegBankDumper(enabled) {}

    void reset() {
        nr = 0;
        regbank.clear();
    }

    void nextTrace() override { regbank.emplace_back(); }

    void dump(const std::vector<uint64_t> &regs) override {
        if (regbank.empty()) {
            nr = regs.size();
            nextTrace();
        }
        regbank.back().insert(regbank.back().end(), regs.begin(), regs.end());
    }

    [[nodiscard]] AssertionResult check(size_t trace, size_t idx,
                                        const vector<uint64_t> &ref) const {
        if (trace >= regbank.size())
            return reportError("trace index out of bound");
        if (idx >= regbank[trace].size())
            return reportError("snapshot index out of bound");
        if (ref.size() != nr)
            return reportError("size discrepancy");
        for (size_t i = 0; i < ref.size(); i++)
            if (ref[i] != regbank[trace][idx * nr + i])
                return reportError("regbank error", ref, regbank.back());
        return AssertionSuccess();
    }

    static void dump(AssertionResult &AR, const char *msg,
                     const vector<uint64_t> &regs) {
        AR << msg;
        for (const auto &v : regs)
            AR << ' ' << v;
        AR << '\n';
    }

    AssertionResult reportError(const char *msg) const {
        return AssertionFailure() << msg;
    }
    AssertionResult reportError(const char *msg,
                                const vector<uint64_t> &expected,
                                const vector<uint64_t> &actual) const {
        AssertionResult AR = AssertionFailure();
        AR << msg << "\n";
        dump(AR, "Expected:", expected);
        dump(AR, "Actual:", actual);
        return AR;
    }

    [[nodiscard]] size_t numSnapshots() const {
        return regbank.empty() ? 0 : regbank.back().size() / nr;
    }
    [[nodiscard]] size_t numTraces() const { return regbank.size(); }

    void dump() const {
        std::cout << "NR: " << nr << '\n';
        std::cout << "Num traces: " << numTraces() << '\n';
        std::cout << "Num snapshots: " << numSnapshots() << '\n';
    }

  private:
    vector<vector<uint64_t>> regbank;
    size_t nr{0};
};

// A mock for testing memory accesses traces.
struct TestMemAccessesDumper : public MemoryAccessesDumper {

    TestMemAccessesDumper(bool enabled = false)
        : MemoryAccessesDumper(enabled) {}

    void dump(uint64_t pc, const vector<MemoryAccess> &MA) override {
        if (!MA.empty())
            accessesCount += 1;
        lastAccesses = MA;
    }

    [[nodiscard]] size_t lastAccessesSize() const {
        return lastAccesses.size();
    }
    [[nodiscard]] size_t instrWithAccesses() const { return accessesCount; }

    void reset() {
        accessesCount = 0;
        lastAccesses.clear();
    }

    [[nodiscard]] AssertionResult check(const vector<MemoryAccess> &MA) const {
        if (MA.size() != lastAccesses.size())
            return AssertionFailure() << "Memory accesses differ in size";

        for (size_t i = 0; i < MA.size(); i++)
            if (MA[i] != lastAccesses[i])
                return AssertionFailure()
                       << "Memory accesses differ at index " << i;

        return AssertionSuccess();
    }

  private:
    vector<MemoryAccess> lastAccesses;
    size_t accessesCount{0};
};

// A mock for testing instruction dumps.
struct TestInstrDumper : public InstrDumper {
    TestInstrDumper(bool enabled = false, bool dumpMemAccess = false,
                    bool dumpRegBank = false)
        : InstrDumper(enabled, dumpMemAccess, dumpRegBank) {}

    [[nodiscard]] size_t numInstructions() const { return instrCount; }

    void reset() { instrCount = 0; }

  private:
    size_t instrCount{0};

    void dumpImpl(const ReferenceInstruction &I,
                  const vector<uint64_t> *regs) override {
        instrCount++;
    }
};

class TestOracle : public PowerTrace::Oracle {
  public:
    TestOracle(const ReferenceInstruction *Inst, size_t N) {
        // Gather how many registers we have in this instruction sequence.
        // And check time is strictly monotonically increasing.
        Time t;
        for (size_t i = 0; i < N; i++) {
            if (i != 0)
                assert(Inst[i].time > t && "Time must be strictly monotonic");
            t = Inst[i].time;
            for (const auto &RA : Inst[i].regAccess)
                if (RA.access == PAF::RegisterAccess::Type::WRITE)
                    if (registers.count(RA.name) == 0)
                        registers[RA.name] = registers.size();
        }
        nr = registers.size();

        // Build the different register bank states.
        for (size_t i = 0; i < N; i++) {
            // Extend the trace with a copy of the last snapshot.
            regbank.insert(make_pair(Inst[i].time,
                                     i == 0 ? vector<uint64_t>(nr, defaultValue)
                                            : regbank[Inst[i - 1].time]));

            // Add register updates to the snapshot
            for (const auto &RA : Inst[i].regAccess) {
                if (RA.access == PAF::RegisterAccess::Type::WRITE) {
                    assert(registers.count(RA.name) != 0 &&
                           "Unknown register name");
                    regbank.rbegin()->second[registers[RA.name]] = RA.value;
                }
            }
        }
    }

    [[nodiscard]] std::vector<uint64_t> getRegBankState(Time t) const override {
        if (regbank.empty() || t < regbank.begin()->first)
            return {nr, defaultValue};
        const auto it = regbank.find(t);
        if (it == regbank.end())
            return regbank.rend()->second;
        return it->second;
    }

    [[nodiscard]] uint64_t getMemoryState(Addr address, size_t size,
                                          Time t) const override {
        assert(false &&
               "TestOracle does not yet have getMemoryState implementation");
        return 0;
    }

    void dump() const {
        std::cout << "DefaultValue: " << defaultValue << '\n';
        std::cout << "Nun regs: " << nr << '\n';
        std::cout << "Registers:";
        for (const auto &r : registers)
            std::cout << ' ' << r.first << '=' << r.second;
        std::cout << '\n';
        std::cout << "Regbank snapshots:\n";
        for (const auto &s : regbank) {
            std::cout << " - " << s.first << ':';
            for (const auto &v : s.second)
                std::cout << ' ' << v;
            std::cout << '\n';
        }
    }

  private:
    map<string, unsigned> registers;
    map<Time, vector<uint64_t>> regbank;
    size_t nr{0};
    const uint64_t defaultValue{0};
};

static const array<ReferenceInstruction, 4> Insts{
    {
        // clang-format off
    {
        27, IE_EXECUTED, 0x089bc, THUMB, 16, 0x02105, "MOVS r1,#5",
        {},
        {
            RegisterAccess("r1", 5, RegisterAccess::Type::WRITE),
            RegisterAccess("cpsr", 0x21000000, RegisterAccess::Type::WRITE),
        }
    },
    {
        28, IE_EXECUTED, 0x089be, THUMB, 16, 0x0460a, "MOV r2,r1",
        {},
        {
            RegisterAccess("r1", 5, RegisterAccess::Type::READ),
            RegisterAccess("r2", 5, RegisterAccess::Type::WRITE)
        }
    },
    {
        29, IE_EXECUTED, 0x08326, ARM, 32, 0xe9425504, "STRD r5,r1,[r2,#-0x10]",
        {
            MemoryAccess(4, 0x00021afc, 5, MemoryAccess::Type::WRITE),
            MemoryAccess(4, 0x00021b00, 5, MemoryAccess::Type::WRITE)
        },
        {}
    },
    {
        30, IE_EXECUTED, 0x0832a, ARM, 32, 0xe9d63401, "LDRD r3,r4,[r6,#4]",
        {
            MemoryAccess(4, 0x00021f5c, 0x00000003, MemoryAccess::Type::READ),
            MemoryAccess(4, 0x00021f60, 0x00021f64, MemoryAccess::Type::READ)
        },
        {
            RegisterAccess("r3", 0x00000003, RegisterAccess::Type::WRITE),
            RegisterAccess("r4", 0x00021f64, RegisterAccess::Type::WRITE)
        }
    },
    }
    // clang-format on
};

TEST(PowerDumper, base) {
    TestPowerDumper TPD;

    TPD.preDump();
    TPD.dump(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, &Insts[0]);
    TPD.postDump();
    TPD.nextTrace();

    EXPECT_EQ(TPD.pwf.size(), 1);
    EXPECT_EQ(TPD.pwf[0],
              PowerFields(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, &Insts[0]));
}

TEST(CSVPowerDumper, base) {
    std::ostringstream s;
    CSVPowerDumper CPD1(s, false);
    CPD1.preDump();
    EXPECT_EQ(
        s.str(),
        "\"Total\",\"PC\",\"Instr\",\"ORegs\",\"IRegs\",\"Addr\",\"Data\"\n");
    s.str("");
    CPD1.dump(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, &Insts[0]);
    EXPECT_EQ(s.str(), "1.00,2.00,3.00,4.00,5.00,6.00,7.00\n");
    s.str("");
    CPD1.dump(2.0, 4.0, 6.0, 8.0, 10.0, 12.0, 14.0, &Insts[2]);
    EXPECT_EQ(s.str(), "2.00,4.00,6.00,8.00,10.00,12.00,14.00\n");
    s.str("");
    CPD1.postDump();
    CPD1.nextTrace();
    EXPECT_EQ(s.str(), "\n");

    s.str("");
    CSVPowerDumper CPD2(s, true);
    CPD2.preDump();
    EXPECT_EQ(
        s.str(),
        "\"Total\",\"PC\",\"Instr\",\"ORegs\",\"IRegs\",\"Addr\",\"Data\","
        "\"Time\",\"PC\",\"Instr\",\"Exe\",\"Asm\",\"Memory "
        "accesses\",\"Register accesses\"\n");
    s.str("");
    CPD2.dump(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, &Insts[0]);
    EXPECT_EQ(
        s.str(),
        "1.00,2.00,3.00,4.00,5.00,6.00,7.00,27,0x89bc,0x2105,\"X\",\"MOVS "
        "r1,#5\",\"\",\"W(0x5)@r1 W(0x21000000)@cpsr\"\n");
    s.str("");
    CPD2.dump(2.0, 4.0, 6.0, 8.0, 10.0, 12.0, 14.0, &Insts[2]);
    EXPECT_EQ(s.str(),
              "2.00,4.00,6.00,8.00,10.00,12.00,14.00,29,0x8326,0xe9425504,"
              "\"X\",\"STRD r5,r1,[r2,#-0x10]\",\"W4(0x5)@0x21afc "
              "W4(0x5)@0x21b00\",\"\"\n");
    s.str("");
    CPD2.postDump();
    CPD2.nextTrace();
    EXPECT_EQ(s.str(), "\n");
}

// Create the test fixture for NPYPowerDumper.
TEST_WITH_TEMP_FILE(NPYPowerDumperF, "test-Power.npy.XXXXXX");

TEST_F(NPYPowerDumperF, base) {
    {
        NPYPowerDumper NPD(getTemporaryFilename(), 2);
        NPD.preDump();
        NPD.dump(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, &Insts[0]);
        NPD.postDump();
        NPD.nextTrace();

        NPD.preDump();
        NPD.dump(2.0, 4.0, 6.0, 8.0, 10.0, 12.0, 14.0, &Insts[0]);
        NPD.postDump();
        NPD.nextTrace();
    }

    NPArray<double> npy(getTemporaryFilename().c_str());
    EXPECT_TRUE(npy.error() == nullptr);
    EXPECT_EQ(npy.rows(), 2);
    EXPECT_EQ(npy.cols(), 1);
    EXPECT_EQ(npy.elementSize(), sizeof(double));
    for (size_t col = 0; col < npy.cols(); col++)
        for (size_t row = 0; row < npy.rows(); row++)
            EXPECT_EQ(npy(row, col), double((row + 1) * (col + 1)));
}

TEST(RegBankDumper, base) {
    TestRegBankDumper TRBD(true);

    TRBD.preDump();
    TRBD.dump({0, 1, 2, 3});
    TRBD.postDump();
    TRBD.nextTrace();

    TRBD.preDump();
    TRBD.dump({4, 5, 6, 7});
    TRBD.postDump();
    TRBD.nextTrace();

    EXPECT_EQ(TRBD.numTraces(), 3);
    EXPECT_TRUE(TRBD.check(0, 0, {0, 1, 2, 3}));
    EXPECT_TRUE(TRBD.check(1, 0, {4, 5, 6, 7}));
}

// Create the test fixture for NPYRegBankDumper.
TEST_WITH_TEMP_FILE(NPYRegBankDumperF, "test-RegBank.npy.XXXXXX");

TEST_F(NPYRegBankDumperF, base) {
    {
        NPYRegBankDumper NRBD(getTemporaryFilename(), 2);
        NRBD.preDump();
        NRBD.dump({0, 1, 2, 3, 4});
        NRBD.dump({5, 6, 7, 8, 9});
        NRBD.postDump();
        NRBD.nextTrace();

        NRBD.preDump();
        NRBD.dump({10, 11, 12, 13, 14});
        NRBD.dump({15, 16, 17, 18, 19});
        NRBD.postDump();
        NRBD.nextTrace();
    }

    NPArray<uint64_t> npy(getTemporaryFilename().c_str());
    EXPECT_TRUE(npy.error() == nullptr);
    EXPECT_EQ(npy.rows(), 2);
    EXPECT_EQ(npy.cols(), 10);
    EXPECT_EQ(npy.elementSize(), sizeof(uint64_t));
    for (size_t row = 0; row < npy.rows(); row++)
        for (size_t col = 0; col < npy.cols(); col++)
            EXPECT_EQ(npy(row, col), row * npy.cols() + col);
}

TEST(PowerTraceConfig, base) {
    PowerTraceConfig PTC;
    EXPECT_TRUE(PTC.withAll());

    PTC.clear();
    EXPECT_TRUE(PTC.withNone());
    EXPECT_FALSE(PTC.withAll());
    EXPECT_FALSE(PTC.withPC());
    EXPECT_FALSE(PTC.withOpcode());
    EXPECT_FALSE(PTC.withMemAddress());
    EXPECT_FALSE(PTC.withMemData());
    EXPECT_FALSE(PTC.withInstructionsInputs());
    EXPECT_FALSE(PTC.withInstructionsOutputs());
    EXPECT_FALSE(PTC.withLoadToLoadTransitions());
    EXPECT_FALSE(PTC.withStoreToStoreTransitions());
    EXPECT_FALSE(PTC.withLastMemoryAccessTransitions());
    EXPECT_FALSE(PTC.withMemoryUpdateTransitions());
    EXPECT_FALSE(PTC.withMemoryAccessTransitions());

    PTC.set(PowerTraceConfig::WITH_PC);
    EXPECT_TRUE(PTC.withPC());
    EXPECT_FALSE(PTC.withOpcode());
    EXPECT_FALSE(PTC.withMemAddress());
    EXPECT_FALSE(PTC.withMemData());
    EXPECT_FALSE(PTC.withInstructionsInputs());
    EXPECT_FALSE(PTC.withInstructionsOutputs());
    EXPECT_FALSE(PTC.withLoadToLoadTransitions());
    EXPECT_FALSE(PTC.withStoreToStoreTransitions());
    EXPECT_FALSE(PTC.withLastMemoryAccessTransitions());
    EXPECT_FALSE(PTC.withMemoryUpdateTransitions());
    EXPECT_FALSE(PTC.withMemoryAccessTransitions());

    PTC.set(PowerTraceConfig::WITH_OPCODE);
    EXPECT_TRUE(PTC.withPC());
    EXPECT_TRUE(PTC.withOpcode());
    EXPECT_FALSE(PTC.withMemAddress());
    EXPECT_FALSE(PTC.withMemData());
    EXPECT_FALSE(PTC.withInstructionsInputs());
    EXPECT_FALSE(PTC.withInstructionsOutputs());
    EXPECT_FALSE(PTC.withLoadToLoadTransitions());
    EXPECT_FALSE(PTC.withStoreToStoreTransitions());
    EXPECT_FALSE(PTC.withLastMemoryAccessTransitions());
    EXPECT_FALSE(PTC.withMemoryUpdateTransitions());
    EXPECT_FALSE(PTC.withMemoryAccessTransitions());

    PTC.set(PowerTraceConfig::WITH_MEM_ADDRESS);
    EXPECT_TRUE(PTC.withPC());
    EXPECT_TRUE(PTC.withOpcode());
    EXPECT_TRUE(PTC.withMemAddress());
    EXPECT_FALSE(PTC.withMemData());
    EXPECT_FALSE(PTC.withInstructionsInputs());
    EXPECT_FALSE(PTC.withInstructionsOutputs());
    EXPECT_FALSE(PTC.withLoadToLoadTransitions());
    EXPECT_FALSE(PTC.withStoreToStoreTransitions());
    EXPECT_FALSE(PTC.withLastMemoryAccessTransitions());
    EXPECT_FALSE(PTC.withMemoryUpdateTransitions());
    EXPECT_FALSE(PTC.withMemoryAccessTransitions());

    PTC.set(PowerTraceConfig::WITH_MEM_DATA);
    EXPECT_TRUE(PTC.withPC());
    EXPECT_TRUE(PTC.withOpcode());
    EXPECT_TRUE(PTC.withMemAddress());
    EXPECT_TRUE(PTC.withMemData());
    EXPECT_FALSE(PTC.withInstructionsInputs());
    EXPECT_FALSE(PTC.withInstructionsOutputs());
    EXPECT_FALSE(PTC.withLoadToLoadTransitions());
    EXPECT_FALSE(PTC.withStoreToStoreTransitions());
    EXPECT_FALSE(PTC.withLastMemoryAccessTransitions());
    EXPECT_FALSE(PTC.withMemoryUpdateTransitions());
    EXPECT_FALSE(PTC.withMemoryAccessTransitions());

    PTC.set(PowerTraceConfig::WITH_INSTRUCTIONS_INPUTS);
    EXPECT_TRUE(PTC.withPC());
    EXPECT_TRUE(PTC.withOpcode());
    EXPECT_TRUE(PTC.withMemAddress());
    EXPECT_TRUE(PTC.withMemData());
    EXPECT_TRUE(PTC.withInstructionsInputs());
    EXPECT_FALSE(PTC.withInstructionsOutputs());
    EXPECT_FALSE(PTC.withLoadToLoadTransitions());
    EXPECT_FALSE(PTC.withStoreToStoreTransitions());
    EXPECT_FALSE(PTC.withLastMemoryAccessTransitions());
    EXPECT_FALSE(PTC.withMemoryUpdateTransitions());
    EXPECT_FALSE(PTC.withMemoryAccessTransitions());

    PTC.set(PowerTraceConfig::WITH_INSTRUCTIONS_OUTPUTS);
    EXPECT_TRUE(PTC.withPC());
    EXPECT_TRUE(PTC.withOpcode());
    EXPECT_TRUE(PTC.withMemAddress());
    EXPECT_TRUE(PTC.withMemData());
    EXPECT_TRUE(PTC.withInstructionsInputs());
    EXPECT_TRUE(PTC.withInstructionsOutputs());
    EXPECT_FALSE(PTC.withLoadToLoadTransitions());
    EXPECT_FALSE(PTC.withStoreToStoreTransitions());
    EXPECT_FALSE(PTC.withLastMemoryAccessTransitions());
    EXPECT_FALSE(PTC.withMemoryUpdateTransitions());
    EXPECT_FALSE(PTC.withMemoryAccessTransitions());

    PTC.set(PowerTraceConfig::WITH_LOAD_TO_LOAD_TRANSITIONS);
    EXPECT_TRUE(PTC.withPC());
    EXPECT_TRUE(PTC.withOpcode());
    EXPECT_TRUE(PTC.withMemAddress());
    EXPECT_TRUE(PTC.withMemData());
    EXPECT_TRUE(PTC.withInstructionsInputs());
    EXPECT_TRUE(PTC.withInstructionsOutputs());
    EXPECT_TRUE(PTC.withLoadToLoadTransitions());
    EXPECT_FALSE(PTC.withStoreToStoreTransitions());
    EXPECT_FALSE(PTC.withLastMemoryAccessTransitions());
    EXPECT_FALSE(PTC.withMemoryUpdateTransitions());
    EXPECT_TRUE(PTC.withMemoryAccessTransitions());

    PTC.set(PowerTraceConfig::WITH_STORE_TO_STORE_TRANSITIONS);
    EXPECT_TRUE(PTC.withPC());
    EXPECT_TRUE(PTC.withOpcode());
    EXPECT_TRUE(PTC.withMemAddress());
    EXPECT_TRUE(PTC.withMemData());
    EXPECT_TRUE(PTC.withInstructionsInputs());
    EXPECT_TRUE(PTC.withInstructionsOutputs());
    EXPECT_TRUE(PTC.withLoadToLoadTransitions());
    EXPECT_TRUE(PTC.withStoreToStoreTransitions());
    EXPECT_FALSE(PTC.withLastMemoryAccessTransitions());
    EXPECT_FALSE(PTC.withMemoryUpdateTransitions());
    EXPECT_TRUE(PTC.withMemoryAccessTransitions());

    PTC.set(PowerTraceConfig::WITH_LAST_MEMORY_ACCESSES_TRANSITIONS);
    EXPECT_TRUE(PTC.withPC());
    EXPECT_TRUE(PTC.withOpcode());
    EXPECT_TRUE(PTC.withMemAddress());
    EXPECT_TRUE(PTC.withMemData());
    EXPECT_TRUE(PTC.withInstructionsInputs());
    EXPECT_TRUE(PTC.withInstructionsOutputs());
    EXPECT_TRUE(PTC.withLoadToLoadTransitions());
    EXPECT_TRUE(PTC.withStoreToStoreTransitions());
    EXPECT_TRUE(PTC.withLastMemoryAccessTransitions());
    EXPECT_FALSE(PTC.withMemoryUpdateTransitions());
    EXPECT_TRUE(PTC.withMemoryAccessTransitions());

    PTC.set(PowerTraceConfig::WITH_MEMORY_UPDATE_TRANSITIONS);
    EXPECT_TRUE(PTC.withPC());
    EXPECT_TRUE(PTC.withOpcode());
    EXPECT_TRUE(PTC.withMemAddress());
    EXPECT_TRUE(PTC.withMemData());
    EXPECT_TRUE(PTC.withInstructionsInputs());
    EXPECT_TRUE(PTC.withInstructionsOutputs());
    EXPECT_TRUE(PTC.withLoadToLoadTransitions());
    EXPECT_TRUE(PTC.withStoreToStoreTransitions());
    EXPECT_TRUE(PTC.withLastMemoryAccessTransitions());
    EXPECT_TRUE(PTC.withMemoryUpdateTransitions());
    EXPECT_TRUE(PTC.withMemoryAccessTransitions());
}

TEST(PowerAnalysisConfig, base) {
    PowerAnalysisConfig PACHW(PowerAnalysisConfig::HAMMING_WEIGHT,
                              make_unique<TestPowerDumper>(), NoiseSource::ZERO,
                              1.);
    EXPECT_TRUE(PACHW.isHammingWeight());
    EXPECT_FALSE(PACHW.isHammingDistance());
    EXPECT_TRUE(PACHW.addNoise());
    EXPECT_EQ(PACHW.getPowerModel(), PowerAnalysisConfig::HAMMING_WEIGHT);
    for (unsigned i = 0; i < 10; i++)
        EXPECT_EQ(PACHW.getNoise(), 0.0);

    PowerAnalysisConfig PACHD(PowerAnalysisConfig::HAMMING_DISTANCE,
                              make_unique<TestPowerDumper>(),
                              NoiseSource::CONSTANT, 3.);
    EXPECT_FALSE(PACHD.isHammingWeight());
    EXPECT_TRUE(PACHD.isHammingDistance());
    EXPECT_EQ(PACHD.getPowerModel(), PowerAnalysisConfig::HAMMING_DISTANCE);
    for (unsigned i = 0; i < 10; i++)
        EXPECT_EQ(PACHD.getNoise(), 3.0);

    // Test switching power model to use.
    PACHD.set(PowerAnalysisConfig::HAMMING_WEIGHT);
    EXPECT_TRUE(PACHD.isHammingWeight());
    EXPECT_FALSE(PACHD.isHammingDistance());
    EXPECT_EQ(PACHD.getPowerModel(), PowerAnalysisConfig::HAMMING_WEIGHT);
}

TEST(PowerTrace, Oracle) {
    PAF::SCA::PowerTrace::Oracle oracle;
    EXPECT_EQ(oracle.getMemoryState(0x1234, 4, 5), 0ull);
}

TEST_WITH_TEMP_FILE(PowerTraceTarmac, "test-MTAOracle-XXXXXX");
TEST_F(PowerTraceTarmac, MTAOracle) {
    std::ofstream O(getTemporaryFilename());
    O << "1 clk IT (1) 00008000 2000 T thread : MOVS     r0,#0x0\n";
    O << "1 clk R r0 00000000\n";
    O << "1 clk R cpsr 01000000\n";
    O << "2 clk IT (2) 00008000 2101 T thread : MOVS     r1,#0x1\n";
    O << "2 clk R r0 00000001\n";
    O << "2 clk R cpsr 01000000\n";
    O.close();

    PowerTraceConfig PTC;
    unique_ptr<ArchInfo> CPU = make_unique<PAF::V7MInfo>();
    PowerTrace PT(PTC, *CPU);
    for (const auto &I : Insts) {
        PT.add(I);
    }
    EXPECT_EQ(PT.size(), Insts.size());
}

TEST(PowerTrace, base) {
    TestRegBankDumper TRBD(true);
    TestMemAccessesDumper TMAD(true);
    TestInstrDumper TID(true);
    TestTimingInfo TTI;
    PowerTraceConfig PTC;
    vector<PowerAnalysisConfig> PAConfig;
    PAConfig.emplace_back(PowerAnalysisConfig::HAMMING_WEIGHT,
                          make_unique<TestPowerDumper>(), NoiseSource::ZERO,
                          1.0);
    unique_ptr<ArchInfo> CPU = make_unique<PAF::V7MInfo>();
    auto &TPD = dynamic_cast<TestPowerDumper &>(PAConfig[0].getDumper());
    TestOracle oracle(&Insts[0], Insts.size());

    PowerTrace PT(PTC, *CPU);
    EXPECT_STREQ(PT.getArchInfo().description(), "Arm V7M ISA");
    PT.add(Insts[0]);
    EXPECT_EQ(PT.size(), 1);
    EXPECT_EQ(PT[0], Insts[0]);
    PT.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(), 1);
    EXPECT_EQ(TPD.pwf[0], PowerFields(17, 8, 4, 4, 0, 0, 0, &Insts[0]));
    EXPECT_EQ(TRBD.numTraces(), 1);
    EXPECT_EQ(TRBD.numSnapshots(), 1);
    EXPECT_TRUE(TRBD.check(0, 0, {5, 0x21000000, 0, 0, 0}));
    EXPECT_EQ(TMAD.instrWithAccesses(), 0);
    EXPECT_EQ(TMAD.lastAccessesSize(), 0);
    EXPECT_EQ(TID.numInstructions(), 1);

    TPD.reset();
    TRBD.reset();
    TMAD.reset();
    TID.reset();
    PT.add(Insts[1]);
    EXPECT_EQ(PT.size(), 2);
    EXPECT_EQ(PT[0], Insts[0]);
    EXPECT_EQ(PT[1], Insts[1]);
    PT.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(), 2);
    EXPECT_EQ(TPD.pwf[0], PowerFields(17, 8, 4, 4, 0, 0, 0, &Insts[0]));
    EXPECT_EQ(TPD.pwf[1], PowerFields(22, 9, 5, 2, 2, 0, 0, &Insts[1]));
    EXPECT_EQ(TRBD.numTraces(), 1);
    EXPECT_EQ(TRBD.numSnapshots(), 2);
    EXPECT_TRUE(TRBD.check(0, 0, {5, 0x21000000, 0, 0, 0}));
    EXPECT_TRUE(TRBD.check(0, 1, {5, 0x21000000, 5, 0, 0}));
    EXPECT_EQ(TMAD.instrWithAccesses(), 0);
    EXPECT_EQ(TMAD.lastAccessesSize(), 0);
    EXPECT_EQ(TID.numInstructions(), 2);

    TPD.reset();
    TRBD.reset();
    TMAD.reset();
    TID.reset();
    PT.add(Insts[2]);
    PT.add(Insts[3]);
    EXPECT_EQ(PT.size(), 4);
    EXPECT_EQ(PT[0], Insts[0]);
    EXPECT_EQ(PT[1], Insts[1]);
    EXPECT_EQ(PT[2], Insts[2]);
    EXPECT_EQ(PT[3], Insts[3]);
    PT.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(),
              4 + 2); // 4 instructions, 2 extra cycles for LDRD and STRD.
    EXPECT_EQ(TPD.pwf[0], PowerFields(17, 8, 4, 4, 0, 0, 0, &Insts[0]));
    EXPECT_EQ(TPD.pwf[1], PowerFields(22, 9, 5, 2, 2, 0, 0, &Insts[1]));
    EXPECT_EQ(TPD.pwf[2], PowerFields(34, 6, 12, 0, 0, 10, 2, &Insts[2]));
    EXPECT_EQ(TPD.pwf[3], PowerFields(28, 6, 12, 0, 0, 5, 2, nullptr));
    EXPECT_EQ(TPD.pwf[4], PowerFields(40, 6, 14, 2, 0, 10, 2, &Insts[3]));
    EXPECT_EQ(TPD.pwf[5], PowerFields(65.6, 6, 14, 9, 0, 8, 9, nullptr));
    EXPECT_EQ(TRBD.numTraces(), 1);
    EXPECT_EQ(TRBD.numSnapshots(), 4);
    EXPECT_TRUE(TRBD.check(0, 0, {5, 0x21000000, 0, 0, 0}));
    EXPECT_TRUE(TRBD.check(0, 1, {5, 0x21000000, 5, 0, 0}));
    EXPECT_TRUE(TRBD.check(0, 2, {5, 0x21000000, 5, 0, 0}));
    EXPECT_TRUE(TRBD.check(0, 3, {5, 0x21000000, 5, 3, 139108}));
    EXPECT_EQ(TMAD.instrWithAccesses(), 2);
    EXPECT_EQ(TMAD.lastAccessesSize(), 2);
    EXPECT_EQ(TID.numInstructions(), 4);

    // Move construct.
    PowerTrace PT2(std::move(PT));
    TPD.reset();
    TRBD.reset();
    TMAD.reset();
    TID.reset();
    PT2.add(Insts[0]);
    PT2.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(), 7);
    EXPECT_EQ(TPD.pwf[0], PowerFields(17, 8, 4, 4, 0, 0, 0, &Insts[0]));
    EXPECT_EQ(TPD.pwf[1], PowerFields(22, 9, 5, 2, 2, 0, 0, &Insts[1]));
    EXPECT_EQ(TPD.pwf[2], PowerFields(34, 6, 12, 0, 0, 10, 2, &Insts[2]));
    EXPECT_EQ(TPD.pwf[3], PowerFields(28, 6, 12, 0, 0, 5, 2, nullptr));
    EXPECT_EQ(TPD.pwf[4], PowerFields(40, 6, 14, 2, 0, 10, 2, &Insts[3]));
    EXPECT_EQ(TPD.pwf[5], PowerFields(65.6, 6, 14, 9, 0, 8, 9, nullptr));
    EXPECT_EQ(TPD.pwf[6], PowerFields(17, 8, 4, 4, 0, 0, 0, &Insts[0]));
}

TEST(PowerTrace, withNoise) {
    TestRegBankDumper TRBD;
    TestMemAccessesDumper TMAD;
    TestInstrDumper TID;
    TestTimingInfo TTI;
    PowerTraceConfig PTC;
    vector<PowerAnalysisConfig> PAConfig;
    PAConfig.emplace_back(PowerAnalysisConfig::HAMMING_WEIGHT,
                          make_unique<TestPowerDumper>(), NoiseSource::CONSTANT,
                          2.0);
    auto &TPD = dynamic_cast<TestPowerDumper &>(PAConfig[0].getDumper());
    unique_ptr<ArchInfo> CPU = make_unique<PAF::V7MInfo>();
    TestOracle oracle(&Insts[0], Insts.size());

    PowerTrace PT(PTC, *CPU);
    PT.add(Insts[0]);
    PT.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    PAConfig[0].setWithoutNoise();
    PT.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(), 2);
    EXPECT_GT(PowerFields::noise(TPD.pwf[1], TPD.pwf[0]), 0.0);
    EXPECT_EQ(TRBD.numTraces(), 0);
    EXPECT_EQ(TRBD.numSnapshots(), 0);
    EXPECT_EQ(TMAD.instrWithAccesses(), 0);
    EXPECT_EQ(TMAD.lastAccessesSize(), 0);
    EXPECT_EQ(TID.numInstructions(), 0);
}

TEST(PowerTrace, HammingWeightWithConfig) {
    // Tests that only the source contributing to the power have non zero power.
    TestRegBankDumper TRBD(true);
    TestMemAccessesDumper TMAD(true);
    TestInstrDumper TID(true);
    TestTimingInfo TTI;
    PowerTraceConfig PTC;
    vector<PowerAnalysisConfig> PAConfig;
    PAConfig.emplace_back(PowerAnalysisConfig::HAMMING_WEIGHT,
                          make_unique<TestPowerDumper>(), NoiseSource::ZERO,
                          1.0);
    auto &TPD = dynamic_cast<TestPowerDumper &>(PAConfig[0].getDumper());
    unique_ptr<ArchInfo> CPU = make_unique<PAF::V7MInfo>();
    TestOracle oracle(&Insts[0], Insts.size());

    PTC.clear().set(PowerTraceConfig::WITH_PC);
    PowerTrace PT1(PTC, *CPU);
    PT1.add(Insts[0]);
    PT1.add(Insts[1]);
    PT1.add(Insts[2]);
    PT1.add(Insts[3]);
    PT1.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(),
              6); // 4 instructions, 2 extra cycles.
    EXPECT_EQ(TPD.pwf[0], PowerFields(8, 8, 0, 0, 0, 0, 0, &Insts[0]));
    EXPECT_EQ(TPD.pwf[1], PowerFields(9, 9, 0, 0, 0, 0, 0, &Insts[1]));
    EXPECT_EQ(TPD.pwf[2], PowerFields(6, 6, 0, 0, 0, 0, 0, &Insts[2]));
    EXPECT_EQ(TPD.pwf[3], PowerFields(6, 6, 0, 0, 0, 0, 0, nullptr));
    EXPECT_EQ(TPD.pwf[4], PowerFields(6, 6, 0, 0, 0, 0, 0, &Insts[3]));
    EXPECT_EQ(TPD.pwf[5], PowerFields(6, 6, 0, 0, 0, 0, 0, nullptr));
    EXPECT_EQ(TRBD.numTraces(), 1);
    EXPECT_EQ(TRBD.numSnapshots(), 4);
    EXPECT_TRUE(TRBD.check(0, 0, {5, 0x21000000, 0, 0, 0}));
    EXPECT_TRUE(TRBD.check(0, 1, {5, 0x21000000, 5, 0, 0}));
    EXPECT_TRUE(TRBD.check(0, 2, {5, 0x21000000, 5, 0, 0}));
    EXPECT_TRUE(TRBD.check(0, 3, {5, 0x21000000, 5, 3, 139108}));
    EXPECT_EQ(TMAD.instrWithAccesses(), 2);
    EXPECT_EQ(TMAD.lastAccessesSize(), 2);
    EXPECT_TRUE(TMAD.check(Insts[3].memAccess));
    EXPECT_EQ(TID.numInstructions(), 4);

    TPD.reset();
    TRBD.reset();
    TMAD.reset();
    TID.reset();
    PTC.clear().set(PowerTraceConfig::WITH_MEM_ADDRESS);
    PowerTrace PT2(PTC, *CPU);
    PT2.add(Insts[0]);
    PT2.add(Insts[1]);
    PT2.add(Insts[2]);
    PT2.add(Insts[3]);
    PT2.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(),
              6); // 4 instructions, 2 extra cycles.
    EXPECT_EQ(TPD.pwf[0], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts[0]));
    EXPECT_EQ(TPD.pwf[1], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts[1]));
    EXPECT_EQ(TPD.pwf[2], PowerFields(12, 0, 0, 0, 0, 10, 0, &Insts[2]));
    EXPECT_EQ(TPD.pwf[3], PowerFields(6, 0, 0, 0, 0, 5, 0, nullptr));
    EXPECT_EQ(TPD.pwf[4], PowerFields(12, 0, 0, 0, 0, 10, 0, &Insts[3]));
    EXPECT_EQ(TPD.pwf[5], PowerFields(9.6, 0, 0, 0, 0, 8, 0, nullptr));

    TPD.reset();
    TRBD.reset();
    TMAD.reset();
    TID.reset();
    PTC.clear().set(PowerTraceConfig::WITH_MEM_DATA);
    PowerTrace PT3(PTC, *CPU);
    PT3.add(Insts[0]);
    PT3.add(Insts[1]);
    PT3.add(Insts[2]);
    PT3.add(Insts[3]);
    PT3.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(),
              6); // 4 instructions, 2 extra cycles.
    EXPECT_EQ(TPD.pwf[0], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts[0]));
    EXPECT_EQ(TPD.pwf[1], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts[1]));
    EXPECT_EQ(TPD.pwf[2], PowerFields(4, 0, 0, 0, 0, 0, 2, &Insts[2]));
    EXPECT_EQ(TPD.pwf[3], PowerFields(4, 0, 0, 0, 0, 0, 2, nullptr));
    EXPECT_EQ(TPD.pwf[4], PowerFields(4, 0, 0, 0, 0, 0, 2, &Insts[3]));
    EXPECT_EQ(TPD.pwf[5], PowerFields(18, 0, 0, 0, 0, 0, 9, nullptr));

    TPD.reset();
    TRBD.reset();
    TMAD.reset();
    TID.reset();
    PTC.clear().set(PowerTraceConfig::WITH_OPCODE);
    PowerTrace PT4(PTC, *CPU);
    PT4.add(Insts[0]);
    PT4.add(Insts[1]);
    PT4.add(Insts[2]);
    PT4.add(Insts[3]);
    PT4.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(),
              6); // 4 instructions, 2 extra cycles.
    EXPECT_EQ(TPD.pwf[0], PowerFields(4, 0, 4, 0, 0, 0, 0, &Insts[0]));
    EXPECT_EQ(TPD.pwf[1], PowerFields(5, 0, 5, 0, 0, 0, 0, &Insts[1]));
    EXPECT_EQ(TPD.pwf[2], PowerFields(12, 0, 12, 0, 0, 0, 0, &Insts[2]));
    EXPECT_EQ(TPD.pwf[3], PowerFields(12, 0, 12, 0, 0, 0, 0, nullptr));
    EXPECT_EQ(TPD.pwf[4], PowerFields(14, 0, 14, 0, 0, 0, 0, &Insts[3]));
    EXPECT_EQ(TPD.pwf[5], PowerFields(14, 0, 14, 0, 0, 0, 0, nullptr));

    TPD.reset();
    TRBD.reset();
    TMAD.reset();
    TID.reset();
    PTC.clear().set(PowerTraceConfig::WITH_INSTRUCTIONS_INPUTS);
    PowerTrace PT5(PTC, *CPU);
    PT5.add(Insts[0]);
    PT5.add(Insts[1]);
    PT5.add(Insts[2]);
    PT5.add(Insts[3]);
    PT5.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(),
              6); // 4 instructions, 2 extra cycles.
    EXPECT_EQ(TPD.pwf[0], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts[0]));
    EXPECT_EQ(TPD.pwf[1], PowerFields(4, 0, 0, 0, 2, 0, 0, &Insts[1]));
    EXPECT_EQ(TPD.pwf[2], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts[2]));
    EXPECT_EQ(TPD.pwf[3], PowerFields(0, 0, 0, 0, 0, 0, 0, nullptr));
    EXPECT_EQ(TPD.pwf[4], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts[3]));
    EXPECT_EQ(TPD.pwf[5], PowerFields(0, 0, 0, 0, 0, 0, 0, nullptr));

    TPD.reset();
    TRBD.reset();
    TMAD.reset();
    TID.reset();
    PTC.clear().set(PowerTraceConfig::WITH_INSTRUCTIONS_OUTPUTS);
    PowerTrace PT6(PTC, *CPU);
    PT6.add(Insts[0]);
    PT6.add(Insts[1]);
    PT6.add(Insts[2]);
    PT6.add(Insts[3]);
    PT6.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(),
              6); // 4 instructions, 2 extra cycles.
    EXPECT_EQ(TPD.pwf[0], PowerFields(5, 0, 0, 4, 0, 0, 0, &Insts[0]));
    EXPECT_EQ(TPD.pwf[1], PowerFields(4, 0, 0, 2, 0, 0, 0, &Insts[1]));
    EXPECT_EQ(TPD.pwf[2], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts[2]));
    EXPECT_EQ(TPD.pwf[3], PowerFields(0, 0, 0, 0, 0, 0, 0, nullptr));
    EXPECT_EQ(TPD.pwf[4], PowerFields(4, 0, 0, 2, 0, 0, 0, &Insts[3]));
    EXPECT_EQ(TPD.pwf[5], PowerFields(18, 0, 0, 9, 0, 0, 0, nullptr));
}

// clang-format off
// Test sequence for checking load-to-load / store-to-store hamming distance computation.
static const array<ReferenceInstruction,7> Insts2{{
    {
        27, IE_EXECUTED, 0x08324, THUMB, 16, 0x02105, "movs r1,#5",
        {},
        {
            RegisterAccess("r1", 5, RegisterAccess::Type::WRITE),
            RegisterAccess("cpsr", 0x21000000, RegisterAccess::Type::WRITE),
        }
    },
    {
        28, IE_EXECUTED, 0x08326, ARM, 32, 0xf8db0800, "ldr.w      r0,[r11,#2048]",
        {
            MemoryAccess(4, 0xf939b40, 0xdeadbeef, MemoryAccess::Type::READ)
        },
        {
            RegisterAccess("r0", 0xdeadbeef, RegisterAccess::Type::WRITE),
            RegisterAccess("r11", 0xf939340, RegisterAccess::Type::READ)
        }
    },
    {
        29, IE_EXECUTED, 0x0832a, THUMB, 16, 0x4408, "add      r0,r1",
        {},
        {
            RegisterAccess("r0", 0xdeadbef4, RegisterAccess::Type::WRITE),
            RegisterAccess("r1", 0x05, RegisterAccess::Type::READ)
        }
    },
    {
        30, IE_EXECUTED, 0x0832c, ARM, 32, 0xf8cb07fc, "str.w      r0,[r11,#2044]",
        {
            MemoryAccess(4, 0xf939b3c, 0xdeadbef4, MemoryAccess::Type::WRITE)
        },
        {
            RegisterAccess("r0", 0xdeadbef4, RegisterAccess::Type::READ),
            RegisterAccess("r11", 0xf93933c, RegisterAccess::Type::READ)
        }
    },
    {
        31, IE_EXECUTED, 0x08330, ARM, 32, 0xf8db07fc, "ldr.w      r0,[r11,#2044]",
        {
            MemoryAccess(4, 0xf939b3c, 0xdeadbef4, MemoryAccess::Type::READ)
        },
        {
            RegisterAccess("r0", 0xdeadbef4, RegisterAccess::Type::WRITE),
            RegisterAccess("r11", 0xf939340, RegisterAccess::Type::READ)
        }
    },
    {
        32, IE_EXECUTED, 0x08332, THUMB, 16, 0x4408, "add      r0,r1",
        {},
        {
            RegisterAccess("r0", 0xdeadbef9, RegisterAccess::Type::WRITE),
            RegisterAccess("r1", 0x05, RegisterAccess::Type::READ)
        }
    },
    {
        33, IE_EXECUTED, 0x08334, ARM, 32, 0xf8cb0800, "str.w      r0,[r11,#2048]",
        {
            MemoryAccess(4, 0xf939b40, 0xdeadbef9, MemoryAccess::Type::WRITE)
        },
        {
            RegisterAccess("r0", 0xdeadbef9, RegisterAccess::Type::READ),
            RegisterAccess("r11", 0xf93933c, RegisterAccess::Type::READ)
        }
    },
}};
// clang-format on

namespace {
// For use with Insts sequence.
class InstsStateOracle : public PowerTrace::Oracle {
  public:
    InstsStateOracle(std::initializer_list<uint64_t> il)
        : regBankInitialState(il) {}
    InstsStateOracle(size_t NR = 18, uint64_t v = 0)
        : regBankInitialState(NR, v) {}

    [[nodiscard]] std::vector<uint64_t> getRegBankState(Time t) const override {
        return regBankInitialState;
    }

  private:
    const vector<uint64_t> regBankInitialState;
};

// For use with Insts2 sequence.
class Insts2StateOracle : public PowerTrace::Oracle {
  public:
    Insts2StateOracle(std::initializer_list<uint64_t> il)
        : regBankInitialState(il) {}
    Insts2StateOracle(size_t NR = 18, uint64_t v = 0)
        : regBankInitialState(NR, v) {}

    [[nodiscard]] std::vector<uint64_t> getRegBankState(Time t) const override {
        return regBankInitialState;
    }

    [[nodiscard]] uint64_t getMemoryState(Addr address, size_t size,
                                          Time t) const override {
        if (t == Insts2[3].time - 1 && address == 0xf939b3c)
            return 0x00cafe00;
        if (t == Insts2[6].time - 1 && address == 0xf939b40)
            return 0xdeadbeef;
        return 0;
    }

  private:
    const vector<uint64_t> regBankInitialState;
};
} // namespace

TEST(PowerTrace, HammingDistanceWithConfig) {

    // Tests that only the source contributing to the power have non zero power.
    TestRegBankDumper TRBD;
    TestMemAccessesDumper TMAD;
    TestInstrDumper TID;
    TestTimingInfo TTI;
    PowerTraceConfig PTC;
    vector<PowerAnalysisConfig> PAConfig;
    PAConfig.emplace_back(PowerAnalysisConfig::HAMMING_DISTANCE,
                          make_unique<TestPowerDumper>(), NoiseSource::ZERO,
                          0.0);
    auto &TPD = dynamic_cast<TestPowerDumper &>(PAConfig[0].getDumper());
    unique_ptr<ArchInfo> CPU = make_unique<PAF::V7MInfo>();
    InstsStateOracle oracle;

    EXPECT_TRUE(PAConfig[0].isHammingDistance());

    PTC.clear().set(PowerTraceConfig::WITH_PC);
    PowerTrace PT1(PTC, *CPU);
    PT1.add(Insts[0]);
    PT1.add(Insts[1]);
    PT1.add(Insts[2]);
    PT1.add(Insts[3]);
    PT1.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(),
              6); // 4 instructions, 2 extra cycles.
    EXPECT_EQ(TPD.pwf[0], PowerFields(8, 8, 0, 0, 0, 0, 0, &Insts[0]));
    EXPECT_EQ(TPD.pwf[1], PowerFields(1, 1, 0, 0, 0, 0, 0, &Insts[1]));
    EXPECT_EQ(TPD.pwf[2], PowerFields(5, 5, 0, 0, 0, 0, 0, &Insts[2]));
    EXPECT_EQ(TPD.pwf[3], PowerFields(5, 5, 0, 0, 0, 0, 0, nullptr));
    EXPECT_EQ(TPD.pwf[4], PowerFields(2, 2, 0, 0, 0, 0, 0, &Insts[3]));
    EXPECT_EQ(TPD.pwf[5], PowerFields(2, 2, 0, 0, 0, 0, 0, nullptr));
    EXPECT_EQ(TMAD.instrWithAccesses(), 0);
    EXPECT_EQ(TMAD.lastAccessesSize(), 0);
    EXPECT_EQ(TID.numInstructions(), 0);

    TPD.reset();
    TRBD.reset();
    TMAD.reset();
    TID.reset();
    PTC.clear().set(PowerTraceConfig::WITH_OPCODE);
    PowerTrace PT2(PTC, *CPU);
    PT2.add(Insts[0]);
    PT2.add(Insts[1]);
    PT2.add(Insts[2]);
    PT2.add(Insts[3]);
    PT2.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(),
              6); // 4 instructions, 2 extra cycles.
    EXPECT_EQ(TPD.pwf[0], PowerFields(4, 0, 4, 0, 0, 0, 0, &Insts[0]));
    EXPECT_EQ(TPD.pwf[1], PowerFields(9, 0, 9, 0, 0, 0, 0, &Insts[1]));
    EXPECT_EQ(TPD.pwf[2], PowerFields(13, 0, 13, 0, 0, 0, 0, &Insts[2]));
    EXPECT_EQ(TPD.pwf[3], PowerFields(13, 0, 13, 0, 0, 0, 0, nullptr));
    EXPECT_EQ(TPD.pwf[4], PowerFields(8, 0, 8, 0, 0, 0, 0, &Insts[3]));
    EXPECT_EQ(TPD.pwf[5], PowerFields(8, 0, 8, 0, 0, 0, 0, nullptr));

    // Instructions' inputs are ignored in the Hamming distance power model.
    TPD.reset();
    TRBD.reset();
    TMAD.reset();
    TID.reset();
    PTC.clear().set(PowerTraceConfig::WITH_INSTRUCTIONS_INPUTS);
    PowerTrace PT3(PTC, *CPU);
    PT3.add(Insts[0]);
    PT3.add(Insts[1]);
    PT3.add(Insts[2]);
    PT3.add(Insts[3]);
    PT3.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(),
              6); // 4 instructions, 2 extra cycles.
    EXPECT_EQ(TPD.pwf[0], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts[0]));
    EXPECT_EQ(TPD.pwf[1], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts[1]));
    EXPECT_EQ(TPD.pwf[2], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts[2]));
    EXPECT_EQ(TPD.pwf[3], PowerFields(0, 0, 0, 0, 0, 0, 0, nullptr));
    EXPECT_EQ(TPD.pwf[4], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts[3]));
    EXPECT_EQ(TPD.pwf[5], PowerFields(0, 0, 0, 0, 0, 0, 0, nullptr));

    TPD.reset();
    TRBD.reset();
    TMAD.reset();
    TID.reset();
    PTC.clear().set(PowerTraceConfig::WITH_INSTRUCTIONS_OUTPUTS);
    PowerTrace PT4(PTC, *CPU);
    PT4.add(Insts[0]);
    PT4.add(Insts[1]);
    PT4.add(Insts[2]);
    PT4.add(Insts[3]);
    InstsStateOracle oracle2{{/* R0: */ 0,
                              /* R1: */ 0,
                              /* R2: */ 3,
                              /* R3: */ 0,
                              /* R4: */ 0,
                              /* R5: */ 0,
                              /* R6: */ 0,
                              /* R7: */ 0,
                              /* R8: */ 0,
                              /* R9: */ 0,
                              /* R10: */ 0,
                              /* R11: */ 0,
                              /* R12: */ 0,
                              /* MSP: */ 0,
                              /* LR: */ 0,
                              /* PC: */ 0,
                              /* CPSR: */ 0,
                              /* PSR: */ 0}};
    PT4.analyze(PAConfig, oracle2, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(),
              6); // 4 instructions, 2 extra cycles.
    EXPECT_EQ(TPD.pwf[0], PowerFields(5, 0, 0, 4, 0, 0, 0, &Insts[0]));
    EXPECT_EQ(TPD.pwf[1], PowerFields(4, 0, 0, 2, 0, 0, 0, &Insts[1]));
    EXPECT_EQ(TPD.pwf[2], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts[2]));
    EXPECT_EQ(TPD.pwf[3], PowerFields(0, 0, 0, 0, 0, 0, 0, nullptr));
    EXPECT_EQ(TPD.pwf[4], PowerFields(4, 0, 0, 2, 0, 0, 0, &Insts[3]));
    EXPECT_EQ(TPD.pwf[5], PowerFields(18, 0, 0, 9, 0, 0, 0, nullptr));

    TPD.reset();
    TRBD.reset();
    TMAD.reset();
    TID.reset();
    PTC.clear().set(PowerTraceConfig::WITH_MEM_ADDRESS,
                    PowerTraceConfig::WITH_LAST_MEMORY_ACCESSES_TRANSITIONS);
    PowerTrace PT5(PTC, *CPU);
    PT5.add(Insts[0]);
    PT5.add(Insts[1]);
    PT5.add(Insts[2]);
    PT5.add(Insts[3]);
    PT5.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(),
              6); // 4 instructions, 2 extra cycles.
    EXPECT_EQ(TPD.pwf[0], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts[0]));
    EXPECT_EQ(TPD.pwf[1], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts[1]));
    EXPECT_EQ(TPD.pwf[2], PowerFields(12, 0, 0, 0, 0, 10, 0, &Insts[2]));
    EXPECT_EQ(TPD.pwf[3], PowerFields(8.4, 0, 0, 0, 0, 7, 0, nullptr));
    EXPECT_EQ(TPD.pwf[4], PowerFields(6, 0, 0, 0, 0, 5, 0, &Insts[3]));
    EXPECT_EQ(TPD.pwf[5], PowerFields(4.8, 0, 0, 0, 0, 4, 0, nullptr));

    TPD.reset();
    TRBD.reset();
    TMAD.reset();
    TID.reset();
    PTC.clear().set(PowerTraceConfig::WITH_MEM_DATA,
                    PowerTraceConfig::WITH_LAST_MEMORY_ACCESSES_TRANSITIONS);
    PowerTrace PT6(PTC, *CPU);
    PT6.add(Insts[0]);
    PT6.add(Insts[1]);
    PT6.add(Insts[2]);
    PT6.add(Insts[3]);
    PT6.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(),
              6); // 4 instructions, 2 extra cycles.
    EXPECT_EQ(TPD.pwf[0], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts[0]));
    EXPECT_EQ(TPD.pwf[1], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts[1]));
    EXPECT_EQ(TPD.pwf[2], PowerFields(4, 0, 0, 0, 0, 0, 2, &Insts[2]));
    EXPECT_EQ(TPD.pwf[3], PowerFields(0, 0, 0, 0, 0, 0, 0, nullptr));
    EXPECT_EQ(TPD.pwf[4], PowerFields(4, 0, 0, 0, 0, 0, 2, &Insts[3]));
    EXPECT_EQ(TPD.pwf[5], PowerFields(22, 0, 0, 0, 0, 0, 11, nullptr));

    TPD.reset();
    TRBD.reset();
    TMAD.reset();
    TID.reset();
    PTC.clear().set(PowerTraceConfig::WITH_MEM_ADDRESS,
                    PowerTraceConfig::WITH_LOAD_TO_LOAD_TRANSITIONS,
                    PowerTraceConfig::WITH_STORE_TO_STORE_TRANSITIONS);
    PowerTrace PT7(PTC, *CPU);
    PT7.add(Insts2[0]);
    PT7.add(Insts2[1]);
    PT7.add(Insts2[2]);
    PT7.add(Insts2[3]);
    PT7.add(Insts2[4]);
    PT7.add(Insts2[5]);
    PT7.add(Insts2[6]);
    PT7.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(),
              7); // 7 instructions.
    EXPECT_EQ(TPD.pwf[0], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts2[0]));
    EXPECT_EQ(TPD.pwf[1], PowerFields(16.8, 0, 0, 0, 0, 14, 0, &Insts2[1]));
    EXPECT_EQ(TPD.pwf[2], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts2[2]));
    EXPECT_EQ(TPD.pwf[3], PowerFields(20.4, 0, 0, 0, 0, 17, 0, &Insts2[3]));
    EXPECT_EQ(TPD.pwf[4], PowerFields(6, 0, 0, 0, 0, 5, 0, &Insts2[4]));
    EXPECT_EQ(TPD.pwf[5], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts2[5]));
    EXPECT_EQ(TPD.pwf[6], PowerFields(6, 0, 0, 0, 0, 5, 0, &Insts2[6]));
    EXPECT_EQ(TMAD.instrWithAccesses(), 0);
    EXPECT_EQ(TMAD.lastAccessesSize(), 0);
    EXPECT_EQ(TID.numInstructions(), 0);

    TPD.reset();
    TRBD.reset();
    TMAD.reset();
    TID.reset();
    PTC.clear().set(PowerTraceConfig::WITH_MEM_DATA,
                    PowerTraceConfig::WITH_LOAD_TO_LOAD_TRANSITIONS,
                    PowerTraceConfig::WITH_STORE_TO_STORE_TRANSITIONS);
    PowerTrace PT8(PTC, *CPU);
    PT8.add(Insts2[0]);
    PT8.add(Insts2[1]);
    PT8.add(Insts2[2]);
    PT8.add(Insts2[3]);
    PT8.add(Insts2[4]);
    PT8.add(Insts2[5]);
    PT8.add(Insts2[6]);
    PT8.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(),
              7); // 7 instructions.
    EXPECT_EQ(TPD.pwf[0], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts2[0]));
    EXPECT_EQ(TPD.pwf[1], PowerFields(48, 0, 0, 0, 0, 0, 24, &Insts2[1]));
    EXPECT_EQ(TPD.pwf[2], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts2[2]));
    EXPECT_EQ(TPD.pwf[3], PowerFields(44, 0, 0, 0, 0, 0, 22, &Insts2[3]));
    EXPECT_EQ(TPD.pwf[4], PowerFields(8, 0, 0, 0, 0, 0, 4, &Insts2[4]));
    EXPECT_EQ(TPD.pwf[5], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts2[5]));
    EXPECT_EQ(TPD.pwf[6], PowerFields(6, 0, 0, 0, 0, 0, 3, &Insts2[6]));

    TPD.reset();
    TRBD.reset();
    TMAD.reset();
    TID.reset();
    PTC.clear().set(PowerTraceConfig::WITH_MEM_DATA,
                    PowerTraceConfig::WITH_MEMORY_UPDATE_TRANSITIONS);
    PowerTrace PT9(PTC, *CPU);
    PT9.add(Insts2[0]);
    PT9.add(Insts2[1]);
    PT9.add(Insts2[2]);
    PT9.add(Insts2[3]);
    PT9.add(Insts2[4]);
    PT9.add(Insts2[5]);
    PT9.add(Insts2[6]);
    Insts2StateOracle oracle3;
    PT9.analyze(PAConfig, oracle3, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(),
              7); // 7 instructions.
    EXPECT_EQ(TPD.pwf[0], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts2[0]));
    EXPECT_EQ(TPD.pwf[1], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts2[1]));
    EXPECT_EQ(TPD.pwf[2], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts2[2]));
    EXPECT_EQ(TPD.pwf[3], PowerFields(34, 0, 0, 0, 0, 0, 17, &Insts2[3]));
    EXPECT_EQ(TPD.pwf[4], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts2[4]));
    EXPECT_EQ(TPD.pwf[5], PowerFields(0, 0, 0, 0, 0, 0, 0, &Insts2[5]));
    EXPECT_EQ(TPD.pwf[6], PowerFields(6, 0, 0, 0, 0, 0, 3, &Insts2[6]));
}

TEST(PowerTrace, withConfigAndNoise) {
    // Tests that only the sources contributing to the overall power get some
    // noise.
    TestRegBankDumper TRBD;
    TestMemAccessesDumper TMAD;
    TestInstrDumper TID;
    TestTimingInfo TTI;
    unique_ptr<ArchInfo> CPU = make_unique<PAF::V7MInfo>();
    vector<PowerAnalysisConfig> PAConfig;
    PAConfig.emplace_back(PowerAnalysisConfig::HAMMING_WEIGHT,
                          make_unique<TestPowerDumper>(), NoiseSource::CONSTANT,
                          1.0);
    auto &TPD = dynamic_cast<TestPowerDumper &>(PAConfig[0].getDumper());
    PowerTraceConfig PTC(PowerTraceConfig::WITH_OPCODE);
    PowerTrace::Oracle oracle;

    PowerTrace PT(PTC, *CPU);
    PT.add(Insts[0]);
    PT.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    PAConfig[0].setWithoutNoise();
    PT.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(), 2);
    EXPECT_GT(PowerFields::noise(TPD.pwf[1], TPD.pwf[0]), 0.0);
    EXPECT_EQ(TPD.pwf[0].addr, 0.0);
    EXPECT_EQ(TPD.pwf[0].data, 0.0);
    EXPECT_EQ(TPD.pwf[0].ireg, 0.0);
    EXPECT_EQ(TPD.pwf[0].oreg, 0.0);
    EXPECT_EQ(TPD.pwf[0].pc, 0.0);
    EXPECT_EQ(TMAD.instrWithAccesses(), 0);
    EXPECT_EQ(TMAD.lastAccessesSize(), 0);
    EXPECT_EQ(TID.numInstructions(), 0);

    PTC.clear().set(PowerTraceConfig::WITH_INSTRUCTIONS_OUTPUTS);
    PAConfig[0].setWithNoise();
    TPD.reset();
    TRBD.reset();
    TMAD.reset();
    TID.reset();
    PowerTrace PT2(PTC, *CPU);
    PT2.add(Insts[0]);
    PT2.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    PAConfig[0].setWithoutNoise();
    PT2.analyze(PAConfig, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPD.pwf.size(), 2);
    EXPECT_GT(PowerFields::noise(TPD.pwf[1], TPD.pwf[0]), 0.0);
    EXPECT_EQ(TPD.pwf[0].addr, 0.0);
    EXPECT_EQ(TPD.pwf[0].data, 0.0);
    EXPECT_EQ(TPD.pwf[0].ireg, 0.0);
    EXPECT_EQ(TPD.pwf[0].instr, 0.0);
    EXPECT_EQ(TPD.pwf[0].pc, 0.0);
}

TEST(PowerTrace, multipleAnalyses) {
    TestRegBankDumper TRBD;
    TestMemAccessesDumper TMAD;
    TestInstrDumper TID;
    TestTimingInfo TTI;
    PowerTraceConfig PTC;
    vector<PowerAnalysisConfig> PAConfigs;
    PAConfigs.emplace_back(PowerAnalysisConfig::HAMMING_WEIGHT,
                           make_unique<TestPowerDumper>(), NoiseSource::ZERO,
                           1.0);
    PAConfigs.emplace_back(PowerAnalysisConfig::HAMMING_DISTANCE,
                           make_unique<TestPowerDumper>(), NoiseSource::ZERO,
                           1.0);
    for (auto &cfg : PAConfigs)
        cfg.setWithoutNoise();
    unique_ptr<ArchInfo> CPU = make_unique<PAF::V7MInfo>();
    auto &TPDHW = dynamic_cast<TestPowerDumper &>(PAConfigs[0].getDumper());
    auto &TPDHD = dynamic_cast<TestPowerDumper &>(PAConfigs[1].getDumper());
    InstsStateOracle oracle;

    PowerTrace PT(PTC, *CPU);
    EXPECT_STREQ(PT.getArchInfo().description(), "Arm V7M ISA");
    PT.add(Insts[0]);
    PT.add(Insts[1]);
    PT.add(Insts[2]);
    PT.add(Insts[3]);
    PT.analyze(PAConfigs, oracle, TTI, TRBD, TMAD, TID);
    EXPECT_EQ(TPDHW.pwf.size(),
              6); // 4 instructions, 2 extra cycles.
    EXPECT_EQ(TPDHW.pwf[0], PowerFields(17, 8, 4, 4, 0, 0, 0, &Insts[0]));
    EXPECT_EQ(TPDHW.pwf[1], PowerFields(22, 9, 5, 2, 2, 0, 0, &Insts[1]));
    EXPECT_EQ(TPDHW.pwf[2], PowerFields(34, 6, 12, 0, 0, 10, 2, &Insts[2]));
    EXPECT_EQ(TPDHW.pwf[3], PowerFields(28, 6, 12, 0, 0, 5, 2, nullptr));
    EXPECT_EQ(TPDHW.pwf[4], PowerFields(40, 6, 14, 2, 0, 10, 2, &Insts[3]));
    EXPECT_EQ(TPDHW.pwf[5], PowerFields(65.6, 6, 14, 9, 0, 8, 9, nullptr));

    EXPECT_EQ(TPDHD.pwf.size(),
              6); // 4 instructions, 2 extra cycles.
    // Check TPDHD

    // Test regbank, memaccesses, instr and timing.
}

#if 0
// Additional tests for PowerTrace::Oracle and PowerTrace::MTAOracle
namespace {
// Helper to build a TracePair pointing to existing sample files in build/unit-tests
// Helper to build a TracePair pointing to tarmac and index files
static ::TracePair makeTracePairLocal(const std::string &tarmac,
                                      const std::string &index) {
    ::TracePair TP;
    TP.tarmac_filename = tarmac;
    TP.index_on_disk = true;
    TP.index_filename = index;
    TP.memory_index = nullptr;
    return TP;
}

// Fake MTAnalyzer overriding register and memory queries
class FakeMTA : public PAF::MTAnalyzer {
  public:
    FakeMTA()
        : MTAnalyzer(tracePair,
                     std::string(SAMPLES_SRC_DIR) + "instances-v7m.elf"),
          tracePair(makeTracePairLocal(
              std::string(SAMPLES_SRC_DIR) + "instances-v7m.trace",
              std::string(SAMPLES_SRC_DIR) + "instances-v7m.trace.index")) {
        // Copy index file to working dir for MTAnalyzer
        std::ifstream in(std::string(SAMPLES_SRC_DIR) +
                             "instances-v7m.trace.index",
                         std::ios::binary);
        std::ofstream out("instances-v7m.trace.index", std::ios::binary);
        out << in.rdbuf();
        run_indexer(tracePair, IndexerParams(), IndexerDiagnostics(),
                    ParseParams(/*big_endian=*/false));
    }

  private:
    ::TracePair tracePair;
    uint64_t getRegisterValueAtTime(const std::string &reg, Time t) const {
        // return reg index + time for r0..r9
        if (reg.size() > 1 && reg[0] == 'r' && reg[1] >= '0' && reg[1] <= '9') {
            unsigned idx = std::stoul(reg.substr(1));
            return idx + t;
        }
        // default for other regs
        return 1000 + t;
    }
    std::vector<uint8_t> getMemoryValueAtTime(uint64_t address,
                                              size_t num_bytes, Time t) const {
        std::vector<uint8_t> m(num_bytes);
        for (size_t i = 0; i < num_bytes; ++i)
            m[i] = static_cast<uint8_t>(address + i + t);
        return m;
    }
};
} // namespace

TEST(MTAOracle, GetRegBankState) {
    FakeMTA mta;
    PAF::V7MInfo cpu;
    PAF::SCA::PowerTrace::MTAOracle oracle(mta, cpu);
    auto regs = oracle.getRegBankState(10);
    ASSERT_GE(regs.size(), size_t(4));
    EXPECT_EQ(regs[0], 0 + 10);
    EXPECT_EQ(regs[1], 1 + 10);
    EXPECT_EQ(regs[2], 2 + 10);
    EXPECT_EQ(regs[3], 3 + 10);
}

TEST(MTAOracle, GetMemoryState) {
    FakeMTA mta;
    PAF::V7MInfo cpu;
    PAF::SCA::PowerTrace::MTAOracle oracle(mta, cpu);
    // prepare a 2-byte pattern: byte0 = addr+0+t, byte1 = addr+1+t
    uint64_t v = oracle.getMemoryState(0x10, 2, 3);
    // little-endian: v = (mem[1] << 1) | mem[0]
    uint64_t exp = (uint64_t((0x10 + 1 + 3)) << 1) | (0x10 + 0 + 3);
    EXPECT_EQ(v, exp);
}
#endif

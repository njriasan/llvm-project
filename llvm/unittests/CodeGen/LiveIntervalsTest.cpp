//===- LiveIntervalsTest.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/LiveIntervals.h"
#include "CodeGenTestBase.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Config/Targets.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace llvm;

namespace {

class LiveIntervalsTest : public CodeGenTestBase {
public:
  static void SetUpTestCase() {
#if LLVM_HAS_AMDGPU_TARGET
    LLVMInitializeAMDGPUTargetInfo();
    LLVMInitializeAMDGPUTarget();
    LLVMInitializeAMDGPUTargetMC();
#else
    GTEST_SKIP();
#endif
  }

  void SetUp() override { setUpImpl("amdgcn--", "gfx942", ""); }

  void checkShrink(StringRef MIRBody, unsigned RegIdx, bool HasSubRanges) {
    std::string MIRString =
        (Twine("---\nname: func\ntracksRegLiveness: true\nbody: |\n") +
         MIRBody + "...\n")
            .str();
    ASSERT_TRUE(parseMIR(MIRString));
    MachineFunction &MF = getMF("func");
    LiveIntervals &LIS = MFAM.getResult<LiveIntervalsAnalysis>(MF);
    LiveInterval &LI = LIS.getInterval(Register::index2VirtReg(RegIdx));
    ASSERT_EQ(LI.hasSubRanges(), HasSubRanges);
    std::string Errors;
    raw_string_ostream OS(Errors);
    ASSERT_TRUE(MF.verify(&LIS, LIS.getSlotIndexes(), nullptr, &OS,
                          /*AbortOnError=*/false))
        << Errors;

    // With no uses removed, shrinking must preserve the computed liveness.
    auto Segments = LI.segments;
    for (LiveInterval::SubRange &SR : LI.subranges()) {
      auto SubSegments = SR.segments;
      LIS.shrinkToUses(SR, LI.reg());
      EXPECT_EQ(SR.segments, SubSegments);
      for (const auto &[Actual, Expected] : zip(SR.segments, SubSegments))
        EXPECT_EQ(Actual.valno, Expected.valno);
    }
    LIS.shrinkToUses(&LI);
    EXPECT_EQ(LI.segments, Segments);
    for (const auto &[Actual, Expected] : zip(LI.segments, Segments))
      EXPECT_EQ(Actual.valno, Expected.valno);
    Errors.clear();
    EXPECT_TRUE(MF.verify(&LIS, LIS.getSlotIndexes(), nullptr, &OS,
                          /*AbortOnError=*/false))
        << Errors;
  }
};

TEST_F(LiveIntervalsTest, ShrinkToLoopPHIUse) {
  checkShrink(R"MIR(
  bb.0:
    successors: %bb.1
    %0:sreg_32 = S_MOV_B32 0

  bb.1:
    successors: %bb.1, %bb.2
    %1:sreg_32 = PHI %0, %bb.0, %2, %bb.1
    %2:sreg_32 = S_ADD_I32 %1, 1, implicit-def $scc
    S_CBRANCH_SCC1 %bb.1, implicit $scc
    S_BRANCH %bb.2

  bb.2:
    S_ENDPGM 0
)MIR",
              2, false);
}

TEST_F(LiveIntervalsTest, ShrinkToMultiplePHIEdges) {
  checkShrink(R"MIR(
  bb.0:
    successors: %bb.1, %bb.2
    liveins: $scc
    %0:sreg_32 = S_MOV_B32 0
    S_CBRANCH_SCC1 %bb.2, implicit $scc

  bb.1:
    successors: %bb.3
    S_BRANCH %bb.3

  bb.2:
    successors: %bb.3
    S_BRANCH %bb.3

  bb.3:
    %1:sreg_32 = PHI %0, %bb.1, %0, %bb.2
    S_NOP 0, implicit %1
    S_ENDPGM 0
)MIR",
              0, false);
}

TEST_F(LiveIntervalsTest, ShrinkSubRangesToLoopPHIUse) {
  checkShrink(R"MIR(
  bb.0:
    successors: %bb.1
    liveins: $scc
    %0:vreg_64 = IMPLICIT_DEF

  bb.1:
    successors: %bb.1, %bb.2
    liveins: $scc
    %1:vreg_64 = PHI %0, %bb.0, %2, %bb.1
    %2:vreg_64 = REG_SEQUENCE %1.sub1, %subreg.sub0, %1.sub0, %subreg.sub1
    S_NOP 0, implicit %2.sub0
    S_CBRANCH_SCC1 %bb.1, implicit $scc
    S_BRANCH %bb.2

  bb.2:
    S_ENDPGM 0
)MIR",
              2, true);
}

TEST_F(LiveIntervalsTest, ShrinkSubRangesToPartialPHIUse) {
  checkShrink(R"MIR(
  bb.0:
    successors: %bb.1
    liveins: $scc
    %0:vreg_64 = IMPLICIT_DEF

  bb.1:
    successors: %bb.1, %bb.2
    liveins: $scc
    %1:vgpr_32 = PHI %0.sub0, %bb.0, %2.sub0, %bb.1
    S_NOP 0, implicit %1
    undef %2.sub0:vreg_64 = V_MOV_B32_e32 0, implicit $exec
    S_CBRANCH_SCC1 %bb.1, implicit $scc
    S_BRANCH %bb.2

  bb.2:
    S_ENDPGM 0
)MIR",
              2, true);
}

TEST_F(LiveIntervalsTest, ShrinkToPHIUsesOfDifferentValues) {
  checkShrink(R"MIR(
  bb.0:
    successors: %bb.1, %bb.2
    liveins: $scc
    %0:sreg_32 = S_MOV_B32 0
    S_CBRANCH_SCC1 %bb.2, implicit $scc

  bb.1:
    successors: %bb.2
    %0:sreg_32 = S_MOV_B32 1

  bb.2:
    %1:sreg_32 = PHI %0, %bb.0, %0, %bb.1
    S_NOP 0, implicit %0, implicit %1
    S_ENDPGM 0
)MIR",
              0, false);
}

TEST_F(LiveIntervalsTest, ShrinkToUndefinedPHIUse) {
  checkShrink(R"MIR(
  bb.0:
    successors: %bb.1
    %0:sreg_32 = S_MOV_B32 0

  bb.1:
    %1:sreg_32 = PHI undef %0, %bb.0
    S_NOP 0, implicit %1
    S_ENDPGM 0
)MIR",
              0, false);
}

} // namespace

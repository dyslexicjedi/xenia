/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Developers. All rights reserved.                      *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_sequences.h"

#include <algorithm>
#include <cstring>

#include "xenia/cpu/backend/a64/a64_op.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

volatile int anchor_control = 0;

// Emits a branch to `label` if the value is true (any bit set within its
// type's width) / false (all zero). Sub-word garbage bits are masked.
template <typename OP>
static void EmitBranchOnValue(A64Emitter& e, const OP& src, bool branch_if_true,
                              Xbyak_aarch64::Label& label) {
  if (src.is_constant) {
    bool value = src.constant() != 0;
    if (value == branch_if_true) {
      e.b(label);
    }
    return;
  }
  if (OP::key_type == KEY_TYPE_V_I8) {
    e.tst(src.reg(), 0xFFu);
    branch_if_true ? e.bne(label) : e.beq(label);
  } else if (OP::key_type == KEY_TYPE_V_I16) {
    e.tst(src.reg(), 0xFFFFu);
    branch_if_true ? e.bne(label) : e.beq(label);
  } else {
    branch_if_true ? e.cbnz(src.reg(), label) : e.cbz(src.reg(), label);
  }
}

static void EmitBranchOnFloat(A64Emitter& e, const SReg& src,
                              bool branch_if_true,
                              Xbyak_aarch64::Label& label) {
  // Bitwise test (matches x64 vptest semantics: -0.0f is "true").
  e.fmov(WReg(0), src);
  branch_if_true ? e.cbnz(WReg(0), label) : e.cbz(WReg(0), label);
}

static void EmitBranchOnFloat(A64Emitter& e, const DReg& src,
                              bool branch_if_true,
                              Xbyak_aarch64::Label& label) {
  e.fmov(XReg(0), src);
  branch_if_true ? e.cbnz(XReg(0), label) : e.cbz(XReg(0), label);
}

// ============================================================================
// OPCODE_DEBUG_BREAK
// ============================================================================
struct DEBUG_BREAK : Sequence<DEBUG_BREAK, I<OPCODE_DEBUG_BREAK, VoidOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) { e.DebugBreak(); }
};
EMITTER_OPCODE_TABLE(OPCODE_DEBUG_BREAK, DEBUG_BREAK);

// ============================================================================
// OPCODE_DEBUG_BREAK_TRUE
// ============================================================================
template <typename SEQ_ARG>
static void EmitDebugBreakTrue(A64Emitter& e, const SEQ_ARG& i) {
  Xbyak_aarch64::Label skip;
  EmitBranchOnValue(e, i.src1, false, skip);
  e.DebugBreak();
  e.L(skip);
}
struct DEBUG_BREAK_TRUE_I8
    : Sequence<DEBUG_BREAK_TRUE_I8, I<OPCODE_DEBUG_BREAK_TRUE, VoidOp, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitDebugBreakTrue(e, i);
  }
};
struct DEBUG_BREAK_TRUE_I16
    : Sequence<DEBUG_BREAK_TRUE_I16,
               I<OPCODE_DEBUG_BREAK_TRUE, VoidOp, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitDebugBreakTrue(e, i);
  }
};
struct DEBUG_BREAK_TRUE_I32
    : Sequence<DEBUG_BREAK_TRUE_I32,
               I<OPCODE_DEBUG_BREAK_TRUE, VoidOp, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitDebugBreakTrue(e, i);
  }
};
struct DEBUG_BREAK_TRUE_I64
    : Sequence<DEBUG_BREAK_TRUE_I64,
               I<OPCODE_DEBUG_BREAK_TRUE, VoidOp, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitDebugBreakTrue(e, i);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DEBUG_BREAK_TRUE, DEBUG_BREAK_TRUE_I8,
                     DEBUG_BREAK_TRUE_I16, DEBUG_BREAK_TRUE_I32,
                     DEBUG_BREAK_TRUE_I64);

// ============================================================================
// OPCODE_TRAP
// ============================================================================
struct TRAP : Sequence<TRAP, I<OPCODE_TRAP, VoidOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.Trap(i.instr->flags);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_TRAP, TRAP);

// ============================================================================
// OPCODE_TRAP_TRUE
// ============================================================================
template <typename SEQ_ARG>
static void EmitTrapTrue(A64Emitter& e, const SEQ_ARG& i) {
  Xbyak_aarch64::Label skip;
  EmitBranchOnValue(e, i.src1, false, skip);
  e.Trap(i.instr->flags);
  e.L(skip);
}
struct TRAP_TRUE_I8
    : Sequence<TRAP_TRUE_I8, I<OPCODE_TRAP_TRUE, VoidOp, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) { EmitTrapTrue(e, i); }
};
struct TRAP_TRUE_I16
    : Sequence<TRAP_TRUE_I16, I<OPCODE_TRAP_TRUE, VoidOp, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) { EmitTrapTrue(e, i); }
};
struct TRAP_TRUE_I32
    : Sequence<TRAP_TRUE_I32, I<OPCODE_TRAP_TRUE, VoidOp, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) { EmitTrapTrue(e, i); }
};
struct TRAP_TRUE_I64
    : Sequence<TRAP_TRUE_I64, I<OPCODE_TRAP_TRUE, VoidOp, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) { EmitTrapTrue(e, i); }
};
EMITTER_OPCODE_TABLE(OPCODE_TRAP_TRUE, TRAP_TRUE_I8, TRAP_TRUE_I16,
                     TRAP_TRUE_I32, TRAP_TRUE_I64);

// ============================================================================
// OPCODE_CALL
// ============================================================================
struct CALL : Sequence<CALL, I<OPCODE_CALL, VoidOp, SymbolOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src1.value->is_guest());
    e.Call(i.instr, static_cast<GuestFunction*>(i.src1.value));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CALL, CALL);

// ============================================================================
// OPCODE_CALL_TRUE
// ============================================================================
template <typename SEQ_ARG>
static void EmitCallTrue(A64Emitter& e, const SEQ_ARG& i) {
  assert_true(i.src2.value->is_guest());
  Xbyak_aarch64::Label skip;
  EmitBranchOnValue(e, i.src1, false, skip);
  e.Call(i.instr, static_cast<GuestFunction*>(i.src2.value));
  e.L(skip);
}
struct CALL_TRUE_I8
    : Sequence<CALL_TRUE_I8, I<OPCODE_CALL_TRUE, VoidOp, I8Op, SymbolOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) { EmitCallTrue(e, i); }
};
struct CALL_TRUE_I16
    : Sequence<CALL_TRUE_I16, I<OPCODE_CALL_TRUE, VoidOp, I16Op, SymbolOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) { EmitCallTrue(e, i); }
};
struct CALL_TRUE_I32
    : Sequence<CALL_TRUE_I32, I<OPCODE_CALL_TRUE, VoidOp, I32Op, SymbolOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) { EmitCallTrue(e, i); }
};
struct CALL_TRUE_I64
    : Sequence<CALL_TRUE_I64, I<OPCODE_CALL_TRUE, VoidOp, I64Op, SymbolOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) { EmitCallTrue(e, i); }
};
EMITTER_OPCODE_TABLE(OPCODE_CALL_TRUE, CALL_TRUE_I8, CALL_TRUE_I16,
                     CALL_TRUE_I32, CALL_TRUE_I64);

// ============================================================================
// OPCODE_CALL_INDIRECT
// ============================================================================
struct CALL_INDIRECT
    : Sequence<CALL_INDIRECT, I<OPCODE_CALL_INDIRECT, VoidOp, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.CallIndirect(i.instr, WReg(i.src1.reg().getIdx()));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CALL_INDIRECT, CALL_INDIRECT);

// ============================================================================
// OPCODE_CALL_INDIRECT_TRUE
// ============================================================================
template <typename SEQ_ARG>
static void EmitCallIndirectTrue(A64Emitter& e, const SEQ_ARG& i) {
  Xbyak_aarch64::Label skip;
  EmitBranchOnValue(e, i.src1, false, skip);
  e.CallIndirect(i.instr, WReg(i.src2.reg().getIdx()));
  e.L(skip);
}
struct CALL_INDIRECT_TRUE_I8
    : Sequence<CALL_INDIRECT_TRUE_I8,
               I<OPCODE_CALL_INDIRECT_TRUE, VoidOp, I8Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCallIndirectTrue(e, i);
  }
};
struct CALL_INDIRECT_TRUE_I16
    : Sequence<CALL_INDIRECT_TRUE_I16,
               I<OPCODE_CALL_INDIRECT_TRUE, VoidOp, I16Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCallIndirectTrue(e, i);
  }
};
struct CALL_INDIRECT_TRUE_I32
    : Sequence<CALL_INDIRECT_TRUE_I32,
               I<OPCODE_CALL_INDIRECT_TRUE, VoidOp, I32Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCallIndirectTrue(e, i);
  }
};
struct CALL_INDIRECT_TRUE_I64
    : Sequence<CALL_INDIRECT_TRUE_I64,
               I<OPCODE_CALL_INDIRECT_TRUE, VoidOp, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCallIndirectTrue(e, i);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CALL_INDIRECT_TRUE, CALL_INDIRECT_TRUE_I8,
                     CALL_INDIRECT_TRUE_I16, CALL_INDIRECT_TRUE_I32,
                     CALL_INDIRECT_TRUE_I64);

// ============================================================================
// OPCODE_CALL_EXTERN
// ============================================================================
struct CALL_EXTERN
    : Sequence<CALL_EXTERN, I<OPCODE_CALL_EXTERN, VoidOp, SymbolOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.CallExtern(i.instr, i.src1.value);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CALL_EXTERN, CALL_EXTERN);

// ============================================================================
// OPCODE_RETURN
// ============================================================================
struct RETURN : Sequence<RETURN, I<OPCODE_RETURN, VoidOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // If this is the last instruction in the last block, just let us
    // fall through.
    if (i.instr->next || i.instr->block->next) {
      e.b(e.epilog_label());
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_RETURN, RETURN);

// ============================================================================
// OPCODE_RETURN_TRUE
// ============================================================================
struct RETURN_TRUE_I8
    : Sequence<RETURN_TRUE_I8, I<OPCODE_RETURN_TRUE, VoidOp, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBranchOnValue(e, i.src1, true, e.epilog_label());
  }
};
struct RETURN_TRUE_I16
    : Sequence<RETURN_TRUE_I16, I<OPCODE_RETURN_TRUE, VoidOp, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBranchOnValue(e, i.src1, true, e.epilog_label());
  }
};
struct RETURN_TRUE_I32
    : Sequence<RETURN_TRUE_I32, I<OPCODE_RETURN_TRUE, VoidOp, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBranchOnValue(e, i.src1, true, e.epilog_label());
  }
};
struct RETURN_TRUE_I64
    : Sequence<RETURN_TRUE_I64, I<OPCODE_RETURN_TRUE, VoidOp, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBranchOnValue(e, i.src1, true, e.epilog_label());
  }
};
struct RETURN_TRUE_F32
    : Sequence<RETURN_TRUE_F32, I<OPCODE_RETURN_TRUE, VoidOp, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBranchOnFloat(e, i.src1.reg(), true, e.epilog_label());
  }
};
struct RETURN_TRUE_F64
    : Sequence<RETURN_TRUE_F64, I<OPCODE_RETURN_TRUE, VoidOp, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBranchOnFloat(e, i.src1.reg(), true, e.epilog_label());
  }
};
EMITTER_OPCODE_TABLE(OPCODE_RETURN_TRUE, RETURN_TRUE_I8, RETURN_TRUE_I16,
                     RETURN_TRUE_I32, RETURN_TRUE_I64, RETURN_TRUE_F32,
                     RETURN_TRUE_F64);

// ============================================================================
// OPCODE_SET_RETURN_ADDRESS
// ============================================================================
struct SET_RETURN_ADDRESS
    : Sequence<SET_RETURN_ADDRESS,
               I<OPCODE_SET_RETURN_ADDRESS, VoidOp, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.SetReturnAddress(i.src1.constant());
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SET_RETURN_ADDRESS, SET_RETURN_ADDRESS);

// ============================================================================
// OPCODE_BRANCH
// ============================================================================
struct BRANCH : Sequence<BRANCH, I<OPCODE_BRANCH, VoidOp, LabelOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.b(e.hir_label(i.src1.value->name));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_BRANCH, BRANCH);

// ============================================================================
// OPCODE_BRANCH_TRUE / OPCODE_BRANCH_FALSE
// ============================================================================
#define EMIT_BRANCH_COND_SEQUENCES(NAME, OPCODE_NAME, BRANCH_IF)              \
  struct NAME##_I8                                                            \
      : Sequence<NAME##_I8, I<OPCODE_NAME, VoidOp, I8Op, LabelOp>> {          \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                   \
      EmitBranchOnValue(e, i.src1, BRANCH_IF,                                 \
                        e.hir_label(i.src2.value->name));                     \
    }                                                                         \
  };                                                                          \
  struct NAME##_I16                                                           \
      : Sequence<NAME##_I16, I<OPCODE_NAME, VoidOp, I16Op, LabelOp>> {        \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                   \
      EmitBranchOnValue(e, i.src1, BRANCH_IF,                                 \
                        e.hir_label(i.src2.value->name));                     \
    }                                                                         \
  };                                                                          \
  struct NAME##_I32                                                           \
      : Sequence<NAME##_I32, I<OPCODE_NAME, VoidOp, I32Op, LabelOp>> {        \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                   \
      EmitBranchOnValue(e, i.src1, BRANCH_IF,                                 \
                        e.hir_label(i.src2.value->name));                     \
    }                                                                         \
  };                                                                          \
  struct NAME##_I64                                                           \
      : Sequence<NAME##_I64, I<OPCODE_NAME, VoidOp, I64Op, LabelOp>> {        \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                   \
      EmitBranchOnValue(e, i.src1, BRANCH_IF,                                 \
                        e.hir_label(i.src2.value->name));                     \
    }                                                                         \
  };                                                                          \
  struct NAME##_F32                                                           \
      : Sequence<NAME##_F32, I<OPCODE_NAME, VoidOp, F32Op, LabelOp>> {        \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                   \
      EmitBranchOnFloat(e, i.src1.reg(), BRANCH_IF,                           \
                        e.hir_label(i.src2.value->name));                     \
    }                                                                         \
  };                                                                          \
  struct NAME##_F64                                                           \
      : Sequence<NAME##_F64, I<OPCODE_NAME, VoidOp, F64Op, LabelOp>> {        \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                   \
      EmitBranchOnFloat(e, i.src1.reg(), BRANCH_IF,                           \
                        e.hir_label(i.src2.value->name));                     \
    }                                                                         \
  };                                                                          \
  EMITTER_OPCODE_TABLE(OPCODE_NAME, NAME##_I8, NAME##_I16, NAME##_I32,        \
                       NAME##_I64, NAME##_F32, NAME##_F64);

EMIT_BRANCH_COND_SEQUENCES(BRANCH_TRUE, OPCODE_BRANCH_TRUE, true)
EMIT_BRANCH_COND_SEQUENCES(BRANCH_FALSE, OPCODE_BRANCH_FALSE, false)

#undef EMIT_BRANCH_COND_SEQUENCES

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

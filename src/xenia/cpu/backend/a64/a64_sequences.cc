/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// A note about sub-word (i8/i16) values: they live in W registers and, like
// the x64 backend's partial registers, MAY carry garbage above their width.
// Width-sensitive consumers (compares, shifts, extends, stores) must mask or
// extend; producers don't clean up.

#include "xenia/cpu/backend/a64/a64_sequences.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/cpu/backend/a64/a64_op.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

volatile int anchor_general = 0;

std::unordered_map<uint32_t, SequenceSelectFn>& sequence_table() {
  static std::unordered_map<uint32_t, SequenceSelectFn> table;
  return table;
}

// ============================================================================
// OPCODE_COMMENT
// ============================================================================
struct COMMENT : Sequence<COMMENT, I<OPCODE_COMMENT, VoidOp, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Comments are in the HIR only; nothing to emit.
  }
};
EMITTER_OPCODE_TABLE(OPCODE_COMMENT, COMMENT);

// ============================================================================
// OPCODE_NOP
// ============================================================================
struct NOP : Sequence<NOP, I<OPCODE_NOP, VoidOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) { e.nop(); }
};
EMITTER_OPCODE_TABLE(OPCODE_NOP, NOP);

// ============================================================================
// OPCODE_CONTEXT_BARRIER
// ============================================================================
struct CONTEXT_BARRIER
    : Sequence<CONTEXT_BARRIER, I<OPCODE_CONTEXT_BARRIER, VoidOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {}
};
EMITTER_OPCODE_TABLE(OPCODE_CONTEXT_BARRIER, CONTEXT_BARRIER);

// ============================================================================
// OPCODE_SOURCE_OFFSET
// ============================================================================
struct SOURCE_OFFSET
    : Sequence<SOURCE_OFFSET, I<OPCODE_SOURCE_OFFSET, VoidOp, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.MarkSourceOffset(i.instr);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SOURCE_OFFSET, SOURCE_OFFSET);


// Returns the register holding src's value, materializing v128 constants
// into temp (v0/v1 scratch).
static const QReg GetVWithConst(A64Emitter& e, const V128Op& src,
                                const QReg& temp) {
  if (src.is_constant) {
    e.LoadConstantV(temp, src.constant());
    return temp;
  }
  return src.reg();
}

// ============================================================================
// OPCODE_ASSIGN
// ============================================================================
struct ASSIGN_I8 : Sequence<ASSIGN_I8, I<OPCODE_ASSIGN, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src) {
      e.mov(dest, src);
    });
  }
};
struct ASSIGN_I16 : Sequence<ASSIGN_I16, I<OPCODE_ASSIGN, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src) {
      e.mov(dest, src);
    });
  }
};
struct ASSIGN_I32 : Sequence<ASSIGN_I32, I<OPCODE_ASSIGN, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src) {
      e.mov(dest, src);
    });
  }
};
struct ASSIGN_I64 : Sequence<ASSIGN_I64, I<OPCODE_ASSIGN, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(e, i, [](A64Emitter& e, const XReg& dest, const XReg& src) {
      e.mov(dest, src);
    });
  }
};
struct ASSIGN_F32 : Sequence<ASSIGN_F32, I<OPCODE_ASSIGN, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.fmov(i.dest, i.src1);
  }
};
struct ASSIGN_F64 : Sequence<ASSIGN_F64, I<OPCODE_ASSIGN, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.fmov(i.dest, i.src1);
  }
};
struct ASSIGN_V128 : Sequence<ASSIGN_V128, I<OPCODE_ASSIGN, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const VReg16B dest(i.dest.reg().getIdx());
    const VReg16B src(i.src1.reg().getIdx());
    e.mov(dest, src);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ASSIGN, ASSIGN_I8, ASSIGN_I16, ASSIGN_I32,
                     ASSIGN_I64, ASSIGN_F32, ASSIGN_F64, ASSIGN_V128);

// ============================================================================
// OPCODE_ZERO_EXTEND
// ============================================================================
struct ZERO_EXTEND_I16_I8
    : Sequence<ZERO_EXTEND_I16_I8, I<OPCODE_ZERO_EXTEND, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.uxtb(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I32_I8
    : Sequence<ZERO_EXTEND_I32_I8, I<OPCODE_ZERO_EXTEND, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.uxtb(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I64_I8
    : Sequence<ZERO_EXTEND_I64_I8, I<OPCODE_ZERO_EXTEND, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Writing the W view zeroes the upper half.
    e.uxtb(WReg(i.dest.reg().getIdx()), i.src1);
  }
};
struct ZERO_EXTEND_I32_I16
    : Sequence<ZERO_EXTEND_I32_I16, I<OPCODE_ZERO_EXTEND, I32Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.uxth(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I64_I16
    : Sequence<ZERO_EXTEND_I64_I16, I<OPCODE_ZERO_EXTEND, I64Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.uxth(WReg(i.dest.reg().getIdx()), i.src1);
  }
};
struct ZERO_EXTEND_I64_I32
    : Sequence<ZERO_EXTEND_I64_I32, I<OPCODE_ZERO_EXTEND, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.mov(WReg(i.dest.reg().getIdx()), i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ZERO_EXTEND, ZERO_EXTEND_I16_I8,
                     ZERO_EXTEND_I32_I8, ZERO_EXTEND_I64_I8,
                     ZERO_EXTEND_I32_I16, ZERO_EXTEND_I64_I16,
                     ZERO_EXTEND_I64_I32);

// ============================================================================
// OPCODE_SIGN_EXTEND
// ============================================================================
struct SIGN_EXTEND_I16_I8
    : Sequence<SIGN_EXTEND_I16_I8, I<OPCODE_SIGN_EXTEND, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.sxtb(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I32_I8
    : Sequence<SIGN_EXTEND_I32_I8, I<OPCODE_SIGN_EXTEND, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.sxtb(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I64_I8
    : Sequence<SIGN_EXTEND_I64_I8, I<OPCODE_SIGN_EXTEND, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.sxtb(i.dest, WReg(i.src1.reg().getIdx()));
  }
};
struct SIGN_EXTEND_I32_I16
    : Sequence<SIGN_EXTEND_I32_I16, I<OPCODE_SIGN_EXTEND, I32Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.sxth(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I64_I16
    : Sequence<SIGN_EXTEND_I64_I16, I<OPCODE_SIGN_EXTEND, I64Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.sxth(i.dest, WReg(i.src1.reg().getIdx()));
  }
};
struct SIGN_EXTEND_I64_I32
    : Sequence<SIGN_EXTEND_I64_I32, I<OPCODE_SIGN_EXTEND, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.sxtw(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SIGN_EXTEND, SIGN_EXTEND_I16_I8,
                     SIGN_EXTEND_I32_I8, SIGN_EXTEND_I64_I8,
                     SIGN_EXTEND_I32_I16, SIGN_EXTEND_I64_I16,
                     SIGN_EXTEND_I64_I32);

// ============================================================================
// OPCODE_TRUNCATE
// ============================================================================
struct TRUNCATE_I8_I16
    : Sequence<TRUNCATE_I8_I16, I<OPCODE_TRUNCATE, I8Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.uxtb(i.dest, i.src1);
  }
};
struct TRUNCATE_I8_I32
    : Sequence<TRUNCATE_I8_I32, I<OPCODE_TRUNCATE, I8Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.uxtb(i.dest, i.src1);
  }
};
struct TRUNCATE_I8_I64
    : Sequence<TRUNCATE_I8_I64, I<OPCODE_TRUNCATE, I8Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.uxtb(i.dest, WReg(i.src1.reg().getIdx()));
  }
};
struct TRUNCATE_I16_I32
    : Sequence<TRUNCATE_I16_I32, I<OPCODE_TRUNCATE, I16Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.uxth(i.dest, i.src1);
  }
};
struct TRUNCATE_I16_I64
    : Sequence<TRUNCATE_I16_I64, I<OPCODE_TRUNCATE, I16Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.uxth(i.dest, WReg(i.src1.reg().getIdx()));
  }
};
struct TRUNCATE_I32_I64
    : Sequence<TRUNCATE_I32_I64, I<OPCODE_TRUNCATE, I32Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, WReg(i.src1.reg().getIdx()));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_TRUNCATE, TRUNCATE_I8_I16, TRUNCATE_I8_I32,
                     TRUNCATE_I8_I64, TRUNCATE_I16_I32, TRUNCATE_I16_I64,
                     TRUNCATE_I32_I64);

// ============================================================================
// OPCODE_LOAD_CONTEXT
// ============================================================================
struct LOAD_CONTEXT_I8
    : Sequence<LOAD_CONTEXT_I8, I<OPCODE_LOAD_CONTEXT, I8Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ldrb(i.dest, ptr(e.GetContextReg(), (int32_t)i.src1.value));
  }
};
struct LOAD_CONTEXT_I16
    : Sequence<LOAD_CONTEXT_I16, I<OPCODE_LOAD_CONTEXT, I16Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ldrh(i.dest, ptr(e.GetContextReg(), (int32_t)i.src1.value));
  }
};
struct LOAD_CONTEXT_I32
    : Sequence<LOAD_CONTEXT_I32, I<OPCODE_LOAD_CONTEXT, I32Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ldr(i.dest, ptr(e.GetContextReg(), (int32_t)i.src1.value));
  }
};
struct LOAD_CONTEXT_I64
    : Sequence<LOAD_CONTEXT_I64, I<OPCODE_LOAD_CONTEXT, I64Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ldr(i.dest, ptr(e.GetContextReg(), (int32_t)i.src1.value));
  }
};
struct LOAD_CONTEXT_F32
    : Sequence<LOAD_CONTEXT_F32, I<OPCODE_LOAD_CONTEXT, F32Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ldr(i.dest, ptr(e.GetContextReg(), (int32_t)i.src1.value));
  }
};
struct LOAD_CONTEXT_F64
    : Sequence<LOAD_CONTEXT_F64, I<OPCODE_LOAD_CONTEXT, F64Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ldr(i.dest, ptr(e.GetContextReg(), (int32_t)i.src1.value));
  }
};
struct LOAD_CONTEXT_V128
    : Sequence<LOAD_CONTEXT_V128, I<OPCODE_LOAD_CONTEXT, V128Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ldr(i.dest, ptr(e.GetContextReg(), (int32_t)i.src1.value));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_CONTEXT, LOAD_CONTEXT_I8, LOAD_CONTEXT_I16,
                     LOAD_CONTEXT_I32, LOAD_CONTEXT_I64, LOAD_CONTEXT_F32,
                     LOAD_CONTEXT_F64, LOAD_CONTEXT_V128);

// ============================================================================
// OPCODE_STORE_CONTEXT
// ============================================================================
// Note: all types are always aligned in the context.
struct STORE_CONTEXT_I8
    : Sequence<STORE_CONTEXT_I8,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src = GetWithConst(e, i.src2, WReg(0));
    e.strb(src, ptr(e.GetContextReg(), (int32_t)i.src1.value));
  }
};
struct STORE_CONTEXT_I16
    : Sequence<STORE_CONTEXT_I16,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src = GetWithConst(e, i.src2, WReg(0));
    e.strh(src, ptr(e.GetContextReg(), (int32_t)i.src1.value));
  }
};
struct STORE_CONTEXT_I32
    : Sequence<STORE_CONTEXT_I32,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src = GetWithConst(e, i.src2, WReg(0));
    e.str(src, ptr(e.GetContextReg(), (int32_t)i.src1.value));
  }
};
struct STORE_CONTEXT_I64
    : Sequence<STORE_CONTEXT_I64,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src = GetWithConst(e, i.src2, XReg(0));
    e.str(src, ptr(e.GetContextReg(), (int32_t)i.src1.value));
  }
};
struct STORE_CONTEXT_F32
    : Sequence<STORE_CONTEXT_F32,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.MovConst(WReg(0), i.src2.value->constant.i32);
      e.str(WReg(0), ptr(e.GetContextReg(), (int32_t)i.src1.value));
    } else {
      e.str(i.src2.reg(), ptr(e.GetContextReg(), (int32_t)i.src1.value));
    }
  }
};
struct STORE_CONTEXT_F64
    : Sequence<STORE_CONTEXT_F64,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.MovConst(XReg(0), i.src2.value->constant.i64);
      e.str(XReg(0), ptr(e.GetContextReg(), (int32_t)i.src1.value));
    } else {
      e.str(i.src2.reg(), ptr(e.GetContextReg(), (int32_t)i.src1.value));
    }
  }
};
struct STORE_CONTEXT_V128
    : Sequence<STORE_CONTEXT_V128,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.MovConst(XReg(0), i.src2.constant().low);
      e.MovConst(XReg(1), i.src2.constant().high);
      e.str(XReg(0), ptr(e.GetContextReg(), (int32_t)i.src1.value));
      e.str(XReg(1), ptr(e.GetContextReg(), (int32_t)i.src1.value + 8));
    } else {
      e.str(i.src2.reg(), ptr(e.GetContextReg(), (int32_t)i.src1.value));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE_CONTEXT, STORE_CONTEXT_I8, STORE_CONTEXT_I16,
                     STORE_CONTEXT_I32, STORE_CONTEXT_I64, STORE_CONTEXT_F32,
                     STORE_CONTEXT_F64, STORE_CONTEXT_V128);

// ============================================================================
// OPCODE_ADD
// ============================================================================
struct ADD_I8 : Sequence<ADD_I8, I<OPCODE_ADD, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.add(dest, src1, src2); });
  }
};
struct ADD_I16 : Sequence<ADD_I16, I<OPCODE_ADD, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.add(dest, src1, src2); });
  }
};
struct ADD_I32 : Sequence<ADD_I32, I<OPCODE_ADD, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.add(dest, src1, src2); });
  }
};
struct ADD_I64 : Sequence<ADD_I64, I<OPCODE_ADD, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const XReg& dest, const XReg& src1,
                          const XReg& src2) { e.add(dest, src1, src2); });
  }
};
struct ADD_F32 : Sequence<ADD_F32, I<OPCODE_ADD, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(!i.src1.is_constant && !i.src2.is_constant);
    e.fadd(i.dest, i.src1, i.src2);
  }
};
struct ADD_F64 : Sequence<ADD_F64, I<OPCODE_ADD, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(!i.src1.is_constant && !i.src2.is_constant);
    e.fadd(i.dest, i.src1, i.src2);
  }
};
struct ADD_V128 : Sequence<ADD_V128, I<OPCODE_ADD, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const VReg4S dest(i.dest.reg().getIdx());
    const VReg4S src1(GetVWithConst(e, i.src1, QReg(0)).getIdx());
    const VReg4S src2(GetVWithConst(e, i.src2, QReg(1)).getIdx());
    e.fadd(dest, src1, src2);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ADD, ADD_I8, ADD_I16, ADD_I32, ADD_I64, ADD_F32,
                     ADD_F64, ADD_V128);

// ============================================================================
// OPCODE_SUB
// ============================================================================
struct SUB_I8 : Sequence<SUB_I8, I<OPCODE_SUB, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.sub(dest, src1, src2); });
  }
};
struct SUB_I16 : Sequence<SUB_I16, I<OPCODE_SUB, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.sub(dest, src1, src2); });
  }
};
struct SUB_I32 : Sequence<SUB_I32, I<OPCODE_SUB, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.sub(dest, src1, src2); });
  }
};
struct SUB_I64 : Sequence<SUB_I64, I<OPCODE_SUB, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const XReg& dest, const XReg& src1,
                          const XReg& src2) { e.sub(dest, src1, src2); });
  }
};
struct SUB_F32 : Sequence<SUB_F32, I<OPCODE_SUB, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(!i.src1.is_constant && !i.src2.is_constant);
    e.fsub(i.dest, i.src1, i.src2);
  }
};
struct SUB_F64 : Sequence<SUB_F64, I<OPCODE_SUB, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(!i.src1.is_constant && !i.src2.is_constant);
    e.fsub(i.dest, i.src1, i.src2);
  }
};
struct SUB_V128 : Sequence<SUB_V128, I<OPCODE_SUB, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const VReg4S dest(i.dest.reg().getIdx());
    const VReg4S src1(GetVWithConst(e, i.src1, QReg(0)).getIdx());
    const VReg4S src2(GetVWithConst(e, i.src2, QReg(1)).getIdx());
    e.fsub(dest, src1, src2);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SUB, SUB_I8, SUB_I16, SUB_I32, SUB_I64, SUB_F32,
                     SUB_F64, SUB_V128);

// ============================================================================
// OPCODE_AND
// ============================================================================
struct AND_I8 : Sequence<AND_I8, I<OPCODE_AND, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.and_(dest, src1, src2); });
  }
};
struct AND_I16 : Sequence<AND_I16, I<OPCODE_AND, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.and_(dest, src1, src2); });
  }
};
struct AND_I32 : Sequence<AND_I32, I<OPCODE_AND, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.and_(dest, src1, src2); });
  }
};
struct AND_I64 : Sequence<AND_I64, I<OPCODE_AND, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const XReg& dest, const XReg& src1,
                          const XReg& src2) { e.and_(dest, src1, src2); });
  }
};
struct AND_V128 : Sequence<AND_V128, I<OPCODE_AND, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const VReg16B dest(i.dest.reg().getIdx());
    const VReg16B src1(GetVWithConst(e, i.src1, QReg(0)).getIdx());
    const VReg16B src2(GetVWithConst(e, i.src2, QReg(1)).getIdx());
    e.and_(dest, src1, src2);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_AND, AND_I8, AND_I16, AND_I32, AND_I64, AND_V128);

// ============================================================================
// OPCODE_OR
// ============================================================================
struct OR_I8 : Sequence<OR_I8, I<OPCODE_OR, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.orr(dest, src1, src2); });
  }
};
struct OR_I16 : Sequence<OR_I16, I<OPCODE_OR, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.orr(dest, src1, src2); });
  }
};
struct OR_I32 : Sequence<OR_I32, I<OPCODE_OR, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.orr(dest, src1, src2); });
  }
};
struct OR_I64 : Sequence<OR_I64, I<OPCODE_OR, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const XReg& dest, const XReg& src1,
                          const XReg& src2) { e.orr(dest, src1, src2); });
  }
};
struct OR_V128 : Sequence<OR_V128, I<OPCODE_OR, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const VReg16B dest(i.dest.reg().getIdx());
    const VReg16B src1(GetVWithConst(e, i.src1, QReg(0)).getIdx());
    const VReg16B src2(GetVWithConst(e, i.src2, QReg(1)).getIdx());
    e.orr(dest, src1, src2);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_OR, OR_I8, OR_I16, OR_I32, OR_I64, OR_V128);

// ============================================================================
// OPCODE_XOR
// ============================================================================
struct XOR_I8 : Sequence<XOR_I8, I<OPCODE_XOR, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.eor(dest, src1, src2); });
  }
};
struct XOR_I16 : Sequence<XOR_I16, I<OPCODE_XOR, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.eor(dest, src1, src2); });
  }
};
struct XOR_I32 : Sequence<XOR_I32, I<OPCODE_XOR, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.eor(dest, src1, src2); });
  }
};
struct XOR_I64 : Sequence<XOR_I64, I<OPCODE_XOR, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const XReg& dest, const XReg& src1,
                          const XReg& src2) { e.eor(dest, src1, src2); });
  }
};
struct XOR_V128 : Sequence<XOR_V128, I<OPCODE_XOR, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const VReg16B dest(i.dest.reg().getIdx());
    const VReg16B src1(GetVWithConst(e, i.src1, QReg(0)).getIdx());
    const VReg16B src2(GetVWithConst(e, i.src2, QReg(1)).getIdx());
    e.eor(dest, src1, src2);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_XOR, XOR_I8, XOR_I16, XOR_I32, XOR_I64, XOR_V128);

// ============================================================================
// OPCODE_NOT
// ============================================================================
struct NOT_I8 : Sequence<NOT_I8, I<OPCODE_NOT, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src) {
      e.mvn(dest, src);
    });
  }
};
struct NOT_I16 : Sequence<NOT_I16, I<OPCODE_NOT, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src) {
      e.mvn(dest, src);
    });
  }
};
struct NOT_I32 : Sequence<NOT_I32, I<OPCODE_NOT, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src) {
      e.mvn(dest, src);
    });
  }
};
struct NOT_I64 : Sequence<NOT_I64, I<OPCODE_NOT, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(e, i, [](A64Emitter& e, const XReg& dest, const XReg& src) {
      e.mvn(dest, src);
    });
  }
};
struct NOT_V128 : Sequence<NOT_V128, I<OPCODE_NOT, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const VReg16B dest(i.dest.reg().getIdx());
    const VReg16B src(GetVWithConst(e, i.src1, QReg(0)).getIdx());
    e.mvn(dest, src);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_NOT, NOT_I8, NOT_I16, NOT_I32, NOT_I64, NOT_V128);

// ============================================================================
// OPCODE_NEG
// ============================================================================
struct NEG_I8 : Sequence<NEG_I8, I<OPCODE_NEG, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src) {
      e.neg(dest, src);
    });
  }
};
struct NEG_I16 : Sequence<NEG_I16, I<OPCODE_NEG, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src) {
      e.neg(dest, src);
    });
  }
};
struct NEG_I32 : Sequence<NEG_I32, I<OPCODE_NEG, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src) {
      e.neg(dest, src);
    });
  }
};
struct NEG_I64 : Sequence<NEG_I64, I<OPCODE_NEG, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(e, i, [](A64Emitter& e, const XReg& dest, const XReg& src) {
      e.neg(dest, src);
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_NEG, NEG_I8, NEG_I16, NEG_I32, NEG_I64);

// ============================================================================
// OPCODE_SHL
// ============================================================================
// Shift amounts are masked to the operand width by the hardware (LSLV uses
// amount mod width), matching HIR semantics.
struct SHL_I8 : Sequence<SHL_I8, I<OPCODE_SHL, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.lsl(i.dest, i.src1.reg(), i.src2.constant() & 0x7);
    } else {
      e.uxtb(WReg(1), i.src2);
      e.lsl(i.dest, i.src1.reg(), WReg(1));
    }
  }
};
struct SHL_I16 : Sequence<SHL_I16, I<OPCODE_SHL, I16Op, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.lsl(i.dest, i.src1.reg(), i.src2.constant() & 0xF);
    } else {
      e.uxtb(WReg(1), i.src2);
      e.lsl(i.dest, i.src1.reg(), WReg(1));
    }
  }
};
struct SHL_I32 : Sequence<SHL_I32, I<OPCODE_SHL, I32Op, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    if (i.src2.is_constant) {
      e.lsl(i.dest, src1, i.src2.constant() & 0x1F);
    } else {
      e.uxtb(WReg(1), i.src2);
      e.lsl(i.dest, src1, WReg(1));
    }
  }
};
struct SHL_I64 : Sequence<SHL_I64, I<OPCODE_SHL, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, XReg(0));
    if (i.src2.is_constant) {
      e.lsl(i.dest, src1, i.src2.constant() & 0x3F);
    } else {
      e.uxtb(WReg(1), i.src2);
      e.lsl(i.dest, src1, XReg(1));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHL, SHL_I8, SHL_I16, SHL_I32, SHL_I64);

// ============================================================================
// OPCODE_SHR
// ============================================================================
// Sub-word logical right shifts must see zero-extended sources.
struct SHR_I8 : Sequence<SHR_I8, I<OPCODE_SHR, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    e.uxtb(WReg(0), src1);
    if (i.src2.is_constant) {
      e.lsr(i.dest, WReg(0), i.src2.constant() & 0x7);
    } else {
      e.uxtb(WReg(1), i.src2);
      e.lsr(i.dest, WReg(0), WReg(1));
    }
  }
};
struct SHR_I16 : Sequence<SHR_I16, I<OPCODE_SHR, I16Op, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    e.uxth(WReg(0), src1);
    if (i.src2.is_constant) {
      e.lsr(i.dest, WReg(0), i.src2.constant() & 0xF);
    } else {
      e.uxtb(WReg(1), i.src2);
      e.lsr(i.dest, WReg(0), WReg(1));
    }
  }
};
struct SHR_I32 : Sequence<SHR_I32, I<OPCODE_SHR, I32Op, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    if (i.src2.is_constant) {
      e.lsr(i.dest, src1, i.src2.constant() & 0x1F);
    } else {
      e.uxtb(WReg(1), i.src2);
      e.lsr(i.dest, src1, WReg(1));
    }
  }
};
struct SHR_I64 : Sequence<SHR_I64, I<OPCODE_SHR, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, XReg(0));
    if (i.src2.is_constant) {
      e.lsr(i.dest, src1, i.src2.constant() & 0x3F);
    } else {
      e.uxtb(WReg(1), i.src2);
      e.lsr(i.dest, src1, XReg(1));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHR, SHR_I8, SHR_I16, SHR_I32, SHR_I64);

// ============================================================================
// OPCODE_SHA
// ============================================================================
// Sub-word arithmetic right shifts must see sign-extended sources.
struct SHA_I8 : Sequence<SHA_I8, I<OPCODE_SHA, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    e.sxtb(WReg(0), src1);
    if (i.src2.is_constant) {
      e.asr(i.dest, WReg(0), i.src2.constant() & 0x7);
    } else {
      e.uxtb(WReg(1), i.src2);
      e.asr(i.dest, WReg(0), WReg(1));
    }
  }
};
struct SHA_I16 : Sequence<SHA_I16, I<OPCODE_SHA, I16Op, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    e.sxth(WReg(0), src1);
    if (i.src2.is_constant) {
      e.asr(i.dest, WReg(0), i.src2.constant() & 0xF);
    } else {
      e.uxtb(WReg(1), i.src2);
      e.asr(i.dest, WReg(0), WReg(1));
    }
  }
};
struct SHA_I32 : Sequence<SHA_I32, I<OPCODE_SHA, I32Op, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    if (i.src2.is_constant) {
      e.asr(i.dest, src1, i.src2.constant() & 0x1F);
    } else {
      e.uxtb(WReg(1), i.src2);
      e.asr(i.dest, src1, WReg(1));
    }
  }
};
struct SHA_I64 : Sequence<SHA_I64, I<OPCODE_SHA, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, XReg(0));
    if (i.src2.is_constant) {
      e.asr(i.dest, src1, i.src2.constant() & 0x3F);
    } else {
      e.uxtb(WReg(1), i.src2);
      e.asr(i.dest, src1, XReg(1));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHA, SHA_I8, SHA_I16, SHA_I32, SHA_I64);

// ============================================================================
// OPCODE_ROTATE_LEFT
// ============================================================================
// ARM64 only has rotate-right: rol(n) == ror(width - n).
struct ROTATE_LEFT_I32
    : Sequence<ROTATE_LEFT_I32, I<OPCODE_ROTATE_LEFT, I32Op, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    if (i.src2.is_constant) {
      e.ror(i.dest, src1, (32 - (i.src2.constant() & 0x1F)) & 0x1F);
    } else {
      e.uxtb(WReg(1), i.src2);
      e.neg(WReg(1), WReg(1));
      e.ror(i.dest, src1, WReg(1));
    }
  }
};
struct ROTATE_LEFT_I64
    : Sequence<ROTATE_LEFT_I64, I<OPCODE_ROTATE_LEFT, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, XReg(0));
    if (i.src2.is_constant) {
      e.ror(i.dest, src1, (64 - (i.src2.constant() & 0x3F)) & 0x3F);
    } else {
      e.uxtb(WReg(1), i.src2);
      e.neg(XReg(1), XReg(1));
      e.ror(i.dest, src1, XReg(1));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ROTATE_LEFT, ROTATE_LEFT_I32, ROTATE_LEFT_I64);

// ============================================================================
// Compares. dest is an i8 0/1 flag.
// Sub-word sources are extended into temps first (signedness matching the
// comparison) to clear undefined upper bits.
// ============================================================================
template <typename SEQ, typename ARGS>
struct CompareEmitter {
  static void EmitI8(A64Emitter& e, const ARGS& i, Cond cond, bool is_signed) {
    auto src1 = SEQ::GetWithConst(e, i.src1, WReg(0));
    auto src2 = SEQ::GetWithConst(e, i.src2, WReg(1));
    if (is_signed) {
      e.sxtb(WReg(0), src1);
      e.sxtb(WReg(1), src2);
    } else {
      e.uxtb(WReg(0), src1);
      e.uxtb(WReg(1), src2);
    }
    e.cmp(WReg(0), WReg(1));
    e.cset(i.dest, cond);
  }
  static void EmitI16(A64Emitter& e, const ARGS& i, Cond cond, bool is_signed) {
    auto src1 = SEQ::GetWithConst(e, i.src1, WReg(0));
    auto src2 = SEQ::GetWithConst(e, i.src2, WReg(1));
    if (is_signed) {
      e.sxth(WReg(0), src1);
      e.sxth(WReg(1), src2);
    } else {
      e.uxth(WReg(0), src1);
      e.uxth(WReg(1), src2);
    }
    e.cmp(WReg(0), WReg(1));
    e.cset(i.dest, cond);
  }
};

#define EMIT_COMPARE_SEQUENCES(OP, COND, SIGNED)                              \
  struct COMPARE_##OP##_I8                                                    \
      : Sequence<COMPARE_##OP##_I8, I<OPCODE_COMPARE_##OP, I8Op, I8Op, I8Op>> { \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                   \
      CompareEmitter<COMPARE_##OP##_I8, EmitArgType>::EmitI8(e, i, COND,      \
                                                             SIGNED);         \
    }                                                                         \
  };                                                                          \
  struct COMPARE_##OP##_I16 : Sequence<COMPARE_##OP##_I16,                    \
                                       I<OPCODE_COMPARE_##OP, I8Op, I16Op,    \
                                         I16Op>> {                            \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                   \
      CompareEmitter<COMPARE_##OP##_I16, EmitArgType>::EmitI16(e, i, COND,    \
                                                               SIGNED);       \
    }                                                                         \
  };                                                                          \
  struct COMPARE_##OP##_I32 : Sequence<COMPARE_##OP##_I32,                    \
                                       I<OPCODE_COMPARE_##OP, I8Op, I32Op,    \
                                         I32Op>> {                            \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                   \
      EmitCompare(e, i);                                                      \
      e.cset(i.dest, COND);                                                   \
    }                                                                         \
  };                                                                          \
  struct COMPARE_##OP##_I64 : Sequence<COMPARE_##OP##_I64,                    \
                                       I<OPCODE_COMPARE_##OP, I8Op, I64Op,    \
                                         I64Op>> {                            \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                   \
      EmitCompare(e, i);                                                      \
      e.cset(i.dest, COND);                                                   \
    }                                                                         \
  };                                                                          \
  EMITTER_OPCODE_TABLE(OPCODE_COMPARE_##OP, COMPARE_##OP##_I8,                \
                       COMPARE_##OP##_I16, COMPARE_##OP##_I32,                \
                       COMPARE_##OP##_I64);

EMIT_COMPARE_SEQUENCES(EQ, Cond::EQ, false)
EMIT_COMPARE_SEQUENCES(NE, Cond::NE, false)
EMIT_COMPARE_SEQUENCES(SLT, Cond::LT, true)
EMIT_COMPARE_SEQUENCES(SLE, Cond::LE, true)
EMIT_COMPARE_SEQUENCES(SGT, Cond::GT, true)
EMIT_COMPARE_SEQUENCES(SGE, Cond::GE, true)
EMIT_COMPARE_SEQUENCES(ULT, Cond::LO, false)
EMIT_COMPARE_SEQUENCES(ULE, Cond::LS, false)
EMIT_COMPARE_SEQUENCES(UGT, Cond::HI, false)
EMIT_COMPARE_SEQUENCES(UGE, Cond::HS, false)

#undef EMIT_COMPARE_SEQUENCES

// ============================================================================
// OPCODE_BYTE_SWAP
// ============================================================================
struct BYTE_SWAP_I16
    : Sequence<BYTE_SWAP_I16, I<OPCODE_BYTE_SWAP, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(e, i, [](A64Emitter& e, const WReg& dest,
                         const WReg& src1) { e.rev16(dest, src1); });
  }
};
struct BYTE_SWAP_I32
    : Sequence<BYTE_SWAP_I32, I<OPCODE_BYTE_SWAP, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(e, i, [](A64Emitter& e, const WReg& dest,
                         const WReg& src1) { e.rev(dest, src1); });
  }
};
struct BYTE_SWAP_I64
    : Sequence<BYTE_SWAP_I64, I<OPCODE_BYTE_SWAP, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(e, i, [](A64Emitter& e, const XReg& dest,
                         const XReg& src1) { e.rev(dest, src1); });
  }
};
struct BYTE_SWAP_V128
    : Sequence<BYTE_SWAP_V128, I<OPCODE_BYTE_SWAP, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Swap bytes within each 32-bit element (x64's XMMByteSwapMask).
    const VReg16B dest_b(i.dest.reg().getIdx());
    const VReg16B src_b(GetVWithConst(e, i.src1, QReg(0)).getIdx());
    e.rev32(dest_b, src_b);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_BYTE_SWAP, BYTE_SWAP_I16, BYTE_SWAP_I32,
                     BYTE_SWAP_I64, BYTE_SWAP_V128);

// ============================================================================
// OPCODE_ADD_CARRY
// ============================================================================
// dest = src1 + src2 + (src3 & 1). Only bit 0 of the carry-in is meaningful
// (matching x64's sahf); carry-out is computed by separate HIR compares in
// ppc_emit_alu.cc, so only the sum is produced here.
template <typename SEQ, typename REG, typename ARGS>
void EmitAddCarry(A64Emitter& e, const ARGS& i) {
  auto src1 = SEQ::GetWithConst(e, i.src1, GetTempReg1<REG>(e));
  auto src2 = SEQ::GetWithConst(e, i.src2, GetTempReg2<REG>(e));
  if (i.src3.is_constant) {
    e.add(i.dest, src1, src2);
    if (i.src3.constant() & 1) {
      e.add(i.dest, i.dest, 1);
    }
  } else {
    e.and_(WReg(17), i.src3.reg(), 1);
    e.add(i.dest, src1, src2);
    e.add(i.dest, i.dest, REG(17));
  }
}
struct ADD_CARRY_I8
    : Sequence<ADD_CARRY_I8, I<OPCODE_ADD_CARRY, I8Op, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAddCarry<ADD_CARRY_I8, WReg>(e, i);
  }
};
struct ADD_CARRY_I16
    : Sequence<ADD_CARRY_I16, I<OPCODE_ADD_CARRY, I16Op, I16Op, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAddCarry<ADD_CARRY_I16, WReg>(e, i);
  }
};
struct ADD_CARRY_I32
    : Sequence<ADD_CARRY_I32, I<OPCODE_ADD_CARRY, I32Op, I32Op, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAddCarry<ADD_CARRY_I32, WReg>(e, i);
  }
};
struct ADD_CARRY_I64
    : Sequence<ADD_CARRY_I64, I<OPCODE_ADD_CARRY, I64Op, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAddCarry<ADD_CARRY_I64, XReg>(e, i);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ADD_CARRY, ADD_CARRY_I8, ADD_CARRY_I16,
                     ADD_CARRY_I32, ADD_CARRY_I64);

// ============================================================================
// OPCODE_MUL
// ============================================================================
// Low half of the product: independent of operand signedness and of garbage
// above sub-word widths, so a plain mul works for every integer type.
struct MUL_I8 : Sequence<MUL_I8, I<OPCODE_MUL, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.mul(dest, src1, src2); });
  }
};
struct MUL_I16 : Sequence<MUL_I16, I<OPCODE_MUL, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.mul(dest, src1, src2); });
  }
};
struct MUL_I32 : Sequence<MUL_I32, I<OPCODE_MUL, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.mul(dest, src1, src2); });
  }
};
struct MUL_I64 : Sequence<MUL_I64, I<OPCODE_MUL, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const XReg& dest, const XReg& src1,
                          const XReg& src2) { e.mul(dest, src1, src2); });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL, MUL_I8, MUL_I16, MUL_I32, MUL_I64);

// ============================================================================
// OPCODE_MUL_HI
// ============================================================================
struct MUL_HI_I8 : Sequence<MUL_HI_I8, I<OPCODE_MUL_HI, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    auto src2 = GetWithConst(e, i.src2, WReg(1));
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      e.uxtb(WReg(0), src1);
      e.uxtb(WReg(1), src2);
    } else {
      e.sxtb(WReg(0), src1);
      e.sxtb(WReg(1), src2);
    }
    e.mul(WReg(0), WReg(0), WReg(1));
    e.lsr(i.dest, WReg(0), 8);
  }
};
struct MUL_HI_I16
    : Sequence<MUL_HI_I16, I<OPCODE_MUL_HI, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    auto src2 = GetWithConst(e, i.src2, WReg(1));
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      e.uxth(WReg(0), src1);
      e.uxth(WReg(1), src2);
    } else {
      e.sxth(WReg(0), src1);
      e.sxth(WReg(1), src2);
    }
    e.mul(WReg(0), WReg(0), WReg(1));
    e.lsr(i.dest, WReg(0), 16);
  }
};
struct MUL_HI_I32
    : Sequence<MUL_HI_I32, I<OPCODE_MUL_HI, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    auto src2 = GetWithConst(e, i.src2, WReg(1));
    const XReg dest_x(i.dest.reg().getIdx());
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      e.umull(dest_x, src1, src2);
    } else {
      e.smull(dest_x, src1, src2);
    }
    e.lsr(dest_x, dest_x, 32);
  }
};
struct MUL_HI_I64
    : Sequence<MUL_HI_I64, I<OPCODE_MUL_HI, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, XReg(0));
    auto src2 = GetWithConst(e, i.src2, XReg(1));
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      e.umulh(i.dest, src1, src2);
    } else {
      e.smulh(i.dest, src1, src2);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL_HI, MUL_HI_I8, MUL_HI_I16, MUL_HI_I32,
                     MUL_HI_I64);

// ============================================================================
// OPCODE_DIV
// ============================================================================
// PPC leaves divide-by-zero results undefined; sdiv/udiv naturally produce 0
// without trapping, so no zero guard is needed (x64 branches around idiv).
struct DIV_I8 : Sequence<DIV_I8, I<OPCODE_DIV, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    auto src2 = GetWithConst(e, i.src2, WReg(1));
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      e.uxtb(WReg(0), src1);
      e.uxtb(WReg(1), src2);
      e.udiv(i.dest, WReg(0), WReg(1));
    } else {
      e.sxtb(WReg(0), src1);
      e.sxtb(WReg(1), src2);
      e.sdiv(i.dest, WReg(0), WReg(1));
    }
  }
};
struct DIV_I16 : Sequence<DIV_I16, I<OPCODE_DIV, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    auto src2 = GetWithConst(e, i.src2, WReg(1));
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      e.uxth(WReg(0), src1);
      e.uxth(WReg(1), src2);
      e.udiv(i.dest, WReg(0), WReg(1));
    } else {
      e.sxth(WReg(0), src1);
      e.sxth(WReg(1), src2);
      e.sdiv(i.dest, WReg(0), WReg(1));
    }
  }
};
struct DIV_I32 : Sequence<DIV_I32, I<OPCODE_DIV, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    auto src2 = GetWithConst(e, i.src2, WReg(1));
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      e.udiv(i.dest, src1, src2);
    } else {
      e.sdiv(i.dest, src1, src2);
    }
  }
};
struct DIV_I64 : Sequence<DIV_I64, I<OPCODE_DIV, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, XReg(0));
    auto src2 = GetWithConst(e, i.src2, XReg(1));
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      e.udiv(i.dest, src1, src2);
    } else {
      e.sdiv(i.dest, src1, src2);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DIV, DIV_I8, DIV_I16, DIV_I32, DIV_I64);

// ============================================================================
// OPCODE_AND_NOT
// ============================================================================
// dest = src1 & ~src2
struct AND_NOT_I8 : Sequence<AND_NOT_I8, I<OPCODE_AND_NOT, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.bic(dest, src1, src2); });
  }
};
struct AND_NOT_I16
    : Sequence<AND_NOT_I16, I<OPCODE_AND_NOT, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.bic(dest, src1, src2); });
  }
};
struct AND_NOT_I32
    : Sequence<AND_NOT_I32, I<OPCODE_AND_NOT, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const WReg& dest, const WReg& src1,
                          const WReg& src2) { e.bic(dest, src1, src2); });
  }
};
struct AND_NOT_I64
    : Sequence<AND_NOT_I64, I<OPCODE_AND_NOT, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryOp(e, i, [](A64Emitter& e, const XReg& dest, const XReg& src1,
                          const XReg& src2) { e.bic(dest, src1, src2); });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_AND_NOT, AND_NOT_I8, AND_NOT_I16, AND_NOT_I32,
                     AND_NOT_I64);

// ============================================================================
// OPCODE_CNTLZ
// ============================================================================
// dest (i8) = count of leading zeros at the source's width.
struct CNTLZ_I8 : Sequence<CNTLZ_I8, I<OPCODE_CNTLZ, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    e.and_(WReg(0), src1, 0xFF);
    e.clz(WReg(0), WReg(0));
    e.sub(i.dest, WReg(0), 24);
  }
};
struct CNTLZ_I16 : Sequence<CNTLZ_I16, I<OPCODE_CNTLZ, I8Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    e.uxth(WReg(0), src1);
    e.clz(WReg(0), WReg(0));
    e.sub(i.dest, WReg(0), 16);
  }
};
struct CNTLZ_I32 : Sequence<CNTLZ_I32, I<OPCODE_CNTLZ, I8Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    e.clz(i.dest, src1);
  }
};
struct CNTLZ_I64 : Sequence<CNTLZ_I64, I<OPCODE_CNTLZ, I8Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, XReg(0));
    const XReg dest_x(i.dest.reg().getIdx());
    e.clz(dest_x, src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CNTLZ, CNTLZ_I8, CNTLZ_I16, CNTLZ_I32, CNTLZ_I64);

// ============================================================================
// OPCODE_IS_TRUE / OPCODE_IS_FALSE
// ============================================================================
// dest (i8) = (src != 0) / (src == 0), testing only the source's width.
#define EMIT_IS_SEQUENCES(OP, COND)                                            \
  struct IS_##OP##_I8 : Sequence<IS_##OP##_I8, I<OPCODE_IS_##OP, I8Op, I8Op>> { \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      auto src1 = GetWithConst(e, i.src1, WReg(0));                            \
      e.tst(src1, 0xFF);                                                       \
      e.cset(i.dest, COND);                                                    \
    }                                                                          \
  };                                                                           \
  struct IS_##OP##_I16                                                         \
      : Sequence<IS_##OP##_I16, I<OPCODE_IS_##OP, I8Op, I16Op>> {              \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      auto src1 = GetWithConst(e, i.src1, WReg(0));                            \
      e.tst(src1, 0xFFFF);                                                     \
      e.cset(i.dest, COND);                                                    \
    }                                                                          \
  };                                                                           \
  struct IS_##OP##_I32                                                         \
      : Sequence<IS_##OP##_I32, I<OPCODE_IS_##OP, I8Op, I32Op>> {              \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      auto src1 = GetWithConst(e, i.src1, WReg(0));                            \
      e.cmp(src1, 0);                                                          \
      e.cset(i.dest, COND);                                                    \
    }                                                                          \
  };                                                                           \
  struct IS_##OP##_I64                                                         \
      : Sequence<IS_##OP##_I64, I<OPCODE_IS_##OP, I8Op, I64Op>> {              \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      auto src1 = GetWithConst(e, i.src1, XReg(0));                            \
      e.cmp(src1, 0);                                                          \
      e.cset(i.dest, COND);                                                    \
    }                                                                          \
  };                                                                           \
  EMITTER_OPCODE_TABLE(OPCODE_IS_##OP, IS_##OP##_I8, IS_##OP##_I16,            \
                       IS_##OP##_I32, IS_##OP##_I64);

EMIT_IS_SEQUENCES(TRUE, Cond::NE)
EMIT_IS_SEQUENCES(FALSE, Cond::EQ)

#undef EMIT_IS_SEQUENCES

// ============================================================================
// OPCODE_MIN
// ============================================================================
// Integer min is always a signed compare (see x64's cmovg usage).
struct MIN_I8 : Sequence<MIN_I8, I<OPCODE_MIN, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    auto src2 = GetWithConst(e, i.src2, WReg(1));
    e.sxtb(WReg(0), src1);
    e.sxtb(WReg(1), src2);
    e.cmp(WReg(0), WReg(1));
    e.csel(i.dest, WReg(0), WReg(1), Cond::LT);
  }
};
struct MIN_I16 : Sequence<MIN_I16, I<OPCODE_MIN, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    auto src2 = GetWithConst(e, i.src2, WReg(1));
    e.sxth(WReg(0), src1);
    e.sxth(WReg(1), src2);
    e.cmp(WReg(0), WReg(1));
    e.csel(i.dest, WReg(0), WReg(1), Cond::LT);
  }
};
struct MIN_I32 : Sequence<MIN_I32, I<OPCODE_MIN, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    auto src2 = GetWithConst(e, i.src2, WReg(1));
    e.cmp(src1, src2);
    e.csel(i.dest, src1, src2, Cond::LT);
  }
};
struct MIN_I64 : Sequence<MIN_I64, I<OPCODE_MIN, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, XReg(0));
    auto src2 = GetWithConst(e, i.src2, XReg(1));
    e.cmp(src1, src2);
    e.csel(i.dest, src1, src2, Cond::LT);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MIN, MIN_I8, MIN_I16, MIN_I32, MIN_I64);

// ============================================================================
// OPCODE_SELECT
// ============================================================================
// dest = cond ? src2 : src3; cond is an i8 tested at its width.
template <typename SEQ, typename REG, typename ARGS>
void EmitSelect(A64Emitter& e, const ARGS& i) {
  assert_true(!i.src1.is_constant);
  auto src2 = SEQ::GetWithConst(e, i.src2, GetTempReg1<REG>(e));
  auto src3 = SEQ::GetWithConst(e, i.src3, GetTempReg2<REG>(e));
  e.tst(i.src1.reg(), 0xFF);
  e.csel(i.dest, src2, src3, Cond::NE);
}
struct SELECT_I8
    : Sequence<SELECT_I8, I<OPCODE_SELECT, I8Op, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitSelect<SELECT_I8, WReg>(e, i);
  }
};
struct SELECT_I16
    : Sequence<SELECT_I16, I<OPCODE_SELECT, I16Op, I8Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitSelect<SELECT_I16, WReg>(e, i);
  }
};
struct SELECT_I32
    : Sequence<SELECT_I32, I<OPCODE_SELECT, I32Op, I8Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitSelect<SELECT_I32, WReg>(e, i);
  }
};
struct SELECT_I64
    : Sequence<SELECT_I64, I<OPCODE_SELECT, I64Op, I8Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitSelect<SELECT_I64, XReg>(e, i);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SELECT, SELECT_I8, SELECT_I16, SELECT_I32,
                     SELECT_I64);

// Include anchors to other sequence sources so they get included in the build.
extern volatile int anchor_control;
static int anchor_control_dest = anchor_control;

extern volatile int anchor_memory;
static int anchor_memory_dest = anchor_memory;

extern volatile int anchor_fpu;
static int anchor_fpu_dest = anchor_fpu;

extern volatile int anchor_vector;
static int anchor_vector_dest = anchor_vector;

bool SelectSequence(A64Emitter* e, const Instr* i, const Instr** new_tail) {
  const InstrKey key(i);
  auto it = sequence_table().find(key);
  if (it != sequence_table().end()) {
    if (it->second(*e, i)) {
      *new_tail = i->next;
      return true;
    }
  }
  XELOGE("No sequence match for variant {}", i->opcode->name);
  return false;
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

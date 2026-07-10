/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Developers. All rights reserved.                      *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_sequences.h"

#include "xenia/cpu/backend/a64/a64_op.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

volatile int anchor_memory = 0;

// ============================================================================
// OPCODE_LOAD_LOCAL
// ============================================================================
// Locals live in the function stack frame at [sp + offset]; offsets are
// assigned in A64Emitter::Emit and always naturally aligned.
struct LOAD_LOCAL_I8
    : Sequence<LOAD_LOCAL_I8, I<OPCODE_LOAD_LOCAL, I8Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ldrb(i.dest, ptr(e.sp, i.src1.constant()));
  }
};
struct LOAD_LOCAL_I16
    : Sequence<LOAD_LOCAL_I16, I<OPCODE_LOAD_LOCAL, I16Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ldrh(i.dest, ptr(e.sp, i.src1.constant()));
  }
};
struct LOAD_LOCAL_I32
    : Sequence<LOAD_LOCAL_I32, I<OPCODE_LOAD_LOCAL, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ldr(i.dest, ptr(e.sp, i.src1.constant()));
  }
};
struct LOAD_LOCAL_I64
    : Sequence<LOAD_LOCAL_I64, I<OPCODE_LOAD_LOCAL, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ldr(i.dest, ptr(e.sp, i.src1.constant()));
  }
};
struct LOAD_LOCAL_F32
    : Sequence<LOAD_LOCAL_F32, I<OPCODE_LOAD_LOCAL, F32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ldr(i.dest, ptr(e.sp, i.src1.constant()));
  }
};
struct LOAD_LOCAL_F64
    : Sequence<LOAD_LOCAL_F64, I<OPCODE_LOAD_LOCAL, F64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ldr(i.dest, ptr(e.sp, i.src1.constant()));
  }
};
struct LOAD_LOCAL_V128
    : Sequence<LOAD_LOCAL_V128, I<OPCODE_LOAD_LOCAL, V128Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ldr(i.dest, ptr(e.sp, i.src1.constant()));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_LOCAL, LOAD_LOCAL_I8, LOAD_LOCAL_I16,
                     LOAD_LOCAL_I32, LOAD_LOCAL_I64, LOAD_LOCAL_F32,
                     LOAD_LOCAL_F64, LOAD_LOCAL_V128);

// ============================================================================
// OPCODE_STORE_LOCAL
// ============================================================================
struct STORE_LOCAL_I8
    : Sequence<STORE_LOCAL_I8, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src2 = GetWithConst(e, i.src2, WReg(0));
    e.strb(src2, ptr(e.sp, i.src1.constant()));
  }
};
struct STORE_LOCAL_I16
    : Sequence<STORE_LOCAL_I16, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src2 = GetWithConst(e, i.src2, WReg(0));
    e.strh(src2, ptr(e.sp, i.src1.constant()));
  }
};
struct STORE_LOCAL_I32
    : Sequence<STORE_LOCAL_I32, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src2 = GetWithConst(e, i.src2, WReg(0));
    e.str(src2, ptr(e.sp, i.src1.constant()));
  }
};
struct STORE_LOCAL_I64
    : Sequence<STORE_LOCAL_I64, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src2 = GetWithConst(e, i.src2, XReg(0));
    e.str(src2, ptr(e.sp, i.src1.constant()));
  }
};
struct STORE_LOCAL_F32
    : Sequence<STORE_LOCAL_F32, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(!i.src2.is_constant);
    e.str(i.src2.reg(), ptr(e.sp, i.src1.constant()));
  }
};
struct STORE_LOCAL_F64
    : Sequence<STORE_LOCAL_F64, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(!i.src2.is_constant);
    e.str(i.src2.reg(), ptr(e.sp, i.src1.constant()));
  }
};
struct STORE_LOCAL_V128
    : Sequence<STORE_LOCAL_V128, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(!i.src2.is_constant);
    e.str(i.src2.reg(), ptr(e.sp, i.src1.constant()));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE_LOCAL, STORE_LOCAL_I8, STORE_LOCAL_I16,
                     STORE_LOCAL_I32, STORE_LOCAL_I64, STORE_LOCAL_F32,
                     STORE_LOCAL_F64, STORE_LOCAL_V128);

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

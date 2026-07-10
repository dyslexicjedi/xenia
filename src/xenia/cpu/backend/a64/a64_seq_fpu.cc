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

volatile int anchor_fpu = 0;

namespace {

SReg GetF32WithConst(A64Emitter& e, const F32Op& src, const SReg& temp) {
  if (src.is_constant) {
    e.LoadConstantV(QReg(temp.getIdx()), src.constant());
    return temp;
  }
  return src.reg();
}

DReg GetF64WithConst(A64Emitter& e, const F64Op& src, const DReg& temp) {
  if (src.is_constant) {
    e.LoadConstantV(QReg(temp.getIdx()), src.constant());
    return temp;
  }
  return src.reg();
}

// ============================================================================
// OPCODE_CAST
// ============================================================================
struct CAST_I32_F32 : Sequence<CAST_I32_F32, I<OPCODE_CAST, I32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetF32WithConst(e, i.src1, SReg(0));
    e.fmov(i.dest, src1);
  }
};
struct CAST_I64_F64 : Sequence<CAST_I64_F64, I<OPCODE_CAST, I64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetF64WithConst(e, i.src1, DReg(0));
    e.fmov(i.dest, src1);
  }
};
struct CAST_F32_I32 : Sequence<CAST_F32_I32, I<OPCODE_CAST, F32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    e.fmov(i.dest, src1);
  }
};
struct CAST_F64_I64 : Sequence<CAST_F64_I64, I<OPCODE_CAST, F64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, XReg(0));
    e.fmov(i.dest, src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CAST, CAST_I32_F32, CAST_I64_F64, CAST_F32_I32,
                     CAST_F64_I64);

// ============================================================================
// OPCODE_CONVERT
// ============================================================================
template <typename INT_REG, typename FP_REG>
void EmitFloatToSigned(A64Emitter& e, const INT_REG& dest, const FP_REG& src,
                       RoundMode mode) {
  switch (mode) {
    case ROUND_TO_ZERO:
      e.fcvtzs(dest, src);
      break;
    case ROUND_TO_NEAREST:
      e.fcvtns(dest, src);
      break;
    case ROUND_TO_MINUS_INFINITY:
      e.fcvtms(dest, src);
      break;
    case ROUND_TO_POSITIVE_INFINITY:
      e.fcvtps(dest, src);
      break;
    case ROUND_DYNAMIC: {
      FP_REG rounded(0);
      e.frinti(rounded, src);
      e.fcvtzs(dest, rounded);
      break;
    }
    default:
      assert_unhandled_case(mode);
      break;
  }
}

struct CONVERT_I32_F32
    : Sequence<CONVERT_I32_F32, I<OPCODE_CONVERT, I32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetF32WithConst(e, i.src1, SReg(0));
    EmitFloatToSigned(e, i.dest, src1, RoundMode(i.instr->flags));
  }
};
struct CONVERT_I32_F64
    : Sequence<CONVERT_I32_F64, I<OPCODE_CONVERT, I32Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetF64WithConst(e, i.src1, DReg(0));
    EmitFloatToSigned(e, i.dest, src1, RoundMode(i.instr->flags));
  }
};
struct CONVERT_I64_F64
    : Sequence<CONVERT_I64_F64, I<OPCODE_CONVERT, I64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetF64WithConst(e, i.src1, DReg(0));
    EmitFloatToSigned(e, i.dest, src1, RoundMode(i.instr->flags));
  }
};
struct CONVERT_F32_I32
    : Sequence<CONVERT_F32_I32, I<OPCODE_CONVERT, F32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, WReg(0));
    e.scvtf(i.dest, src1);
  }
};
struct CONVERT_F32_F64
    : Sequence<CONVERT_F32_F64, I<OPCODE_CONVERT, F32Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetF64WithConst(e, i.src1, DReg(0));
    e.fcvt(i.dest, src1);
  }
};
struct CONVERT_F64_I64
    : Sequence<CONVERT_F64_I64, I<OPCODE_CONVERT, F64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetWithConst(e, i.src1, XReg(0));
    e.scvtf(i.dest, src1);
  }
};
struct CONVERT_F64_F32
    : Sequence<CONVERT_F64_F32, I<OPCODE_CONVERT, F64Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetF32WithConst(e, i.src1, SReg(0));
    e.fcvt(i.dest, src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CONVERT, CONVERT_I32_F32, CONVERT_I32_F64,
                     CONVERT_I64_F64, CONVERT_F32_I32, CONVERT_F32_F64,
                     CONVERT_F64_I64, CONVERT_F64_F32);

// ============================================================================
// OPCODE_IS_NAN
// ============================================================================
struct IS_NAN_F32 : Sequence<IS_NAN_F32, I<OPCODE_IS_NAN, I8Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetF32WithConst(e, i.src1, SReg(0));
    e.fcmp(src1, src1);
    e.cset(i.dest, Cond::VS);
  }
};
struct IS_NAN_F64 : Sequence<IS_NAN_F64, I<OPCODE_IS_NAN, I8Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src1 = GetF64WithConst(e, i.src1, DReg(0));
    e.fcmp(src1, src1);
    e.cset(i.dest, Cond::VS);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_IS_NAN, IS_NAN_F32, IS_NAN_F64);

// ============================================================================
// Floating-point compares
// ============================================================================
template <typename OP, typename REG>
REG GetFloatWithConst(A64Emitter& e, const OP& src, const REG& temp);
template <>
SReg GetFloatWithConst(A64Emitter& e, const F32Op& src, const SReg& temp) {
  return GetF32WithConst(e, src, temp);
}
template <>
DReg GetFloatWithConst(A64Emitter& e, const F64Op& src, const DReg& temp) {
  return GetF64WithConst(e, src, temp);
}

template <typename REG, typename ARGS>
void EmitFloatCompare(A64Emitter& e, const ARGS& i, Cond cond) {
  auto src1 = GetFloatWithConst(e, i.src1, REG(0));
  auto src2 = GetFloatWithConst(e, i.src2, REG(1));
  e.fcmp(src1, src2);
  e.cset(i.dest, cond);
}

#define EMIT_FLOAT_COMPARE(OPCODE_NAME, NAME, CONDITION)           \
  struct NAME##_F32                                                \
      : Sequence<NAME##_F32, I<OPCODE_NAME, I8Op, F32Op, F32Op>> { \
    static void Emit(A64Emitter& e, const EmitArgType& i) {        \
      EmitFloatCompare<SReg>(e, i, CONDITION);                     \
    }                                                              \
  };                                                               \
  struct NAME##_F64                                                \
      : Sequence<NAME##_F64, I<OPCODE_NAME, I8Op, F64Op, F64Op>> { \
    static void Emit(A64Emitter& e, const EmitArgType& i) {        \
      EmitFloatCompare<DReg>(e, i, CONDITION);                     \
    }                                                              \
  };                                                               \
  EMITTER_OPCODE_TABLE(OPCODE_NAME, NAME##_F32, NAME##_F64)

EMIT_FLOAT_COMPARE(OPCODE_COMPARE_EQ, COMPARE_EQ, Cond::EQ);
EMIT_FLOAT_COMPARE(OPCODE_COMPARE_NE, COMPARE_NE, Cond::NE);
EMIT_FLOAT_COMPARE(OPCODE_COMPARE_SLT, COMPARE_SLT, Cond::LT);
EMIT_FLOAT_COMPARE(OPCODE_COMPARE_SLE, COMPARE_SLE, Cond::LE);
EMIT_FLOAT_COMPARE(OPCODE_COMPARE_SGT, COMPARE_SGT, Cond::GT);
EMIT_FLOAT_COMPARE(OPCODE_COMPARE_SGE, COMPARE_SGE, Cond::GE);
EMIT_FLOAT_COMPARE(OPCODE_COMPARE_ULT, COMPARE_ULT, Cond::LT);
EMIT_FLOAT_COMPARE(OPCODE_COMPARE_ULE, COMPARE_ULE, Cond::LE);
EMIT_FLOAT_COMPARE(OPCODE_COMPARE_UGT, COMPARE_UGT, Cond::GT);
EMIT_FLOAT_COMPARE(OPCODE_COMPARE_UGE, COMPARE_UGE, Cond::GE);

#undef EMIT_FLOAT_COMPARE

// ============================================================================
// OPCODE_MUL / OPCODE_DIV
// ============================================================================
#define EMIT_FLOAT_BINARY(OPCODE_NAME, NAME, INSTRUCTION)           \
  struct NAME##_F32                                                 \
      : Sequence<NAME##_F32, I<OPCODE_NAME, F32Op, F32Op, F32Op>> { \
    static void Emit(A64Emitter& e, const EmitArgType& i) {         \
      auto src1 = GetF32WithConst(e, i.src1, SReg(0));              \
      auto src2 = GetF32WithConst(e, i.src2, SReg(1));              \
      e.INSTRUCTION(i.dest, src1, src2);                            \
    }                                                               \
  };                                                                \
  struct NAME##_F64                                                 \
      : Sequence<NAME##_F64, I<OPCODE_NAME, F64Op, F64Op, F64Op>> { \
    static void Emit(A64Emitter& e, const EmitArgType& i) {         \
      auto src1 = GetF64WithConst(e, i.src1, DReg(0));              \
      auto src2 = GetF64WithConst(e, i.src2, DReg(1));              \
      e.INSTRUCTION(i.dest, src1, src2);                            \
    }                                                               \
  };                                                                \
  EMITTER_OPCODE_TABLE(OPCODE_NAME, NAME##_F32, NAME##_F64)

EMIT_FLOAT_BINARY(OPCODE_MUL, MUL, fmul);
EMIT_FLOAT_BINARY(OPCODE_DIV, DIV, fdiv);

#undef EMIT_FLOAT_BINARY

// ============================================================================
// OPCODE_MUL_ADD / OPCODE_MUL_SUB
// ============================================================================
#define EMIT_FLOAT_TERNARY(OPCODE_NAME, NAME, INSTRUCTION)                 \
  struct NAME##_F32                                                        \
      : Sequence<NAME##_F32, I<OPCODE_NAME, F32Op, F32Op, F32Op, F32Op>> { \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                \
      auto src1 = GetF32WithConst(e, i.src1, SReg(0));                     \
      auto src2 = GetF32WithConst(e, i.src2, SReg(1));                     \
      auto src3 = GetF32WithConst(e, i.src3, SReg(2));                     \
      e.INSTRUCTION(i.dest, src1, src2, src3);                             \
    }                                                                      \
  };                                                                       \
  struct NAME##_F64                                                        \
      : Sequence<NAME##_F64, I<OPCODE_NAME, F64Op, F64Op, F64Op, F64Op>> { \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                \
      auto src1 = GetF64WithConst(e, i.src1, DReg(0));                     \
      auto src2 = GetF64WithConst(e, i.src2, DReg(1));                     \
      auto src3 = GetF64WithConst(e, i.src3, DReg(2));                     \
      e.INSTRUCTION(i.dest, src1, src2, src3);                             \
    }                                                                      \
  };                                                                       \
  EMITTER_OPCODE_TABLE(OPCODE_NAME, NAME##_F32, NAME##_F64)

EMIT_FLOAT_TERNARY(OPCODE_MUL_ADD, MUL_ADD, fmadd);
// ARM's FNMSUB computes src1 * src2 - src3. FMSUB has the opposite
// subtraction order (src3 - src1 * src2).
EMIT_FLOAT_TERNARY(OPCODE_MUL_SUB, MUL_SUB, fnmsub);

#undef EMIT_FLOAT_TERNARY

// ============================================================================
// OPCODE_NEG / OPCODE_ABS / OPCODE_SQRT
// ============================================================================
#define EMIT_FLOAT_UNARY(OPCODE_NAME, NAME, INSTRUCTION)                   \
  struct NAME##_F32 : Sequence<NAME##_F32, I<OPCODE_NAME, F32Op, F32Op>> { \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                \
      auto src1 = GetF32WithConst(e, i.src1, SReg(0));                     \
      e.INSTRUCTION(i.dest, src1);                                         \
    }                                                                      \
  };                                                                       \
  struct NAME##_F64 : Sequence<NAME##_F64, I<OPCODE_NAME, F64Op, F64Op>> { \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                \
      auto src1 = GetF64WithConst(e, i.src1, DReg(0));                     \
      e.INSTRUCTION(i.dest, src1);                                         \
    }                                                                      \
  };                                                                       \
  EMITTER_OPCODE_TABLE(OPCODE_NAME, NAME##_F32, NAME##_F64)

EMIT_FLOAT_UNARY(OPCODE_NEG, NEG, fneg);
EMIT_FLOAT_UNARY(OPCODE_ABS, ABS, fabs);
EMIT_FLOAT_UNARY(OPCODE_SQRT, SQRT, fsqrt);

#undef EMIT_FLOAT_UNARY

// ============================================================================
// OPCODE_SELECT
// ============================================================================
struct SELECT_F32
    : Sequence<SELECT_F32, I<OPCODE_SELECT, F32Op, I8Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src2 = GetF32WithConst(e, i.src2, SReg(0));
    auto src3 = GetF32WithConst(e, i.src3, SReg(1));
    e.tst(i.src1.reg(), 0xFF);
    e.fcsel(i.dest, src2, src3, Cond::NE);
  }
};
struct SELECT_F64
    : Sequence<SELECT_F64, I<OPCODE_SELECT, F64Op, I8Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto src2 = GetF64WithConst(e, i.src2, DReg(0));
    auto src3 = GetF64WithConst(e, i.src3, DReg(1));
    e.tst(i.src1.reg(), 0xFF);
    e.fcsel(i.dest, src2, src3, Cond::NE);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SELECT, SELECT_F32, SELECT_F64);

}  // namespace

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

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
#include <cmath>
#include <cstring>

#include "xenia/cpu/backend/a64/a64_op.h"

// For OPCODE_PACK/OPCODE_UNPACK FLOAT16 emulation.
#include "third_party/half/include/half.hpp"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

volatile int anchor_vector = 0;

namespace {

// Returns the register holding src's value, materializing constants into
// temp. Mirrors Sequence::GetWithConst for the vector register class.
const QReg GetVWithConst(A64Emitter& e, const V128Op& src, const QReg& temp) {
  if (src.is_constant) {
    e.LoadConstantV(temp, src.constant());
    return temp;
  }
  return src.reg();
}

// Guest-frame scratch used to pass v128 values to host helpers by pointer.
// sp+32..47 is used by LoadConstantV; the 32 bytes at sp+48..79 are free.
constexpr int32_t kVStashOffset = 48;

// Stashes a vector into guest-frame scratch and points native param 0 at it.
// Host helpers take (void* raw_context, vec128_t* value) and write the
// result back through the pointer.
void StashVForCall(A64Emitter& e, const QReg& src) {
  e.str(src, ptr(e.sp, kVStashOffset));
  e.add(e.GetNativeParam(0), e.sp, kVStashOffset);
}

}  // namespace

namespace {

// Emits a binary v128 op with constants materialized into v0/v1.
template <typename ARGS, typename FN>
void EmitBinaryVOp(A64Emitter& e, const ARGS& i, FN&& fn) {
  const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
  const QReg src2 = GetVWithConst(e, i.src2, QReg(1));
  fn(e, i.dest.reg(), src1, src2);
}

}  // namespace

// ============================================================================
// OPCODE_VECTOR_ADD
// ============================================================================
struct VECTOR_ADD
    : Sequence<VECTOR_ADD, I<OPCODE_VECTOR_ADD, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryVOp(e, i, [&i](A64Emitter& e, const QReg& dest, const QReg& s1,
                             const QReg& s2) {
      const TypeName part_type = static_cast<TypeName>(i.instr->flags & 0xFF);
      const uint32_t arithmetic_flags = i.instr->flags >> 8;
      const bool is_unsigned = !!(arithmetic_flags & ARITHMETIC_UNSIGNED);
      const bool saturate = !!(arithmetic_flags & ARITHMETIC_SATURATE);
      const int d = dest.getIdx(), a = s1.getIdx(), b = s2.getIdx();
      switch (part_type) {
        case INT8_TYPE:
          if (saturate) {
            // TODO(benvanik): trace DID_SATURATE
            if (is_unsigned) {
              e.uqadd(VReg16B(d), VReg16B(a), VReg16B(b));
            } else {
              e.sqadd(VReg16B(d), VReg16B(a), VReg16B(b));
            }
          } else {
            e.add(VReg16B(d), VReg16B(a), VReg16B(b));
          }
          break;
        case INT16_TYPE:
          if (saturate) {
            if (is_unsigned) {
              e.uqadd(VReg8H(d), VReg8H(a), VReg8H(b));
            } else {
              e.sqadd(VReg8H(d), VReg8H(a), VReg8H(b));
            }
          } else {
            e.add(VReg8H(d), VReg8H(a), VReg8H(b));
          }
          break;
        case INT32_TYPE:
          if (saturate) {
            if (is_unsigned) {
              e.uqadd(VReg4S(d), VReg4S(a), VReg4S(b));
            } else {
              e.sqadd(VReg4S(d), VReg4S(a), VReg4S(b));
            }
          } else {
            e.add(VReg4S(d), VReg4S(a), VReg4S(b));
          }
          break;
        case FLOAT32_TYPE:
          assert_false(is_unsigned);
          assert_false(saturate);
          e.fadd(VReg4S(d), VReg4S(a), VReg4S(b));
          break;
        default:
          assert_unhandled_case(part_type);
          break;
      }
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_ADD, VECTOR_ADD);

// ============================================================================
// OPCODE_VECTOR_SUB
// ============================================================================
struct VECTOR_SUB
    : Sequence<VECTOR_SUB, I<OPCODE_VECTOR_SUB, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryVOp(e, i, [&i](A64Emitter& e, const QReg& dest, const QReg& s1,
                             const QReg& s2) {
      const TypeName part_type = static_cast<TypeName>(i.instr->flags & 0xFF);
      const uint32_t arithmetic_flags = i.instr->flags >> 8;
      const bool is_unsigned = !!(arithmetic_flags & ARITHMETIC_UNSIGNED);
      const bool saturate = !!(arithmetic_flags & ARITHMETIC_SATURATE);
      const int d = dest.getIdx(), a = s1.getIdx(), b = s2.getIdx();
      switch (part_type) {
        case INT8_TYPE:
          if (saturate) {
            if (is_unsigned) {
              e.uqsub(VReg16B(d), VReg16B(a), VReg16B(b));
            } else {
              e.sqsub(VReg16B(d), VReg16B(a), VReg16B(b));
            }
          } else {
            e.sub(VReg16B(d), VReg16B(a), VReg16B(b));
          }
          break;
        case INT16_TYPE:
          if (saturate) {
            if (is_unsigned) {
              e.uqsub(VReg8H(d), VReg8H(a), VReg8H(b));
            } else {
              e.sqsub(VReg8H(d), VReg8H(a), VReg8H(b));
            }
          } else {
            e.sub(VReg8H(d), VReg8H(a), VReg8H(b));
          }
          break;
        case INT32_TYPE:
          if (saturate) {
            if (is_unsigned) {
              e.uqsub(VReg4S(d), VReg4S(a), VReg4S(b));
            } else {
              e.sqsub(VReg4S(d), VReg4S(a), VReg4S(b));
            }
          } else {
            e.sub(VReg4S(d), VReg4S(a), VReg4S(b));
          }
          break;
        case FLOAT32_TYPE:
          e.fsub(VReg4S(d), VReg4S(a), VReg4S(b));
          break;
        default:
          assert_unhandled_case(part_type);
          break;
      }
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SUB, VECTOR_SUB);

// ============================================================================
// OPCODE_VECTOR_MAX
// ============================================================================
struct VECTOR_MAX
    : Sequence<VECTOR_MAX, I<OPCODE_VECTOR_MAX, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryVOp(e, i, [&i](A64Emitter& e, const QReg& dest, const QReg& s1,
                             const QReg& s2) {
      const uint32_t part_type = i.instr->flags >> 8;
      const int d = dest.getIdx(), a = s1.getIdx(), b = s2.getIdx();
      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        switch (part_type) {
          case INT8_TYPE:
            e.umax(VReg16B(d), VReg16B(a), VReg16B(b));
            break;
          case INT16_TYPE:
            e.umax(VReg8H(d), VReg8H(a), VReg8H(b));
            break;
          case INT32_TYPE:
            e.umax(VReg4S(d), VReg4S(a), VReg4S(b));
            break;
          default:
            assert_unhandled_case(part_type);
            break;
        }
      } else {
        switch (part_type) {
          case INT8_TYPE:
            e.smax(VReg16B(d), VReg16B(a), VReg16B(b));
            break;
          case INT16_TYPE:
            e.smax(VReg8H(d), VReg8H(a), VReg8H(b));
            break;
          case INT32_TYPE:
            e.smax(VReg4S(d), VReg4S(a), VReg4S(b));
            break;
          default:
            assert_unhandled_case(part_type);
            break;
        }
      }
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_MAX, VECTOR_MAX);

// ============================================================================
// OPCODE_VECTOR_MIN
// ============================================================================
struct VECTOR_MIN
    : Sequence<VECTOR_MIN, I<OPCODE_VECTOR_MIN, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryVOp(e, i, [&i](A64Emitter& e, const QReg& dest, const QReg& s1,
                             const QReg& s2) {
      const uint32_t part_type = i.instr->flags >> 8;
      const int d = dest.getIdx(), a = s1.getIdx(), b = s2.getIdx();
      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        switch (part_type) {
          case INT8_TYPE:
            e.umin(VReg16B(d), VReg16B(a), VReg16B(b));
            break;
          case INT16_TYPE:
            e.umin(VReg8H(d), VReg8H(a), VReg8H(b));
            break;
          case INT32_TYPE:
            e.umin(VReg4S(d), VReg4S(a), VReg4S(b));
            break;
          default:
            assert_unhandled_case(part_type);
            break;
        }
      } else {
        switch (part_type) {
          case INT8_TYPE:
            e.smin(VReg16B(d), VReg16B(a), VReg16B(b));
            break;
          case INT16_TYPE:
            e.smin(VReg8H(d), VReg8H(a), VReg8H(b));
            break;
          case INT32_TYPE:
            e.smin(VReg4S(d), VReg4S(a), VReg4S(b));
            break;
          default:
            assert_unhandled_case(part_type);
            break;
        }
      }
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_MIN, VECTOR_MIN);

// ============================================================================
// OPCODE_VECTOR_COMPARE_EQ
// ============================================================================
struct VECTOR_COMPARE_EQ_V128
    : Sequence<VECTOR_COMPARE_EQ_V128,
               I<OPCODE_VECTOR_COMPARE_EQ, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryVOp(e, i, [&i](A64Emitter& e, const QReg& dest, const QReg& s1,
                             const QReg& s2) {
      const int d = dest.getIdx(), a = s1.getIdx(), b = s2.getIdx();
      switch (i.instr->flags) {
        case INT8_TYPE:
          e.cmeq(VReg16B(d), VReg16B(a), VReg16B(b));
          break;
        case INT16_TYPE:
          e.cmeq(VReg8H(d), VReg8H(a), VReg8H(b));
          break;
        case INT32_TYPE:
          e.cmeq(VReg4S(d), VReg4S(a), VReg4S(b));
          break;
        case FLOAT32_TYPE:
          e.fcmeq(VReg4S(d), VReg4S(a), VReg4S(b));
          break;
        default:
          assert_unhandled_case(i.instr->flags);
          break;
      }
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_EQ, VECTOR_COMPARE_EQ_V128);

// ============================================================================
// OPCODE_VECTOR_COMPARE_SGT
// ============================================================================
struct VECTOR_COMPARE_SGT_V128
    : Sequence<VECTOR_COMPARE_SGT_V128,
               I<OPCODE_VECTOR_COMPARE_SGT, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryVOp(e, i, [&i](A64Emitter& e, const QReg& dest, const QReg& s1,
                             const QReg& s2) {
      const int d = dest.getIdx(), a = s1.getIdx(), b = s2.getIdx();
      switch (i.instr->flags) {
        case INT8_TYPE:
          e.cmgt(VReg16B(d), VReg16B(a), VReg16B(b));
          break;
        case INT16_TYPE:
          e.cmgt(VReg8H(d), VReg8H(a), VReg8H(b));
          break;
        case INT32_TYPE:
          e.cmgt(VReg4S(d), VReg4S(a), VReg4S(b));
          break;
        case FLOAT32_TYPE:
          e.fcmgt(VReg4S(d), VReg4S(a), VReg4S(b));
          break;
        default:
          assert_unhandled_case(i.instr->flags);
          break;
      }
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_SGT, VECTOR_COMPARE_SGT_V128);

// ============================================================================
// OPCODE_VECTOR_COMPARE_SGE
// ============================================================================
struct VECTOR_COMPARE_SGE_V128
    : Sequence<VECTOR_COMPARE_SGE_V128,
               I<OPCODE_VECTOR_COMPARE_SGE, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryVOp(e, i, [&i](A64Emitter& e, const QReg& dest, const QReg& s1,
                             const QReg& s2) {
      const int d = dest.getIdx(), a = s1.getIdx(), b = s2.getIdx();
      switch (i.instr->flags) {
        case INT8_TYPE:
          e.cmge(VReg16B(d), VReg16B(a), VReg16B(b));
          break;
        case INT16_TYPE:
          e.cmge(VReg8H(d), VReg8H(a), VReg8H(b));
          break;
        case INT32_TYPE:
          e.cmge(VReg4S(d), VReg4S(a), VReg4S(b));
          break;
        case FLOAT32_TYPE:
          e.fcmge(VReg4S(d), VReg4S(a), VReg4S(b));
          break;
        default:
          assert_unhandled_case(i.instr->flags);
          break;
      }
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_SGE, VECTOR_COMPARE_SGE_V128);

// ============================================================================
// OPCODE_VECTOR_COMPARE_UGT / OPCODE_VECTOR_COMPARE_UGE
// ============================================================================
// Integer forms are native (CMHI/CMHS). The float form mirrors x64: bias
// both operands by the sign mask and do a signed compare.
struct VECTOR_COMPARE_UGT_V128
    : Sequence<VECTOR_COMPARE_UGT_V128,
               I<OPCODE_VECTOR_COMPARE_UGT, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryVOp(e, i, [&i](A64Emitter& e, const QReg& dest, const QReg& s1,
                             const QReg& s2) {
      const int d = dest.getIdx(), a = s1.getIdx(), b = s2.getIdx();
      switch (i.instr->flags) {
        case INT8_TYPE:
          e.cmhi(VReg16B(d), VReg16B(a), VReg16B(b));
          break;
        case INT16_TYPE:
          e.cmhi(VReg8H(d), VReg8H(a), VReg8H(b));
          break;
        case INT32_TYPE:
          e.cmhi(VReg4S(d), VReg4S(a), VReg4S(b));
          break;
        case FLOAT32_TYPE:
          e.LoadVConst(QReg(2), VSignMaskPS);
          e.eor(VReg16B(0), VReg16B(a), VReg16B(2));
          e.eor(VReg16B(1), VReg16B(b), VReg16B(2));
          e.fcmgt(VReg4S(d), VReg4S(0), VReg4S(1));
          break;
        default:
          assert_unhandled_case(i.instr->flags);
          break;
      }
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_UGT, VECTOR_COMPARE_UGT_V128);

struct VECTOR_COMPARE_UGE_V128
    : Sequence<VECTOR_COMPARE_UGE_V128,
               I<OPCODE_VECTOR_COMPARE_UGE, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryVOp(e, i, [&i](A64Emitter& e, const QReg& dest, const QReg& s1,
                             const QReg& s2) {
      const int d = dest.getIdx(), a = s1.getIdx(), b = s2.getIdx();
      switch (i.instr->flags) {
        case INT8_TYPE:
          e.cmhs(VReg16B(d), VReg16B(a), VReg16B(b));
          break;
        case INT16_TYPE:
          e.cmhs(VReg8H(d), VReg8H(a), VReg8H(b));
          break;
        case INT32_TYPE:
          e.cmhs(VReg4S(d), VReg4S(a), VReg4S(b));
          break;
        case FLOAT32_TYPE:
          e.LoadVConst(QReg(2), VSignMaskPS);
          e.eor(VReg16B(0), VReg16B(a), VReg16B(2));
          e.eor(VReg16B(1), VReg16B(b), VReg16B(2));
          e.fcmge(VReg4S(d), VReg4S(0), VReg4S(1));
          break;
        default:
          assert_unhandled_case(i.instr->flags);
          break;
      }
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_UGE, VECTOR_COMPARE_UGE_V128);

// ============================================================================
// OPCODE_VECTOR_AVERAGE
// ============================================================================
// Rounding average (a+b+1)>>1; NEON has it natively for all widths and both
// signednesses (URHADD/SRHADD), including the I32 cases x64 emulates.
struct VECTOR_AVERAGE
    : Sequence<VECTOR_AVERAGE,
               I<OPCODE_VECTOR_AVERAGE, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryVOp(e, i, [&i](A64Emitter& e, const QReg& dest, const QReg& s1,
                             const QReg& s2) {
      const TypeName part_type = static_cast<TypeName>(i.instr->flags & 0xFF);
      const uint32_t arithmetic_flags = i.instr->flags >> 8;
      const bool is_unsigned = !!(arithmetic_flags & ARITHMETIC_UNSIGNED);
      const int d = dest.getIdx(), a = s1.getIdx(), b = s2.getIdx();
      switch (part_type) {
        case INT8_TYPE:
          if (is_unsigned) {
            e.urhadd(VReg16B(d), VReg16B(a), VReg16B(b));
          } else {
            e.srhadd(VReg16B(d), VReg16B(a), VReg16B(b));
          }
          break;
        case INT16_TYPE:
          if (is_unsigned) {
            e.urhadd(VReg8H(d), VReg8H(a), VReg8H(b));
          } else {
            e.srhadd(VReg8H(d), VReg8H(a), VReg8H(b));
          }
          break;
        case INT32_TYPE:
          if (is_unsigned) {
            e.urhadd(VReg4S(d), VReg4S(a), VReg4S(b));
          } else {
            e.srhadd(VReg4S(d), VReg4S(a), VReg4S(b));
          }
          break;
        default:
          assert_unhandled_case(part_type);
          break;
      }
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_AVERAGE, VECTOR_AVERAGE);

// ============================================================================
// V128 variants of scalar opcodes (all lanes f32 unless noted)
// ============================================================================
struct AND_NOT_V128
    : Sequence<AND_NOT_V128, I<OPCODE_AND_NOT, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // dest = src1 & ~src2
    EmitBinaryVOp(e, i, [](A64Emitter& e, const QReg& dest, const QReg& s1,
                           const QReg& s2) {
      e.bic(VReg16B(dest.getIdx()), VReg16B(s1.getIdx()),
            VReg16B(s2.getIdx()));
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_AND_NOT, AND_NOT_V128);

struct NEG_V128 : Sequence<NEG_V128, I<OPCODE_NEG, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    e.fneg(VReg4S(i.dest.reg().getIdx()), VReg4S(src1.getIdx()));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_NEG, NEG_V128);

struct ABS_V128 : Sequence<ABS_V128, I<OPCODE_ABS, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    e.fabs(VReg4S(i.dest.reg().getIdx()), VReg4S(src1.getIdx()));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ABS, ABS_V128);

struct SQRT_V128 : Sequence<SQRT_V128, I<OPCODE_SQRT, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    e.fsqrt(VReg4S(i.dest.reg().getIdx()), VReg4S(src1.getIdx()));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SQRT, SQRT_V128);

// Exact 1/sqrt(x) and 1/x, same policy as the scalar sequences (Stage 4.5):
// host estimate accuracy can't be pinned to the PPC estimate instructions.
struct RSQRT_V128 : Sequence<RSQRT_V128, I<OPCODE_RSQRT, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    e.fsqrt(VReg4S(0), VReg4S(src1.getIdx()));
    e.LoadVConst(QReg(1), VOnePS);
    e.fdiv(VReg4S(i.dest.reg().getIdx()), VReg4S(1), VReg4S(0));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_RSQRT, RSQRT_V128);

struct RECIP_V128 : Sequence<RECIP_V128, I<OPCODE_RECIP, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    e.LoadVConst(QReg(1), VOnePS);
    e.fdiv(VReg4S(i.dest.reg().getIdx()), VReg4S(1), VReg4S(src1.getIdx()));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_RECIP, RECIP_V128);

struct ROUND_V128 : Sequence<ROUND_V128, I<OPCODE_ROUND, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    const VReg4S dest_s(i.dest.reg().getIdx());
    const VReg4S src_s(src1.getIdx());
    switch (i.instr->flags) {
      case ROUND_TO_ZERO:
        e.frintz(dest_s, src_s);
        break;
      case ROUND_TO_NEAREST:
        e.frintn(dest_s, src_s);
        break;
      case ROUND_TO_MINUS_INFINITY:
        e.frintm(dest_s, src_s);
        break;
      case ROUND_TO_POSITIVE_INFINITY:
        e.frintp(dest_s, src_s);
        break;
      default:
        assert_unhandled_case(i.instr->flags);
        break;
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ROUND, ROUND_V128);

// x64 maxps/minps semantics: src2 wins on equal or either-NaN (same policy
// as the scalar MIN/MAX sequences).
struct MAX_V128 : Sequence<MAX_V128, I<OPCODE_MAX, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryVOp(e, i, [](A64Emitter& e, const QReg& dest, const QReg& s1,
                           const QReg& s2) {
      e.fcmgt(VReg4S(2), VReg4S(s1.getIdx()), VReg4S(s2.getIdx()));
      e.bsl(VReg16B(2), VReg16B(s1.getIdx()), VReg16B(s2.getIdx()));
      const VReg16B dest_b(dest.getIdx());
      e.orr(dest_b, VReg16B(2), VReg16B(2));
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MAX, MAX_V128);

struct MIN_V128 : Sequence<MIN_V128, I<OPCODE_MIN, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitBinaryVOp(e, i, [](A64Emitter& e, const QReg& dest, const QReg& s1,
                           const QReg& s2) {
      e.fcmgt(VReg4S(2), VReg4S(s2.getIdx()), VReg4S(s1.getIdx()));
      e.bsl(VReg16B(2), VReg16B(s1.getIdx()), VReg16B(s2.getIdx()));
      const VReg16B dest_b(dest.getIdx());
      e.orr(dest_b, VReg16B(2), VReg16B(2));
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MIN, MIN_V128);

struct MUL_V128 : Sequence<MUL_V128, I<OPCODE_MUL, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitBinaryVOp(e, i, [](A64Emitter& e, const QReg& dest, const QReg& s1,
                           const QReg& s2) {
      e.fmul(VReg4S(dest.getIdx()), VReg4S(s1.getIdx()), VReg4S(s2.getIdx()));
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL, MUL_V128);

struct DIV_V128 : Sequence<DIV_V128, I<OPCODE_DIV, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitBinaryVOp(e, i, [](A64Emitter& e, const QReg& dest, const QReg& s1,
                           const QReg& s2) {
      e.fdiv(VReg4S(dest.getIdx()), VReg4S(s1.getIdx()), VReg4S(s2.getIdx()));
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DIV, DIV_V128);

// Unfused mul+add/sub, mirroring x64 (its FMA path is disabled because the
// test expectations were produced unfused; the scalar F32/F64 sequences are
// fused like PPC fmadd, but VMX tests expect the unfused results).
struct MUL_ADD_V128
    : Sequence<MUL_ADD_V128,
               I<OPCODE_MUL_ADD, V128Op, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    const QReg src2 = GetVWithConst(e, i.src2, QReg(1));
    const QReg src3 = GetVWithConst(e, i.src3, QReg(2));
    e.fmul(VReg4S(0), VReg4S(src1.getIdx()), VReg4S(src2.getIdx()));
    e.fadd(VReg4S(i.dest.reg().getIdx()), VReg4S(0), VReg4S(src3.getIdx()));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL_ADD, MUL_ADD_V128);

struct MUL_SUB_V128
    : Sequence<MUL_SUB_V128,
               I<OPCODE_MUL_SUB, V128Op, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    const QReg src2 = GetVWithConst(e, i.src2, QReg(1));
    const QReg src3 = GetVWithConst(e, i.src3, QReg(2));
    e.fmul(VReg4S(0), VReg4S(src1.getIdx()), VReg4S(src2.getIdx()));
    e.fsub(VReg4S(i.dest.reg().getIdx()), VReg4S(0), VReg4S(src3.getIdx()));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL_SUB, MUL_SUB_V128);

// dest = (src1 & src3) | (~src1 & src2) — set bits of the src1 mask take
// src3 (matches x64; vsel semantics).
struct SELECT_V128_I8
    : Sequence<SELECT_V128_I8, I<OPCODE_SELECT, V128Op, I8Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // dest = src1 != 0 ? src2 : src3
    e.and_(WReg(0), i.src1, 0xFF);
    e.dup(VReg4S(0), WReg(0));
    e.cmeq(VReg4S(0), VReg4S(0), 0);  // all-ones where src1 == 0
    const QReg src2 = GetVWithConst(e, i.src2, QReg(1));
    const QReg src3 = GetVWithConst(e, i.src3, QReg(2));
    e.bsl(VReg16B(0), VReg16B(src3.getIdx()), VReg16B(src2.getIdx()));
    const VReg16B dest_b(i.dest.reg().getIdx());
    e.orr(dest_b, VReg16B(0), VReg16B(0));
  }
};
struct SELECT_V128_V128
    : Sequence<SELECT_V128_V128,
               I<OPCODE_SELECT, V128Op, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    const QReg src2 = GetVWithConst(e, i.src2, QReg(1));
    const QReg src3 = GetVWithConst(e, i.src3, QReg(2));
    // Copy the mask so BSL (which consumes it in place) can't clobber a
    // pool register.
    e.orr(VReg16B(3), VReg16B(src1.getIdx()), VReg16B(src1.getIdx()));
    e.bsl(VReg16B(3), VReg16B(src3.getIdx()), VReg16B(src2.getIdx()));
    const VReg16B dest_b(i.dest.reg().getIdx());
    e.orr(dest_b, VReg16B(3), VReg16B(3));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SELECT, SELECT_V128_I8, SELECT_V128_V128);

struct IS_TRUE_V128 : Sequence<IS_TRUE_V128, I<OPCODE_IS_TRUE, I8Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    e.umaxv(BReg(0), VReg16B(src1.getIdx()));
    e.umov(WReg(0), VReg16B(0)[0]);
    e.cmp(WReg(0), 0);
    e.cset(i.dest, Cond::NE);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_IS_TRUE, IS_TRUE_V128);

struct IS_FALSE_V128
    : Sequence<IS_FALSE_V128, I<OPCODE_IS_FALSE, I8Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    e.umaxv(BReg(0), VReg16B(src1.getIdx()));
    e.umov(WReg(0), VReg16B(0)[0]);
    e.cmp(WReg(0), 0);
    e.cset(i.dest, Cond::EQ);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_IS_FALSE, IS_FALSE_V128);

// Per-lane exp2/log2 through host helpers (same as x64 and the scalar
// sequences).
struct POW2_V128 : Sequence<POW2_V128, I<OPCODE_POW2, V128Op, V128Op>> {
  static void EmulatePow2(void*, vec128_t* v) {
    for (int k = 0; k < 4; ++k) {
      v->f32[k] = std::exp2(v->f32[k]);
    }
  }
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    StashVForCall(e, src1);
    e.CallNativeSafe(reinterpret_cast<void*>(EmulatePow2));
    e.ldr(i.dest, ptr(e.sp, kVStashOffset));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_POW2, POW2_V128);

struct LOG2_V128 : Sequence<LOG2_V128, I<OPCODE_LOG2, V128Op, V128Op>> {
  static void EmulateLog2(void*, vec128_t* v) {
    for (int k = 0; k < 4; ++k) {
      v->f32[k] = std::log2(v->f32[k]);
    }
  }
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    StashVForCall(e, src1);
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateLog2));
    e.ldr(i.dest, ptr(e.sp, kVStashOffset));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOG2, LOG2_V128);

// ============================================================================
// OPCODE_VECTOR_CONVERT_I2F / OPCODE_VECTOR_CONVERT_F2I
// ============================================================================
// NEON converts are the AltiVec semantics natively: UCVTF rounds an
// unsigned 32-bit value once (x64 needs a manual-rounding dance), and
// FCVTZU/FCVTZS saturate with NaN -> 0 exactly like the x64 emulation.
struct VECTOR_CONVERT_I2F
    : Sequence<VECTOR_CONVERT_I2F,
               I<OPCODE_VECTOR_CONVERT_I2F, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    const VReg4S dest_s(i.dest.reg().getIdx());
    const VReg4S src_s(src1.getIdx());
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      e.ucvtf(dest_s, src_s);
    } else {
      e.scvtf(dest_s, src_s);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_CONVERT_I2F, VECTOR_CONVERT_I2F);

struct VECTOR_CONVERT_F2I
    : Sequence<VECTOR_CONVERT_F2I,
               I<OPCODE_VECTOR_CONVERT_F2I, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    const VReg4S dest_s(i.dest.reg().getIdx());
    const VReg4S src_s(src1.getIdx());
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      e.fcvtzu(dest_s, src_s);
    } else {
      e.fcvtzs(dest_s, src_s);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_CONVERT_F2I, VECTOR_CONVERT_F2I);

// ============================================================================
// OPCODE_DOT_PRODUCT_3 / OPCODE_DOT_PRODUCT_4
// ============================================================================
// Multiply, horizontal-add, and turn overflow (+-inf) into QNaN — the
// equivalent of x64's dpps + MXCSR-overflow check. Input NaNs propagate
// unchanged (FCMGE is false on unordered).
namespace {

void EmitDotProduct(A64Emitter& e, const SReg& dest, const QReg& s1,
                    const QReg& s2, bool zero_lane3) {
  e.fmul(VReg4S(0), VReg4S(s1.getIdx()), VReg4S(s2.getIdx()));
  if (zero_lane3) {
    e.ins(VReg4S(0)[3], e.wzr);
  }
  e.faddp(VReg4S(0), VReg4S(0), VReg4S(0));
  e.faddp(SReg(0), VReg2S(0));
  e.fabs(SReg(1), SReg(0));
  e.MovConst(WReg(0), 0x7F800000u);
  e.fmov(SReg(2), WReg(0));
  e.fcmge(SReg(1), SReg(1), SReg(2));  // lane 0 all-ones on overflow
  e.LoadVConst(QReg(2), VQNaN);
  e.bit(VReg16B(0), VReg16B(2), VReg16B(1));
  e.fmov(dest, SReg(0));
}

}  // namespace

struct DOT_PRODUCT_3_V128
    : Sequence<DOT_PRODUCT_3_V128,
               I<OPCODE_DOT_PRODUCT_3, F32Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(1));
    const QReg src2 = GetVWithConst(e, i.src2, QReg(2));
    EmitDotProduct(e, i.dest, src1, src2, true);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DOT_PRODUCT_3, DOT_PRODUCT_3_V128);

struct DOT_PRODUCT_4_V128
    : Sequence<DOT_PRODUCT_4_V128,
               I<OPCODE_DOT_PRODUCT_4, F32Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(1));
    const QReg src2 = GetVWithConst(e, i.src2, QReg(2));
    EmitDotProduct(e, i.dest, src1, src2, false);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DOT_PRODUCT_4, DOT_PRODUCT_4_V128);

// ============================================================================
// OPCODE_LOAD_VECTOR_SHL / OPCODE_LOAD_VECTOR_SHR
// ============================================================================
static const vec128_t lvsl_table[16] = {
    vec128b(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
    vec128b(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16),
    vec128b(2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17),
    vec128b(3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18),
    vec128b(4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19),
    vec128b(5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20),
    vec128b(6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21),
    vec128b(7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22),
    vec128b(8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23),
    vec128b(9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24),
    vec128b(10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25),
    vec128b(11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26),
    vec128b(12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27),
    vec128b(13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28),
    vec128b(14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29),
    vec128b(15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30),
};
static const vec128_t lvsr_table[16] = {
    vec128b(16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31),
    vec128b(15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30),
    vec128b(14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29),
    vec128b(13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28),
    vec128b(12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27),
    vec128b(11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26),
    vec128b(10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25),
    vec128b(9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24),
    vec128b(8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23),
    vec128b(7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22),
    vec128b(6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21),
    vec128b(5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20),
    vec128b(4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19),
    vec128b(3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18),
    vec128b(2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17),
    vec128b(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16),
};

namespace {

void EmitLoadShiftTable(A64Emitter& e, const V128Op& dest, const I8Op& sh,
                        const vec128_t* table) {
  if (sh.is_constant) {
    e.MovConst(XReg(0), reinterpret_cast<uintptr_t>(&table[sh.constant() &
                                                           0xF]));
    e.ldr(dest.reg(), ptr(XReg(0)));
  } else {
    e.and_(WReg(1), sh, 0xF);
    e.lsl(WReg(1), WReg(1), 4);
    e.MovConst(XReg(0), reinterpret_cast<uintptr_t>(table));
    e.ldr(dest.reg(), ptr(XReg(0), XReg(1)));
  }
}

}  // namespace

struct LOAD_VECTOR_SHL_I8
    : Sequence<LOAD_VECTOR_SHL_I8, I<OPCODE_LOAD_VECTOR_SHL, V128Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitLoadShiftTable(e, i.dest, i.src1, lvsl_table);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_VECTOR_SHL, LOAD_VECTOR_SHL_I8);

struct LOAD_VECTOR_SHR_I8
    : Sequence<LOAD_VECTOR_SHR_I8, I<OPCODE_LOAD_VECTOR_SHR, V128Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitLoadShiftTable(e, i.dest, i.src1, lvsr_table);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_VECTOR_SHR, LOAD_VECTOR_SHR_I8);

// ============================================================================
// OPCODE_VECTOR_SHL / OPCODE_VECTOR_SHR / OPCODE_VECTOR_SHA /
// OPCODE_VECTOR_ROTATE_LEFT
// ============================================================================
// NEON USHL/SSHL shift each lane by a per-lane *signed* count (negative =
// right), so all four ops reduce to masking the count and (for right
// shifts) negating it. PPC semantics mask the count to the element width.
namespace {

enum class VecShiftKind { kShl, kShr, kSha, kRotate };

template <typename VELEM>
void EmitVecShiftT(A64Emitter& e, const QReg& dest, const QReg& src1,
                   const QReg& src2, uint32_t bits, VecShiftKind kind) {
  const VELEM v2(2), v3(3), v4(4);
  const VELEM a(src1.getIdx());
  const VELEM b(src2.getIdx());
  const VELEM d(dest.getIdx());
  const VReg16B v2_b(2), v4_b(4), b_b(src2.getIdx());
  // v2 = shift counts masked to the element width.
  e.movi(v2, bits - 1);
  e.and_(v2_b, b_b, v2_b);
  switch (kind) {
    case VecShiftKind::kShl:
      e.ushl(d, a, v2);
      break;
    case VecShiftKind::kShr:
      e.neg(v2, v2);
      e.ushl(d, a, v2);
      break;
    case VecShiftKind::kSha:
      e.neg(v2, v2);
      e.sshl(d, a, v2);
      break;
    case VecShiftKind::kRotate:
      // (src1 << n) | (src1 >> (bits - n)); n == 0 degenerates cleanly
      // because a right shift by the full width gives zero.
      e.ushl(v3, a, v2);
      e.movi(v4, bits);
      e.sub(v2, v2, v4);
      e.ushl(v2, a, v2);
      e.orr(VReg16B(d.getIdx()), VReg16B(3), VReg16B(2));
      break;
  }
}

template <typename ARGS>
void EmitVecShift(A64Emitter& e, const ARGS& i, VecShiftKind kind) {
  const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
  const QReg src2 = GetVWithConst(e, i.src2, QReg(1));
  const QReg dest = i.dest.reg();
  switch (i.instr->flags) {
    case INT8_TYPE:
      EmitVecShiftT<VReg16B>(e, dest, src1, src2, 8, kind);
      break;
    case INT16_TYPE:
      EmitVecShiftT<VReg8H>(e, dest, src1, src2, 16, kind);
      break;
    case INT32_TYPE:
      EmitVecShiftT<VReg4S>(e, dest, src1, src2, 32, kind);
      break;
    default:
      assert_unhandled_case(i.instr->flags);
      break;
  }
}

}  // namespace

struct VECTOR_SHL_V128
    : Sequence<VECTOR_SHL_V128, I<OPCODE_VECTOR_SHL, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitVecShift(e, i, VecShiftKind::kShl);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SHL, VECTOR_SHL_V128);

struct VECTOR_SHR_V128
    : Sequence<VECTOR_SHR_V128, I<OPCODE_VECTOR_SHR, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitVecShift(e, i, VecShiftKind::kShr);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SHR, VECTOR_SHR_V128);

struct VECTOR_SHA_V128
    : Sequence<VECTOR_SHA_V128, I<OPCODE_VECTOR_SHA, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitVecShift(e, i, VecShiftKind::kSha);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SHA, VECTOR_SHA_V128);

struct VECTOR_ROTATE_LEFT_V128
    : Sequence<VECTOR_ROTATE_LEFT_V128,
               I<OPCODE_VECTOR_ROTATE_LEFT, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitVecShift(e, i, VecShiftKind::kRotate);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_ROTATE_LEFT, VECTOR_ROTATE_LEFT_V128);

// ============================================================================
// OPCODE_SHL / OPCODE_SHR (whole-vector bit shifts, vsl/vsr)
// ============================================================================
// Shift the full 128-bit value by 0-7 bits through a host helper, same as
// x64. The byte loop is endian-flipped (^0x3) like the vec128 layout.
struct SHL_V128 : Sequence<SHL_V128, I<OPCODE_SHL, V128Op, V128Op, I8Op>> {
  static void EmulateShlV128(void*, vec128_t* v, uint64_t src2) {
    const uint8_t shamt = src2 & 0x7;
    vec128_t value = *v;
    for (int k = 0; k < 15; ++k) {
      value.u8[k ^ 0x3] = (value.u8[k ^ 0x3] << shamt) |
                          (value.u8[(k + 1) ^ 0x3] >> (8 - shamt));
    }
    value.u8[15 ^ 0x3] = value.u8[15 ^ 0x3] << shamt;
    *v = value;
  }
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    StashVForCall(e, src1);
    if (i.src2.is_constant) {
      e.MovConst(WReg(2), uint32_t(i.src2.constant()));
    } else {
      e.mov(WReg(2), i.src2);
    }
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateShlV128));
    e.ldr(i.dest, ptr(e.sp, kVStashOffset));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHL, SHL_V128);

struct SHR_V128 : Sequence<SHR_V128, I<OPCODE_SHR, V128Op, V128Op, I8Op>> {
  static void EmulateShrV128(void*, vec128_t* v, uint64_t src2) {
    const uint8_t shamt = src2 & 0x7;
    vec128_t value = *v;
    for (int k = 15; k > 0; --k) {
      value.u8[k ^ 0x3] = (value.u8[k ^ 0x3] >> shamt) |
                          (value.u8[(k - 1) ^ 0x3] << (8 - shamt));
    }
    value.u8[0 ^ 0x3] = value.u8[0 ^ 0x3] >> shamt;
    *v = value;
  }
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    StashVForCall(e, src1);
    if (i.src2.is_constant) {
      e.MovConst(WReg(2), uint32_t(i.src2.constant()));
    } else {
      e.mov(WReg(2), i.src2);
    }
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateShrV128));
    e.ldr(i.dest, ptr(e.sp, kVStashOffset));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHR, SHR_V128);

// ============================================================================
// OPCODE_DID_SATURATE
// ============================================================================
struct DID_SATURATE
    : Sequence<DID_SATURATE, I<OPCODE_DID_SATURATE, I8Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): implement saturation check (VECTOR_ADD, etc).
    // Stubbed to 0, same as the x64 backend.
    e.eor(i.dest, i.dest, i.dest);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DID_SATURATE, DID_SATURATE);

// ============================================================================
// OPCODE_PERMUTE
// ============================================================================
// AltiVec-style permutes: src1 selects bytes/halves/words from the 32-byte
// concatenation of src2 (indices 0-15) and src3 (16-31), in big-endian byte
// numbering. NEON TBL with a two-register table does this natively; the
// table registers must be consecutive, so sources are staged into v1/v2 and
// the (fixed-up) control into v0.
namespace {

void LoadPermuteTables(A64Emitter& e, const V128Op& src2, const V128Op& src3) {
  const VReg16B v1_b(1);
  const VReg16B v2_b(2);
  if (src2.is_constant) {
    e.LoadConstantV(QReg(1), src2.constant());
  } else {
    const VReg16B s2(src2.reg().getIdx());
    e.orr(v1_b, s2, s2);
  }
  if (src3.is_constant) {
    e.LoadConstantV(QReg(2), src3.constant());
  } else {
    const VReg16B s3(src3.reg().getIdx());
    e.orr(v2_b, s3, s3);
  }
}

void EmitPermuteTbl2(A64Emitter& e, const QReg& dest) {
  const VReg16B dest_b(dest.getIdx());
  e.tbl(dest_b, VReg16BList(VReg16B(1), VReg16B(2)), VReg16B(0));
}

}  // namespace

struct PERMUTE_I32
    : Sequence<PERMUTE_I32, I<OPCODE_PERMUTE, V128Op, I32Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.instr->flags == INT32_TYPE);
    // Permute words between src2 and src3 by a constant control
    // (see MakePermuteMask): result word w <- word (control >> (8*w)) & 3 of
    // src2/src3 selected by bit (control >> (8*w + 2)) & 1.
    if (i.src1.is_constant) {
      const uint32_t control = i.src1.constant();
      vec128_t ctrl = vec128b(0);
      for (uint32_t w = 0; w < 4; ++w) {
        const uint32_t field = control >> (8 * w);
        const uint32_t base = ((field >> 2) & 0x1) * 16 + (field & 0x3) * 4;
        for (uint32_t b = 0; b < 4; ++b) {
          ctrl.u8[w * 4 + b] = uint8_t(base + b);
        }
      }
      LoadPermuteTables(e, i.src2, i.src3);
      e.LoadConstantV(QReg(0), ctrl);
      EmitPermuteTbl2(e, i.dest);
    } else {
      // Permute by non-constant.
      assert_always();
    }
  }
};
struct PERMUTE_V128
    : Sequence<PERMUTE_V128,
               I<OPCODE_PERMUTE, V128Op, V128Op, V128Op, V128Op>> {
  static void EmitByInt8(A64Emitter& e, const EmitArgType& i) {
    LoadPermuteTables(e, i.src2, i.src3);
    // Convert the big-endian byte indices to lane order (^0x03) and mask to
    // the 5 bits vperm honors (TBL zeroes out-of-range indices instead of
    // wrapping).
    if (i.src1.is_constant) {
      vec128_t ctrl = i.src1.constant();
      for (int k = 0; k < 16; ++k) {
        ctrl.u8[k] = (ctrl.u8[k] ^ 0x03) & 0x1F;
      }
      e.LoadConstantV(QReg(0), ctrl);
    } else {
      const VReg16B v0_b(0);
      const VReg16B v3_b(3);
      const VReg16B src1_b(i.src1.reg().getIdx());
      e.LoadVConst(QReg(0), VSwapWordMask);
      e.eor(v0_b, src1_b, v0_b);
      e.LoadVConst(QReg(3), VPermuteByteMask);
      e.and_(v0_b, v0_b, v3_b);
    }
    EmitPermuteTbl2(e, i.dest);
  }
  static void EmitByInt16(A64Emitter& e, const EmitArgType& i) {
    // src1 is an array of 16-bit indices into the src2:src3 halfword
    // concatenation, big-endian fixed up with ^1.
    assert_true(i.src1.is_constant);
    LoadPermuteTables(e, i.src2, i.src3);
    vec128_t perm = (i.src1.constant() & vec128s(0xF)) ^ vec128s(0x1);
    vec128_t ctrl = vec128b(0);
    for (int k = 0; k < 8; ++k) {
      const uint8_t v = uint8_t(perm.u16[k]);
      ctrl.u8[k * 2] = v * 2;
      ctrl.u8[k * 2 + 1] = v * 2 + 1;
    }
    e.LoadConstantV(QReg(0), ctrl);
    EmitPermuteTbl2(e, i.dest);
  }
  static void EmitByInt32(A64Emitter& e, const EmitArgType& i) {
    assert_always();
  }
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case INT8_TYPE:
        EmitByInt8(e, i);
        break;
      case INT16_TYPE:
        EmitByInt16(e, i);
        break;
      case INT32_TYPE:
        EmitByInt32(e, i);
        break;
      default:
        assert_unhandled_case(i.instr->flags);
        return;
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_PERMUTE, PERMUTE_I32, PERMUTE_V128);

// ============================================================================
// OPCODE_SWIZZLE
// ============================================================================
struct SWIZZLE
    : Sequence<SWIZZLE, I<OPCODE_SWIZZLE, V128Op, V128Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto element_type = i.instr->flags;
    if (element_type == INT32_TYPE || element_type == FLOAT32_TYPE) {
      // Same imm encoding as x64 vpshufd: result lane k <- src lane
      // (mask >> 2k) & 3.
      const uint8_t swizzle_mask = static_cast<uint8_t>(i.src2.value);
      const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
      vec128_t ctrl = vec128b(0);
      for (uint32_t k = 0; k < 4; ++k) {
        const uint32_t idx = (swizzle_mask >> (2 * k)) & 0x3;
        for (uint32_t b = 0; b < 4; ++b) {
          ctrl.u8[k * 4 + b] = uint8_t(idx * 4 + b);
        }
      }
      const QReg temp(1);
      e.LoadConstantV(temp, ctrl);
      e.tbl(VReg16B(i.dest.reg().getIdx()),
            VReg16BList(VReg16B(src1.getIdx())), VReg16B(temp.getIdx()));
    } else {
      assert_always();
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SWIZZLE, SWIZZLE);

// ============================================================================
// OPCODE_INSERT
// ============================================================================
// dest = src1 with the lane at (flipped) constant index src2 replaced by
// src3. INS writes in place, so src1 is copied into dest first.
namespace {

void CopyToDest(A64Emitter& e, const V128Op& dest, const V128Op& src) {
  if (src.is_constant) {
    e.LoadConstantV(dest.reg(), src.constant());
  } else if (dest.reg().getIdx() != src.reg().getIdx()) {
    const VReg16B dest_b(dest.reg().getIdx());
    const VReg16B src_b(src.reg().getIdx());
    e.orr(dest_b, src_b, src_b);
  }
}

}  // namespace

struct INSERT_I8
    : Sequence<INSERT_I8, I<OPCODE_INSERT, V128Op, V128Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    CopyToDest(e, i.dest, i.src1);
    // After CopyToDest: LoadConstantV clobbers x0/x1.
    const WReg src3 = GetWithConst(e, i.src3, WReg(0));
    e.ins(VReg16B(i.dest.reg().getIdx())[VEC128_B(i.src2.constant() & 0xF)],
          src3);
  }
};
struct INSERT_I16
    : Sequence<INSERT_I16, I<OPCODE_INSERT, V128Op, V128Op, I8Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    CopyToDest(e, i.dest, i.src1);
    // After CopyToDest: LoadConstantV clobbers x0/x1.
    const WReg src3 = GetWithConst(e, i.src3, WReg(0));
    e.ins(VReg8H(i.dest.reg().getIdx())[VEC128_W(i.src2.constant() & 0x7)],
          src3);
  }
};
struct INSERT_I32
    : Sequence<INSERT_I32, I<OPCODE_INSERT, V128Op, V128Op, I8Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    CopyToDest(e, i.dest, i.src1);
    // After CopyToDest: LoadConstantV clobbers x0/x1.
    const WReg src3 = GetWithConst(e, i.src3, WReg(0));
    e.ins(VReg4S(i.dest.reg().getIdx())[i.src2.constant() & 0x3], src3);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_INSERT, INSERT_I8, INSERT_I16, INSERT_I32);

// ============================================================================
// OPCODE_EXTRACT
// ============================================================================
// Constant lane indices extract directly with UMOV; dynamic indices go
// through the guest-frame stash (like x64's shuffle trick, the index is
// masked to the vector, matching pshufb's low-4-bit behavior).
struct EXTRACT_I8
    : Sequence<EXTRACT_I8, I<OPCODE_EXTRACT, I8Op, V128Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    if (i.src2.is_constant) {
      e.umov(i.dest, VReg16B(src1.getIdx())[VEC128_B(i.src2.constant() & 0xF)]);
    } else {
      e.str(src1, ptr(e.sp, kVStashOffset));
      e.eor(WReg(1), i.src2, 0x3);
      e.and_(WReg(1), WReg(1), 0xF);
      e.add(XReg(0), e.sp, kVStashOffset);
      e.ldrb(i.dest, ptr(XReg(0), XReg(1)));
    }
  }
};
struct EXTRACT_I16
    : Sequence<EXTRACT_I16, I<OPCODE_EXTRACT, I16Op, V128Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    if (i.src2.is_constant) {
      e.umov(i.dest, VReg8H(src1.getIdx())[VEC128_W(i.src2.constant() & 0x7)]);
    } else {
      e.str(src1, ptr(e.sp, kVStashOffset));
      e.eor(WReg(1), i.src2, 0x1);
      e.and_(WReg(1), WReg(1), 0x7);
      e.lsl(WReg(1), WReg(1), 1);
      e.add(XReg(0), e.sp, kVStashOffset);
      e.ldrh(i.dest, ptr(XReg(0), XReg(1)));
    }
  }
};
struct EXTRACT_I32
    : Sequence<EXTRACT_I32, I<OPCODE_EXTRACT, I32Op, V128Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    if (i.src2.is_constant) {
      e.umov(i.dest, VReg4S(src1.getIdx())[i.src2.constant() & 0x3]);
    } else {
      e.str(src1, ptr(e.sp, kVStashOffset));
      e.and_(WReg(1), i.src2, 0x3);
      e.lsl(WReg(1), WReg(1), 2);
      e.add(XReg(0), e.sp, kVStashOffset);
      e.ldr(i.dest, ptr(XReg(0), XReg(1)));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_EXTRACT, EXTRACT_I8, EXTRACT_I16, EXTRACT_I32);

// ============================================================================
// OPCODE_SPLAT
// ============================================================================
// Copy a value into all elements of a vector.
struct SPLAT_I8 : Sequence<SPLAT_I8, I<OPCODE_SPLAT, V128Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const VReg16B dest_b(i.dest.reg().getIdx());
    if (i.src1.is_constant) {
      e.MovConst(WReg(0), uint8_t(i.src1.constant()));
      e.dup(dest_b, WReg(0));
    } else {
      e.dup(dest_b, i.src1);
    }
  }
};
struct SPLAT_I16 : Sequence<SPLAT_I16, I<OPCODE_SPLAT, V128Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const VReg8H dest_h(i.dest.reg().getIdx());
    if (i.src1.is_constant) {
      e.MovConst(WReg(0), uint16_t(i.src1.constant()));
      e.dup(dest_h, WReg(0));
    } else {
      e.dup(dest_h, i.src1);
    }
  }
};
struct SPLAT_I32 : Sequence<SPLAT_I32, I<OPCODE_SPLAT, V128Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const VReg4S dest_s(i.dest.reg().getIdx());
    if (i.src1.is_constant) {
      e.MovConst(WReg(0), uint32_t(i.src1.constant()));
      e.dup(dest_s, WReg(0));
    } else {
      e.dup(dest_s, i.src1);
    }
  }
};
struct SPLAT_F32 : Sequence<SPLAT_F32, I<OPCODE_SPLAT, V128Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const VReg4S dest_s(i.dest.reg().getIdx());
    if (i.src1.is_constant) {
      e.MovConst(WReg(0), uint32_t(i.src1.value->constant.i32));
      e.dup(dest_s, WReg(0));
    } else {
      e.dup(dest_s, VReg4S(i.src1.reg().getIdx())[0]);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SPLAT, SPLAT_I8, SPLAT_I16, SPLAT_I32, SPLAT_F32);

// ============================================================================
// OPCODE_PACK
// ============================================================================
struct PACK : Sequence<PACK, I<OPCODE_PACK, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags & PACK_TYPE_MODE) {
      case PACK_TYPE_D3DCOLOR:
        EmitD3DCOLOR(e, i);
        break;
      case PACK_TYPE_FLOAT16_2:
        EmitFLOAT16_2(e, i);
        break;
      case PACK_TYPE_FLOAT16_4:
        EmitFLOAT16_4(e, i);
        break;
      case PACK_TYPE_SHORT_2:
        EmitSHORT_2(e, i);
        break;
      case PACK_TYPE_SHORT_4:
        EmitSHORT_4(e, i);
        break;
      case PACK_TYPE_UINT_2101010:
        EmitUINT_2101010(e, i);
        break;
      case PACK_TYPE_ULONG_4202020:
        EmitULONG_4202020(e, i);
        break;
      case PACK_TYPE_8_IN_16:
        Emit8_IN_16(e, i, i.instr->flags);
        break;
      case PACK_TYPE_16_IN_32:
        Emit16_IN_32(e, i, i.instr->flags);
        break;
      default:
        assert_unhandled_case(i.instr->flags);
        break;
    }
  }
  static void EmitD3DCOLOR(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->IsConstantZero());
    const QReg src = GetVWithConst(e, i.src1, QReg(0));
    const VReg4S dest_s(i.dest.reg().getIdx());
    const VReg16B dest_b(i.dest.reg().getIdx());
    const QReg temp(1);
    const VReg4S temp_s(1);
    const VReg16B temp_b(1);
    // Saturate to [3,3....] so that only values between 3...[00] and 3...[FF]
    // are valid - max before min to pack NaN as zero.
    e.LoadVConst(temp, V3333);
    // fmaxnm, not fmax: x64 maxps returns the clamp constant when src is NaN.
    e.fmaxnm(dest_s, VReg4S(src.getIdx()), temp_s);
    e.LoadVConst(temp, VPackD3DCOLORSat);
    e.fmin(dest_s, dest_s, temp_s);
    // Extract bytes: RGBA (XYZW) -> ARGB (WXYZ).
    e.LoadVConst(temp, VPackD3DCOLOR);
    e.tbl(dest_b, VReg16BList(dest_b), temp_b);
  }
  static void EmulateFLOAT16_2(void*, vec128_t* v) {
    alignas(16) float a[4];
    alignas(16) uint16_t b[8];
    std::memcpy(a, v, sizeof(a));
    std::memset(b, 0, sizeof(b));
    for (int k = 0; k < 2; ++k) {
      b[7 - k] = half_float::detail::float2half<std::round_toward_zero>(a[k]);
    }
    std::memcpy(v, b, sizeof(b));
  }
  static void EmitFLOAT16_2(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->IsConstantZero());
    // dest = [(src1.x | src1.y), 0, 0, 0]
    // Exact round-toward-zero float->half via host helper (FCVTN rounds by
    // FPCR, which the guest controls, so it can't be pinned to RTZ inline).
    const QReg src = GetVWithConst(e, i.src1, QReg(0));
    StashVForCall(e, src);
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateFLOAT16_2));
    e.ldr(i.dest, ptr(e.sp, kVStashOffset));
  }
  static void EmulateFLOAT16_4(void*, vec128_t* v) {
    alignas(16) float a[4];
    alignas(16) uint16_t b[8];
    std::memcpy(a, v, sizeof(a));
    std::memset(b, 0, sizeof(b));
    for (int k = 0; k < 4; ++k) {
      b[7 - (k ^ 2)] =
          half_float::detail::float2half<std::round_toward_zero>(a[k]);
    }
    std::memcpy(v, b, sizeof(b));
  }
  static void EmitFLOAT16_4(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->IsConstantZero());
    // dest = [(src1.z | src1.w), (src1.x | src1.y), 0, 0]
    const QReg src = GetVWithConst(e, i.src1, QReg(0));
    StashVForCall(e, src);
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateFLOAT16_4));
    e.ldr(i.dest, ptr(e.sp, kVStashOffset));
  }
  static void EmitSHORT_2(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->IsConstantZero());
    const QReg src = GetVWithConst(e, i.src1, QReg(0));
    const VReg4S dest_s(i.dest.reg().getIdx());
    const VReg16B dest_b(i.dest.reg().getIdx());
    const QReg temp(1);
    const VReg4S temp_s(1);
    const VReg16B temp_b(1);
    // Saturate.
    e.LoadVConst(temp, VPackSHORT_Min);
    // fmaxnm, not fmax: x64 maxps returns the clamp constant when src is NaN.
    e.fmaxnm(dest_s, VReg4S(src.getIdx()), temp_s);
    e.LoadVConst(temp, VPackSHORT_Max);
    e.fmin(dest_s, dest_s, temp_s);
    // Pack.
    e.LoadVConst(temp, VPackSHORT_2);
    e.tbl(dest_b, VReg16BList(dest_b), temp_b);
  }
  static void EmitSHORT_4(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->IsConstantZero());
    const QReg src = GetVWithConst(e, i.src1, QReg(0));
    const VReg4S dest_s(i.dest.reg().getIdx());
    const VReg16B dest_b(i.dest.reg().getIdx());
    const QReg temp(1);
    const VReg4S temp_s(1);
    const VReg16B temp_b(1);
    // Saturate.
    e.LoadVConst(temp, VPackSHORT_Min);
    // fmaxnm, not fmax: x64 maxps returns the clamp constant when src is NaN.
    e.fmaxnm(dest_s, VReg4S(src.getIdx()), temp_s);
    e.LoadVConst(temp, VPackSHORT_Max);
    e.fmin(dest_s, dest_s, temp_s);
    // Pack.
    e.LoadVConst(temp, VPackSHORT_4);
    e.tbl(dest_b, VReg16BList(dest_b), temp_b);
  }
  static void EmitUINT_2101010(A64Emitter& e, const EmitArgType& i) {
    // XYZ are 10 bits, signed and saturated. W is 2 bits, unsigned and
    // saturated.
    const QReg src = GetVWithConst(e, i.src1, QReg(0));
    const VReg4S dest_s(i.dest.reg().getIdx());
    const VReg16B dest_b(i.dest.reg().getIdx());
    const QReg temp(1);
    const VReg4S temp_s(1);
    const VReg16B temp_b(1);
    const VReg4S v0_s(0);
    const VReg16B v0_b(0);
    // Saturate.
    e.LoadVConst(temp, VPackUINT_2101010_MinUnpacked);
    // fmaxnm, not fmax: x64 maxps returns the clamp constant when src is NaN.
    e.fmaxnm(dest_s, VReg4S(src.getIdx()), temp_s);
    e.LoadVConst(temp, VPackUINT_2101010_MaxUnpacked);
    e.fmin(dest_s, dest_s, temp_s);
    // Remove the unneeded bits of the floats.
    e.LoadVConst(temp, VPackUINT_2101010_MaskUnpacked);
    e.and_(dest_b, dest_b, temp_b);
    // Shift the components up (per-lane variable shift).
    e.LoadVConst(temp, VPackUINT_2101010_Shift);
    e.ushl(dest_s, dest_s, temp_s);
    // OR-combine all four lanes into every lane.
    e.ext(v0_b, dest_b, dest_b, 4);
    e.orr(dest_b, dest_b, v0_b);
    e.ext(v0_b, dest_b, dest_b, 8);
    e.orr(dest_b, dest_b, v0_b);
  }
  static void EmitULONG_4202020(A64Emitter& e, const EmitArgType& i) {
    // XYZ are 20 bits, signed and saturated. W is 4 bits, unsigned and
    // saturated.
    const QReg src = GetVWithConst(e, i.src1, QReg(0));
    const VReg4S dest_s(i.dest.reg().getIdx());
    const VReg16B dest_b(i.dest.reg().getIdx());
    const QReg temp(1);
    const VReg4S temp_s(1);
    const VReg16B temp_b(1);
    const VReg4S v0_s(0);
    const VReg16B v0_b(0);
    const VReg16B v2_b(2);
    // Saturate.
    e.LoadVConst(temp, VPackULONG_4202020_MinUnpacked);
    // fmaxnm, not fmax: x64 maxps returns the clamp constant when src is NaN.
    e.fmaxnm(dest_s, VReg4S(src.getIdx()), temp_s);
    e.LoadVConst(temp, VPackULONG_4202020_MaxUnpacked);
    e.fmin(dest_s, dest_s, temp_s);
    // Remove the unneeded bits of the floats (so excess nibbles will also be
    // cleared).
    e.LoadVConst(temp, VPackULONG_4202020_MaskUnpacked);
    e.and_(dest_b, dest_b, temp_b);
    // Store Y and W shifted left by 4 so the byte shuffle can use them.
    e.shl(v0_s, dest_s, 4);
    // Place XZ where they're supposed to be.
    e.LoadVConst(temp, VPackULONG_4202020_PermuteXZ);
    e.tbl(dest_b, VReg16BList(dest_b), temp_b);
    // Place YW.
    e.LoadVConst(temp, VPackULONG_4202020_PermuteYW);
    e.tbl(v2_b, VReg16BList(v0_b), temp_b);
    // Merge XZ and YW.
    e.orr(dest_b, dest_b, v2_b);
  }
  static void Emit8_IN_16(A64Emitter& e, const EmitArgType& i,
                          uint32_t flags) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    const QReg src2 = GetVWithConst(e, i.src2, QReg(1));
    const VReg8H src1_h(src1.getIdx());
    const VReg8H src2_h(src2.getIdx());
    const VReg8B v2_b8(2);
    const VReg16B v2_b(2);
    const VReg8H dest_h(i.dest.reg().getIdx());
    const VReg8H v2_h(2);
    // Narrow src1 into the low 8 bytes and src2 into the high 8, then swap
    // the 16-bit halves within each word (the big-endian element fixup x64
    // does with XMMByteOrderMask).
    if (IsPackInUnsigned(flags)) {
      if (IsPackOutUnsigned(flags)) {
        if (IsPackOutSaturate(flags)) {
          // unsigned -> unsigned + saturate
          e.uqxtn(v2_b8, src1_h);
          e.uqxtn2(v2_b, src2_h);
        } else {
          // unsigned -> unsigned
          e.xtn(v2_b8, src1_h);
          e.xtn2(v2_b, src2_h);
        }
      } else {
        // unsigned -> signed
        assert_always();
        return;
      }
    } else {
      if (IsPackOutUnsigned(flags)) {
        if (IsPackOutSaturate(flags)) {
          // signed -> unsigned + saturate
          e.sqxtun(v2_b8, src1_h);
          e.sqxtun2(v2_b, src2_h);
        } else {
          // signed -> unsigned
          assert_always();
          return;
        }
      } else {
        if (IsPackOutSaturate(flags)) {
          // signed -> signed + saturate
          e.sqxtn(v2_b8, src1_h);
          e.sqxtn2(v2_b, src2_h);
        } else {
          // signed -> signed
          assert_always();
          return;
        }
      }
    }
    e.rev32(dest_h, v2_h);
  }
  // Pack 2 32-bit vectors into a 16-bit vector.
  static void Emit16_IN_32(A64Emitter& e, const EmitArgType& i,
                           uint32_t flags) {
    const QReg src1 = GetVWithConst(e, i.src1, QReg(0));
    const QReg src2 = GetVWithConst(e, i.src2, QReg(1));
    const VReg4S src1_s(src1.getIdx());
    const VReg4S src2_s(src2.getIdx());
    const VReg4H v2_h4(2);
    const VReg8H v2_h(2);
    const VReg8H dest_h(i.dest.reg().getIdx());
    // Narrow then swap the 16-bit halves within each 32-bit word (the
    // big-endian element fixup x64 does with vpshuflw/vpshufhw 0xB1).
    if (IsPackInUnsigned(flags)) {
      if (IsPackOutUnsigned(flags)) {
        if (IsPackOutSaturate(flags)) {
          // unsigned -> unsigned + saturate
          e.uqxtn(v2_h4, src1_s);
          e.uqxtn2(v2_h, src2_s);
        } else {
          // unsigned -> unsigned
          e.xtn(v2_h4, src1_s);
          e.xtn2(v2_h, src2_s);
        }
      } else {
        // unsigned -> signed
        assert_always();
        return;
      }
    } else {
      if (IsPackOutUnsigned(flags)) {
        if (IsPackOutSaturate(flags)) {
          // signed -> unsigned + saturate
          e.sqxtun(v2_h4, src1_s);
          e.sqxtun2(v2_h, src2_s);
        } else {
          // signed -> unsigned
          assert_always();
          return;
        }
      } else {
        if (IsPackOutSaturate(flags)) {
          // signed -> signed + saturate
          e.sqxtn(v2_h4, src1_s);
          e.sqxtn2(v2_h, src2_s);
        } else {
          // signed -> signed
          assert_always();
          return;
        }
      }
    }
    e.rev32(dest_h, v2_h);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_PACK, PACK);

// ============================================================================
// OPCODE_UNPACK
// ============================================================================
struct UNPACK : Sequence<UNPACK, I<OPCODE_UNPACK, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags & PACK_TYPE_MODE) {
      case PACK_TYPE_D3DCOLOR:
        EmitD3DCOLOR(e, i);
        break;
      case PACK_TYPE_FLOAT16_2:
        EmitFLOAT16_2(e, i);
        break;
      case PACK_TYPE_FLOAT16_4:
        EmitFLOAT16_4(e, i);
        break;
      case PACK_TYPE_SHORT_2:
        EmitSHORT_2(e, i);
        break;
      case PACK_TYPE_SHORT_4:
        EmitSHORT_4(e, i);
        break;
      case PACK_TYPE_UINT_2101010:
        EmitUINT_2101010(e, i);
        break;
      case PACK_TYPE_ULONG_4202020:
        EmitULONG_4202020(e, i);
        break;
      case PACK_TYPE_8_IN_16:
        Emit8_IN_16(e, i, i.instr->flags);
        break;
      case PACK_TYPE_16_IN_32:
        Emit16_IN_32(e, i, i.instr->flags);
        break;
      default:
        assert_unhandled_case(i.instr->flags);
        break;
    }
  }
  // Replaces lanes of dest with QNaN where they compare equal to the
  // overflow constant (the negative-overflow fixup all the D3D unpacks
  // share).
  static void EmitOverflowQNaN(A64Emitter& e, const QReg& dest,
                               VConst overflow) {
    const VReg4S dest_s(dest.getIdx());
    const VReg16B dest_b(dest.getIdx());
    const QReg temp(1);
    const VReg4S temp_s(1);
    const VReg16B temp_b(1);
    const VReg4S v0_s(0);
    const VReg16B v0_b(0);
    e.LoadVConst(temp, overflow);
    e.fcmeq(v0_s, dest_s, temp_s);
    e.LoadVConst(temp, VQNaN);
    e.bit(dest_b, temp_b, v0_b);
  }
  static void EmitD3DCOLOR(A64Emitter& e, const EmitArgType& i) {
    // ARGB (WXYZ) -> RGBA (XYZW)
    const VReg16B dest_b(i.dest.reg().getIdx());
    const QReg temp(1);
    const VReg16B temp_b(1);
    if (i.src1.is_constant && i.src1.value->IsConstantZero()) {
      e.LoadVConst(i.dest, VOnePS);
      return;
    }
    const QReg src = GetVWithConst(e, i.src1, QReg(0));
    // Unpack to 000000ZZ,000000YY,000000XX,000000WW.
    e.LoadVConst(temp, VUnpackD3DCOLOR);
    e.tbl(dest_b, VReg16BList(VReg16B(src.getIdx())), temp_b);
    // Add 1.0f to each.
    e.LoadVConst(temp, VOnePS);
    e.orr(dest_b, dest_b, temp_b);
  }
  static void EmitFLOAT16_2(A64Emitter& e, const EmitArgType& i) {
    // Half->float is exact regardless of rounding mode, so unpack natively:
    // shuffle the two halves into h[0]/h[1], widen, then set w to 1.0f.
    const VReg16B dest_b(i.dest.reg().getIdx());
    const VReg4S dest_s(i.dest.reg().getIdx());
    const VReg4H dest_h4(i.dest.reg().getIdx());
    const QReg temp(1);
    const VReg16B temp_b(1);
    const QReg src = GetVWithConst(e, i.src1, QReg(0));
    e.LoadVConst(temp, VUnpackFLOAT16_2);
    e.tbl(dest_b, VReg16BList(VReg16B(src.getIdx())), temp_b);
    e.fcvtl(dest_s, dest_h4);
    // dest = [x, y, 0, 1.0f]
    e.LoadVConst(temp, V0001);
    e.orr(dest_b, dest_b, temp_b);
  }
  static void EmitFLOAT16_4(A64Emitter& e, const EmitArgType& i) {
    const VReg16B dest_b(i.dest.reg().getIdx());
    const VReg4S dest_s(i.dest.reg().getIdx());
    const VReg4H dest_h4(i.dest.reg().getIdx());
    const QReg temp(1);
    const VReg16B temp_b(1);
    const QReg src = GetVWithConst(e, i.src1, QReg(0));
    e.LoadVConst(temp, VUnpackFLOAT16_4);
    e.tbl(dest_b, VReg16BList(VReg16B(src.getIdx())), temp_b);
    e.fcvtl(dest_s, dest_h4);
  }
  static void EmitSHORT_2(A64Emitter& e, const EmitArgType& i) {
    // (VD.x) = 3.0 + (VB.x>>16)*2^-22
    // (VD.y) = 3.0 + (VB.x)*2^-22
    // (VD.z) = 0.0
    // (VD.w) = 1.0
    const VReg16B dest_b(i.dest.reg().getIdx());
    const VReg4S dest_s(i.dest.reg().getIdx());
    const QReg temp(1);
    const VReg16B temp_b(1);
    const VReg4S temp_s(1);
    if (i.src1.is_constant && i.src1.value->IsConstantZero()) {
      e.LoadVConst(i.dest, V3301);
      return;
    }
    const QReg src = GetVWithConst(e, i.src1, QReg(0));
    // Shuffle bytes.
    e.LoadVConst(temp, VUnpackSHORT_2);
    e.tbl(dest_b, VReg16BList(VReg16B(src.getIdx())), temp_b);
    // If negative, make smaller than 3 - sign extend before adding.
    e.shl(dest_s, dest_s, 16);
    e.sshr(dest_s, dest_s, 16);
    // Add 3,3,0,1.
    e.LoadVConst(temp, V3301);
    e.add(dest_s, dest_s, temp_s);
    // Return quiet NaNs in case of negative overflow.
    EmitOverflowQNaN(e, i.dest, VUnpackSHORT_Overflow);
  }
  static void EmitSHORT_4(A64Emitter& e, const EmitArgType& i) {
    // (VD.x) = 3.0 + (VB.x>>16)*2^-22
    // (VD.y) = 3.0 + (VB.x)*2^-22
    // (VD.z) = 3.0 + (VB.y>>16)*2^-22
    // (VD.w) = 3.0 + (VB.y)*2^-22
    const VReg16B dest_b(i.dest.reg().getIdx());
    const VReg4S dest_s(i.dest.reg().getIdx());
    const QReg temp(1);
    const VReg16B temp_b(1);
    const VReg4S temp_s(1);
    if (i.src1.is_constant && i.src1.value->IsConstantZero()) {
      e.LoadVConst(i.dest, V3333);
      return;
    }
    const QReg src = GetVWithConst(e, i.src1, QReg(0));
    // Shuffle bytes.
    e.LoadVConst(temp, VUnpackSHORT_4);
    e.tbl(dest_b, VReg16BList(VReg16B(src.getIdx())), temp_b);
    // If negative, make smaller than 3 - sign extend before adding.
    e.shl(dest_s, dest_s, 16);
    e.sshr(dest_s, dest_s, 16);
    // Add 3,3,3,3.
    e.LoadVConst(temp, V3333);
    e.add(dest_s, dest_s, temp_s);
    // Return quiet NaNs in case of negative overflow.
    EmitOverflowQNaN(e, i.dest, VUnpackSHORT_Overflow);
  }
  static void EmitUINT_2101010(A64Emitter& e, const EmitArgType& i) {
    const VReg16B dest_b(i.dest.reg().getIdx());
    const VReg4S dest_s(i.dest.reg().getIdx());
    const QReg temp(1);
    const VReg16B temp_b(1);
    const VReg4S temp_s(1);
    const VReg4S v0_s(0);
    if (i.src1.is_constant && i.src1.value->IsConstantZero()) {
      e.LoadVConst(i.dest, V3331);
      return;
    }
    const QReg src = GetVWithConst(e, i.src1, QReg(0));
    // Splat W.
    e.dup(dest_s, VReg4S(src.getIdx())[3]);
    // Keep only the needed components.
    // Red in 0-9 now, green in 10-19, blue in 20-29, alpha in 30-31.
    e.LoadVConst(temp, VPackUINT_2101010_MaskPacked);
    e.and_(dest_b, dest_b, temp_b);
    // Shift the components down (negated shifts = logical right).
    e.LoadVConst(temp, VPackUINT_2101010_Shift);
    e.neg(v0_s, temp_s);
    e.ushl(dest_s, dest_s, v0_s);
    // If XYZ are negative, make smaller than 3 - sign extend XYZ before
    // adding. W is unsigned.
    e.shl(dest_s, dest_s, 22);
    e.sshr(dest_s, dest_s, 22);
    // Add 3,3,3,1.
    e.LoadVConst(temp, V3331);
    e.add(dest_s, dest_s, temp_s);
    // Return quiet NaNs in case of negative overflow.
    EmitOverflowQNaN(e, i.dest, VUnpackUINT_2101010_Overflow);
  }
  static void EmitULONG_4202020(A64Emitter& e, const EmitArgType& i) {
    const VReg16B dest_b(i.dest.reg().getIdx());
    const VReg4S dest_s(i.dest.reg().getIdx());
    const QReg temp(1);
    const VReg16B temp_b(1);
    const VReg4S temp_s(1);
    const VReg4S v0_s(0);
    const VReg16B v0_b(0);
    if (i.src1.is_constant && i.src1.value->IsConstantZero()) {
      e.LoadVConst(i.dest, V3331);
      return;
    }
    const QReg src = GetVWithConst(e, i.src1, QReg(0));
    // Extract pairs of nibbles to XZYW. XZ will have excess 4 upper bits, YW
    // will have excess 4 lower bits.
    e.LoadVConst(temp, VUnpackULONG_4202020_Permute);
    e.tbl(dest_b, VReg16BList(VReg16B(src.getIdx())), temp_b);
    // Drop the excess nibble of YW.
    e.ushr(v0_s, dest_s, 4);
    // Merge XZ and YW now both starting at offset 0: low half (XZ) from
    // dest, high half (YW) from the shifted copy.
    e.ins(VReg2D(i.dest.reg().getIdx())[1], VReg2D(0)[1]);
    // Reorder as XYZW: [d0, d2, d1, d3].
    e.orr(v0_b, dest_b, dest_b);
    e.ins(VReg4S(i.dest.reg().getIdx())[1], VReg4S(0)[2]);
    e.ins(VReg4S(i.dest.reg().getIdx())[2], VReg4S(0)[1]);
    // Drop the excess upper nibble in XZ and sign-extend XYZ.
    e.shl(dest_s, dest_s, 12);
    e.sshr(dest_s, dest_s, 12);
    // Add 3,3,3,1.
    e.LoadVConst(temp, V3331);
    e.add(dest_s, dest_s, temp_s);
    // Return quiet NaNs in case of negative overflow.
    EmitOverflowQNaN(e, i.dest, VUnpackULONG_4202020_Overflow);
  }
  static void Emit8_IN_16(A64Emitter& e, const EmitArgType& i,
                          uint32_t flags) {
    assert_false(IsPackOutSaturate(flags));
    const QReg src = GetVWithConst(e, i.src1, QReg(0));
    const VReg16B v2_b(2);
    const VReg8H v2_h(2);
    const VReg8H dest_h(i.dest.reg().getIdx());
    if (IsPackInUnsigned(flags) || IsPackOutUnsigned(flags)) {
      // Only signed -> signed is emitted by the PPC frontend.
      assert_always();
      return;
    }
    // Swap 16-bit halves within words, duplicate each byte of the selected
    // half into a 16-bit lane, then arithmetic-shift to sign-extend
    // (mirrors x64 vpshufb ByteOrderMask + vpunpck{h,l}bw + vpsraw).
    e.rev32(v2_h, VReg8H(src.getIdx()));
    if (IsPackToLo(flags)) {
      e.zip2(v2_b, v2_b, v2_b);
    } else {
      e.zip1(v2_b, v2_b, v2_b);
    }
    e.sshr(dest_h, v2_h, 8);
  }
  static void Emit16_IN_32(A64Emitter& e, const EmitArgType& i,
                           uint32_t flags) {
    assert_false(IsPackOutSaturate(flags));
    const QReg src = GetVWithConst(e, i.src1, QReg(0));
    const VReg8H src_h(src.getIdx());
    const VReg8H v2_h(2);
    const VReg4S v2_s(2);
    const VReg4S dest_s(i.dest.reg().getIdx());
    if (IsPackInUnsigned(flags) || IsPackOutUnsigned(flags)) {
      // Only signed -> signed is emitted by the PPC frontend.
      assert_always();
      return;
    }
    // Duplicate each 16-bit lane of the selected half into a 32-bit lane,
    // sign-extend, then swap the two words within each doubleword
    // (mirrors x64 vpunpck{h,l}wd + vpsrad + vpshufd 0xB1).
    if (IsPackToLo(flags)) {
      e.zip2(v2_h, src_h, src_h);
    } else {
      e.zip1(v2_h, src_h, src_h);
    }
    e.sshr(dest_s, v2_s, 16);
    e.rev64(dest_s, dest_s);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_UNPACK, UNPACK);

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

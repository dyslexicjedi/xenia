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

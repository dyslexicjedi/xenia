/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Developers. All rights reserved.                      *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_sequences.h"

#include "xenia/base/memory.h"
#include "xenia/cpu/backend/a64/a64_op.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

volatile int anchor_memory = 0;

// Computes the host offset of a guest address into w0 (zero-extended in x0)
// and returns an [membase, x0] operand. When the host allocation granularity
// exceeds 4KB (16KB pages on Apple Silicon) the 4KB physical-address bias in
// the 0xE0000000+ range can't be produced by memory mapping, so it is applied
// here, exactly mirroring x64's ComputeMemoryAddress.
template <typename T>
AdrReg ComputeMemoryAddressOffset(A64Emitter& e, const T& guest,
                                  int64_t offset_const) {
  if (guest.is_constant) {
    uint32_t address = static_cast<uint32_t>(guest.constant());
    address += static_cast<int32_t>(offset_const);
    if (address >= 0xE0000000 &&
        xe::memory::allocation_granularity() > 0x1000) {
      e.MovConst(WReg(0), address + 0x1000);
    } else {
      e.MovConst(WReg(0), address);
    }
  } else {
    // Work on the low 32 bits; the top 32 are likely garbage.
    const WReg guest_w(guest.reg().getIdx());
    if (xe::memory::allocation_granularity() > 0x1000) {
      // w0 = guest + ((guest >= 0xE0000000 - offset) << 12)
      e.MovConst(WReg(0),
                 static_cast<uint32_t>(0xE0000000 - offset_const));
      e.cmp(guest_w, WReg(0));
      e.cset(WReg(0), Cond::HS);
      e.add(WReg(0), guest_w, WReg(0), LSL, 12);
    } else {
      e.mov(WReg(0), guest_w);
    }
    if (offset_const) {
      e.MovConst(WReg(1), static_cast<uint32_t>(offset_const));
      e.add(WReg(0), WReg(0), WReg(1));
    }
  }
  return ptr(e.GetMembaseReg(), XReg(0));
}

template <typename T>
AdrReg ComputeMemoryAddress(A64Emitter& e, const T& guest) {
  return ComputeMemoryAddressOffset(e, guest, 0);
}

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

// ============================================================================
// OPCODE_LOAD_MMIO / OPCODE_STORE_MMIO
// ============================================================================
struct LOAD_MMIO_I32
    : Sequence<LOAD_MMIO_I32, I<OPCODE_LOAD_MMIO, I32Op, OffsetOp, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto* mmio_range = reinterpret_cast<MMIORange*>(i.src1.value);
    e.MovConst(e.GetNativeParam(0),
               reinterpret_cast<uint64_t>(mmio_range->callback_context));
    e.MovConst(e.GetNativeParam(1), uint32_t(i.src2.value));
    e.CallNativeSafe(reinterpret_cast<void*>(mmio_range->read));
    e.rev(WReg(0), WReg(0));
    e.mov(i.dest, WReg(0));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_MMIO, LOAD_MMIO_I32);

struct STORE_MMIO_I32
    : Sequence<STORE_MMIO_I32,
               I<OPCODE_STORE_MMIO, VoidOp, OffsetOp, OffsetOp, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto* mmio_range = reinterpret_cast<MMIORange*>(i.src1.value);
    e.MovConst(e.GetNativeParam(0),
               reinterpret_cast<uint64_t>(mmio_range->callback_context));
    e.MovConst(e.GetNativeParam(1), uint32_t(i.src2.value));
    if (i.src3.is_constant) {
      e.MovConst(WReg(e.GetNativeParam(2).getIdx()),
                 xe::byte_swap(uint32_t(i.src3.constant())));
    } else {
      e.rev(WReg(e.GetNativeParam(2).getIdx()), i.src3.reg());
    }
    e.CallNativeSafe(reinterpret_cast<void*>(mmio_range->write));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE_MMIO, STORE_MMIO_I32);

// ============================================================================
// OPCODE_LOAD
// ============================================================================
struct LOAD_I8 : Sequence<LOAD_I8, I<OPCODE_LOAD, I8Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.ldrb(i.dest, addr);
  }
};
struct LOAD_I16 : Sequence<LOAD_I16, I<OPCODE_LOAD, I16Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.ldrh(i.dest, addr);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      e.rev16(i.dest, i.dest);
    }
  }
};
struct LOAD_I32 : Sequence<LOAD_I32, I<OPCODE_LOAD, I32Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.ldr(i.dest, addr);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      e.rev(i.dest, i.dest);
    }
  }
};
struct LOAD_I64 : Sequence<LOAD_I64, I<OPCODE_LOAD, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.ldr(i.dest, addr);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      e.rev(i.dest, i.dest);
    }
  }
};
struct LOAD_F32 : Sequence<LOAD_F32, I<OPCODE_LOAD, F32Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.ldr(i.dest, addr);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_always("not implemented yet");
    }
  }
};
struct LOAD_F64 : Sequence<LOAD_F64, I<OPCODE_LOAD, F64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.ldr(i.dest, addr);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_always("not implemented yet");
    }
  }
};
struct LOAD_V128 : Sequence<LOAD_V128, I<OPCODE_LOAD, V128Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.ldr(i.dest, addr);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      // Swap bytes within each 32-bit element (x64's XMMByteSwapMask).
      const VReg16B dest_b(i.dest.reg().getIdx());
      e.rev32(dest_b, dest_b);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD, LOAD_I8, LOAD_I16, LOAD_I32, LOAD_I64,
                     LOAD_F32, LOAD_F64, LOAD_V128);

// ============================================================================
// OPCODE_STORE
// ============================================================================
// The address in w0/x0 must stay live across the value materialization, so
// values (and swaps) use w1/x1 and v0.
struct STORE_I8 : Sequence<STORE_I8, I<OPCODE_STORE, VoidOp, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    auto src2 = GetWithConst(e, i.src2, WReg(1));
    e.strb(src2, addr);
  }
};
struct STORE_I16 : Sequence<STORE_I16, I<OPCODE_STORE, VoidOp, I64Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      if (i.src2.is_constant) {
        e.MovConst(WReg(1), xe::byte_swap(i.src2.constant()));
      } else {
        e.rev16(WReg(1), i.src2.reg());
      }
      e.strh(WReg(1), addr);
    } else {
      auto src2 = GetWithConst(e, i.src2, WReg(1));
      e.strh(src2, addr);
    }
  }
};
struct STORE_I32 : Sequence<STORE_I32, I<OPCODE_STORE, VoidOp, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      if (i.src2.is_constant) {
        e.MovConst(WReg(1), xe::byte_swap(i.src2.constant()));
      } else {
        e.rev(WReg(1), i.src2.reg());
      }
      e.str(WReg(1), addr);
    } else {
      auto src2 = GetWithConst(e, i.src2, WReg(1));
      e.str(src2, addr);
    }
  }
};
struct STORE_I64 : Sequence<STORE_I64, I<OPCODE_STORE, VoidOp, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      if (i.src2.is_constant) {
        e.MovConst(XReg(1), xe::byte_swap(i.src2.constant()));
      } else {
        e.rev(XReg(1), i.src2.reg());
      }
      e.str(XReg(1), addr);
    } else {
      auto src2 = GetWithConst(e, i.src2, XReg(1));
      e.str(src2, addr);
    }
  }
};
struct STORE_F32 : Sequence<STORE_F32, I<OPCODE_STORE, VoidOp, I64Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(!(i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP));
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.MovConst(WReg(1), i.src2.value->constant.i32);
      e.str(WReg(1), addr);
    } else {
      e.str(i.src2.reg(), addr);
    }
  }
};
struct STORE_F64 : Sequence<STORE_F64, I<OPCODE_STORE, VoidOp, I64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(!(i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP));
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.MovConst(XReg(1), i.src2.value->constant.i64);
      e.str(XReg(1), addr);
    } else {
      e.str(i.src2.reg(), addr);
    }
  }
};
struct STORE_V128
    : Sequence<STORE_V128, I<OPCODE_STORE, VoidOp, I64Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_true(!i.src2.is_constant);
      const VReg16B src_b(i.src2.reg().getIdx());
      e.rev32(VReg16B(0), src_b);
      e.str(QReg(0), addr);
    } else {
      if (i.src2.is_constant) {
        e.LoadConstantV(QReg(0), i.src2.constant());
        e.str(QReg(0), addr);
      } else {
        e.str(i.src2.reg(), addr);
      }
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE, STORE_I8, STORE_I16, STORE_I32, STORE_I64,
                     STORE_F32, STORE_F64, STORE_V128);

// ============================================================================
// OPCODE_LOAD_OFFSET
// ============================================================================
struct LOAD_OFFSET_I8
    : Sequence<LOAD_OFFSET_I8, I<OPCODE_LOAD_OFFSET, I8Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    auto addr = ComputeMemoryAddressOffset(e, i.src1, i.src2.constant());
    e.ldrb(i.dest, addr);
  }
};
struct LOAD_OFFSET_I16
    : Sequence<LOAD_OFFSET_I16, I<OPCODE_LOAD_OFFSET, I16Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    auto addr = ComputeMemoryAddressOffset(e, i.src1, i.src2.constant());
    e.ldrh(i.dest, addr);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      e.rev16(i.dest, i.dest);
    }
  }
};
struct LOAD_OFFSET_I32
    : Sequence<LOAD_OFFSET_I32, I<OPCODE_LOAD_OFFSET, I32Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    auto addr = ComputeMemoryAddressOffset(e, i.src1, i.src2.constant());
    e.ldr(i.dest, addr);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      e.rev(i.dest, i.dest);
    }
  }
};
struct LOAD_OFFSET_I64
    : Sequence<LOAD_OFFSET_I64, I<OPCODE_LOAD_OFFSET, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    auto addr = ComputeMemoryAddressOffset(e, i.src1, i.src2.constant());
    e.ldr(i.dest, addr);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      e.rev(i.dest, i.dest);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_OFFSET, LOAD_OFFSET_I8, LOAD_OFFSET_I16,
                     LOAD_OFFSET_I32, LOAD_OFFSET_I64);

// ============================================================================
// OPCODE_STORE_OFFSET
// ============================================================================
struct STORE_OFFSET_I8
    : Sequence<STORE_OFFSET_I8,
               I<OPCODE_STORE_OFFSET, VoidOp, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    auto addr = ComputeMemoryAddressOffset(e, i.src1, i.src2.constant());
    auto src3 = GetWithConst(e, i.src3, WReg(1));
    e.strb(src3, addr);
  }
};
struct STORE_OFFSET_I16
    : Sequence<STORE_OFFSET_I16,
               I<OPCODE_STORE_OFFSET, VoidOp, I64Op, I64Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    auto addr = ComputeMemoryAddressOffset(e, i.src1, i.src2.constant());
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      if (i.src3.is_constant) {
        e.MovConst(WReg(1), xe::byte_swap(i.src3.constant()));
      } else {
        e.rev16(WReg(1), i.src3.reg());
      }
      e.strh(WReg(1), addr);
    } else {
      auto src3 = GetWithConst(e, i.src3, WReg(1));
      e.strh(src3, addr);
    }
  }
};
struct STORE_OFFSET_I32
    : Sequence<STORE_OFFSET_I32,
               I<OPCODE_STORE_OFFSET, VoidOp, I64Op, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    auto addr = ComputeMemoryAddressOffset(e, i.src1, i.src2.constant());
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      if (i.src3.is_constant) {
        e.MovConst(WReg(1), xe::byte_swap(i.src3.constant()));
      } else {
        e.rev(WReg(1), i.src3.reg());
      }
      e.str(WReg(1), addr);
    } else {
      auto src3 = GetWithConst(e, i.src3, WReg(1));
      e.str(src3, addr);
    }
  }
};
struct STORE_OFFSET_I64
    : Sequence<STORE_OFFSET_I64,
               I<OPCODE_STORE_OFFSET, VoidOp, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    auto addr = ComputeMemoryAddressOffset(e, i.src1, i.src2.constant());
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      if (i.src3.is_constant) {
        e.MovConst(XReg(1), xe::byte_swap(i.src3.constant()));
      } else {
        e.rev(XReg(1), i.src3.reg());
      }
      e.str(XReg(1), addr);
    } else {
      auto src3 = GetWithConst(e, i.src3, XReg(1));
      e.str(src3, addr);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE_OFFSET, STORE_OFFSET_I8, STORE_OFFSET_I16,
                     STORE_OFFSET_I32, STORE_OFFSET_I64);

// ============================================================================
// OPCODE_ATOMIC_EXCHANGE
// ============================================================================
// dest = old value at [src1]; [src1] = src2. Note that src1 is a HOST
// address (same oddity as x64; produced by LoadMembase arithmetic in the HIR).
// LSE swpal gives acquire+release ordering, matching x64's lock xchg.
struct ATOMIC_EXCHANGE_I8
    : Sequence<ATOMIC_EXCHANGE_I8,
               I<OPCODE_ATOMIC_EXCHANGE, I8Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Copy the address so dest may freely alias src1.
    e.mov(XReg(0), i.src1.reg());
    auto src2 = GetWithConst(e, i.src2, WReg(1));
    e.swpalb(src2, i.dest, ptr(XReg(0)));
  }
};
struct ATOMIC_EXCHANGE_I16
    : Sequence<ATOMIC_EXCHANGE_I16,
               I<OPCODE_ATOMIC_EXCHANGE, I16Op, I64Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.mov(XReg(0), i.src1.reg());
    auto src2 = GetWithConst(e, i.src2, WReg(1));
    e.swpalh(src2, i.dest, ptr(XReg(0)));
  }
};
struct ATOMIC_EXCHANGE_I32
    : Sequence<ATOMIC_EXCHANGE_I32,
               I<OPCODE_ATOMIC_EXCHANGE, I32Op, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.mov(XReg(0), i.src1.reg());
    auto src2 = GetWithConst(e, i.src2, WReg(1));
    e.swpal(src2, i.dest, ptr(XReg(0)));
  }
};
struct ATOMIC_EXCHANGE_I64
    : Sequence<ATOMIC_EXCHANGE_I64,
               I<OPCODE_ATOMIC_EXCHANGE, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.mov(XReg(0), i.src1.reg());
    auto src2 = GetWithConst(e, i.src2, XReg(1));
    e.swpal(src2, i.dest, ptr(XReg(0)));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ATOMIC_EXCHANGE, ATOMIC_EXCHANGE_I8,
                     ATOMIC_EXCHANGE_I16, ATOMIC_EXCHANGE_I32,
                     ATOMIC_EXCHANGE_I64);

// ============================================================================
// OPCODE_ATOMIC_COMPARE_EXCHANGE
// ============================================================================
// dest (i8) = ([guest src1] == src2); if equal, [guest src1] = src3.
// casal wants the absolute host address, so membase is added explicitly.
template <typename REG, typename ARGS>
void EmitAtomicCompareExchange(A64Emitter& e, const ARGS& i) {
  // x0 = absolute host address (with the 16KB-page 0xE0000000 fixup).
  const WReg guest_w(i.src1.reg().getIdx());
  if (xe::memory::allocation_granularity() > 0x1000) {
    e.MovConst(WReg(0), 0xE0000000u);
    e.cmp(guest_w, WReg(0));
    e.cset(WReg(0), Cond::HS);
    e.add(WReg(0), guest_w, WReg(0), LSL, 12);
  } else {
    e.mov(WReg(0), guest_w);
  }
  e.add(XReg(0), e.GetMembaseReg(), XReg(0));

  // w16 keeps the comparand for the result compare; w1 receives the old
  // value from casal.
  if (i.src2.is_constant) {
    e.MovConst(REG(16), i.src2.constant());
  } else {
    e.mov(REG(16), i.src2.reg());
  }
  e.mov(REG(1), REG(16));
  if (i.src3.is_constant) {
    e.MovConst(REG(17), i.src3.constant());
    e.casal(REG(1), REG(17), ptr(XReg(0)));
  } else {
    e.casal(REG(1), i.src3.reg(), ptr(XReg(0)));
  }
  e.cmp(REG(1), REG(16));
  e.cset(i.dest, Cond::EQ);
}
struct ATOMIC_COMPARE_EXCHANGE_I32
    : Sequence<ATOMIC_COMPARE_EXCHANGE_I32,
               I<OPCODE_ATOMIC_COMPARE_EXCHANGE, I8Op, I64Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAtomicCompareExchange<WReg>(e, i);
  }
};
struct ATOMIC_COMPARE_EXCHANGE_I64
    : Sequence<ATOMIC_COMPARE_EXCHANGE_I64,
               I<OPCODE_ATOMIC_COMPARE_EXCHANGE, I8Op, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAtomicCompareExchange<XReg>(e, i);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ATOMIC_COMPARE_EXCHANGE,
                     ATOMIC_COMPARE_EXCHANGE_I32, ATOMIC_COMPARE_EXCHANGE_I64);

// ============================================================================
// OPCODE_CACHE_CONTROL
// ============================================================================
struct CACHE_CONTROL
    : Sequence<CACHE_CONTROL,
               I<OPCODE_CACHE_CONTROL, VoidOp, I64Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    ComputeMemoryAddress(e, i.src1);
    e.add(XReg(0), e.GetMembaseReg(), XReg(0));

    const auto type = CacheControlType(i.instr->flags);
    auto emit_line = [&e, type]() {
      switch (type) {
        case CacheControlType::CACHE_CONTROL_TYPE_DATA_TOUCH:
          e.prfm(PLDL1KEEP, ptr(XReg(0)));
          break;
        case CacheControlType::CACHE_CONTROL_TYPE_DATA_TOUCH_FOR_STORE:
          e.prfm(PSTL1KEEP, ptr(XReg(0)));
          break;
        case CacheControlType::CACHE_CONTROL_TYPE_DATA_STORE:
          e.sys(3, 7, 10, 1, XReg(0));  // DC CVAC, x0.
          break;
        case CacheControlType::CACHE_CONTROL_TYPE_DATA_STORE_AND_FLUSH:
          e.sys(3, 7, 14, 1, XReg(0));  // DC CIVAC, x0.
          break;
        default:
          assert_unhandled_case(type);
          break;
      }
    };

    emit_line();
    const size_t cache_line_size = i.src2.value;
    if (cache_line_size >= 128) {
      // Also touch the other 64-byte host line on implementations whose
      // cache line is smaller than the Xenon 128-byte line.
      e.eor(XReg(0), XReg(0), 64);
      emit_line();
      assert_true(cache_line_size == 128);
    }
    if (type == CacheControlType::CACHE_CONTROL_TYPE_DATA_STORE ||
        type == CacheControlType::CACHE_CONTROL_TYPE_DATA_STORE_AND_FLUSH) {
      e.dsb(ISH);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CACHE_CONTROL, CACHE_CONTROL);

// ============================================================================
// OPCODE_MEMORY_BARRIER
// ============================================================================
struct MEMORY_BARRIER
    : Sequence<MEMORY_BARRIER, I<OPCODE_MEMORY_BARRIER, VoidOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) { e.dmb(ISH); }
};
EMITTER_OPCODE_TABLE(OPCODE_MEMORY_BARRIER, MEMORY_BARRIER);

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

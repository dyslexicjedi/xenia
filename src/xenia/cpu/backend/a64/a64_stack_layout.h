/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_A64_A64_STACK_LAYOUT_H_
#define XENIA_CPU_BACKEND_A64_A64_STACK_LAYOUT_H_

#include "xenia/base/vec128.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

class StackLayout {
 public:
  /**
   * Stack Layout
   * ----------------------------
   * NOTE: sp must always be 16b aligned on arm64.
   *
   * Both thunks and guest functions push a standard fp/lr frame record with
   * `stp x29, x30, [sp, #-16]!` before allocating their frame, so the frame
   * layouts below sit *under* that record and stack walkers see a normal
   * chain.
   *
   * Thunk frame (see A64ThunkEmitter):
   *   host-to-guest saves the callee-saved registers it repurposes
   *   (x19-x28, v8-v15); guest-to-host and resolve save the caller-saved
   *   registers guest code keeps live values in (v16-v31 = the HIR vector
   *   pool, plus x1 for the guest return address convention).
   */
  XEPACKEDSTRUCT(Thunk, {
    uint64_t arg_temp[4];  // 4 (not 3) so v[] lands 16-aligned for stp q.
    uint64_t r[12];
    vec128_t v[16];
  });
  static_assert(sizeof(Thunk) % 16 == 0,
                "sizeof(Thunk) must be a multiple of 16!");
  static const size_t THUNK_STACK_SIZE = sizeof(Thunk);

  /**
   * Guest stack:
   *  +------------------+
   *  | arg temp, 3 * 8  | sp + 0
   *  |                  |
   *  |                  |
   *  +------------------+
   *  | scratch, 48b     | sp + 32
   *  |                  |
   *  +------------------+
   *  | x27 / context    | sp + 80
   *  +------------------+
   *  | guest ret addr   | sp + 88
   *  +------------------+
   *  | call ret addr    | sp + 96
   *  +------------------+
   *  | (padding)        | sp + 104
   *  +------------------+
   *    ... locals ...
   *  +------------------+
   *  | saved x29, x30   | (pushed above the frame)
   *  +------------------+
   */
  static const size_t GUEST_STACK_SIZE = 112;
  static const size_t GUEST_CTX_HOME = 80;
  static const size_t GUEST_RET_ADDR = 88;
  static const size_t GUEST_CALL_RET_ADDR = 96;
};

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_STACK_LAYOUT_H_

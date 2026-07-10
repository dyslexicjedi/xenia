/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Developers. All rights reserved.                      *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/testing/util.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

using namespace xe::cpu::hir;
using namespace xe::cpu;
using namespace xe::cpu::testing;
using xe::cpu::ppc::PPCContext;

namespace {

double DoubleFromBits(uint64_t bits) {
  double value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

uint64_t DoubleToBits(double value) {
  uint64_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void CheckRoundF64(RoundMode mode, double input, double expected) {
  TestFunction test([mode](HIRBuilder& b) {
    StoreFPR(b, 3, b.Round(LoadFPR(b, 4), mode));
    b.Return();
  });
  test.Run([input](PPCContext* ctx) { ctx->f[4] = input; },
           [expected](PPCContext* ctx) { REQUIRE(ctx->f[3] == expected); });
}

}  // namespace

TEST_CASE("FPU_ROUND_F64", "[instr][fpu]") {
  CheckRoundF64(ROUND_TO_ZERO, -1.75, -1.0);
  CheckRoundF64(ROUND_TO_NEAREST, 2.5, 2.0);
  CheckRoundF64(ROUND_TO_MINUS_INFINITY, -1.25, -2.0);
  CheckRoundF64(ROUND_TO_POSITIVE_INFINITY, 1.25, 2.0);
}

TEST_CASE("FPU_ROUND_F32", "[instr][fpu]") {
  TestFunction test([](HIRBuilder& b) {
    auto value = b.Convert(LoadFPR(b, 4), FLOAT32_TYPE);
    value = b.Round(value, ROUND_TO_NEAREST);
    StoreFPR(b, 3, b.Convert(value, FLOAT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->f[4] = 2.5; },
           [](PPCContext* ctx) { REQUIRE(ctx->f[3] == 2.0); });
}

TEST_CASE("FPU_DYNAMIC_ROUNDING_AND_FZ", "[instr][fpu]") {
  TestFunction test([](HIRBuilder& b) {
    auto mode = b.Truncate(LoadGPR(b, 4), INT32_TYPE);
    b.SetRoundingMode(mode);
    StoreFPR(b, 3, b.Round(LoadFPR(b, 4), ROUND_DYNAMIC));
    auto converted = b.Convert(LoadFPR(b, 4), INT32_TYPE, ROUND_DYNAMIC);
    StoreGPR(b, 3, b.SignExtend(converted, INT64_TYPE));
    StoreFPR(b, 6, b.Add(LoadFPR(b, 5), LoadFPR(b, 7)));
    // Don't leak the guest mode into the host test runner.
    b.SetRoundingMode(b.LoadConstantInt32(0));
    b.Return();
  });

  struct RoundCase {
    uint32_t mode;
    double input;
    double expected;
  };
  const RoundCase cases[] = {
      {0, 1.5, 2.0},  {1, 1.5, 1.0},   {2, 1.5, 2.0},
      {3, 1.5, 1.0},  {1, -1.5, -1.0}, {2, -1.5, -1.0},
      {3, -1.5, -2.0},
  };
  for (const auto& test_case : cases) {
    test.Run(
        [&test_case](PPCContext* ctx) {
          ctx->r[4] = test_case.mode;
          ctx->f[4] = test_case.input;
          ctx->f[5] = 1.0;
          ctx->f[7] = 1.0;
        },
        [&test_case](PPCContext* ctx) {
          REQUIRE(ctx->f[3] == test_case.expected);
          REQUIRE(int64_t(ctx->r[3]) == int64_t(test_case.expected));
          REQUIRE(ctx->f[6] == 2.0);
        });
  }

  const double denormal = std::numeric_limits<double>::denorm_min();
  test.Run(
      [denormal](PPCContext* ctx) {
        ctx->r[4] = 0;
        ctx->f[4] = 0.0;
        ctx->f[5] = denormal;
        ctx->f[7] = 0.0;
      },
      [denormal](PPCContext* ctx) { REQUIRE(ctx->f[6] == denormal); });
  test.Run(
      [denormal](PPCContext* ctx) {
        ctx->r[4] = 4;
        ctx->f[4] = 0.0;
        ctx->f[5] = denormal;
        ctx->f[7] = 0.0;
      },
      [](PPCContext* ctx) { REQUIRE(ctx->f[6] == 0.0); });
}

TEST_CASE("FPU_MIN_MAX_F64", "[instr][fpu]") {
  TestFunction test([](HIRBuilder& b) {
    auto src1 = LoadFPR(b, 4);
    auto src2 = LoadFPR(b, 5);
    StoreFPR(b, 3, b.Min(src1, src2));
    StoreFPR(b, 6, b.Max(src1, src2));
    b.Return();
  });

  test.Run(
      [](PPCContext* ctx) {
        ctx->f[4] = 4.0;
        ctx->f[5] = -2.0;
      },
      [](PPCContext* ctx) {
        REQUIRE(ctx->f[3] == -2.0);
        REQUIRE(ctx->f[6] == 4.0);
      });

  test.Run(
      [](PPCContext* ctx) {
        ctx->f[4] = 0.0;
        ctx->f[5] = -0.0;
      },
      [](PPCContext* ctx) {
        REQUIRE(std::signbit(ctx->f[3]));
        REQUIRE(std::signbit(ctx->f[6]));
      });

  const double payload_nan = DoubleFromBits(0x7FF8000000001234ull);
  test.Run(
      [payload_nan](PPCContext* ctx) {
        ctx->f[4] = payload_nan;
        ctx->f[5] = 7.0;
      },
      [](PPCContext* ctx) {
        REQUIRE(ctx->f[3] == 7.0);
        REQUIRE(ctx->f[6] == 7.0);
      });
  test.Run(
      [payload_nan](PPCContext* ctx) {
        ctx->f[4] = 7.0;
        ctx->f[5] = payload_nan;
      },
      [](PPCContext* ctx) {
        REQUIRE(DoubleToBits(ctx->f[3]) == 0x7FF8000000001234ull);
        REQUIRE(DoubleToBits(ctx->f[6]) == 0x7FF8000000001234ull);
      });
}

TEST_CASE("FPU_MIN_MAX_F32", "[instr][fpu]") {
  TestFunction test([](HIRBuilder& b) {
    auto src1 = b.Convert(LoadFPR(b, 4), FLOAT32_TYPE);
    auto src2 = b.Convert(LoadFPR(b, 5), FLOAT32_TYPE);
    StoreFPR(b, 3, b.Convert(b.Min(src1, src2), FLOAT64_TYPE));
    StoreFPR(b, 6, b.Convert(b.Max(src1, src2), FLOAT64_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->f[4] = 12.5;
        ctx->f[5] = -3.25;
      },
      [](PPCContext* ctx) {
        REQUIRE(ctx->f[3] == -3.25);
        REQUIRE(ctx->f[6] == 12.5);
      });
}

TEST_CASE("FPU_RECIP_RSQRT", "[instr][fpu]") {
  {
    TestFunction test_f64([](HIRBuilder& b) {
      StoreFPR(b, 3, b.Recip(LoadFPR(b, 4)));
      StoreFPR(b, 6, b.RSqrt(LoadFPR(b, 5)));
      b.Return();
    });
    test_f64.Run(
        [](PPCContext* ctx) {
          ctx->f[4] = 4.0;
          ctx->f[5] = 16.0;
        },
        [](PPCContext* ctx) {
          REQUIRE(ctx->f[3] == 0.25);
          REQUIRE(ctx->f[6] == 0.25);
        });
  }

  {
    TestFunction test_f32([](HIRBuilder& b) {
      auto recip = b.Recip(b.Convert(LoadFPR(b, 4), FLOAT32_TYPE));
      auto rsqrt = b.RSqrt(b.Convert(LoadFPR(b, 5), FLOAT32_TYPE));
      StoreFPR(b, 3, b.Convert(recip, FLOAT64_TYPE));
      StoreFPR(b, 6, b.Convert(rsqrt, FLOAT64_TYPE));
      b.Return();
    });
    test_f32.Run(
        [](PPCContext* ctx) {
          ctx->f[4] = 8.0;
          ctx->f[5] = 64.0;
        },
        [](PPCContext* ctx) {
          REQUIRE(ctx->f[3] == 0.125);
          REQUIRE(ctx->f[6] == 0.125);
        });
  }
}

TEST_CASE("FPU_POW2_LOG2", "[instr][fpu]") {
  {
    TestFunction test_f64([](HIRBuilder& b) {
      StoreFPR(b, 3, b.Pow2(LoadFPR(b, 4)));
      StoreFPR(b, 6, b.Log2(LoadFPR(b, 5)));
      b.Return();
    });
    test_f64.Run(
        [](PPCContext* ctx) {
          ctx->f[4] = 5.0;
          ctx->f[5] = 8.0;
        },
        [](PPCContext* ctx) {
          REQUIRE(ctx->f[3] == 32.0);
          REQUIRE(ctx->f[6] == 3.0);
        });
  }

  {
    TestFunction test_f32([](HIRBuilder& b) {
      auto pow2 = b.Pow2(b.Convert(LoadFPR(b, 4), FLOAT32_TYPE));
      auto log2 = b.Log2(b.Convert(LoadFPR(b, 5), FLOAT32_TYPE));
      StoreFPR(b, 3, b.Convert(pow2, FLOAT64_TYPE));
      StoreFPR(b, 6, b.Convert(log2, FLOAT64_TYPE));
      b.Return();
    });
    test_f32.Run(
        [](PPCContext* ctx) {
          ctx->f[4] = 3.0;
          ctx->f[5] = 16.0;
        },
        [](PPCContext* ctx) {
          REQUIRE(ctx->f[3] == 8.0);
          REQUIRE(ctx->f[6] == 4.0);
        });
  }
}

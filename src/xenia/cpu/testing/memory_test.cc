/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Developers. All rights reserved.                      *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/testing/util.h"

using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::hir;
using namespace xe::cpu::testing;
using xe::cpu::ppc::PPCContext;

TEST_CASE("ATOMIC_EXCHANGE_AND_COMPARE_EXCHANGE", "[instr][memory]") {
  TestFunction test([](HIRBuilder& b) {
    auto host_address = LoadGPR(b, 4);
    auto old_value =
        b.AtomicExchange(host_address, b.LoadConstantInt32(0x11223344));
    StoreGPR(b, 3, old_value);
    auto guest_address = LoadGPR(b, 5);
    auto exchanged = b.AtomicCompareExchange(
        guest_address, b.LoadConstantInt32(0x11223344),
        b.LoadConstantInt32(0x55667788));
    b.StoreContext(offsetof(PPCContext, r) + 6 * 8, exchanged);
    b.Return();
  });

  uint32_t value = 0xAABBCCDD;
  const uint32_t guest_address = test.memory->SystemHeapAlloc(sizeof(uint32_t));
  REQUIRE(guest_address != 0);
  auto* guest_value =
      reinterpret_cast<uint32_t*>(test.memory->TranslateVirtual(guest_address));
  *guest_value = 0x11223344;
  test.Run(
      [&value, guest_address](PPCContext* ctx) {
        ctx->r[4] = reinterpret_cast<uintptr_t>(&value);
        ctx->r[5] = guest_address;
      },
      [&value, guest_value](PPCContext* ctx) {
        REQUIRE(uint32_t(ctx->r[3]) == 0xAABBCCDD);
        REQUIRE(uint8_t(ctx->r[6]) == 1);
        REQUIRE(value == 0x11223344);
        REQUIRE(*guest_value == 0x55667788);
      });
}

TEST_CASE("CACHE_CONTROL", "[instr][memory]") {
  TestFunction test([](HIRBuilder& b) {
    auto address = b.LoadConstantInt64(0x10000);
    b.CacheControl(address, 128,
                   CacheControlType::CACHE_CONTROL_TYPE_DATA_TOUCH);
    b.CacheControl(address, 128,
                   CacheControlType::CACHE_CONTROL_TYPE_DATA_TOUCH_FOR_STORE);
    b.CacheControl(address, 128,
                   CacheControlType::CACHE_CONTROL_TYPE_DATA_STORE);
    b.CacheControl(address, 128,
                   CacheControlType::CACHE_CONTROL_TYPE_DATA_STORE_AND_FLUSH);
    b.Return();
  });
  test.Run([](PPCContext*) {}, [](PPCContext*) { SUCCEED(); });
}

namespace {

struct MMIOTestState {
  uint32_t read_count = 0;
  uint32_t write_count = 0;
  uint32_t written_value = 0;
};

uint32_t MMIORead(void*, void* context, uint32_t address) {
  auto* state = reinterpret_cast<MMIOTestState*>(context);
  ++state->read_count;
  return 0x12345678;
}

void MMIOWrite(void*, void* context, uint32_t address, uint32_t value) {
  auto* state = reinterpret_cast<MMIOTestState*>(context);
  ++state->write_count;
  state->written_value = value;
}

}  // namespace

TEST_CASE("MMIO_EXCEPTION_LOAD_STORE", "[instr][memory][mmio]") {
  constexpr uint32_t kMMIOAddress = 0x7F000000;
  TestFunction test([](HIRBuilder& b) {
    auto address = b.LoadConstantInt64(kMMIOAddress);
    StoreGPR(b, 3, b.Load(address, INT32_TYPE));
    b.Store(address, b.LoadConstantInt32(0x89ABCDEF));
    b.Return();
  });

  MMIOTestState state;
  REQUIRE(test.memory->AddVirtualMappedRange(
      kMMIOAddress, 0xFFFFF000, xe::memory::page_size(), &state, MMIORead,
      MMIOWrite));
  test.Run([](PPCContext*) {}, [&state](PPCContext* ctx) {
    REQUIRE(uint32_t(ctx->r[3]) == 0x78563412);
    REQUIRE(state.read_count == 1);
    REQUIRE(state.write_count == 1);
    REQUIRE(state.written_value == 0xEFCDAB89);
  });
}

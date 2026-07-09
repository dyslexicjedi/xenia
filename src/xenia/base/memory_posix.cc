/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/memory.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstddef>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/platform.h"
#include "xenia/base/string.h"

#if XE_PLATFORM_ANDROID
#include <dlfcn.h>
#include <linux/ashmem.h>
#include <string.h>
#include <sys/ioctl.h>

#include "xenia/base/main_android.h"
#endif

#if XE_PLATFORM_MAC
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>

// Darwin off_t is always 64-bit; the transitional LFS64 interfaces don't
// exist.
#define ftruncate64 ftruncate
#define mmap64 mmap
#endif

namespace xe {
namespace memory {

#if XE_PLATFORM_ANDROID
// May be null if no dynamically loaded functions are required.
static void* libandroid_;
// API 26+.
static int (*android_ASharedMemory_create_)(const char* name, size_t size);

void AndroidInitialize() {
  if (xe::GetAndroidApiLevel() >= 26) {
    libandroid_ = dlopen("libandroid.so", RTLD_NOW);
    assert_not_null(libandroid_);
    if (libandroid_) {
      android_ASharedMemory_create_ =
          reinterpret_cast<decltype(android_ASharedMemory_create_)>(
              dlsym(libandroid_, "ASharedMemory_create"));
      assert_not_null(android_ASharedMemory_create_);
    }
  }
}

void AndroidShutdown() {
  android_ASharedMemory_create_ = nullptr;
  if (libandroid_) {
    dlclose(libandroid_);
    libandroid_ = nullptr;
  }
}
#endif

size_t page_size() { return getpagesize(); }
size_t allocation_granularity() { return page_size(); }

#if XE_PLATFORM_MAC
// Darwin's MAP_FIXED silently replaces existing mappings (including the
// executable image), and address hints aren't reliably honored even for free
// ranges - so failing on occupied ranges, as VirtualAlloc/MapViewOfFileEx do
// on Windows, requires an explicit occupancy query.
static bool IsRangeFree(void* base_address, size_t length) {
  mach_vm_address_t address = reinterpret_cast<mach_vm_address_t>(base_address);
  mach_vm_size_t size = 0;
  vm_region_basic_info_data_64_t info;
  mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
  mach_port_t object_name = MACH_PORT_NULL;
  kern_return_t result = mach_vm_region(
      mach_task_self(), &address, &size, VM_REGION_BASIC_INFO_64,
      reinterpret_cast<vm_region_info_t>(&info), &info_count, &object_name);
  if (object_name != MACH_PORT_NULL) {
    mach_port_deallocate(mach_task_self(), object_name);
  }
  if (result != KERN_SUCCESS) {
    // No mappings at or above the requested address.
    return true;
  }
  // mach_vm_region returns the first region at or above the address - the
  // range is free if that region starts beyond its end.
  return address >= reinterpret_cast<mach_vm_address_t>(base_address) + length;
}

static void AlignRangeToPage(void*& base_address, size_t& length) {
  const uintptr_t page_mask = page_size() - 1;
  const uintptr_t start = reinterpret_cast<uintptr_t>(base_address);
  const uintptr_t aligned_start = start & ~page_mask;
  const uintptr_t aligned_end = (start + length + page_mask) & ~page_mask;
  base_address = reinterpret_cast<void*>(aligned_start);
  length = aligned_end - aligned_start;
}
#endif  // XE_PLATFORM_MAC

uint32_t ToPosixProtectFlags(PageAccess access) {
  switch (access) {
    case PageAccess::kNoAccess:
      return PROT_NONE;
    case PageAccess::kReadOnly:
      return PROT_READ;
    case PageAccess::kReadWrite:
      return PROT_READ | PROT_WRITE;
    case PageAccess::kExecuteReadOnly:
      return PROT_READ | PROT_EXEC;
    case PageAccess::kExecuteReadWrite:
      return PROT_READ | PROT_WRITE | PROT_EXEC;
    default:
      assert_unhandled_case(access);
      return PROT_NONE;
  }
}

bool IsWritableExecutableMemorySupported() { return true; }

void SetJitThreadWriteAccess(bool write_access) {
#if XE_PLATFORM_MAC
  pthread_jit_write_protect_np(!write_access);
#endif
}

void FlushInstructionCache(void* base_address, size_t length) {
  __builtin___clear_cache(reinterpret_cast<char*>(base_address),
                          reinterpret_cast<char*>(base_address) + length);
}

void* AllocFixed(void* base_address, size_t length,
                 AllocationType allocation_type, PageAccess access) {
  // mmap does not support reserve / commit, so ignore allocation_type.
  uint32_t prot = ToPosixProtectFlags(access);
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
  if (base_address) {
    flags |= MAP_FIXED;
  }
#if XE_PLATFORM_MAC
  if (base_address && !IsRangeFree(base_address, length)) {
    // VirtualAlloc's commit operations apply to existing mappings. Guest
    // memory uses this to make portions of its already-mapped shared backing
    // readable and writable; replacing that mapping with MAP_FIXED would lose
    // the aliasing, while rejecting it leaves every guest heap inaccessible.
    if (allocation_type == AllocationType::kCommit) {
      void* protect_base = base_address;
      size_t protect_length = length;
      AlignRangeToPage(protect_base, protect_length);
      if (mprotect(protect_base, protect_length, prot) == 0) {
        return base_address;
      }
      XELOGE("mprotect({:#x}, {}) failed: {}",
             reinterpret_cast<uintptr_t>(base_address), length,
             strerror(errno));
      return nullptr;
    }
    // Reserving fixed allocations (with or without commit) must retain the
    // Windows failure-on-collision behavior rather than letting Darwin
    // MAP_FIXED clobber a live mapping.
    return nullptr;
  }
  if (access == PageAccess::kExecuteReadWrite ||
      access == PageAccess::kExecuteReadOnly) {
    // Executable anonymous memory must be allocated as a JIT region on Apple
    // Silicon. Writing to it additionally requires the calling thread to hold
    // write (rather than execute) access - see SetJitThreadWriteAccess.
    flags |= MAP_JIT;
  }
#endif
  void* result = mmap(base_address, length, prot, flags, -1, 0);
  if (result == MAP_FAILED) {
    XELOGE("mmap({:#x}, {}) failed: {}",
           reinterpret_cast<uintptr_t>(base_address), length, strerror(errno));
    return nullptr;
  } else {
    return result;
  }
}

bool DeallocFixed(void* base_address, size_t length,
                  DeallocationType deallocation_type) {
  return munmap(base_address, length) == 0;
}

bool Protect(void* base_address, size_t length, PageAccess access,
             PageAccess* out_old_access) {
  // Linux does not have a syscall to query memory permissions.
  assert_null(out_old_access);

  uint32_t prot = ToPosixProtectFlags(access);
#if XE_PLATFORM_MAC
  AlignRangeToPage(base_address, length);
#endif
  return mprotect(base_address, length, prot) == 0;
}

bool QueryProtect(void* base_address, size_t& length, PageAccess& access_out) {
  return false;
}

FileMappingHandle CreateFileMappingHandle(const std::filesystem::path& path,
                                          size_t length, PageAccess access,
                                          bool commit) {
#if XE_PLATFORM_ANDROID
  // TODO(Triang3l): Check if memfd can be used instead on API 30+.
  if (android_ASharedMemory_create_) {
    int sharedmem_fd = android_ASharedMemory_create_(path.c_str(), length);
    return sharedmem_fd >= 0 ? sharedmem_fd : kFileMappingHandleInvalid;
  }

  // Use /dev/ashmem on API versions below 26, which added ASharedMemory.
  // /dev/ashmem was disabled on API 29 for apps targeting it.
  // https://chromium.googlesource.com/chromium/src/+/master/third_party/ashmem/ashmem-dev.c
  int ashmem_fd = open("/" ASHMEM_NAME_DEF, O_RDWR);
  if (ashmem_fd < 0) {
    return kFileMappingHandleInvalid;
  }
  char ashmem_name[ASHMEM_NAME_LEN];
  strlcpy(ashmem_name, path.c_str(), xe::countof(ashmem_name));
  if (ioctl(ashmem_fd, ASHMEM_SET_NAME, ashmem_name) < 0 ||
      ioctl(ashmem_fd, ASHMEM_SET_SIZE, length) < 0) {
    close(ashmem_fd);
    return kFileMappingHandleInvalid;
  }
  return ashmem_fd;
#else
  int oflag;
  switch (access) {
    case PageAccess::kNoAccess:
      oflag = 0;
      break;
    case PageAccess::kReadOnly:
    case PageAccess::kExecuteReadOnly:
      oflag = O_RDONLY;
      break;
    case PageAccess::kReadWrite:
    case PageAccess::kExecuteReadWrite:
      oflag = O_RDWR;
      break;
    default:
      assert_always();
      return kFileMappingHandleInvalid;
  }
  oflag |= O_CREAT;
  auto full_path = "/" / path;
  int ret = shm_open(full_path.c_str(), oflag, 0777);
  if (ret < 0) {
    XELOGE("shm_open({}) failed: {}", full_path.string(), strerror(errno));
    return kFileMappingHandleInvalid;
  }
  if (ftruncate64(ret, length) != 0) {
    XELOGE("ftruncate({}, {}) failed: {}", full_path.string(), length,
           strerror(errno));
    close(ret);
    shm_unlink(full_path.c_str());
    return kFileMappingHandleInvalid;
  }
  return ret;
#endif
}

void CloseFileMappingHandle(FileMappingHandle handle,
                            const std::filesystem::path& path) {
  close(handle);
#if !XE_PLATFORM_ANDROID
  auto full_path = "/" / path;
  shm_unlink(full_path.c_str());
#endif
}

void* MapFileView(FileMappingHandle handle, void* base_address, size_t length,
                  PageAccess access, size_t file_offset) {
  uint32_t prot = ToPosixProtectFlags(access);
  // Like MapViewOfFileEx on Windows: a shared view of the file mapping placed
  // exactly at base_address (if requested), or nullptr on failure.
  int flags = MAP_SHARED;
  if (base_address) {
    flags |= MAP_FIXED;
#if XE_PLATFORM_MAC
    if (!IsRangeFree(base_address, length)) {
      return nullptr;
    }
#endif
  }
  void* result = mmap64(base_address, length, prot, flags, handle, file_offset);
  return result == MAP_FAILED ? nullptr : result;
}

bool UnmapFileView(FileMappingHandle handle, void* base_address,
                   size_t length) {
  return munmap(base_address, length) == 0;
}

}  // namespace memory
}  // namespace xe

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/trace_dump.h"

#include <algorithm>

#include "third_party/stb/stb_image_write.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/base/string.h"
#include "xenia/base/utf8.h"
#include "xenia/base/threading.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/memory.h"
#include "xenia/ui/file_picker.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/window.h"
#include "xenia/xbox.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#undef _CRT_SECURE_NO_WARNINGS
#undef _CRT_NONSTDC_NO_DEPRECATE
#include "third_party/stb/stb_image_write.h"

DEFINE_path(target_trace_file, "", "Specifies the trace file to load.", "GPU");
DEFINE_path(trace_dump_path, "", "Output path for dumped files.", "GPU");
DEFINE_int32(trace_dump_frame, 0,
             "Index of the frame to dump, or -1 for the last frame.", "GPU");
DEFINE_int32(trace_dump_frame_step, 0,
             "If non-zero, also dump a PNG every this many frames while "
             "replaying up to trace_dump_frame (named <output>_NNNNN.png).",
             "GPU");
DEFINE_int32(trace_dump_final_frame_draw_budget, -1,
             "Debug: value to set the draw_budget cvar to right before "
             "replaying the captured frame, to bisect which draw within that "
             "frame introduces an artifact; -1 = unlimited.",
             "GPU");
DEFINE_string(trace_dump_memory_ranges, "",
              "Debug: comma-separated hex physical ranges (start:length) to "
              "write to <output>_<start>.bin after playback, e.g. "
              "\"137A0000:500000,12D97000:398000\".",
              "GPU");
DEFINE_int32(trace_dump_final_frame_draw_skip, 0,
             "Debug: value to set the draw_skip cvar to right before "
             "replaying the captured frame.",
             "GPU");
DECLARE_int32(draw_budget);
DECLARE_int32(draw_skip);

namespace xe {
namespace gpu {

using namespace xe::gpu::xenos;

TraceDump::TraceDump() = default;

TraceDump::~TraceDump() = default;

int TraceDump::Main(const std::vector<std::string>& args) {
  // Grab path from the flag or unnamed argument.
  std::filesystem::path path;
  std::filesystem::path output_path;
  if (!cvars::target_trace_file.empty()) {
    // Passed as a named argument.
    // TODO(benvanik): find something better than gflags that supports
    // unicode.
    path = cvars::target_trace_file;
  } else if (args.size() >= 2) {
    // Passed as an unnamed argument.
    path = xe::to_path(args[1]);

    if (args.size() >= 3) {
      output_path = xe::to_path(args[2]);
    }
  }

  if (path.empty()) {
    XELOGE("No trace file specified");
    return 5;
  }

  // Normalize the path and make absolute.
  auto abs_path = std::filesystem::absolute(path);
  XELOGI("Loading trace file {}...", xe::path_to_utf8(abs_path));

  if (!Setup()) {
    XELOGE("Unable to setup trace dump tool");
    return 4;
  }
  if (!Load(std::move(abs_path))) {
    XELOGE("Unable to load trace file; not found?");
    return 5;
  }

  // Root file name for outputs.
  if (output_path.empty()) {
    base_output_path_ = cvars::trace_dump_path;
    auto output_name = path.filename().replace_extension();

    base_output_path_ = base_output_path_ / output_name;
  } else {
    base_output_path_ = output_path;
  }

  // Ensure output path exists.
  xe::filesystem::CreateParentFolder(base_output_path_);

  return Run();
}

bool TraceDump::Setup() {
  // Create the emulator but don't initialize so we can setup the window.
  emulator_ = std::make_unique<Emulator>("", "", "", "");
  X_STATUS result = emulator_->Setup(
      nullptr, nullptr, false, nullptr,
      [this]() { return CreateGraphicsSystem(); }, nullptr);
  if (XFAILED(result)) {
    XELOGE("Failed to setup emulator: {:08X}", result);
    return false;
  }
  graphics_system_ = emulator_->graphics_system();
  player_ = std::make_unique<TracePlayer>(graphics_system_);
  return true;
}

bool TraceDump::Load(const std::filesystem::path& trace_file_path) {
  trace_file_path_ = trace_file_path;

  if (!player_->Open(xe::path_to_utf8(trace_file_path_))) {
    XELOGE("Could not load trace file");
    return false;
  }

  XELOGI("Loaded trace with {} frame(s)", player_->frame_count());
  return true;
}

int TraceDump::Run() {
  int frame_index = cvars::trace_dump_frame;
  if (frame_index < 0) {
    frame_index += player_->frame_count();
  }
  frame_index =
      std::max(0, std::min(frame_index, player_->frame_count() - 1));

  // Stream traces contain a full GPU state snapshot only at their beginning.
  // Replay all preceding frames so EDRAM, shared memory, registers, and the
  // gamma ramp have the state expected by the requested frame. Capture only
  // the requested frame to keep host GPU captures focused and reasonably
  // sized.
  for (int replay_frame = 0; replay_frame < frame_index; ++replay_frame) {
    if (replay_frame) {
      player_->SeekFrame(replay_frame);
    } else {
      player_->SeekCommand(
          static_cast<int>(player_->current_frame()->commands.size() - 1));
    }
    player_->WaitOnPlayback();
    if (cvars::trace_dump_frame_step &&
        !(replay_frame % cvars::trace_dump_frame_step)) {
      SaveCurrentFrame(replay_frame);
    }
  }

  if (cvars::trace_dump_final_frame_draw_budget >= 0) {
    cvars::draw_budget = cvars::trace_dump_final_frame_draw_budget;
  }
  if (cvars::trace_dump_final_frame_draw_skip > 0) {
    cvars::draw_skip = cvars::trace_dump_final_frame_draw_skip;
  }

  BeginHostCapture();
  if (frame_index) {
    player_->SeekFrame(frame_index);
  } else {
    player_->SeekCommand(
        static_cast<int>(player_->current_frame()->commands.size() - 1));
  }
  player_->WaitOnPlayback();
  EndHostCapture();

  // Capture.
  int result = SaveCurrentFrame(-1) ? 0 : 1;

  if (!cvars::trace_dump_memory_ranges.empty()) {
    // Download the GPU-side contents of the shared memory (resolve
    // destinations and other GPU writes never reach the CPU-visible guest
    // memory otherwise).
    if (!graphics_system_->command_processor()
             ->DebugDownloadSharedMemoryToGuest()) {
      XELOGW(
          "trace_dump_memory_ranges: GPU shared memory download not "
          "performed - dumps will contain CPU-side data only");
    }
    std::vector<std::string_view> ranges =
        xe::utf8::split(cvars::trace_dump_memory_ranges, ",");
    for (const std::string_view range_view : ranges) {
      std::string range(range_view);
      uint32_t start, length;
      if (std::sscanf(range.c_str(), "%x:%x", &start, &length) != 2) {
        XELOGE("trace_dump_memory_ranges: can't parse '{}'", range);
        continue;
      }
      const uint8_t* data =
          emulator_->memory()->TranslatePhysical(start);
      std::filesystem::path bin_path = base_output_path_;
      bin_path += fmt::format("_{:08X}", start);
      bin_path.replace_extension(".bin");
      auto handle = filesystem::OpenFile(bin_path, "wb");
      if (handle) {
        fwrite(data, 1, length, handle);
        fclose(handle);
        XELOGI("trace_dump_memory_ranges: wrote {:08X}:{:08X}", start, length);
      }
    }
  }

  player_.reset();
  emulator_.reset();
  return result;
}

bool TraceDump::SaveCurrentFrame(int frame_suffix) {
  ui::Presenter* presenter = graphics_system_->presenter();
  ui::RawImage raw_image;
  if (!presenter || !presenter->CaptureGuestOutput(raw_image)) {
    return false;
  }
  std::filesystem::path png_path = base_output_path_;
  if (frame_suffix >= 0) {
    png_path += fmt::format("_{:05d}", frame_suffix);
  }
  png_path.replace_extension(".png");
  auto handle = filesystem::OpenFile(png_path, "wb");
  if (!handle) {
    return false;
  }
  auto callback = [](void* context, void* data, int size) {
    fwrite(data, 1, size, (FILE*)context);
  };
  stbi_write_png_to_func(callback, handle, static_cast<int>(raw_image.width),
                         static_cast<int>(raw_image.height), 4,
                         raw_image.data.data(),
                         static_cast<int>(raw_image.stride));
  fclose(handle);
  return true;
}

}  //  namespace gpu
}  //  namespace xe

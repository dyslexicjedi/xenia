/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/surface_mac.h"

#import <QuartzCore/CAMetalLayer.h>

namespace xe {
namespace ui {

bool MacMetalLayerSurface::GetSizeImpl(uint32_t& width_out,
                                       uint32_t& height_out) const {
  // The drawable size in physical pixels is maintained by the view that owns
  // the layer (updated on resizes and backing scale factor changes).
  CGSize drawable_size = layer_.drawableSize;
  width_out = uint32_t(drawable_size.width);
  height_out = uint32_t(drawable_size.height);
  return true;
}

}  // namespace ui
}  // namespace xe

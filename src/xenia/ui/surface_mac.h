/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_SURFACE_MAC_H_
#define XENIA_UI_SURFACE_MAC_H_

#include "xenia/ui/surface.h"

// Compatible with the forward declaration in vulkan_metal.h.
#ifdef __OBJC__
@class CAMetalLayer;
#else
typedef void CAMetalLayer;
#endif

namespace xe {
namespace ui {

// The layer is owned (retained) by the view of the window the surface was
// created for - the Surface only holds a reference for passing to
// vkCreateMetalSurfaceEXT and querying the drawable size, and is destroyed
// before the window is closed.
class MacMetalLayerSurface final : public Surface {
 public:
  explicit MacMetalLayerSurface(CAMetalLayer* layer) : layer_(layer) {}
  TypeIndex GetType() const override { return kTypeIndex_MacMetalLayer; }
  CAMetalLayer* layer() const { return layer_; }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override;

 private:
  CAMetalLayer* layer_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_SURFACE_MAC_H_

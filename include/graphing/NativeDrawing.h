#pragma once

#include "Scene.h"
#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#endif
struct _cairo;

namespace graphing
{
#ifdef __APPLE__
// CoreGraphics contexts use a bottom-left origin; the scene uses top-left pixels.
HRESULT DrawCoreGraphicsScene(const Scene &scene, CGContextRef context);
#endif
HRESULT DrawCairoScene(const Scene &scene, _cairo *context);
} // namespace graphing

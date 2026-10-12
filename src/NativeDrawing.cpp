#include "graphing/NativeDrawing.h"
#include "Text.h"
#include <algorithm>
#include <cmath>
#ifdef __APPLE__
#include <CoreText/CoreText.h>
#endif
#ifdef GRAPHING_NATIVE_CAIRO
#include <cairo.h>
#endif

namespace graphing
{
namespace
{
std::vector<double> GetDashPattern(Graphing::Renderer::LineStyle style, float width)
{
    using Graphing::Renderer::LineStyle;
    const double unit = std::max(1.f, width);
    switch (style)
    {
    case LineStyle::Dot:
        return {unit, 3 * unit};
    case LineStyle::Dash:
        return {6 * unit, 3 * unit};
    case LineStyle::DashDot:
        return {6 * unit, 3 * unit, unit, 3 * unit};
    case LineStyle::DashDotDot:
        return {6 * unit, 3 * unit, unit, 3 * unit, unit, 3 * unit};
    default:
        return {};
    }
}
bool HasValidSize(const Scene &scene)
{
    return scene.width && scene.height && scene.width <= 4096 && scene.height <= 4096 &&
           static_cast<std::size_t>(scene.width) * scene.height <= 4 * 1024 * 1024;
}
} // namespace

#ifdef __APPLE__
HRESULT DrawCoreGraphicsScene(const Scene &scene, CGContextRef context)
{
    if (!context)
        return E_POINTER;
    if (!HasValidSize(scene))
        return E_INVALIDARG;
    CGContextSaveGState(context);
    CGContextTranslateCTM(context, 0, scene.height);
    CGContextScaleCTM(context, 1, -1);
    CGContextClipToRect(context, CGRectMake(0, 0, scene.width, scene.height));
    const auto setFill = [context](Graphing::Color color) {
        CGContextSetRGBFillColor(context, color.R / 255., color.G / 255., color.B / 255., color.A / 255.);
    };
    setFill(scene.background);
    CGContextFillRect(context, CGRectMake(0, 0, scene.width, scene.height));
    for (const auto &fill : scene.fills)
    {
        if (fill.polygon.size() < 3)
            continue;
        setFill(fill.color);
        CGContextBeginPath(context);
        CGContextMoveToPoint(context, fill.polygon[0].x, fill.polygon[0].y);
        for (std::size_t index = 1; index < fill.polygon.size(); ++index)
            CGContextAddLineToPoint(context, fill.polygon[index].x, fill.polygon[index].y);
        CGContextClosePath(context);
        CGContextFillPath(context);
    }
    for (const auto &stroke : scene.strokes)
    {
        if (stroke.points.size() < 2)
            continue;
        const auto color = stroke.color;
        CGContextSetRGBStrokeColor(context, color.R / 255., color.G / 255., color.B / 255., color.A / 255.);
        CGContextSetLineWidth(context, stroke.width);
        const auto pattern = GetDashPattern(stroke.style, stroke.width);
        CGContextSetLineDash(context, 0, pattern.data(), pattern.size());
        CGContextBeginPath(context);
        CGContextMoveToPoint(context, stroke.points[0].x, stroke.points[0].y);
        for (std::size_t index = 1; index < stroke.points.size(); ++index)
            CGContextAddLineToPoint(context, stroke.points[index].x, stroke.points[index].y);
        CGContextStrokePath(context);
    }
    for (const auto &label : scene.labels)
    {
        const auto text =
            CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(label.text.data()),
                                    static_cast<CFIndex>(label.text.size() * sizeof(wchar_t)), kCFStringEncodingUTF32LE, false);
        if (!text)
            continue;
        const auto font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 11, nullptr);
        const auto space = CGColorSpaceCreateDeviceRGB();
        const CGFloat channels[] = {label.color.R / 255., label.color.G / 255., label.color.B / 255., label.color.A / 255.};
        const auto color = CGColorCreate(space, channels);
        const void *keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
        const void *values[] = {font, color};
        const auto attributes =
            CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        const auto attributed = CFAttributedStringCreate(kCFAllocatorDefault, text, attributes);
        const auto line = CTLineCreateWithAttributedString(attributed);
        CGContextSaveGState(context);
        CGContextTranslateCTM(context, label.position.x, label.position.y);
        CGContextScaleCTM(context, 1, -1);
        CGContextSetTextMatrix(context, CGAffineTransformIdentity);
        CGContextSetTextPosition(context, 0, 0);
        CTLineDraw(line, context);
        CGContextRestoreGState(context);
        CFRelease(line);
        CFRelease(attributed);
        CFRelease(attributes);
        CGColorRelease(color);
        CGColorSpaceRelease(space);
        CFRelease(font);
        CFRelease(text);
    }
    CGContextRestoreGState(context);
    return S_OK;
}
#endif

HRESULT DrawCairoScene(const Scene &scene, _cairo *context)
{
#ifdef GRAPHING_NATIVE_CAIRO
    if (!context)
        return E_POINTER;
    if (!HasValidSize(scene))
        return E_INVALIDARG;
    cairo_save(context);
    cairo_rectangle(context, 0, 0, scene.width, scene.height);
    cairo_clip(context);
    const auto setColor = [context](Graphing::Color color) {
        cairo_set_source_rgba(context, color.R / 255., color.G / 255., color.B / 255., color.A / 255.);
    };
    setColor(scene.background);
    cairo_paint(context);
    for (const auto &fill : scene.fills)
    {
        if (fill.polygon.size() < 3)
            continue;
        setColor(fill.color);
        cairo_new_path(context);
        cairo_move_to(context, fill.polygon[0].x, fill.polygon[0].y);
        for (std::size_t index = 1; index < fill.polygon.size(); ++index)
            cairo_line_to(context, fill.polygon[index].x, fill.polygon[index].y);
        cairo_close_path(context);
        cairo_fill(context);
    }
    for (const auto &stroke : scene.strokes)
    {
        if (stroke.points.size() < 2)
            continue;
        setColor(stroke.color);
        cairo_set_line_width(context, stroke.width);
        const auto pattern = GetDashPattern(stroke.style, stroke.width);
        cairo_set_dash(context, pattern.data(), static_cast<int>(pattern.size()), 0);
        cairo_new_path(context);
        cairo_move_to(context, stroke.points[0].x, stroke.points[0].y);
        for (std::size_t index = 1; index < stroke.points.size(); ++index)
            cairo_line_to(context, stroke.points[index].x, stroke.points[index].y);
        cairo_stroke(context);
    }
    cairo_select_font_face(context, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(context, 11);
    for (const auto &label : scene.labels)
    {
        try
        {
            const auto text = detail::EncodeUtf8(label.text);
            setColor(label.color);
            cairo_move_to(context, label.position.x, label.position.y);
            cairo_show_text(context, text.c_str());
        }
        catch (const std::invalid_argument &)
        {
            cairo_restore(context);
            return E_INVALIDARG;
        }
    }
    const bool success = cairo_status(context) == CAIRO_STATUS_SUCCESS;
    cairo_restore(context);
    return success ? S_OK : E_FAIL;
#else
    (void)scene;
    (void)context;
    return E_NOTIMPL;
#endif
}
} // namespace graphing

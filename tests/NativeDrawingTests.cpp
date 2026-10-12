#include "GraphingInterfaces/IBitmap.h"
#include "Text.h"
#include "graphing/NativeDrawing.h"
#include <algorithm>
#include <iostream>
#ifdef __APPLE__
#include <ImageIO/ImageIO.h>
#endif
#ifdef GRAPHING_NATIVE_CAIRO
#include <cairo.h>
#endif

int main()
{
    const std::string unicode = u8"θ π ∞ 🧮";
    if (graphing::detail::EncodeUtf8(graphing::detail::DecodeUtf8(unicode)) != unicode)
        return 1;
    for (const std::string invalid : {std::string("\xc0\x80", 2), std::string("\xed\xa0\x80", 3), std::string("\xf4\x90\x80\x80", 4)})
    {
        bool rejected = false;
        try
        {
            graphing::detail::DecodeUtf8(invalid);
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        if (!rejected)
            return 1;
    }
    auto solver = Graphing::IMathSolver::CreateMathSolver();
    int code = 0, type = 0;
    auto expression = solver->ParseInput(L"y=sin(x)", code, type);
    auto graph = solver->CreateGrapher(expression.get());
    if (!graph->TryInitialize())
        return 1;
    graphing::Scene scene;
    if (graphing::GetScene(*graph->GetRenderer(), scene) != S_OK)
        return 1;
    scene.labels.push_back({{20, 30}, L"θ π ∞", {10, 10, 10}});
    std::shared_ptr<Graphing::IBitmap> bitmap;
    bool missing = false;
    if (graph->GetRenderer()->GetBitmap(bitmap, missing) != S_OK || !bitmap)
        return 1;
    const auto png = bitmap->GetData();
#ifdef __APPLE__
    auto data = CFDataCreate(kCFAllocatorDefault, png.data(), static_cast<CFIndex>(png.size()));
    auto source = CGImageSourceCreateWithData(data, nullptr);
    auto image = source ? CGImageSourceCreateImageAtIndex(source, 0, nullptr) : nullptr;
    if (!image || CGImageGetWidth(image) != scene.width || CGImageGetHeight(image) != scene.height)
        return 1;
    CGImageRelease(image);
    CFRelease(source);
    CFRelease(data);
    auto space = CGColorSpaceCreateDeviceRGB();
    auto context = CGBitmapContextCreate(nullptr, scene.width, scene.height, 8, scene.width * 4, space, kCGImageAlphaPremultipliedLast);
    if (!context || graphing::DrawCoreGraphicsScene(scene, context) != S_OK)
        return 1;
    auto *bytes = static_cast<unsigned char *>(CGBitmapContextGetData(context));
    const bool painted = std::any_of(bytes, bytes + scene.width * scene.height * 4, [](unsigned char value) { return value != 255; });
    CGContextRelease(context);
    CGColorSpaceRelease(space);
    if (!painted || graphing::DrawCoreGraphicsScene(scene, nullptr) != E_POINTER)
        return 1;
#elif defined(GRAPHING_NATIVE_CAIRO)
    struct Reader
    {
        const std::vector<BYTE> &bytes;
        std::size_t position = 0;
    } reader{png};
    auto *decoded = cairo_image_surface_create_from_png_stream(
        [](void *closure, unsigned char *data, unsigned length) {
            auto &input = *static_cast<Reader *>(closure);
            if (length > input.bytes.size() - input.position)
                return CAIRO_STATUS_READ_ERROR;
            std::copy_n(input.bytes.data() + input.position, length, data);
            input.position += length;
            return CAIRO_STATUS_SUCCESS;
        },
        &reader);
    if (cairo_surface_status(decoded) != CAIRO_STATUS_SUCCESS || cairo_image_surface_get_width(decoded) != static_cast<int>(scene.width))
        return 1;
    cairo_surface_destroy(decoded);
    auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, scene.width, scene.height);
    auto *context = cairo_create(surface);
    if (graphing::DrawCairoScene(scene, context) != S_OK)
        return 1;
    cairo_surface_flush(surface);
    auto *bytes = cairo_image_surface_get_data(surface);
    const bool painted = std::any_of(bytes, bytes + cairo_image_surface_get_stride(surface) * scene.height,
                                     [](unsigned char value) { return value != 255; });
    cairo_destroy(context);
    cairo_surface_destroy(surface);
    if (!painted || graphing::DrawCairoScene(scene, nullptr) != E_POINTER)
        return 1;
#endif
    std::cout << "Native drawing and Unicode validation passed\n";
}

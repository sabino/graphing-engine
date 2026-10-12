#include <GraphingInterfaces/IBitmap.h>
#include <GraphingInterfaces/IMathSolver.h>
#include <graphing/Scene.h>
#include <iostream>
#include <stdexcept>
#ifdef GRAPHING_CONSUMER_NATIVE
#include <graphing/NativeDrawing.h>
#endif
#ifdef GRAPHING_NATIVE_CAIRO
#include <cairo.h>
#endif

int main()
{
    try
    {
        auto solver = Graphing::IMathSolver::CreateMathSolver();
        int code = 0, type = 0;
        auto expression = solver->ParseInput(L"y=x^2-4", code, type);
        if (!expression || code != 0 || type != 0)
            throw std::runtime_error("Installed factory or parser failed");
        auto graph = solver->CreateGrapher(expression.get());
        if (!graph || !graph->TryInitialize())
            throw std::runtime_error("Installed graph initialization failed");
        auto renderer = graph->GetRenderer();
        if (renderer->SetGraphSize(160, 120) != S_OK)
            throw std::runtime_error("Installed renderer rejected valid size");
        graphing::Scene scene;
        if (graphing::GetScene(*renderer, scene) != S_OK || scene.strokes.empty())
            throw std::runtime_error("Installed scene export failed");
        std::shared_ptr<Graphing::IBitmap> bitmap;
        bool missing = true;
        if (renderer->GetBitmap(bitmap, missing) != S_OK || !bitmap || bitmap->GetData().size() < 32 || missing)
            throw std::runtime_error("Installed bitmap export failed");
#if defined(GRAPHING_CONSUMER_NATIVE) && defined(__APPLE__)
        auto space = CGColorSpaceCreateDeviceRGB();
        auto context = CGBitmapContextCreate(nullptr, 160, 120, 8, 640, space, kCGImageAlphaPremultipliedLast);
        CGColorSpaceRelease(space);
        if (!context)
            throw std::runtime_error("Consumer native context creation failed");
        const auto result = graphing::DrawCoreGraphicsScene(scene, context);
        CGContextRelease(context);
        if (result != S_OK)
            throw std::runtime_error("Installed CoreGraphics adapter failed");
#elif defined(GRAPHING_NATIVE_CAIRO)
        auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 160, 120);
        auto context = cairo_create(surface);
        const auto result = graphing::DrawCairoScene(scene, context);
        const auto status = cairo_status(context);
        cairo_destroy(context);
        cairo_surface_destroy(surface);
        if (result != S_OK || status != CAIRO_STATUS_SUCCESS)
            throw std::runtime_error("Installed Cairo adapter failed");
#endif
        std::cout << "Installed CMake package consumer passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

# Portable graphing engine

A C++17 implementation of Windows Calculator's `GraphingInterfaces` contract.
It parses equations, produces 2D graph geometry, handles viewport operations, and analyzes a supported subset of real functions.
The implementation is independent. Microsoft interface headers retain their MIT license.

Microsoft behavioral parity and retail DLL compatibility are unverified. See [coverage and limits](docs/Compatibility.md) and [reference comparison](docs/CompatibilityTesting.md).

## Build and test

Install CMake 3.21 or later and a C++17 compiler. The core uses the platform SDK and C++ standard library.

```sh
cmake -S . -B out/build -DCMAKE_BUILD_TYPE=Release
cmake --build out/build --config Release --parallel 4
ctest --test-dir out/build -C Release --output-on-failure
```

The default build produces a shared library:

| Platform | Library | Drawing |
| --- | --- | --- |
| Windows | `GraphingImpl.dll` and `GraphingImpl.lib` | Direct2D |
| macOS | `libGraphingImpl.dylib` | CoreGraphics and CoreText adapter |
| Linux | `libGraphingImpl.so` | Optional Cairo adapter |

Use `-DBUILD_SHARED_LIBS=OFF` for a static core.
On Linux, enable native drawing with `-DGRAPHING_BUILD_NATIVE_DRAWING=ON`; this requires Cairo 1.16 headers and pkg-config.
macOS enables its native adapter by default. The adapter builds as `GraphingNative`, separate from the core.
[CI](https://github.com/sabino/graphing-engine/actions/workflows/build.yml) builds installed-package archives for each platform.
Manual runs can enable `upload_packages` when GitHub Actions storage is available.
Archives include the library, headers, CMake configuration, and license notices. Extract them and use `install` as the package prefix.

## Use the library

```sh
cmake --install out/build --config Release --prefix out/install
```

Point the consumer's `CMAKE_PREFIX_PATH` at that installation. Its CMake file can use:

```cmake
find_package(GraphingEngine 0.1 CONFIG REQUIRED)
target_link_libraries(calculator PRIVATE Graphing::GraphingImpl)
# For a built CoreGraphics or Cairo adapter:
# target_link_libraries(calculator PRIVATE Graphing::GraphingNative)
```

```cpp
#include <GraphingInterfaces/IMathSolver.h>
#include <graphing/Scene.h>

auto solver = Graphing::IMathSolver::CreateMathSolver();
int error_code = 0;
int error_type = 0;
auto expression = solver->ParseInput(L"y=x^2-4", error_code, error_type);
if (expression) {
    auto graph = solver->CreateGrapher(expression.get());
    if (graph->TryInitialize()) {
        graphing::Scene scene;
        const auto result = graphing::GetScene(*graph->GetRenderer(), scene);
        // Draw or export scene when result == S_OK.
    }
}
```

`IGraphRenderer::GetBitmap` returns an encoded PNG. `graphing::EncodeBitmap` exports BMP; `SerializeSvg` exports SVG.
Compile C++ consumers with compatible compiler, standard library, architecture, and runtime settings.
This interface passes C++ objects across the library boundary; it is not a stable C ABI.

## Try a graph

```sh
./out/build/graphing-cli 'x^2+y^2=25' out/circle.png
./out/build/graphing-cli 'y=a*sin(x)' out/sine.svg --set a=2
```

On Windows, run `out\build\Release\graphing-cli.exe` from the configured build directory.
See [source and licensing](docs/Source.md) and the [compatibility procedure](docs/CompatibilityTesting.md).

## License

The implementation uses the [MIT license](LICENSE).
Copied Microsoft interfaces retain [Microsoft's MIT notice](docs/Microsoft-LICENSE).

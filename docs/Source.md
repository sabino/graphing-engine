# Source and licensing

The project implements Microsoft Calculator's public C++ `GraphingInterfaces` contract.
Interface headers come from the [MIT-licensed Calculator source](https://github.com/microsoft/calculator/tree/d125246a4e19842ce1332e6c7839cf0e110027d8/src/GraphingInterfaces).
Their copyright notices are retained in the headers and [Microsoft license](Microsoft-LICENSE).

`src/` contains the project's parser, evaluator, analyzer, geometry, image encoders, and platform drawing adapters.
The implementation uses bounded expression trees, checked rational arithmetic, polynomial root isolation, and adaptive curve sampling.
The core depends on the C++ standard library and platform SDK.

Tests cover the public contract, mathematical examples, native drawing, and installed-package consumers.
The [comparison probe](CompatibilityTesting.md) records library behavior through the public interface.
Microsoft compatibility requires matched reference results and application acceptance tests.

The project's implementation uses the [MIT license](../LICENSE).

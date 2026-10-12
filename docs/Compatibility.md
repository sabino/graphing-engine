# Coverage and limits

The compatibility target is Microsoft Calculator's public `GraphingInterfaces` source contract.
Headers originate from revision `d125246a4e19842ce1332e6c7839cf0e110027d8`.
Portability edits supply platform types, export macros, and required includes. Virtual method signatures and enum values are preserved.

Passing project tests establishes implementation behavior. Microsoft equivalence requires [reference comparisons](CompatibilityTesting.md).
Replacing a developer-build mock with this source is distinct from replacing a retail binary.

## Implemented behavior

| Area | Coverage |
| --- | --- |
| Parsing | Linear input, formula syntax, bounded presentation MathML, and Calculator's 2D MathML wrappers |
| Real evaluation | Arithmetic, powers, roots, logarithms, trigonometric and hyperbolic functions, absolute value, rounding, min/max |
| Coordinates | Explicit x/y, implicit equations, inequalities, polar equations, and parametric pairs |
| State | Multiple equations, parameters, radians/degrees/grads, equation selection, colors, line styles, options |
| Viewport | Ranges, zoom, pan, reset, pixel hit testing, and returned world coordinates |
| Analysis | Bounded polynomial/rational analysis and selected affine elementary functions |
| Export | Encoded PNG through `IBitmap`, plus PNG, BMP, SVG, and platform-neutral scene data |
| Native drawing | CoreGraphics/CoreText, optional Cairo, and Windows Direct2D bitmap drawing |

Analysis uses checked rational arithmetic and root isolation for supported polynomial operations.
Coefficients originate from binary64 numbers. Returned numerical feature coordinates are approximations.
Unsupported features remain empty and set `TooComplexFeatures`; an unknown result does not certify absence.
Analysis honors requested feature masks and invalidates results after relevant state changes.

## Known gaps

| Area | Limit | How to close it |
| --- | --- | --- |
| Microsoft parity | No Microsoft runtime snapshots have been captured | Run the reference probe and resolve each difference |
| Retail binary replacement | C++ ABI and packaged-runtime compatibility are unverified | Validate vtables, runtime settings, loading, and UI integration on Windows |
| Expression language | No complex numbers, factorial, binary/base64 formats, LaTeX input, or complete MathML grammar | Add source-backed cases and measured reference behavior |
| Analysis | Polynomial degree is bounded at 12; coefficient overflow and work limits return unknown | Expand algorithms against independent mathematics and reference cases |
| Analysis coverage | General rational ranges, general transcendental analysis, and some trig features remain unknown | Add feature-specific analysis with explicit unknown handling |
| Graph dimensions | 2D only; Z-range actions return `E_NOTIMPL` | Specify and implement a separate 3D contract if needed |
| Appearance | Sampling, labels, fonts, dashes, and bitmap encoding may differ from Microsoft | Compare geometry and native rendering against declared tolerances |
| Native Calculator UI | The Calculator checkout integrates AppKit graphing; GTK graphing remains absent | Connect and validate GTK graphing separately |

## Deliberate bounds

Inputs allow 65,536 characters, 8,192 nodes, and nesting depth 128.
A graph accepts at most 64 equations. Images allow at most 4,096 pixels per axis and 4,194,304 pixels total.
Rendering caps evaluation work and time. Exhausted rendering reports missing data.
Undefined real values split curves; they do not create lines across domain gaps.

`SetMaxExecutionTime` uses milliseconds in this implementation; zero retains safety limits.
The retail engine's unit and zero semantics have not been observed.
Parameter sampling, default values, errors, locale details, and numerical thresholds also require reference comparison.

## Acceptance gates

1. Capture pinned Microsoft outputs on Windows and compare the same corpus through this library.
2. Expand cases across formats, locales, angle modes, parameters, singularities, selection, and viewport sequences.
3. Build Calculator with the real engine and verify its graphing UI on Windows, macOS, and Linux.

Declare compatibility by tested Microsoft version and feature coverage. Finite tests cannot establish universal mathematical equivalence.

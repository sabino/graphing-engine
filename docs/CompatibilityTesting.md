# Comparing the public contract

Run `graphing-compatibility-probe` against each engine in separate processes. Compare complete snapshots from the same case file.

A match proves agreement for recorded fields. It does not establish full Microsoft compatibility. Local tests verify this implementation alone.

## Capture the portable engine

Run from the repository root after building the probe:

```sh
./out/build/graphing-compatibility-probe \
  --cases tests/fixtures/graphing-cases.tsv \
  --output portable.tsv
./out/build/graphing-compatibility-probe --compare portable.tsv portable.tsv
```

The fixture includes ten required cases: polynomials, repeated roots, rational holes, parameters, degree angles, explicit x, implicit curves, inequalities, selection, and parse failure. It exercises Linear, Formula, localized decimal comma, and MathML inputs.

Each capture records parsing errors, serializer availability, parameter options, variable names, initialization, selection, analysis, ranges, transformations, and three hit queries. It uses only `GraphingInterfaces` headers.

## Capture a licensed Windows reference

Use an existing licensed copy. Keep binaries, resources, captures, and reference paths outside the repository.

Record the reference library's SHA-256, application version, and compiler/runtime settings with the capture.

The public headers are pinned to Calculator commit `d125246a4e19842ce1332e6c7839cf0e110027d8`.

Build the probe with matching architecture and compatible MSVC settings. Match release/debug CRT, STL settings, `_ITERATOR_DEBUG_LEVEL`, and calling conventions. C++ ABI compatibility remains unverified. A successful load does not verify it.

The factory returns STL ownership types across the DLL boundary. A mismatch can crash before a complete capture exists.

Use PowerShell, substituting an absolute reference path:

```powershell
$reference = 'C:\licensed-reference\GraphingImpl.dll'
Get-FileHash -Algorithm SHA256 -LiteralPath $reference
$factory = '?CreateMathSolver@IMathSolver@Graphing@@SA?AV?$unique_ptr@UIMathSolver@Graphing@@U?$default_delete@UIMathSolver@Graphing@@@std@@@std@@XZ'

.\out\build\Release\graphing-compatibility-probe.exe `
  --cases tests\fixtures\graphing-cases.tsv `
  --output portable-windows.tsv

.\out\build\Release\graphing-compatibility-probe.exe `
  --cases tests\fixtures\graphing-cases.tsv `
  --output microsoft-windows.tsv `
  --engine $reference `
  --factory-export $factory

.\out\build\Release\graphing-compatibility-probe.exe `
  --compare portable-windows.tsv microsoft-windows.tsv
```

Reference loading requires the exact export and an absolute file path. DLL dependency searches use the requested DLL directory and System32. The probe verifies loaded file identity before calling the factory.

Run each capture under an external process timeout. The probe sets a 1000 ms analysis option, but cannot bound an unresponsive reference call.

## Read the comparison

Snapshots are sorted four-column TSV: case, field, type, escaped value. UTF-8 inputs and wide strings use canonical ASCII escapes. Newlines become `\n`; Unicode becomes `\uXXXX`.

The fixture fingerprint ignores comments and CRLF differences. It identifies the cases, not the engine artifact. Use SHA-256 for artifact provenance.

Exit codes are:

| Code | Meaning |
| --- | --- |
| 0 | Complete capture, or equal recorded comparison fields |
| 1 | Unexpected or missing capture result, or comparison differences |
| 2 | Invalid input, incomplete snapshot, loader failure, or probe error |

Comparison rejects missing required cases, duplicate fields, incomplete markers, mismatched manifests, and truncated case records. Failed parsing is complete only for its expected failure case. Unexpected initialization or unavailable required serializers prevents a complete capture.

`exact` fields compare escaped text exactly. This includes HRESULTs, error codes, names, selection, analysis flags, and MathML. Equivalent mathematical strings can differ; inspect those differences.

`numeric` fields also compare exactly. These contain viewport and hit coordinates. Tolerance acceptance requires a separate reviewed rule; this probe applies none.

`identity` metadata records engine path, runtime/compiler family, and wchar width. Comparison excludes those three fields. Pointer architecture remains an exact field.

Variable names and equation indices replace allocation IDs. Invariant serializer availability is recorded, but its identity-dependent payload is excluded. Analysis lists are sorted; their multiplicity remains visible.

Unknown analysis bits, empty reported fields, and unset numeric outputs remain visible. Equal unknowns are excluded from mathematical proof. Check the summary's unknown count.

## Acceptance gates

1. Verify the exact reference artifact, matching ABI prerequisites, complete captures, and repeatable results.
2. Review every difference against the Calculator call sequence. Add cases for each accepted behavior difference and boundary.
3. Validate drawing, interaction, DPI, and resources with a separate Windows UI/render oracle.

This probe does not compare bitmap pixels, Direct2D drawing, typography, UI resources, or complete formula families. It also leaves randomized coverage and performance acceptance open.

# Contributing to InputGlass

Thanks for helping make input diagnostics easier to understand.

## Before Opening an Issue

- Search existing issues first.
- Include your Windows or compatibility-layer version.
- Identify the physical or virtual device and any remapper involved.
- Attach the smallest useful log excerpt after removing sensitive key sequences.
- Describe expected and observed behavior with reproducible steps.

## Development

Keep changes focused and preserve the project's native, dependency-free design. Build warnings are
treated as errors.

On Linux:

```sh
./build.sh
```

On Windows from a Visual Studio developer shell:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Changes to input handling should be tested with the relevant physical or virtual device. Note what
you tested in the pull request; a successful cross-build alone does not validate runtime input
behavior.

## Pull Requests

- Use clear commit messages.
- Explain behavior changes and their motivation.
- Update the README or changelog when user-facing behavior changes.
- Do not commit build output, session logs, or captured sensitive input.
- Keep platform compatibility at Windows 7 or newer unless a change is discussed first.

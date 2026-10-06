# MotecDataAnalyser — Toolchain, Build & Debug

> **Architecture & diagnostic engine:** see [`agents.md`](../../agents.md) at the repo root.
> This file covers **only** the compiler toolchain, build commands, packaging, and debug notes.

## Product and compiler

| Item | Value |
|------|--------|
| IDE | Embarcadero RAD Studio / **C++ Builder 12** |
| Studio version folder | `23.0` |
| Install root | `C:\Program Files (x86)\Embarcadero\Studio\23.0` |
| Environment script | `C:\Program Files (x86)\Embarcadero\Studio\23.0\bin\rsvars.bat` |
| Personality | C++Builder VCL (`CppVCLApplication`) |
| Project file | `Project1.cbproj` |
| App type | Win32 VCL GUI (`FrameworkType=VCL`) |
| Default config / platform | **Debug** / **Win32** |
| C++ frontend | **Clang** (`BCC_UseClassicCompiler=false`) — not classic BCC32 |
| PCH | `Project1PCH1.h` |
| TCHAR | `wchar_t` |
| RTL | Dynamic RTL, runtime packages (`rtl`, `vcl`, `vclwinx`, `vclimg`, `bindengine`) |

Do **not** build this project with MSVC, MinGW, or g++. VCL (`#include <vcl.h>`, `__fastcall`, `__published`, `.dfm`) only compiles with C++ Builder.

Clang here is Embarcadero's toolchain (bcc32c / bcc64), not stock LLVM. C++17-ish features work, but the STL is the RAD Studio one. Prefer portable C++11/14 style if something fails to compile.

## Command-line build (Windows)

PowerShell treats `(x86)` as a subexpression. Call `cmd.exe` with **single-quoted** `/c` argument:

```bat
cmd.exe /c 'call "C:\Program Files (x86)\Embarcadero\Studio\23.0\bin\rsvars.bat" && msbuild Project1.cbproj /p:Config=Debug /p:Platform=Win32 /t:Build /v:minimal'
```

Outputs:

- Objects / exe: `.\Win32\Debug\` (or `.\$(Platform)\$(Config)`)
- Settings written by the app: `Win32\Debug\MotecDataAnalyser.ini`
- Analysis thresholds (auto-created if missing): `Win32\Debug\MotecDataAnalyser.thresholds.ini`

From the IDE: open `Project1.cbproj`, target **Win32**, configuration **Debug**, then Build / Compile.

### Quick start with `build.ps1`

A wrapper script in the repo root performs the same build and prints a clear verdict:

```powershell
.\build.ps1                 # incremental Debug/Win32 build
.\build.ps1 -Clean          # delete Win32\Debug first, then rebuild
.\build.ps1 -Config Release # also supports -Platform Win64
```

- Full MSBuild output is written to `build_log.txt` (repo root) and echoed.
- Success = msbuild exit code 0 **and** `Win32\<Config>\Project1.exe` on disk. Do not rely on the "Build succeeded" line: with `/v:minimal`, MSBuild 4.8 prints only its banner even for a full clean rebuild.
- Studio is expected at the default path; adjust `$StudioRoot` in the script if it differs.

Gotchas already handled by the script (do not regress):

- The `.ps1` must stay **pure ASCII** — PowerShell 5.1 reads BOM-less scripts as ANSI/cp1252, so accents or em-dashes break parsing ("string missing terminator").
- The msbuild command is passed to `cmd.exe /c` as ONE argument built from single-quoted literals; backtick-escaped double quotes get re-escaped by PowerShell for native commands and corrupt the `(x86)` path.

### Packaging (portable distribution) with `scripts\package.ps1`

Builds a ready-to-share folder after a successful build:

```powershell
.\build.ps1                              # first, produce Win32\Debug\Project1.exe
.\scripts\package.ps1                    # -> MotecDataAnalyser_Portable\  (Debug/Win32)
.\scripts\package.ps1 -Config Release -Platform Win64
.\scripts\package.ps1 -SkipData          # skip the ~58 MB data\ samples for a quick package
```

Layout produced: `MotecDataAnalyser.exe` + 5 runtime `.bpl` beside it, plus `runtime\`, `config\`, `data\` (samples), and `README.txt`.

- The app links the dynamic packages `rtl vcl vclwinx vclimg bindengine`; their redistributables are `<name>290.bpl` in `Studio\23.0\Redist\win32|win64` (~18.9 MB for the 5, vs ~89 MB for all 93).
- The `.bpl` MUST sit next to the exe: Windows DLL search checks the exe dir first and never a `runtime\` subfolder automatically. On this dev machine the app runs from `Win32\Debug` (no .bpl there) only because C++ Builder registers its package path; on a clean target box without C++ Builder, the shipped `.bpl` are what make it start.
- Keep `package.ps1` pure ASCII too (same PS 5.1 rule as build.ps1).

## Debug / compile notes

Typical C++ Builder 12 Clang errors:

- `E5892` undeclared identifier — wrong function scope (CSV vs LD) or missing include
- `E5550` redefinition with a different type — duplicate locals (`const double` then `double`)
- `E5781` assign to const — same duplicate-const pattern
- Linker (`ilink32`) package/lib path issues — `rsvars.bat` must be called first so `BDS`, `BDSLIB` are set

VCL conventions in this project:

- Form methods are `__fastcall`
- UI strings are `UnicodeString` / `String`; convert paths with `UTF8String` then `std::string`
- Use `NULL` or `nullptr` for VCL owners; `__finally` for dialog cleanup
- Do not "fix" VCL by switching to Win32-only or Qt patterns

When investigating logic (not compile errors):

- CSV path: `MotecSetupForm.cpp` -> `AnalyzeSelectedFile` -> `parseMotecCsv`
- LD path: `AnalyzeSelectedLdFile` -> `parseMotecLd` -> `DecodeMotecLd`
- Binary LD layout notes: `data/ld-file-format.md`

Project `IncludePath` / `ILINK_LibraryPath` still reference `..\..\..\form test\` (legacy). Prefer local files in this repo.

## Agent instructions

- Compile with C++ Builder 12 as above; treat IDE or MSBuild output as the source of truth for compile errors.
- Do not retarget the project to another compiler.
- Do not mix CSV row-index helpers into the LD sample loop.
- Do not commit unless asked.
- Keep changes small and in the existing VCL + `std::` style already used in the `.cpp` files.
- For architecture, diagnostic rules, and configuration details, refer to [`agents.md`](../../agents.md).

cloudForge - native Windows CPU build

Double-click Launch-cloudForge.cmd, then open a PCD file or folder in the app.
Play Clip loads neighboring recorded frames. Original and simplified views stay paired.

This package processes on CPU. CUDA processing in native Windows is not verified:
the Windows CUDA toolkit/toolset is missing on the test machine. The WSL CUDA build
is a separate build and is documented in the technical report.

Built with MSVC 2022 x64 Release; GLFW 3.4 linked statically. Another Windows PC may
need the Microsoft Visual C++ x64 Redistributable. Download from Microsoft:
https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist

No dataset is bundled. Current loader: uncompressed little-endian binary PCD with
scalar float32 x/y/z. Extra attributes are skipped. Code license: MIT (LICENSE.txt).

Source build:
cmake --preset windows-cpu-release
cmake --build --preset windows-cpu-release
ctest --preset windows-cpu-release

The preset can fetch pinned GLFW and GoogleTest into build/ on its first configure.

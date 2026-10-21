# Getting started

Run commands from the repository root. Keep build directories separate by compiler
and backend; do not reuse a WSL build directory for native Windows.

## Requirements and verified configurations

- CMake 3.24 or newer, Git and a C++20 compiler.
- GoogleTest for tests; it is unnecessary when `BUILD_TESTING=OFF`.
- GLFW/OpenGL only when `CLOUDFORGE_BUILD_VIEWER=ON`.
- A compatible NVIDIA GPU, driver and CUDA Toolkit only for the CUDA backend.
- Open3D, PCL and Python are not required to build or run the processing app.

Recorded qualification on 21 October 2026:

| Build | Toolchain / result |
|---|---|
| Native Windows CPU | Visual Studio 2022, MSVC 19.37, x64 Release; 35 tests passed. |
| Ubuntu/WSL CPU | GCC 15.2, CMake 4.2.3, Ninja 1.13.2; 35 shared tests passed. |
| Ubuntu/WSL CUDA | CUDA 13.2.86, RTX 4050 Laptop, driver 595.79; 41 tests passed. |
| Native Windows CUDA | Configure failed with `No CUDA toolset found`; not verified. |

## Native Windows CPU application

Install Visual Studio 2022's C++ toolchain and a Windows SDK. Ensure CMake and Git
are available in the terminal. From a fresh checkout:

```powershell
cmake --preset windows-cpu-release
cmake --build --preset windows-cpu-release
ctest --preset windows-cpu-release
powershell -File scripts/run-windows.ps1
```

The first configure can fetch GLFW 3.4 and GoogleTest 1.17.0 into the build tree.
The option enabling this is `CLOUDFORGE_FETCH_DEPS`; it defaults to OFF outside
the Windows preset. Subsequent disconnected dependency updates are configured by
that preset. Build the `Release` configuration when assessing performance.

You can launch the executable directly if script execution is restricted:

```powershell
.\build\windows-cpu\Release\cloudforge_viewer.exe
```

The local delivery ZIP, if present, contains a CPU executable and launcher. Extract
it and run `Launch-cloudForge.cmd`. Another PC may need the Microsoft x64 VC++
runtime. No scan data is bundled. A Windows CUDA Toolkit is separate from WSL's
installation; a working WSL CUDA program does not verify native CUDA support.

## Ubuntu / WSL prerequisites

For the CPU tools and tests:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build git libgtest-dev
```

For the desktop viewer, also install:

```bash
sudo apt install libglfw3-dev libgl1-mesa-dev
```

The WSL viewer needs a working graphical environment such as WSLg/X11. GPU compute
and OpenGL drawing are separate: software rendering does not imply CUDA runs on
the CPU. CUDA installation should follow the official
[NVIDIA WSL guide](https://docs.nvidia.com/cuda/wsl-user-guide/).

## CPU-only tools and tests

```bash
cmake --preset cpu-release
cmake --build --preset cpu-release
ctest --preset cpu-release
```

This builds the core library, the small CPU CLI, benchmark CLI and shared tests.
It neither enables CUDA nor builds a graphics viewer.

To build the CPU viewer with installed GLFW/OpenGL packages:

```bash
cmake -S . -B build/cpu-viewer -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCLOUDFORGE_ENABLE_CUDA=OFF -DCLOUDFORGE_BUILD_VIEWER=ON -DBUILD_TESTING=ON
cmake --build build/cpu-viewer -j 4
ctest --test-dir build/cpu-viewer --output-on-failure
./build/cpu-viewer/cloudforge_viewer
```

## CUDA tools and viewer in WSL

Check that the GPU is visible and locate the CUDA compiler:

```bash
nvidia-smi
/usr/local/cuda-13.2/bin/nvcc --version
```

That compiler path matches the development machine. Substitute your installed
toolkit path; toolkit/host-compiler compatibility must be checked for another
machine. Configure, build and test:

```bash
cmake --preset cuda-release -DCMAKE_CUDA_COMPILER=/usr/local/cuda-13.2/bin/nvcc
cmake --build --preset cuda-release
ctest --preset cuda-release
bash scripts/run-demo.sh
```

The preset uses the detected GPU architecture. A fixed architecture can be supplied
explicitly, for example `-DCMAKE_CUDA_ARCHITECTURES=89` for the tested RTX 4050.
Set `-DCLOUDFORGE_BUILD_VIEWER=OFF` if only headless CUDA tools are needed.

## Dependency options

| Option | Meaning |
|---|---|
| `CLOUDFORGE_ENABLE_CUDA=OFF` | CPU-only build, with no CUDA compiler detection. |
| `CLOUDFORGE_BUILD_VIEWER=OFF` | Omit GLFW/OpenGL viewer and its dependencies. |
| `BUILD_TESTING=OFF` | Omit tests and their GoogleTest requirement. |
| `CLOUDFORGE_FETCH_DEPS=ON` | Allow fetching pinned GLFW/GoogleTest sources when packages are unavailable. Requires network access on initial download. |

## Try a real file

Obtain a PCD that meets the [input contract](user-guide.md#input-contract). The
Oxford Spires recording used during development is downloaded separately and has
its own license. Paths in examples below are illustrative; replace them with your
actual location.

```bash
./build/cpu-release/cloudforge "DataSets/my-clip/frame.pcd"
./build/demo/cloudforge_viewer "DataSets/my-clip" --backend cuda --voxel 0.3
```

The small `cloudforge` CLI takes one PCD path and uses voxel size `0.1`. It is not
the configurable desktop app. The second command launches the CUDA viewer.

## Common setup failures

| Error / symptom | Next check |
|---|---|
| `cmake` is not found on Windows | Use a terminal with Visual Studio's CMake available, or add the installed CMake to that terminal's PATH. |
| GoogleTest or GLFW package not found | Install the appropriate development package, or explicitly enable dependency fetching. |
| CUDA compiler/toolset missing | Verify the toolkit for the actual host OS; use CPU mode if absent. |
| CMake reports compiler/cache conflicts | Use a new build directory for the new toolchain. |
| No display / failed OpenGL window in WSL | Verify WSLg/X11 and graphics packages. Software rendering can be tried with `LIBGL_ALWAYS_SOFTWARE=1`. |
| App opens, but no data appears | Inspect the visible error and confirm the binary PCD input contract. |

For implementation and debugging work, continue with the
[developer ownership guide](developer-ownership-guide.md).

# cloudForge - developer ownership guide

Prepared 22 October 2026. This is a practical guide to understanding, maintaining
and defending the project. It is not a certificate that reading these pages alone
makes you its expert.

Taking ownership means you can explain what the code does, locate a failure,
change its behavior deliberately, and verify the result. You do not need to have
invented CUDA or personally written every line without help. You do need to know
which parts you can confidently explain and which still need study.

## 1. Your thirty-second explanation

> cloudForge is a C++20 engine and desktop app for recorded 3D point clouds. It
> reads timestamped PCD frames and reduces their density with a voxel grid. Each
> occupied voxel produces one centroid. A tested CPU reference and an optional
> CUDA backend implement the same behavior. The app shows original and simplified
> frames together and includes a separate, repeatable benchmark workflow.

This is recorded-data replay and processing. It does not generate LiDAR from an
ordinary video, estimate motion, perform SLAM, or simulate a physical environment.

## 2. Follow one frame through the source

Read these files in this order. After each step, explain its input and output
without looking at the implementation.

| File | What you should understand |
|---|---|
| `include/cloudforge/point_cloud.hpp` | A point owns double-precision XYZ coordinates; a point cloud is an owning vector of points. |
| `include/cloudforge/pcd_io.hpp`, `src/pcd_io.cpp` | The PCD header defines field offsets and point stride. The reader finds scalar float32 XYZ, validates data and converts it into the internal cloud. |
| `src/voxel_grid.cpp` | The CPU reference assigns a signed integer key to each point, groups equal keys and emits centroids in sorted key order. |
| `include/cloudforge/engine.hpp`, `src/engine.cpp` | Backend dispatch, timestamp ordering, completed-call timing, output comparison and reduction metrics. |
| `src/voxel_grid_cuda.cu` | GPU key generation, grouping, aggregate reduction, device ownership and wide-range fallback. |
| `apps/processing.hpp` | A long-lived worker, futures and frame processing. Playback processes once; inspection and benchmarking have different measurement needs. |
| `apps/file_browser.hpp` | Cached browser metadata and discovery of files, sequences and immediate clip folders. |
| `apps/benchmark_session.hpp` | Benchmark configuration, fixed warm-up order, alternating timed order, correctness checks and automatic CSV export. |
| `apps/benchmark_results.hpp` | CSV validation, percentile calculation and per-frame grouping for plots. |
| `apps/viewer.cpp`, `apps/gl_support.hpp` | Window events, paired views, camera controls, playback state, benchmark graphs and screenshots. |
| `CMakeLists.txt`, `CMakePresets.json` | Optional dependencies and separate CPU, CUDA and native Windows configurations. |

The complete data path is:

```text
PCD path -> validated host point cloud -> selected CPU/CUDA backend
         -> owned centroid cloud -> completed result delivered to UI
         -> original and simplified frame drawn together
```

## 3. Be able to derive the algorithm by hand

For positive voxel size `s`, the key is:

```text
kx = floor(x / s)
ky = floor(y / s)
kz = floor(z / s)
key = (kx, ky, kz)
```

Two points are grouped only if all three key coordinates match. There is no need
to search every possible voxel or explicitly build the entire 3D grid.

For `s = 1`, `(0.1, 0.2, 0.3)` and `(0.2, 0.3, 0.4)` share `(0, 0, 0)`.
Their centroid is `(0.15, 0.25, 0.35)`. A point at `(-0.1, 0, 0)` has key
`(-1, 0, 0)` and belongs to another voxel.

Why floor matters: converting `-0.1` to an integer by truncation produces zero,
which would incorrectly merge points across the origin. Floor produces minus one.
In one axis, a voxel covers `[k*s, (k+1)*s)`: the lower boundary is included and the
upper boundary belongs to the next cell. Floating-point values close to a boundary
still deserve care; a decimal number is not always represented exactly in binary.

The reference stores a count and a running weighted mean per voxel:

```text
new_mean = old_mean * ((count - 1) / count) + point / count
```

This avoids constructing a potentially overflowing raw coordinate sum. It does
not make floating-point arithmetic exact or establish safety for every possible
finite input. The deterministic promise is repeatability for the same input
order, not bitwise equality for all permutations.

**Explain the tradeoff:** larger voxels usually reduce more points but discard
more fine detail. They do not guarantee a larger GPU speedup. Changing voxel size
changes both the output and the computational workload.

## 4. Understand the CPU reference before the GPU

The CPU uses `std::map` with a tuple of three signed 64-bit integers as the key.
Its ordered output makes the reference straightforward to compare and debug.
It is a single-threaded reference, not a claim to be the fastest possible CPU
algorithm. A hash map, sorting-based approach or parallel CPU implementation would
be reasonable alternatives to evaluate separately.

With `N` input points and `V` occupied voxels, map insertion/grouping costs roughly
`O(N log V)` and the map stores `O(V)` accumulators. Input and output vectors have
their own memory costs. Empty input, duplicate points, negative coordinates,
boundaries, invalid sizes and malformed coordinates have explicit tests.

## 5. Understand what CUDA actually accelerates

The fast path follows these steps:

1. Host-side validation computes valid voxel-coordinate ranges.
2. A kernel produces a packed integer key and an aggregate for each point.
3. CUB radix sorting places equal keys next to each other.
4. Reduce-by-key merges the aggregates for each occupied voxel.
5. A kernel emits centroid points; a completed host copy returns the results.

Packed keys are collision-free only while the observed range can be represented
in the chosen integer encoding. If that range cannot fit, the implementation uses
full signed triple keys in a Thrust sort/reduce fallback. A lossy hash must never
be treated as a unique voxel identifier without checking collisions.

An aggregate contains a mean and count. Merging two groups computes a weighted
mean based on their counts. The mathematical operation represents the same
centroid, but different parallel reduction orders can round differently.

Device buffers and CUB scratch storage use RAII and grow when required. Keeping
them on the same long-lived worker allows reuse across calls. This reduces
allocation overhead; it does not eliminate data transfers or synchronization.

**Know the vocabulary:** host means CPU-side execution and memory; device means
GPU-side execution and memory. A kernel launch schedules GPU work. Launching is
not necessarily completing: the measured API must wait for the result it promises.

Do not claim you hand-wrote CUB's sorting implementation. The project combines
custom kernels, library algorithms, validation and resource management.

## 6. Understand correctness and numerical tolerance

The two backends must produce the same occupied-voxel partition and output count.
The engine compares sorted outputs and finite coordinates with absolute and
relative tolerance, both configured as `1e-10` in the default comparison.

Bitwise equality is inappropriate because floating-point addition and weighted
merging are not perfectly associative. Tolerance is not permission to ignore an
incorrect voxel assignment: a count mismatch or a meaningful coordinate error is
a correctness failure.

The recorded 900-pair benchmark session passed every comparison, with maximum
observed coordinate error about `1.78e-15`. That is evidence for those cases, not
a proof for all inputs. If a new case fails, preserve its input and reproduce the
failure before adjusting tolerances.

## 7. Explain the app's concurrency and playback

The UI thread handles GLFW events and OpenGL drawing. A worker performs file
discovery, loading, processing and benchmark jobs. Futures transfer completed
results or exceptions back to the UI. The worker is long-lived so its CUDA
workspace survives between jobs.

Original and simplified clouds are presented as one completed frame pair. The
displayed frame index is separate from an index whose processing may still be in
flight. Otherwise a label could say frame 12 while the screen still shows frame 11.

Playback uses one selected-backend call per frame. It does not repeatedly benchmark
both backends during animation. PCDs are timestamp-ordered; immediate clip folders
can play consecutively. The app uses an adjustable fixed rate, not exact source
timestamp intervals. Target FPS is a request; actual FPS can be limited by file
I/O, processing and rendering. Processing FPS alone is not end-to-end playback FPS.

If only one PCD is loaded, Play Clip discovers neighboring PCDs in its folder and
starts from the selected file. A stationary recording may still show very little
scene movement. Do not add artificial motion and call it captured movement.

Benchmarking pauses drawing and playback. Cancellation is cooperative between
units of work; it does not forcibly interrupt a kernel or arbitrary filesystem call.

## 8. Defend the benchmark, not just its biggest ratio

The dedicated session used 30 frames, 30 repetitions per backend per frame and
three warm-ups per backend per frame: 900 paired measurements. Warm-up order is
fixed, and measured order alternates CPU-first and CUDA-first.

| Measured scope | Treatment |
|---|---|
| Host validation | Included |
| Required allocation/growth | Included |
| Host/device transfers and GPU completion | Included |
| File loading | Excluded |
| Rendering and CSV writing | Excluded |
| Warm-ups and correctness checks | Excluded from timed calls |

The session's pooled CPU median was `5.3434285 ms`; CUDA median was `1.204919 ms`.
Their ratio is approximately `4.43x`. The ratio of summed CPU and CUDA times is
about `4.49x`. The median ratio for one individual frame can be different again.
These are different summaries; always label which one you show.

Hardware was a Ryzen 7 7735HS and RTX 4050 Laptop GPU in WSL, with CUDA 13.2.86.
The CPU baseline was the single-threaded ordered-map reference. Power, clocks,
thermals and background OS activity were not controlled. Do not claim a constant
4.43x speedup, a kernel-only timing, or superiority over every CPU implementation.

P10-P90 whiskers describe timing variation. They are not confidence intervals.
Read raw data in `output/evidence/benchmark-session.csv`, not just the rounded chart.
The evidence in the documents was captured on 21 October 2026.

## 9. State platform support accurately

| Configuration | Verified evidence |
|---|---|
| Native Windows CPU | MSVC 2022 x64 Release; 35 tests passed; 60 real frames captured. |
| Ubuntu/WSL CUDA build | 41 tests passed: 35 shared tests plus six GPU tests; CUDA playback captured. |
| Native Windows CUDA | Configure attempted; blocked by missing Windows CUDA toolset. Not verified. |

The 35 shared tests overlap between builds. Adding the two totals does not mean
there are 76 unique tests. The NVIDIA Windows driver being available inside WSL
does not mean the Windows CUDA compiler/toolset is installed.

OpenGL rendering and point-cloud processing are different workloads. The native
Windows capture rendered on AMD Radeon Graphics while processing on CPU; WSL
captures rendered through llvmpipe while processing on NVIDIA CUDA.

## 10. Own the build and verification loop

From Ubuntu/WSL in the project root:

```bash
cmake --preset cpu-release
cmake --build --preset cpu-release
ctest --preset cpu-release

cmake --preset cuda-release
cmake --build --preset cuda-release
ctest --preset cuda-release
bash scripts/run-demo.sh
```

For a fresh CUDA configure, supply
`-DCMAKE_CUDA_COMPILER=/usr/local/cuda-13.2/bin/nvcc` if the compiler is not on PATH.
Use the installed toolkit path on a different machine rather than copying this
version blindly. A CPU-only configure must still work with no CUDA installed.

Native Windows, from a terminal with CMake and Git available:

```powershell
cmake --preset windows-cpu-release
cmake --build --preset windows-cpu-release
ctest --preset windows-cpu-release
powershell -File scripts/run-windows.ps1
```

Visual Studio 2022 must have its C++ toolchain and SDK. The first native configure
may fetch GLFW and GoogleTest into `build/`; no system-wide package installation
is required by that dependency-fetch option. A different Windows PC may need the
Microsoft VC++ runtime to run the executable.

For a change: reproduce the problem, identify its layer, make the smallest coherent
fix, run relevant tests, then exercise the affected app flow. Preserve the CPU-only
build. Use a Release build for performance claims. Do not change compiler flags,
workload and algorithm simultaneously and attribute the whole difference to one fix.

## 11. A debugging map

| Symptom | First checks |
|---|---|
| No animation | Is more than one frame loaded? Does the counter advance? Is playback paused by benchmarking? Is the recording itself mostly stationary? |
| Wrong original/simplified pairing | Inspect pending versus displayed indices and when completed results replace the visible pair. |
| Wrong grouping near the origin | Check floor versus truncation and all three voxel coordinates. |
| GPU output differs | Reproduce on a tiny cloud; compare keys, output count, ordering, finite values and fallback behavior before tolerances. |
| File picker stalls | Look for filesystem queries on the UI thread or inside sort/draw loops. |
| CPU faster than CUDA | Check workload size, initialization, allocation, transfers, build type and timing boundary. It can be a valid result. |
| Benchmark ratio changes | Check repeats, distributions, power/thermal state, background work and whether you are comparing one-frame versus pooled results. |
| Native CUDA configure fails | Verify the Windows toolkit/toolset; the WSL installation cannot replace it. |

## 12. Known limits you should volunteer when relevant

- The loader currently supports uncompressed little-endian binary PCD with scalar
  float32 XYZ. It does not support every PCD variant, compression, or arbitrary
  input video. Extra intensity/RGB attributes are not preserved in processing.
- CPU determinism applies to a fixed input order. GPU/CPU agreement is tolerant,
  not a bitwise guarantee across all hardware, toolkits and datasets.
- Playback has no bounded prefetch queue or exact timestamp scheduling. Native
  Windows CUDA qualification remains pending.
- The existing performance study does not establish a workload crossover curve,
  isolate every optimization's contribution, or compare against an optimized
  parallel CPU baseline.
- The application is a working engineering demonstrator, not a claim of production
  deployment, exhaustive robustness certification or industrial customer use.
- Oxford Spires data and derived visuals have separate CC BY-NC-SA 4.0 terms.
  Preserve attribution. The project code is MIT; the dataset is not yours.

## 13. Practice until you can change the project yourself

Work through these tasks in order. Write down your prediction before running each
experiment, then compare it with the result.

1. **Trace six points manually.** Choose positive, negative, boundary and duplicate
   points. Calculate voxel keys and centroids on paper, then create a test for it.
2. **Explain one existing test.** Read a boundary test and explain exactly which
   incorrect implementation it would catch. Modify a test input and predict it.
3. **Read one PCD header.** Find XYZ offsets, point stride and payload size. Explain
   why reading every file as tightly packed XYZ triples would be incorrect.
4. **Trace one GPU group.** Show its packed key, how sorting creates a run, and how
   two aggregates merge into one centroid. Explain when fallback is required.
5. **Make one small feature change.** Change a playback control or visible metric.
   Locate the event, state update and drawing code yourself; exercise it afterwards.
6. **Reproduce a benchmark.** Keep file set and voxel size fixed. Run twice, read the
   CSV and explain both the agreement and the variation without choosing only the
   nicer ratio.
7. **Debug a deliberate local mistake.** In a scratch branch, replace floor with
   truncation, observe the failing test, explain the failure, and restore the code.
   Never merge the intentional defect.
8. **Implement an improvement end to end.** For example, add a labeled playback rate
   preset. Define expected behavior, implement it, verify it and document its scope.

You have stronger evidence of ownership after completing and explaining these
tasks than after memorizing every answer in this guide.

## 14. Presentation and interview answers

**Why not use a library's ready-made voxel filter?**

> The aim was to learn and expose the processing contract, build a reference,
> implement an optional GPU path and measure it. Libraries could be a useful
> external baseline; I do not claim they are unnecessary in other projects.

**Why did the GPU start slower?**

> The measured workload includes more than arithmetic. Initial setup, transfers,
> allocation and synchronization can dominate small clouds. Reusing buffers and
> changing grouping/sorting improved the observed sessions. I still measure each
> workload rather than assuming the GPU wins.

**Is 4.43x the project's guaranteed speedup?**

> No. It was the pooled-median ratio in one 900-pair session on this laptop,
> against the single-threaded CPU reference. I keep the workload, timing scope,
> raw samples and correctness evidence with that claim.

**What did you learn beyond CUDA syntax?**

> Defining boundary behavior, validating binary data, separating ownership and
> UI work, distinguishing asynchronous launch from completion, and presenting
> measurements honestly were as important as writing kernels.

**How did you use assistance?**

> I used AI assistance for implementation, debugging and documentation. My
> responsibility is to understand, review and verify the work, and to be honest
> about the pieces I can explain or still need to study.

Adapt that answer to your actual experience. Do not describe borrowed libraries,
AI-generated implementation or supplied datasets as inventions you created alone.

## 15. Your ownership readiness check

- [ ] I can derive floor-based keys and a centroid without reading the code.
- [ ] I can trace a real frame from its PCD header to both displayed panels.
- [ ] I can explain the CPU container, GPU grouping stages and safe fallback.
- [ ] I can explain which allocations persist and which thread owns them.
- [ ] I can distinguish numerical tolerance from accepting incorrect grouping.
- [ ] I can build and test CPU-only, CUDA/WSL and the verified Windows configuration.
- [ ] I can state exactly what the benchmark includes and excludes.
- [ ] I can distinguish processing FPS, playback FPS and target FPS.
- [ ] I can explain the current limitations and dataset attribution.
- [ ] I have made and verified at least one meaningful change independently.

Unchecked items are a learning plan, not a reason to abandon the project.

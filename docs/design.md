# Design decisions

## Module boundaries and ownership

`PointCloud` owns XYZ doubles. Processing takes const input and returns owned
output. PCD reading, CPU reference, optional CUDA processing, shared metrics and
frame discovery are independent from visualization. Tests generate their own files.
The studio uses a long-lived worker (`apps/processing.hpp`) and futures; only the
UI thread calls GLFW/OpenGL. Jobs are serialized, and exceptions propagate through
futures into visible errors. Closing waits for any current job to finish. The
worker lifetime owns the thread-local CUDA workspace, avoiding per-job teardown.
Host vectors, GPU buffers, windows and contexts use RAII cleanup.

## File input and playback

The app starts empty with CPU/CUDA selection. The in-app browser navigates folders
and filters `.pcd` files; path entry/paste and drop callbacks use the same loading
flow. Input selection processes the selected backend only. Benchmarking opens a
second native GLFW window sharing the main app's loaded frame selection. Current
frame mode uses a snapshot of the loaded point data; sequence mode takes the loaded
timestamp-ordered path list. Parameters and results remain together in this window.
Each click recalculates and automatically creates a separate CSV report. It
warms both, alternates measured calls, compares output, and reports a winner
only if correctness passes. Benchmarks report warmed medians and sample variation;
the main view distinguishes loading/job time from processing latency. Warm-up and
context startup are excluded from warmed benchmark samples and require separate
measurement. No faster-backend claim is guessed from hardware type.

Directory listing and frame discovery run on the worker. The browser caches each
entry's name and type once; sorting and drawing never query the filesystem. This
avoids repeated Windows-drive metadata requests blocking the UI under WSL.

A directory with PCDs forms one clip. A parent without its own PCDs can contain
immediate clip folders, sorted by folder name; deeper trees are not recursively
walked. Decimal frame timestamp stems sort numerically, with lexical fallback.
Playback retains the visible pair and an in-flight processing result rather than
preloading the whole recording. It uses one processing call per frame, a fixed
target rate without dropping frames, and stable camera framing within a clip.
Both panels use the displayed result index rather than a pending frame index.
Exact source timestamp intervals are not replayed. The GUI can repaint during
normal worker processing; both windows pause drawing during benchmark timing.
Building voxel inspection geometry still happens briefly on the UI thread.

## PCD contract

Header-derived SIZE x COUNT offsets handle padding and reordered fields. Supported
XYZ fields are scalar float32 and file byte order is little-endian. Numeric header
entries, dimensions, repeated entries, payload length and coordinates are checked.
Nonfinite frames are rejected instead of silently dropping points. Binary payload
is bulk-read to avoid many small reads through WSL's `/mnt/d`. `memcpy` extracts
coordinates safely without aliasing or alignment assumptions. Other attributes
are skipped, not averaged. ASCII/compressed encodings are explicitly unsupported.

## CPU reference

Voxel origin is zero. Keys are `floor(coordinate / voxel_size)` along X/Y/Z, with
finite/positive size checks and signed-64-bit range checks before conversion.
Conceptually a voxel spans [k*s,(k+1)*s). Ordered maps group points and emit keys
lexicographically. Running weighted means avoid large coordinate sums.
Complexity is O(N log V), with O(V) accumulated state. The same ordered input gives
repeatable results in the same floating-point environment; reordered input can
change rounding. Quantization itself uses finite-precision division.

## CUDA fast path and fallback

Host validation also finds the minimum/maximum voxel key per axis. Extents are
checked using unsigned arithmetic. If the domain product fits uint64, a collision-
free mixed-radix key is formed:

```text
key = ((x - min_x) * extent_y + (y - min_y)) * extent_z + (z - min_z)
```

This preserves lexicographic X/Y/Z order. It is not a hash, truncation, or a
fixed-bit allocation that could collide when coordinates exceed a guessed range.
If spans/product cannot fit, the original full int64 triple-key implementation is
used. Extreme-offset and wide-range tests exercise both cases.

The fast path uses two custom kernels: fused key/aggregate generation and final
centroid emission. CUB radix-sorts key/aggregate pairs, then reduces each run using
count-weighted centroid merging. Device arrays and scratch buffers grow in powers
of two and persist on one thread/device until its exit. They never cache input
results: every call validates and uploads current input. Peak reserved memory is
O(N), with extra capacity for multiple arrays and CUB scratch. Changing CUDA device
on a populated worker is rejected explicitly.

All CUDA runtime and CUB return codes are checked. The occupied-count copy and
final result copy establish completion; host latency is not launch-only time.
Host validation, transfers, growth allocation when needed, kernels and result
production remain within processing latency. The legacy fallback allocates anew
and is not covered by fast-path performance expectations.

## Numerical comparisons

CPU and GPU counts must match, paired centroids must be finite, and sorted outputs
must satisfy `abs(a-b) <= 1e-10 + 1e-10*max(abs(a),abs(b))`. Parallel reduction uses a
different addition tree, so bitwise equality is not required. GPU tests cover
boundaries, negative coordinates, duplicates, multiple sizes, malformed input,
workspace growth/reuse, extreme offsets and range fallback. Real-frame comparisons
add evidence but are not a proof for every floating-point input. Exposing voxel
keys in the public result would improve future membership diagnostics.

## Honest visual comparison

Both panels use the same camera and point size. Full-cloud mode draws every input
or output sample; processing never modifies the original. A larger voxel actually
produces fewer centroids, rather than hiding arbitrary output points.

Voxel detail chooses a dense occupied region (preferably beyond one coordinate
unit from the sensor origin) and shows the same local crop in both panels. Gray
wireframes are its actual occupied voxels. Every global occupied voxel still
produces one output point, even though only local boxes are drawn. Global counts
stay in summary cards and local counts/cropping are labeled explicitly. Empty
cells are omitted. This makes density reduction visible while preserving the
expected overall shape. The local region remains fixed within a clip and refits
at clip transitions, rather than following the densest group every frame.

GLFW/OpenGL was selected after the packaged Open3D GUI failed in its ImGui texture
backend. A built-in bitmap font avoids runtime font resources. WSL rendering on
this laptop uses llvmpipe, independently of NVIDIA CUDA compute. Software drawing
can compete for CPU resources, so in-app benchmarks pause drawing and headless
benchmarks avoid launching rendering work. Native Windows drawing was verified on
AMD Radeon Graphics; this is separate from the selected processing backend.
Compatibility OpenGL 2.1 is appropriate for the prototype, not a claim of
modern high-throughput GPU rendering. See `performance.md` for measured limits.

## Using the core from C++

Link the `cloudforge_core` CMake target and include the public headers. A minimal
CPU integration is:

```cpp
#include "cloudforge/pcd_io.hpp"
#include "cloudforge/point_cloud.hpp"

auto input = cloudforge::load_pcd("frame.pcd");
auto centroids = cloudforge::downsample(input, 0.1);
```

The return value owns its data and the original is retained. For optional backend
dispatch, include `cloudforge/engine.hpp` and call
`cloudforge::process(input, size, cloudforge::Backend::cuda)` only in an appropriate
CUDA-enabled environment. `cuda_backend_compiled()` reports build inclusion, not
a complete runtime device-health check. Errors propagate through exceptions; the
viewer catches job exceptions and displays them. The API does not serialize the
returned cloud or preserve extra source attributes.

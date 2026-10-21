# Recorded performance, profiling and optimization

Measured 21 October 2026 on the development laptop. These are observed results,
not representative claims for other machines or input sizes.

## Environment

- CPU: AMD Ryzen 7 7735HS, 8 cores / 16 logical CPUs visible to WSL.
- GPU: NVIDIA GeForce RTX 4050 Laptop GPU, approximately 6 GB VRAM.
- Windows NVIDIA driver: 595.79; Ubuntu 26.04 under WSL2 kernel
  `6.6.87.2-microsoft-standard-WSL2`.
- GCC 15.2.0, CUDA compiler 13.2.86, CMake 4.2.3, Ninja 1.13.2.
- Release / `-O3 -DNDEBUG`, CUDA architecture 89.
- Dataset files remain on `/mnt/d`; I/O is outside processing timings.
- Desktop renderer: Mesa llvmpipe (LLVM 21.1.8). Viewer was closed during benchmarks.
- Laptop power, temperature, clocks and background tasks were not controlled;
  cross-run variance is expected. No confidence intervals are claimed.

## Baseline protocol

First 30 numerically timestamp-sorted PCDs, from `1710256033.970548000.pcd` through
`1710256036.860217000.pcd`. Input counts range from 55,272 to 56,070.
For every frame, one warm-up per backend and seven measured full processing calls,
alternating CPU/GPU order. A steady clock encloses each complete function call.
CUDA completes the host output copy before returning, so no asynchronous launch-only
timing is reported. Host validation, allocations, transfers, sorting/reduction and
output production are included; file reading and drawing are excluded.

The table reports the median of 30 per-frame median times. Reduction is also a
median across frames. Raw per-frame results are stored in `benchmarks/`.

| Voxel side | Median reduction | CPU ms | CUDA ms | Ratio CPU/CUDA |
|---|---:|---:|---:|---:|
| 0.05 | 54.76% | 6.835 | 7.203 | 0.95x |
| 0.10 | 63.31% | 5.905 | 7.326 | 0.81x |
| 0.20 | 74.93% | 4.603 | 6.857 | 0.67x |

All **90 frame/voxel comparisons passed** with matching counts and centroid
tolerance (absolute `1e-10`, relative `1e-10`). The first frame at voxel 0.1 has
55,518 input points and 20,345 centroids (63.3542% reduction).

## Interpretation of the initial backend

The initial GPU backend was functional but did not beat the CPU median in these runs.
At this input size the implementation pays for host validation, several device
allocations, both transfer directions, and triple-key comparison sorting.
These are plausible overhead sources, not individually profiled conclusions.
No claim that larger clouds will necessarily be faster is made without measuring.

That backend has now been optimized as described below. The baseline CSVs remain
unchanged so the unfavorable initial results are not erased.

## Nsight Systems evidence

The first frame was traced with one warm-up plus seven measured CUDA calls.
Reports: `profile-baseline_cuda_api_sum.csv`,
`profile-baseline_cuda_gpu_kern_sum.csv`, and matching `profile-optimized_*`
summaries in `benchmarks/`. Full `.nsys-rep` files are local ignored build outputs.

| Trace statistic across 8 CUDA invocations | Initial | Optimized |
|---|---:|---:|
| cudaMalloc calls | 64 | 9 |
| cudaFree calls (including teardown) | 64 | 9 |
| explicit cudaStreamSynchronize calls | 104 | 0 |
| GPU kernel launches (runtime launch API calls) | 200 | 80 |

The optimized path still synchronizes through the necessary blocking host copies;
zero stream-synchronize API calls does not mean no synchronization. The nine device
allocations occur during workspace setup, not on every warm invocation. The trace
includes expensive first-context startup (roughly 0.3 s inside its first allocation)
and profiler instrumentation, so trace timings are not the unprofiled benchmark.
The baseline key sort runs merge-sort kernels; the new trace contains CUB radix-sort
kernels and the custom `generate_keys` / `emit_points` kernels.

## Optimized results (unprofiled)

Same first 30 frames and seven-repeat protocol, host validation and both transfer
directions still included. Device memory and CUB scratch space are now reused;
warm-up allocates initial capacity, and subsequent growth allocations remain inside
the measured call if required. Raw data: `optimized-voxel-0.1.csv` and
`optimized-voxel-0.3.csv`.

| Voxel | CPU median ms | CUDA median ms | Ratio of medians CPU/CUDA |
|---|---:|---:|---:|
| 0.1 | 6.457 | 1.937 | 3.33x |
| 0.3 | 3.939 | 2.322 | 1.70x |

All 60 optimized real-frame comparisons passed. The unchanged first-frame counts
at voxel 0.1 are 55,518 input / 20,345 output. At voxel 0.3 they are 55,518 / 9,943
(82.09% reduction). CPU clocks, laptop power/thermal state and background activity
vary; baseline and optimized runs were not simultaneous. The CPU reference was not
changed. These paired-run observations do not establish a universal GPU advantage.

The studio shows first-call costs and total job duration as well as warm medians.
The first CUDA call in a new process can take over one second for runtime/context
and module initialization, even though subsequent calls take a few milliseconds.
For one-off small clouds, startup may outweigh all processing savings.

The improvement combines buffer reuse, fewer synchronization points, fused key
generation and radix sorting. It is not an ablation study assigning an exact
speedup contribution to each change. A future study can isolate those factors.

## Reproduce

```bash
./build/demo/cloudforge_benchmark "DataSets/Oxford-Spires=Dataset/lidar-clouds/lidar-clouds" \
  --frames 30 --repeat 7 --voxel 0.1 --csv benchmarks/reproduced-0.1.csv
```

Change voxel to `0.05` or `0.2` for the other runs. Do not benchmark while playing
the sequence or running another GPU workload. Processing FPS is reciprocal latency,
not sustained video or display FPS.

## Verification performed

- CPU-only Release build: 35 unit tests (voxel algorithm, PCD reader, ordering,
  comparison/metrics and selected-backend/worker behavior).
- CUDA Release build: those 35 plus 6 GPU tests, including capacity growth,
  full-range fallback and large signed offsets.
- Full desktop smoke run: first 30 real frames, matching CPU/GPU results on all;
  screenshot generated at `build/demo/final.bmp` and inspected visually. Studio
  detail comparison separately verified real voxel outlines, a 3,339-point local
  input crop and 66 local centroids; selected-CPU playback confirmed CUDA is not run.
- Native Windows/MSVC, other GPUs, and all 2,867 downloaded frames have not been
  exhaustively validated.


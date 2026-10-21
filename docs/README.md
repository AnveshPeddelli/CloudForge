# cloudForge documentation

cloudForge simplifies recorded 3D point clouds with a voxel grid and replays
matched original/simplified frames. It includes a CPU reference, optional CUDA
processing, a desktop viewer and a repeatable benchmark workflow.

## Choose a starting point

| Your goal | Read |
|---|---|
| Understand the project and what it can do | [Project README](../README.md) |
| Build or run it on Windows or Ubuntu/WSL | [Getting started](getting-started.md) |
| Load data, play clips or interpret graphs | [User guide](user-guide.md) |
| Understand practical uses and impact | [Applications and impact](applications-and-impact.md) |
| Understand implementation decisions | [Design decisions](design.md) |
| Assess or reproduce performance claims | [Performance evidence](performance.md) |

## Evidence and documentation scope

Build and test qualification described here was recorded on **21 October 2026**.
These guides were consolidated on **22 October 2026**. A working build on one
machine is not a guarantee for every compiler, graphics driver or PCD variant.

The repository keeps historical benchmark summaries and the later 900-pair raw
session under [`benchmarks/`](../benchmarks/). They have different schemas and
aggregation methods; the performance guide explains the distinction.

The illustrated PDFs, MP4, subtitles, voice-over script and packaged Windows CPU
app are published under [`output/`](../output/). Screenshots used directly by the
README are kept in `docs/`. Raw Oxford Spires scans and build/intermediate files
are not distributed with the project.

## When updating the implementation

Update the matching guide, preserve CPU-only builds and record the scope of new
test/performance evidence. Do not replace historical measurements with a more
flattering number without retaining their protocol and explaining the new run.

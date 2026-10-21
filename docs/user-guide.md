# User guide

## Input contract

The reader accepts uncompressed, little-endian **binary PCD** with scalar float32
`x`, `y` and `z` fields. Fields may be reordered and the record may include padding
or extra attributes; the header determines coordinate offsets and point stride.
Extra intensity/RGB attributes are skipped. Malformed headers, truncated data,
invalid voxel sizes, nonfinite coordinates and out-of-range voxel keys fail with
errors rather than being silently repaired.

ASCII/compressed PCD, LAS/LAZ, camera videos and direct live sensor feeds are not
supported. cloudForge has no general data-format conversion tool or cloud-file
exporter. The C++ engine returns an owned vector of centroid points for integration.

Coordinate units are taken from your data. Voxel size `0.1` is 0.1 coordinate
units, not automatically 0.1 meters. A centroid is the average of points within a
cell, not necessarily the geometric center of the cell.

## Open and inspect

1. Choose CPU or CUDA. CUDA is selectable only in a CUDA-enabled build.
2. Click **Open PCD / Folder**, paste a path into the top field and press Enter,
   or drop a PCD file onto the main window.
3. The selected backend processes the input on a worker. Both panels receive a
   matched result: original on the left, downsampled on the right.
4. Use Smaller/Larger to change voxel size, then Process Selected. Increasing
   voxel size generally reduces more points while losing more detail.

The browser lists directories first and PCD files by descending byte size. Sizes
are cached during background listing. For a fixed uncompressed point layout,
larger payloads usually mean more points; different field layouts and formats
cannot be compared by file size alone. Playback order is independent of this sort.

### Camera and detail controls

| Action | Control |
|---|---|
| Orbit both views | Left drag |
| Pan both views | Right drag, middle drag or Shift + left drag |
| Zoom | Mouse wheel |
| Reset camera | Reset Camera / R |
| Inspect local voxels | Voxel Detail / V |
| Toggle occupied-cell outlines | Cell Wireframes / G |
| Run selected backend | Process Selected / Enter |
| Open benchmark window | Benchmark Window / C |
| Open browser | O |
| Save screenshot | S; default output `build/cloudforge.bmp` |

The app's no-argument default voxel size is `0.3`; a CLI input defaults to `0.1`
unless `--voxel` is given. Both views use the same camera and point size.

Detail mode displays a fixed local region of the clip, with wireframes for its
actual occupied voxels. It does not draw every global voxel. Global and local
counts are separately labeled. If full clouds look alike, that can be expected:
downsampling changes sampling density while aiming to preserve broad shape.

## Play a recording

Open the directory containing the frames and choose **Use this folder**. A sequence
starts playing automatically. If you already loaded one PCD, **Play Clip** loads
other PCDs in its containing directory and starts at the selected file. A folder
with only one frame produces an explicit message instead of artificial motion.

```text
one-clip/
  1710256033.970548000.pcd
  1710256034.070343000.pcd
  ...
```

For multiple clips, use a parent with no PCD files of its own:

```text
recordings/
  clip-a/
    1.0.pcd
    2.0.pcd
  clip-b/
    1.0.pcd
    2.0.pcd
```

Clip folders play in folder-name order. Within each clip, numeric timestamp stems
sort numerically, with lexical fallback for other names. Only immediate clip
folders are scanned; arbitrary nested directory trees are not recursively walked.
If a folder has its own PCDs, those are treated as its clip.

The bar below the views has Play/Pause (Space), Previous, Next (N), Restart, Loop
and target-FPS minus/plus controls. The clip, filename and visible frame count are
shown above it. Loop wraps after the last frame. Camera framing stays fixed within
a clip and refits between clips.

Playback runs **one selected-backend processing call per frame**. It does not run
benchmark repetitions. The target is a configurable fixed rate, initially 10 FPS;
source timestamps determine order but not exact replay intervals. Actual FPS may
be lower because of loading, processing or rendering. No frames are skipped to
manufacture the target. A recording with little physical motion can look nearly
static even when the frame counter advances.

## Benchmark the loaded input

Open **Benchmark Window**. The separate window starts at 1100 x 780 and can be
resized, with a minimum of 960 x 740. Settings and results stay together.

| Setting | Meaning |
|---|---|
| This Frame | Benchmark a snapshot of the currently loaded frame. No new PCD selection is required. |
| Sequence | Benchmark the loaded frame list, limited by Max Sequence Frames. |
| Voxel Side Length | Sampling size used for this run; completion also updates the main view. |
| Max Sequence Frames | Maximum loaded sequence frames to include; not relevant to one-frame mode. |
| Repeats per Frame | Measured calls per backend per frame; form requires at least 10, default 30. |
| Warm-ups per Backend | Untimed initialization/reuse calls per frame; default three. |

Click a field to edit; Ctrl+A clears it, Ctrl+V pastes and Enter ends editing.
Every **Run Again / Benchmark** click recalculates. A new CSV is saved under
`build/benchmarks/`; an existing file never prevents computation or gets overwritten.

Both windows pause drawing during measurement to reduce interference, while events
and cancellation remain active. Progress appears in the benchmark window title.
Cancel is cooperative and waits for the current unit of work; it does not abort a
running kernel. Closing this window hides it; closing the main app ends the app.

### Read the results

- **Top graph:** X is frame number in temporal order; Y is processing latency in
  milliseconds. Blue is CPU, orange is CUDA. Lower is better. Bars are used for
  one frame; curves for a sequence. Whiskers are P10-P90 across repetitions.
- **Bottom graph:** X is frame number; Y is CPU median divided by CUDA median.
  Above the 1x reference favors CUDA; below favors CPU. Hover a bar or nearby
  plotted point to see its value; latency tooltips also show P10/P90.
- **CSV Samples:** inspect individual measurements with pagination.
- **Main screen:** after completion, the matching visible frame's benchmark
  medians, ratio and processing FPS replace its quick estimate.

P10-P90 describes variation, not a confidence interval. The header's aggregate
uses pooled sample medians; it may differ from one frame's ratio. Partial, failed
and CPU-only runs do not receive a GPU speedup claim. Empty frames have no speedup
point. Large plots use a labeled display stride; raw data is retained.

The [performance guide](performance.md) defines the measured boundary and hardware.
Startup/warm-up is excluded from these warmed measurements. Do not interpret
processing FPS as total playback FPS or expect a fixed ratio on every click.

## Command-line examples

Replace example paths with supported files. CUDA examples require a CUDA build.

```bash
# View a sequence with selected settings.
./build/demo/cloudforge_viewer "DataSets/my-clip" --backend cuda --voxel 0.3 --frames 60 --fps 10

# Headless benchmark: measures both backends when CUDA is compiled, otherwise CPU only.
./build/demo/cloudforge_benchmark "DataSets/my-clip" --voxel 0.1 --frames 30 --repeat 30 --warmup 3 --csv build/my-session.csv

# Open an existing schema 1 raw report without recomputing it.
./build/demo/cloudforge_viewer . --results-csv benchmarks/session-2026-10-21-raw.csv

# Capture one inspected frame, then exit.
./build/demo/cloudforge_viewer "DataSets/my-clip/frame.pcd" --backend cuda --voxel 0.3 --detail on --smoke-frames 1 --screenshot build/detail.bmp
```

The headless CLI defaults to five repeats; the benchmark form defaults to 30.
An existing requested CLI CSV path receives a distinct sibling filename. Save
paths must have an existing parent directory, except the automatic report directory.
The current viewer accepts schema 1 raw CSV; historical per-frame summary CSVs
cannot reconstruct repeated samples and are not supported by that importer.

Optional viewer flags include `--results-view graphs|samples` and `--capture-dir`
for BMP export after completed frames. Export overhead is not a processing
benchmark. `--frames` defaults to 30 for CLI inputs; the empty studio permits all
discovered frames. A file's Play Clip action discovers its containing sequence.

## Troubleshooting

| What you see | Interpretation / action |
|---|---|
| Frame 1 of 1, no motion | Use Play Clip or load a folder containing consecutive recorded frames. |
| Counter advances, geometry barely moves | Inspect a longer interval; the source may be largely stationary. |
| Similar-looking original and output | Check point reduction, increase voxel size or inspect local detail. |
| GPU is slower | This can be valid, especially for small workloads or startup. Use a Release benchmark and inspect variation. |
| No CUDA choice | Use a CUDA-enabled build with a compatible GPU/toolkit. Native Windows CPU builds do not include CUDA. |
| UI pauses drawing during a benchmark | Intentional timing isolation; watch the title progress and use Cancel if needed. |
| Actual playback FPS below target | Loading/drawing may dominate even when processing is fast. |
| Unsupported report / data error | Confirm schema 1 raw CSV or the supported binary PCD contract. |

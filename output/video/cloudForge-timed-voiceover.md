# cloudForge - timed voice-over script

Matches the existing 2:00 video, `cloudForge-demo.mp4`.

Read only the quoted dialogue. The scene descriptions and delivery notes are for
you. Aim for a calm, conversational pace, roughly 130-145 words per minute. Start
each paragraph at its timestamp and leave a short pause at the end. Record scenes
separately if that makes editing easier; do not rush to fill every second.

## 00:00-00:08 | Project introduction

**On screen:** cloudForge title.

> Meet cloudForge: my C++ and CUDA application for simplifying recorded point clouds and visualizing the results.

**Delivery:** Say "C plus plus" and "three-dimensional" naturally. Introduce the
project before the title card ends.

## 00:08-00:18 | Load and inspect

**On screen:** the application and its two visualization panels.

> Each PCD file stores one frame of three-dimensional points. Load a file or sequence to see original and simplified versions together.

**Delivery:** Say "P-C-D" as individual letters. Emphasize "original" and
"simplified" while the audience sees the two panels.

## 00:18-00:30 | Explain voxel downsampling

**On screen:** the illustrated voxel grid.

> The algorithm divides space into small cells called voxels. Points inside the same cell are grouped together and replaced by their average position, called the centroid.

**Delivery:** Briefly pause after "voxels." The diagram is an explanatory 2D slice
of the same rule used in all three dimensions.

## 00:30-00:42 | Native Windows playback

**On screen:** actual frames from the native Windows CPU application.

> Here is the native Windows application processing on the CPU. Recorded frames play in order, with the original on the left and the simplified result on the right.

**Delivery:** The two views are the same frame before and after processing, not
two independent recordings.

## 00:42-00:54 | CUDA playback in WSL

**On screen:** actual frames processed using CUDA inside WSL.

> This version processes the same recording with CUDA on the NVIDIA GPU, inside WSL. The processing backend changes, while the original and simplified views stay synchronized.

**Delivery:** Say "W-S-L." Do not describe this as native Windows CUDA: that build
has not been verified.

## 00:54-01:06 | Voxel detail

**On screen:** occupied-cell wireframes and centroids.

> Zooming into a small region makes the difference clearer. The wireframes show occupied voxels, and each voxel contributes one centroid. Larger voxels reduce more points, but remove finer detail.

**Delivery:** Slow slightly on the final tradeoff. The local inspection crop is
different from the full-cloud counts above it.

## 01:06-01:18 | Engineering and architecture

**On screen:** processing architecture diagram.

> A tested CPU reference anchors the CUDA pipeline: packed keys, radix sorting and reduction. A background worker handles processing, keeping it separate from drawing.

**Delivery:** You can say "reduces each group" instead of "reduction" if that is
easier to pronounce. The underlying technical meaning stays the same.

## 01:18-01:28 | Benchmark method

**On screen:** the benchmark results window.

> Benchmarking pauses rendering, warms both backends and alternates their measurement order. Output pairs are checked, and timings are saved to CSV.

**Delivery:** Say "C-S-V." Stress that correctness is checked alongside timing.

## 01:28-01:40 | Measured result

**On screen:** the measured latency chart and observed speedup.

> Across nine hundred measurements, CPU took five point three milliseconds and CUDA one point two: roughly four point four times faster in this session.

**Delivery:** These numbers belong to the dedicated benchmark session shown here.
Earlier app screenshots may show other timings. Avoid saying "always faster."

## 01:40-01:50 | Platform testing

**On screen:** Windows and WSL qualification summary.

> Windows CPU passed thirty-five tests; WSL CUDA passed forty-one. Native Windows CUDA remains unverified until its toolkit is installed.

**Delivery:** Keep the distinction between the two platforms clear. These are
different build configurations, not seventy-six distinct test cases.

## 01:50-02:00 | Close and next steps

**On screen:** next experiments and dataset credits.

> cloudForge connected correctness, performance and usability. Next, I want smoother playback, individual GPU-stage profiling, and verified CUDA support directly on Windows.

**Delivery:** Finish before 02:00 so the closing credits remain visible.

## Recording and editing notes

- Pronunciation: CUDA is commonly said "COO-duh"; voxel sounds like "VOCK-sul";
  centroid sounds like "SEN-troyd"; radix sounds like "RAY-diks."
- Record in a quiet room, with consistent microphone distance. Leave a little
  silence before and after each take for clean edits.
- Place each take at its scene's start time. If a take is too long, re-record it
  or shorten the wording instead of making the voice unnaturally fast.
- The MP4 is silent. Add your recording as an audio track in your video editor.
  Keep the existing captions; the accompanying SRT is an editable caption copy.
- The playback portions replay six seconds of actual captured frames twice at
  10 FPS. The video is an edited walkthrough, not one uninterrupted screen recording.
- Only say you understand a technique once you can explain it. The separate
  developer ownership guide provides the code walkthrough and practice tasks.


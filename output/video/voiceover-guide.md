# cloudForge voice-over guide

The MP4 is intentionally silent, with burned-in subtitles. Record your voice-over against these scene timings. The SRT is an editable copy of the captions. Playback captures are replayed at 10 FPS; each six-second capture appears twice. Timing numbers shown in captures are illustrative, not the headline benchmark session.

00:00:00,000 - 00:00:08,000 | cloudForge
A C++20 point-cloud player with an optional CUDA processing backend.

00:00:08,000 - 00:00:18,000 | 01 / Load and inspect
Load a PCD file or a sequence. Both views always show the same completed frame.

00:00:18,000 - 00:00:30,000 | 02 / The voxel rule
Divide space into cells. Group points by cell, then output one centroid per occupied voxel.

00:00:30,000 - 00:00:42,000 | 03 / Native Windows playback
Actual native Windows CPU app frames, replayed at 10 FPS. Both views stay synchronized.

00:00:42,000 - 00:00:54,000 | 04 / CUDA playback in WSL
Actual WSL CUDA app frames. The rendering backend is separate from the compute backend.

00:00:54,000 - 00:01:06,000 | 05 / Look inside a voxel
Local detail makes density changes visible. The crop counts are separate from the global counts.

00:01:06,000 - 00:01:18,000 | 06 / Engineer the pipeline
A tested CPU reference anchors the CUDA implementation. A worker keeps processing off the UI thread.

00:01:18,000 - 00:01:28,000 | 07 / Benchmark honestly
Warm up, alternate backend order, check every output pair, and keep every measured sample in CSV.

00:01:28,000 - 00:01:40,000 | 08 / Read the evidence
30 frames, 30 repetitions, 900 checks passed. This result is specific to the workload and laptop.

00:01:40,000 - 00:01:50,000 | 09 / Platform qualification
35 native Windows tests passed. The WSL CUDA build passed 41. Native Windows CUDA still needs its toolkit.

00:01:50,000 - 00:02:00,000 | cloudForge / Next experiments
Next: workload scaling, bounded prefetch, per-stage profiling and native Windows CUDA validation.

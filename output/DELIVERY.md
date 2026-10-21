# Delivery notes

- pdf/cloudForge-technical-report.pdf: 8-page engineering report.
- pdf/cloudForge-portfolio-article.pdf: 4-page illustrated portfolio article draft.
- video/cloudForge-demo.mp4: 120 seconds, H.264, 1920x1080, 15 FPS, intentionally silent.
- video/cloudForge-demo.srt: editable copy of burned-in captions.
- video/voiceover-guide.md: scene timings and caption/narration prompts.
- cloudForge-Windows-CPU.zip: tested native Windows CPU application; extract and run Launch-cloudForge.cmd.
- evidence/: raw 900-pair timing session, test logs, hardware, pinned dependency revisions and Windows CUDA blocker.

The MP4 includes six seconds of captured native Windows playback and six seconds of
WSL CUDA playback; each is replayed twice at 10 FPS in its respective scene. Other
scenes use app captures and explanatory diagrams. This is a narrated-ready walkthrough,
not a screen recording of one continuous interaction. No voice-over or music is included.

Current benchmark: 30 frames x 30 repetitions, voxel 0.1, 3 warm-ups per backend/frame.
CPU median 5.3434285 ms; CUDA median 1.204919 ms; pooled median ratio 4.4346786x.
The CPU baseline is single-threaded std::map. Validation, allocation when needed,
transfers and GPU completion are timed; I/O, rendering, warm-ups and checks are not.
No constant speedup or universally fastest backend is claimed.

Native Windows CPU: 35 tests passed and 60 frames played. WSL CUDA: 41 tests passed.
Native Windows CUDA: configure attempted, blocked by absent Windows CUDA toolset.
No Windows CUDA Toolkit was installed. All downloaded source/media packages for this
work were placed in project build folders.

Data attribution: Oxford Spires Dataset, Tao et al., https://dynamic.robots.ox.ac.uk/datasets/oxford-spires/
Dataset-derived visual media and illustrated documents: CC BY-NC-SA 4.0.
Code license remains MIT. No raw data is bundled. Review the article before publication.
No commits, pushes or portfolio publication were performed when these artifacts
were originally prepared. Later repository publication is a separate action.

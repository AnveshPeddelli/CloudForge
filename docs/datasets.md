# Dataset download and preparation

## Recommended source: Oxford Spires

The project's demonstrated input comes from Oxford Spires. Use the publishers'
links so that download locations and terms can be checked at the source:

- [Official website](https://ori-drs.github.io/datasets/oxford-spires/)
- [Hugging Face file browser](https://huggingface.co/datasets/ori-drs/oxford_spires_dataset/tree/main)
- [Official repository and download instructions](https://github.com/ori-drs/oxford_spires_dataset#download)

The website also provides access through Google Drive. Follow its current link
rather than relying on a copied third-party archive.

## Download only what you need

1. Open the Hugging Face file browser, or the Google Drive link on the website.
2. Navigate to `sequences/` and choose one recording.
3. Open its `raw/` folder and find the LiDAR cloud folder/archive, named
   `lidar-clouds` (archive suffixes may vary).
4. Download and extract it. Start with a short consecutive subset of frames to
   keep initial loading and benchmarking manageable.
5. Locate the actual timestamp-named `.pcd` files. Archive extraction can add an
   extra folder level; select the folder containing the frames in cloudForge.

For this workflow, you do not need camera images, ROS bags, calibration,
ground-truth maps or reconstruction benchmark bundles. A merged static map is
also different from a sequence of recorded frames.

## Store data locally

Use the ignored `DataSets/` directory at the repository root. For example:

```text
CloudForge/
  DataSets/
    oxford-spires/
      clip-01/
        1710256033.970548000.pcd
        ...more timestamp-named frames...
```

Example locations on the development machine:

- Windows: `D:\Annie-Proj\CloudForge\DataSets\oxford-spires\clip-01`
- Ubuntu/WSL: `/mnt/d/Annie-Proj/CloudForge/DataSets/oxford-spires/clip-01`

Your chosen folder names may differ. Keep archives and scans out of Git; they
are downloaded separately from the source code.

## Load and verify

Launch the viewer, select **Open PCD / Folder**, navigate to the extracted frame
folder and choose **Use this folder**. Check that the original point count is
nonzero, process with CPU first, then select CUDA if available. Use multiple
consecutive frames from the same recording to inspect movement during playback.

For a CLI check in Ubuntu, substitute an actual filename:

```bash
./build/cpu-release/cloudforge "DataSets/oxford-spires/clip-01/1710256033.970548000.pcd"
```

Use your build's executable path if it differs. See [Getting started](getting-started.md)
and the [user guide](user-guide.md) for launch paths and controls.

## Other data and compatibility

You can use other providers' recordings or your own sensor exports if they match
the loader: **uncompressed little-endian binary PCD with scalar float32 x, y, z**.
Additional fields are skipped. A `.pcd` extension alone does not guarantee
compatibility; check the header and the loader's error message.

ASCII PCD, compressed PCD, LAS/LAZ, PLY, ROS bags and ordinary MP4 videos cannot be
loaded directly. Convert externally to the supported representation if needed;
cloudForge does not provide a format converter. A normal camera video does not
contain measured LiDAR coordinates. File size alone is not a reliable point-count
comparison across different formats and field layouts.

## Terms and attribution

Oxford Spires is published under **CC BY-NC-SA 4.0**, with stated non-commercial
academic-use terms. Read the [publisher's license section](https://ori-drs.github.io/datasets/oxford-spires/#license)
before using or redistributing it. Attribute Tao et al. and follow the publisher's
citation instructions. The repository's MIT code license does not relicense the
scans or grant commercial rights to them.

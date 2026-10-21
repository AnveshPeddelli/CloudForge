# Applications and impact

## The practical problem

A dense point cloud may contain many samples describing nearby parts of the same
surface. Voxel downsampling provides a spatially defined reduction: divide space
into cells and approximate each occupied cell by the centroid of its samples.
This can create a smaller point set for inspection or later processing, while
deliberately sacrificing fine detail.

This is an established preprocessing technique rather than an invention specific
to cloudForge. PCL's official [VoxelGrid tutorial](https://github.com/PointCloudLibrary/pcl/blob/master/doc/tutorials/content/voxel_grid.rst)
describes centroid-based voxel reduction. Open3D's official
[global registration tutorial](https://www.open3d.org/docs/release/tutorial/pipelines/global_registration.html)
uses downsampling before normal estimation and feature extraction. These references
provide context; cloudForge does not depend on their filter implementations or
implement their registration pipelines.

## Where this project could fit

The workflows below are proposed integrations and examples of relevance. No
industrial deployment, customer outcome or domain certification is claimed.

### Industrial scan review

An engineer reviewing compatible equipment or facility scans could compare the
original scan against several voxel sizes before passing a smaller point set to
another tool. Paired views and local wireframes make the loss of sampling detail
visible rather than presenting a reduction percentage alone.

This is not defect detection, dimensional inspection or tolerance certification.
Small defects and thin features may disappear. Any inspection application needs
its own geometric error limits, test objects and acceptance criteria.

### Construction and building surveys

A surveying or construction workflow could use the app to explore recorded scans
and study a sampling size before integration into a geometry-processing system.
The reusable C++ result is a potential input to that system.

The current app does not interpret BIM objects, compare construction progress,
register scans into one coordinate frame or export reduced cloud files. Units,
registration, coordinate conventions, format adapters and integration must be
handled separately. LAS/LAZ and arbitrary scanner formats are not accepted directly.

### Cultural heritage and site documentation

Recorded architectural/site geometry can illustrate which details remain after
sampling and which are removed. Students or researchers could compare parameter
choices while retaining the original scan as the reference.

Downsampling is not an archival replacement for the original data. Preservation
requirements and data permissions must be established separately. The Oxford
Spires demo dataset has non-commercial-use terms; the code's MIT license does not
override them.

### Research preprocessing

The engine can return a reduced XYZ cloud for an external experiment in normal
estimation, registration, feature extraction or reconstruction. A deterministic
CPU reference and a tested GPU path make it useful for studying processing cost
and numerical agreement under different workloads.

Those downstream algorithms are not implemented here. Fewer input points do not
automatically imply faster total processing or acceptable reconstruction error.
Both must be measured with the real downstream pipeline.

### Education and engineering practice

cloudForge connects binary file parsing, signed spatial indexing, floating-point
behavior, RAII, optional dependencies, GPU libraries, worker-thread ownership,
visualization and experimental measurement. Its value as a learning project is
the ability to explain and change these pieces, not just display a GPU speedup.
The [ownership guide](developer-ownership-guide.md) gives practical exercises.

## What has actually been demonstrated

| Evidence | Meaning | What it does not establish |
|---|---|---|
| 55,518 input points -> 9,943 centroids at voxel 0.3 | An 82.09% point-count reduction on one real frame. | The same reduction on every scan; 82.09% lower file size or peak application memory. |
| CPU median 5.343 ms; CUDA median 1.205 ms | A 4.43x ratio of pooled medians in the documented 900-pair WSL session. | A universal speedup, kernel-only latency, or superiority over an optimized parallel CPU baseline. |
| All 900 output comparisons passed | Counts and coordinates agreed under the configured comparison checks on those inputs. | Preservation of every feature or exhaustive proof for every numerical case. |
| 35 native Windows CPU tests and 41 WSL CUDA tests passed | Recorded build/test coverage for those configurations, with 35 tests shared between them. | Native Windows CUDA support or certification for all drivers and hardware. |
| Real frame playback captured on both tested configurations | The app can display matched original/simplified sequences. | Exact source-time playback or a guaranteed frame rate for large recordings. |

See [performance evidence](performance.md) for the raw CSV, protocol and limits.

## Potential impact, with the missing measurements made explicit

**Geometry workload:** fewer centroid records could reduce work in a downstream
algorithm whose cost depends on input size. Measure its total runtime and accuracy
with and without downsampling before claiming a benefit.

**Visualization:** the app helps explain density/detail tradeoffs and compare
parameters. Rendering speed is not proven to improve simply because output count
falls; the original, UI and other geometry are still drawn.

**Data movement and storage:** a serialized reduced cloud could be smaller, but
cloudForge currently has no point-cloud exporter. Encodings, attributes, headers
and compression affect actual bytes. The app retains original data and GPU
workspace, so reduced output count is not a peak-memory measurement.

**Engineering reliability:** explicit contracts, tests and raw benchmark records
make changes easier to review. Test coverage is evidence, not a production
reliability guarantee.

No measurements of energy consumption, carbon impact, business cost savings,
inspection productivity or customer outcomes have been performed. These should
not be inferred from the point reduction or timing ratio.

## Validate a real integration

1. Choose representative recordings and confirm permission, units and formats.
2. Define the detail/accuracy requirement before choosing a voxel size.
3. Measure reduction, geometric error and the task-specific quality metric.
4. Measure complete pipeline latency, peak host/device memory and actual I/O.
5. Compare appropriate CPU and GPU alternatives, including initialization costs.
6. Test difficult inputs and retain the original data as a reference.

This turns a promising preprocessing technique into evidence for a particular
application, rather than treating a demonstration benchmark as an impact study.

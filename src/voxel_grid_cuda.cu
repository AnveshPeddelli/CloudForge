#include "cloudforge/voxel_grid_cuda.hpp"

#include <thrust/copy.h>
#include <thrust/device_vector.h>
#include <thrust/reduce.h>
#include <thrust/sort.h>
#include <thrust/transform.h>

#include <cstddef>
#include <stdexcept>
#include <cmath>
#include <cstdint>
#include <cub/cub.cuh>
#include <cuda_runtime.h>
#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <climits>

namespace cloudforge
{
    namespace
    {
        struct VoxelKey
        {
            std::int64_t x{};
            std::int64_t y{};
            std::int64_t z{};
        };

        struct KeyLess
        {
            __host__ __device__
            bool operator()(const VoxelKey& a, const VoxelKey& b) const
            {
                if(a.x != b.x)
                {
                    return a.x< b.x;
                }

                if(a.y != b.y)
                {
                    return a.y< b.y;
                }

                return a.z< b.z;
            }
        };

        struct KeyEqual
        {
            __host__ __device__
            bool operator()(const VoxelKey& a, const VoxelKey& b) const
            {
                return a.x == b.x && a.y == b.y && a.z == b.z;
            }
        };

        struct MakeVoxelKey
        {
            double voxel_size;

            __host__ __device__
            VoxelKey operator()(const Point& point) const
            {
                return{
                    static_cast<std::int64_t>(floor(point.x / voxel_size)),
                    static_cast<std::int64_t>(floor(point.y / voxel_size)),
                    static_cast<std::int64_t>(floor(point.z / voxel_size))
                };
            }
        };

        struct Aggregate
        {
            double x{};
            double y{};
            double z{};
            std::size_t count{};
        };

        struct MakeAggregate
        {
            __host__ __device__
            Aggregate operator()(const Point& point) const
            {
                return {point.x, point.y, point.z, 1};
            }
        };

        struct MergeAggregates
        {
            __host__ __device__
            Aggregate operator()(const Aggregate& a, const Aggregate& b) const
            {
                const auto count = a.count + b.count;

                const double weight_a = static_cast<double>(a.count) / static_cast<double>(count);

                const double weight_b = static_cast<double>(b.count) / static_cast<double>(count);

                return {
                    a.x * weight_a + b.x * weight_b,
                    a.y * weight_a + b.y * weight_b,
                    a.z * weight_a + b.z * weight_b,
                    count
                };
            }
        };
        struct MakePoint
        {
            __host__ __device__
            Point operator()(const Aggregate& aggregate) const
            {
                return {aggregate.x, aggregate.y, aggregate.z};
            }
        };
    }

    PointCloud downsample_cuda_legacy(const PointCloud& input, double voxel_size)
    {
        if (!std::isfinite(voxel_size) || voxel_size <= 0.0)
        {
            throw std::invalid_argument("Voxel size must be finite and positive");
        }

        //validate before any floating-point-to-integer conversion on the GPU
        const auto validate_coordinate = [voxel_size](double coordinate)
        {
            if(!std::isfinite(coordinate))
            {
                throw std::invalid_argument("Point coordinates must be finite");
            }

            const double index = std::floor(coordinate / voxel_size);

            if(!std::isfinite(index) || index < -0x1p63 || index >= 0x1p63)
            {
                throw std::out_of_range("Voxel index exceeds int64 range");
            }
        };

        for(const auto& point : input)
        {
            validate_coordinate(point.x);
            validate_coordinate(point.y);
            validate_coordinate(point.z);
        }

        if(input.empty())
        {
            return {};
        }

        const auto count = input.size();

        //copy input from CPU memory into GPU memory.
        thrust::device_vector<Point> device_points(input.begin(), input.end());
        thrust::device_vector<VoxelKey> keys(count);
        thrust::device_vector<Aggregate> aggregates(count);

        //Calculate one voxel key and one initial aggregate per point
        thrust::transform(device_points.begin(), device_points.end(), keys.begin(), MakeVoxelKey{voxel_size});
        thrust::transform(
            device_points.begin(),
            device_points.end(),
            aggregates.begin(),
            MakeAggregate{});

        //Bring points belonging to the same voxel together.
        thrust::stable_sort_by_key(keys.begin(), keys.end(), aggregates.begin(), KeyLess{});

        thrust::device_vector<VoxelKey> output_keys(count);
        thrust::device_vector<Aggregate> output_aggregates(count);

        //Combine each consecutive group into one centroid.
        const auto ends = thrust::reduce_by_key(
            keys.begin(),
            keys.end(),
            aggregates.begin(),
            output_keys.begin(),
            output_aggregates.begin(),
            KeyEqual{},
            MergeAggregates{}
        );

        const auto occupied = static_cast<std::size_t>(ends.second - output_aggregates.begin());
        
        thrust::device_vector<Point> device_output(occupied);

        thrust::transform(
            output_aggregates.begin(),
            output_aggregates.begin() + occupied,
            device_output.begin(),
            MakePoint{});

        //Copy simplified points back into CPU-owned memory.
        PointCloud output(occupied);

        thrust::copy(device_output.begin(), device_output.end(), output.begin());
        
        return output;
    }

    namespace {
    void checked(cudaError_t error, const char* operation) {
        if (error != cudaSuccess)
            throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(error));
    }
    template<class T> struct DeviceBuffer {
        T* data{};
        std::size_t capacity{};
        ~DeviceBuffer() { if (data) cudaFree(data); }
        DeviceBuffer() = default;
        DeviceBuffer(const DeviceBuffer&) = delete;
        DeviceBuffer& operator=(const DeviceBuffer&) = delete;
        void reserve(std::size_t count) {
            if (count <= capacity) return;
            std::size_t size = 1;
            while (size < count) {
                if (size > std::numeric_limits<std::size_t>::max() / 2) { size = count; break; }
                size *= 2;
            }
            if (size > std::numeric_limits<std::size_t>::max() / sizeof(T)) throw std::length_error("CUDA buffer size overflow");
            T* next{};
            checked(cudaMalloc(reinterpret_cast<void**>(&next), size * sizeof(T)), "cudaMalloc");
            if (data) cudaFree(data);
            data = next; capacity = size;
        }
    };
    struct Workspace {
        DeviceBuffer<Point> points;
        DeviceBuffer<std::uint64_t> keys, sorted_keys, unique_keys;
        DeviceBuffer<Aggregate> values, sorted_values, reduced;
        DeviceBuffer<int> occupied;
        DeviceBuffer<unsigned char> scratch;
        int device{-1};
        void reserve(std::size_t n) {
            int current{}; checked(cudaGetDevice(&current), "cudaGetDevice");
            if (device != -1 && device != current) throw std::runtime_error("CUDA workspace is bound to one device per thread");
            device = current;
            points.reserve(n); keys.reserve(n); sorted_keys.reserve(n); unique_keys.reserve(n);
            values.reserve(n); sorted_values.reserve(n); reduced.reserve(n); occupied.reserve(1);
        }
    };
    struct Packing {
        std::int64_t x{}, y{}, z{};
        std::uint64_t ny{}, nz{};
    };
    __global__ void generate_keys(const Point* points, std::uint64_t* keys, Aggregate* values,
                                  std::size_t count, double size, Packing p) {
        const auto i = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
        if (i >= count) return;
        const auto point = points[i];
        const auto x = static_cast<std::int64_t>(floor(point.x / size));
        const auto y = static_cast<std::int64_t>(floor(point.y / size));
        const auto z = static_cast<std::int64_t>(floor(point.z / size));
        const auto dx = static_cast<std::uint64_t>(x) - static_cast<std::uint64_t>(p.x);
        const auto dy = static_cast<std::uint64_t>(y) - static_cast<std::uint64_t>(p.y);
        const auto dz = static_cast<std::uint64_t>(z) - static_cast<std::uint64_t>(p.z);
        keys[i] = (dx * p.ny + dy) * p.nz + dz;
        values[i] = {point.x, point.y, point.z, 1};
    }
    __global__ void emit_points(const Aggregate* values, Point* points, std::size_t count) {
        const auto i = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
        if (i < count) points[i] = {values[i].x, values[i].y, values[i].z};
    }
    }

    PointCloud downsample_cuda(const PointCloud& input, double voxel_size) {
        if (!std::isfinite(voxel_size) || voxel_size <= 0)
            throw std::invalid_argument("Voxel size must be finite and positive");
        if (input.empty()) return {};
        std::array<std::int64_t, 3> low{INT64_MAX, INT64_MAX, INT64_MAX};
        std::array<std::int64_t, 3> high{INT64_MIN, INT64_MIN, INT64_MIN};
        for (const auto& point : input) {
            const std::array<double, 3> coordinates{point.x, point.y, point.z};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (!std::isfinite(coordinates[axis])) throw std::invalid_argument("Point coordinates must be finite");
                const double index = std::floor(coordinates[axis] / voxel_size);
                if (!std::isfinite(index) || index < -0x1p63 || index >= 0x1p63)
                    throw std::out_of_range("Voxel index exceeds int64 range");
                const auto key = static_cast<std::int64_t>(index);
                low[axis] = std::min(low[axis], key); high[axis] = std::max(high[axis], key);
            }
        }
        std::array<std::uint64_t, 3> dimensions{};
        std::uint64_t product = 1;
        bool packed = input.size() <= static_cast<std::size_t>(INT_MAX);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto span = static_cast<std::uint64_t>(high[axis]) - static_cast<std::uint64_t>(low[axis]);
            if (span == UINT64_MAX) { packed = false; break; }
            dimensions[axis] = span + 1;
            if (product > UINT64_MAX / dimensions[axis]) { packed = false; break; }
            product *= dimensions[axis];
        }
        // Arbitrary int64 coordinate ranges retain the full triple-key path.
        if (!packed) return downsample_cuda_legacy(input, voxel_size);
        int bits = 1;
        for (auto max_key = product - 1; max_key >>= 1;) ++bits;
        thread_local Workspace workspace;
        auto& w = workspace;
        const auto n = input.size();
        const int items = static_cast<int>(n);
        w.reserve(n);
        checked(cudaMemcpy(w.points.data, input.data(), n * sizeof(Point), cudaMemcpyHostToDevice), "Upload points");
        generate_keys<<<static_cast<unsigned>((n + 255) / 256), 256>>>(w.points.data, w.keys.data, w.values.data,
            n, voxel_size, Packing{low[0], low[1], low[2], dimensions[1], dimensions[2]});
        checked(cudaGetLastError(), "Generate voxel keys");
        std::size_t sort_bytes{}, reduce_bytes{};
        checked(cub::DeviceRadixSort::SortPairs(nullptr, sort_bytes, w.keys.data, w.sorted_keys.data,
            w.values.data, w.sorted_values.data, items, 0, bits), "Query radix workspace");
        checked(cub::DeviceReduce::ReduceByKey(nullptr, reduce_bytes, w.sorted_keys.data, w.unique_keys.data,
            w.sorted_values.data, w.reduced.data, w.occupied.data, MergeAggregates{}, items), "Query reduction workspace");
        w.scratch.reserve(std::max(sort_bytes, reduce_bytes));
        checked(cub::DeviceRadixSort::SortPairs(w.scratch.data, sort_bytes, w.keys.data, w.sorted_keys.data,
            w.values.data, w.sorted_values.data, items, 0, bits), "Radix sort");
        checked(cub::DeviceReduce::ReduceByKey(w.scratch.data, reduce_bytes, w.sorted_keys.data, w.unique_keys.data,
            w.sorted_values.data, w.reduced.data, w.occupied.data, MergeAggregates{}, items), "Reduce voxels");
        int occupied{};
        checked(cudaMemcpy(&occupied, w.occupied.data, sizeof(occupied), cudaMemcpyDeviceToHost), "Read occupied count");
        if (occupied <= 0 || occupied > items) throw std::runtime_error("Invalid occupied voxel count from CUDA");
        emit_points<<<static_cast<unsigned>((static_cast<std::size_t>(occupied) + 255) / 256), 256>>>(w.reduced.data, w.points.data, static_cast<std::size_t>(occupied));
        checked(cudaGetLastError(), "Emit centroids");
        PointCloud output(static_cast<std::size_t>(occupied));
        checked(cudaMemcpy(output.data(), w.points.data, output.size() * sizeof(Point), cudaMemcpyDeviceToHost), "Download centroids");
        return output;
    }
}

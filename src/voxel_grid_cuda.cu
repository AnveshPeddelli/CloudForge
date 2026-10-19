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

    PointCloud downsample_cuda(const PointCloud& input, double voxel_size)
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
}

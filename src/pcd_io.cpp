#include "cloudforge/pcd_io.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <cstdint>

#include <cstddef>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cloudforge
{
    namespace
    {
        struct Field
        {
            std::string name;
            std::size_t size{};
            char type{};
            std::size_t count{1};
            std::size_t offset{};
        };

        struct Header
        {
            std::vector<Field> fields;
            std::size_t point_count{};
            std::size_t point_stride{};
        };

        Header read_header(std::istream& stream)
        {
            Header header;

            std::vector<std::size_t> sizes;
            std::vector<char> types;
            std::vector<std::size_t> counts;

            bool found_points = false;
            bool found_data = false;

            std::string line;

            while(std::getline(stream, line))
            {
                std::istringstream values(line);
                std::string keyword;
                values >>keyword;

                if(keyword.empty() || keyword.front() == '#')
                {
                    continue;
                }
                if(keyword == "FIELDS")
                {
                    std::string name;

                    while(values >>name)
                    {
                        Field field;
                        field.name = name;
                        header.fields.push_back(field);
                    }
                }
                else if(keyword == "SIZE")
                {
                    std::size_t size;
                    
                    while(values >> size)
                    {
                        sizes.push_back(size);
                    }
                }
                else if(keyword == "TYPE")
                {
                    char type;

                    while(values>> type)
                    {
                        types.push_back(type);
                    }
                }
                else if(keyword == "COUNT")
                {
                    std::size_t count;

                    while(values>> count)
                    {
                        counts.push_back(count);
                    }
                }
                else if(keyword == "POINTS")
                {
                    if(!(values >> header.point_count))
                    {
                        throw std::runtime_error("Invalid PCD POINTS value");
                    }

                    found_points = true;
                }
                else if(keyword == "DATA")
                {
                    std::string encoding;
                    values >> encoding;

                    if(encoding != "binary")
                    {
                        throw std::runtime_error("Only uncompressed binary PCD is supported");
                    }

                    found_data = true;
                    break;
                }
            }

            if (!found_data || !found_points || header.fields.empty())
            {
                throw std::runtime_error("Missing required PCD header entries");
            }

            const auto field_count = header.fields.size();

            if (sizes.size() != field_count || types.size() != field_count)
            {
                throw std::runtime_error("PCD FIELDS, SIZE and TYPE disagree");
            }

            // PCD permits omitted COUNT; each field then has one element.
            if (counts.empty())
            {
                counts.assign(field_count, 1);
            }

            if (counts.size() != field_count)
            {
                throw std::runtime_error("PCD FIELDS and COUNT disagree");
            }

            constexpr auto maximum = std::numeric_limits<std::size_t>::max();

            for (std::size_t i = 0; i < field_count; ++i)
            {
                auto& field = header.fields[i];

                field.size = sizes[i];
                field.type = types[i];
                field.count = counts[i];
                field.offset = header.point_stride;

                if (field.size == 0 || field.count == 0)
                {
                    throw std::runtime_error("PCD field size/count must be positive");
                }

                if (field.count > (maximum - header.point_stride) / field.size)
                {
                    throw std::runtime_error("PCD point stride overflow");
                }

                header.point_stride += field.size * field.count;
            }

            return header;
        }
    }


    PointCloud load_pcd(const std::filesystem::path& path)
    {
        static_assert(sizeof(float) == 4);
        static_assert(std::numeric_limits<float>::is_iec559);

        if constexpr (std::endian::native != std::endian::little)
        {
            throw std::runtime_error("This loader currently requires a little-endian machine");
        }

        std::ifstream file(path, std::ios::binary);

        if(!file)
        {
            throw std::runtime_error("Cannot open PCD file: " + path.string());
        }

        const auto header = read_header(file);

        const auto find_coordinate = [&](const std::string& name)
        {
            std::size_t offset{};
            bool found = false;

            for(const auto& field : header.fields)
            {
                if(field.name != name)
                {
                    continue;
                }

                if(found)
                {
                    throw std::runtime_error("Duplicate PCD coordinate field: " + name);
                }

                if(field.type != 'F' || field.size != 4 || field.count != 1)
                {
                    throw std::runtime_error("PCD coordinate must be a single float32: " + name);
                }

                offset = field.offset;
                found = true;
            }

            if(!found)
            {
                throw std::runtime_error("Missing PCD coordinate filed: " + name);
            }

            return offset;
        };

        const auto x_offset = find_coordinate("x");
        const auto y_offset = find_coordinate("y");
        const auto z_offset = find_coordinate("z");

        //check available data before allocating memory for the poinst.
        const auto data_start = file.tellg();

        if(data_start == std::streampos(-1))
        {
            throw std::runtime_error("Cannot locate PCD binary payload");
        }

        file.seekg(0, std::ios::end);
        const auto data_end = file.tellg();

        if(!file || data_end < data_start)
        {
            throw std::runtime_error("Cannot locate PCD binary payload");
        }

        const auto available = static_cast<std::uintmax_t>(data_end - data_start);

        if(header.point_count > available / header.point_stride)
        {
            throw std::runtime_error("PCD binary payload is truncated");
        }

        if(header.point_stride > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
        {
            throw std::runtime_error("PCD point record is too large");
        }

        file.seekg(data_start);

        if(!file)
        {
            throw std::runtime_error("Cannot seek to PCD binary payload");
        }

        std::vector<char> record(header.point_stride);

        PointCloud points;
        points.reserve(header.point_count);

        const auto read_coordinate = [&](std::size_t offset)
        {
            float value{};
            std::memcpy(&value, record.data() + offset, sizeof(value));
            return static_cast<double>(value);
        };

        for(std::size_t i = 0; i < header.point_count; ++i)
        {
            if(!file.read(record.data(), static_cast<std::streamsize>(record.size())))
            {
                throw std::runtime_error("Failed reading PCD point " + std::to_string(i));
            }

            const Point point{
                read_coordinate(x_offset),
                read_coordinate(y_offset),
                read_coordinate(z_offset)
            };

            if(!std::isfinite(point.x) ||
               !std::isfinite(point.y) ||
               !std::isfinite(point.z))
            {
                throw std::runtime_error("Nonfinite coordinates at PCD point " + std::to_string(i));
            }

            points.push_back(point);
        }
        return points;
    }

}
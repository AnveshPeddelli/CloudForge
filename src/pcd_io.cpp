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
#include <charconv>
#include <set>

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

        std::size_t parse_size(const std::string& text)
        {
            std::size_t value{};
            const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
                throw std::runtime_error("Invalid nonnegative PCD integer: " + text);
            return value;
        }

        Header read_header(std::istream& stream)
        {
            Header header;

            std::vector<std::size_t> sizes;
            std::vector<char> types;
            std::vector<std::size_t> counts;

            bool found_points = false;
            bool found_data = false;
            bool found_count = false;
            std::set<std::string> seen;
            std::size_t width{}, height{};

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
                if (!seen.insert(keyword).second)
                    throw std::runtime_error("Duplicate PCD header entry: " + keyword);
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
                    std::string size;
                    
                    while(values >> size)
                    {
                        sizes.push_back(parse_size(size));
                    }
                }
                else if(keyword == "TYPE")
                {
                    std::string type;

                    while(values>> type)
                    {
                        if (type.size() != 1 || type.find_first_not_of("FIU") != std::string::npos)
                            throw std::runtime_error("Invalid PCD TYPE");
                        types.push_back(type.front());
                    }
                }
                else if(keyword == "COUNT")
                {
                    found_count = true;
                    std::string count;

                    while(values>> count)
                    {
                        counts.push_back(parse_size(count));
                    }
                }
                else if(keyword == "POINTS")
                {
                    std::string text, extra;
                    if (!(values >> text) || (values >> extra))
                    {
                        throw std::runtime_error("Invalid PCD POINTS value");
                    }
                    header.point_count = parse_size(text);
                    found_points = true;
                }
                else if (keyword == "WIDTH" || keyword == "HEIGHT")
                {
                    std::string text, extra;
                    if (!(values >> text) || (values >> extra)) throw std::runtime_error("Invalid PCD dimensions");
                    if (keyword == "WIDTH") width = parse_size(text);
                    else height = parse_size(text);
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
            if (!found_count)
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
                if ((field.size != 1 && field.size != 2 && field.size != 4 && field.size != 8)
                    || (field.type == 'F' && field.size != 4 && field.size != 8))
                    throw std::runtime_error("Unsupported PCD field size/type");

                if (field.count > (maximum - header.point_stride) / field.size)
                {
                    throw std::runtime_error("PCD point stride overflow");
                }

                header.point_stride += field.size * field.count;
            }
            if (seen.contains("WIDTH") != seen.contains("HEIGHT"))
                throw std::runtime_error("PCD WIDTH and HEIGHT must appear together");
            if (seen.contains("WIDTH") && (height == 0 || width > maximum / height || width * height != header.point_count))
                throw std::runtime_error("PCD dimensions disagree with POINTS");

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
                throw std::runtime_error("Missing PCD coordinate field: " + name);
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

        if (header.point_count == 0) return {};
        if (header.point_count > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()) / header.point_stride)
            throw std::runtime_error("PCD payload is too large for one read");
        const auto payload_size = header.point_count * header.point_stride;
        // One bulk read avoids thousands of small reads across WSL's /mnt/d.
        std::vector<char> payload(payload_size);
        if (!file.read(payload.data(), static_cast<std::streamsize>(payload.size())))
            throw std::runtime_error("Failed reading PCD binary payload");

        PointCloud points;
        points.reserve(header.point_count);

        const auto read_coordinate = [&](std::size_t record_start, std::size_t offset)
        {
            float value{};
            std::memcpy(&value, payload.data() + record_start + offset, sizeof(value));
            return static_cast<double>(value);
        };

        for(std::size_t i = 0; i < header.point_count; ++i)
        {
            const auto record_start = i * header.point_stride;
            const Point point{
                read_coordinate(record_start, x_offset),
                read_coordinate(record_start, y_offset),
                read_coordinate(record_start, z_offset)
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

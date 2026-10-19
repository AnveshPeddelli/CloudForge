#include "cloudforge/synthetic.hpp"

namespace cloudforge
{
    PointCloud make_synthetic_frame()
    {
        PointCloud points;
        points.reserve(16000);

        for(int x = -10; x < 10; ++x)
        {
            for(int y = -10; y < 10; ++y)
            {
                for(int z = 0; z < 5; ++z)
                {
                    for(int sample = 0; sample < 8; ++sample)
                    {
                        const double dx = (sample& 1)?0.75 : 0.25;
                        const double dy = (sample& 2)?0.75 : 0.25;
                        const double dz = (sample& 4)?0.75 : 0.25;
                    
                        points.push_back({
                            x + dx,
                            y + dy, 
                            z + dz
                        });
                    }
                }
            }
        }
        return points;
    }
}
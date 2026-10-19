#include <cuda_runtime.h>

#include <iostream>
#include <cstdio>

__global__ void gpu_hello()
{
    printf("Hello from GPU thread %u\n", threadIdx.x);
}

int main()
{
    gpu_hello<<<1, 4>>>();

    auto error = cudaGetLastError();

    if(error != cudaSuccess)
    {
        std::cerr<<"Launch failed: "<< cudaGetErrorString(error)<< '\n';
        return 1;
    }

    error = cudaDeviceSynchronize();

    if(error != cudaSuccess)
    {
        std::cerr<< "Execution failed: "<< cudaGetErrorString(error)<< '\n';
        return 1;
    }

    std::cout<< "GPU execution succeeded\n";
}
/*
 * GPUEngine.cu
 *
 *  Created on: Oct 9, 2014
 *      Author: gonzales
 */

#include "GPUEngine.h"
#include "../Dataset.h"
#include "../Distributor.h"
#include "EntropySearch.h"
#include <cstring>

GPUEngine::GPUEngine(unsigned int proc_num, unsigned int proc_id,
                     std::vector<std::pair<unsigned int, unsigned int>> gpu_map, bool use_mi) :
        proc_num(proc_num),
        proc_id(proc_id),
        use_mi(use_mi) {
    int avail_gpus = 0;
    if (cudaSuccess != cudaGetDeviceCount(&avail_gpus))
        throw CUDAError(cudaGetLastError());
    if (avail_gpus == 0) {
        throw CUDAError("Could not find any CUDA-enabled GPU");
    }

    auto pos = std::find_if(gpu_map.begin(), gpu_map.end(),
                 [&proc_id](std::pair<unsigned int, unsigned int> item) { return item.first == proc_id; });
    gpu_id = pos == gpu_map.end() ? proc_id % avail_gpus : pos->second;

    cudaDeviceProp gpu_prop;
    if (cudaSuccess != cudaGetDeviceProperties(&gpu_prop, gpu_id))
        throw CUDAError(cudaGetLastError());
    if (gpu_prop.major < 2 || !gpu_prop.canMapHostMemory) {
        throw CUDAError("GPU " + std::to_string(gpu_id) + " does not meet compute capabilities\n" +
                        "Name: " + gpu_prop.name + "\n" + "Compute capability: " +
                        std::to_string(gpu_prop.major) + "." + std::to_string(gpu_prop.minor));
    }
    if (cudaSuccess != cudaSetDevice(gpu_id))
        throw CUDAError(cudaGetLastError());
}

void GPUEngine::run(std::string tped, std::string tfam, std::vector<MutualInfo> &mutual_info, size_t num_outputs,
                    Statistics &statistics) {
    statistics.Begin_timer("SNPs read time");
    Dataset *dataset;
    try {
        dataset = new Dataset(tped, tfam, Dataset::Transposed);
    } catch (const Dataset::ReadError &error) {
        throw Engine::Error(error.what());
    }
    statistics.End_timer("SNPs read time");

    statistics.Addi("SNP count", dataset->Get_SNP_count());
    statistics.Addi("Number of cases", dataset->Get_case_count());
    statistics.Addi("Number of controls", dataset->Get_ctrl_count());

    Distributor distributor(proc_num, proc_id, dataset->Get_SNP_count());

    EntropySearch search(use_mi, dataset->Get_SNP_count(), dataset->Get_case_count(), dataset->Get_ctrl_count(),
                         dataset->Get_cases(), dataset->Get_ctrls());

    std::vector<std::pair<uint32_t, uint32_t >> pairs;
    distributor.Get_pairs(1, 0, pairs);

    long myTotalAnal = 0;
    const unsigned int num_snps = dataset->Get_SNP_count();
    for (auto p : pairs) {
        myTotalAnal += num_snps - p.second - 1;
    }
    statistics.Addl("GPU " + std::to_string(gpu_id) + " computations", myTotalAnal);

    std::string timer_label;
    timer_label += "GPU " + std::to_string(gpu_id) + " runtime";
    statistics.Begin_timer(timer_label);

    mutual_info.resize(num_outputs);
    search.mutualInfo(pairs, num_outputs, &mutual_info.at(0));
    cudaDeviceSynchronize();

    delete dataset;

    statistics.End_timer(timer_label);
}
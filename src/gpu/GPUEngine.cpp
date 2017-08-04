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

GPUEngine::GPUEngine(unsigned int proc_num, unsigned int proc_id, bool use_mi) :
        proc_num(proc_num),
        proc_id(proc_id),
        use_mi(use_mi) {
    int avail_gpus = 0;
    if (cudaSuccess != cudaGetDeviceCount(&avail_gpus))
        throw CUDAError(cudaGetLastError());
    if (avail_gpus == 0) {
        throw CUDAError("Could not find any CUDA-enabled GPU");
    }

    gpu_id = proc_id % avail_gpus;
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
    std::string snp_load_label("SNPs load time");
    statistics.Begin_timer(snp_load_label);
    Dataset dataset(tped, tfam, Dataset::Transposed);
    statistics.End_timer(snp_load_label);

    Distributor distributor(proc_num, proc_id, dataset.Get_SNP_count());

    EntropySearch search(use_mi, dataset.Get_SNP_count(), dataset.Get_case_count(), dataset.Get_ctrl_count(),
                         dataset.Get_cases(), dataset.Get_ctrls());

    uint64_t myTotalAnal = 0;

    std::string timer_label;
    timer_label += "GPU " + std::to_string(gpu_id) + " runtime";
    std::string analysis_label;
    analysis_label += "GPU " + std::to_string(gpu_id) + " analysis";

    statistics.Begin_timer(timer_label);

    std::vector<std::pair<uint32_t, uint32_t >> pairs;
    distributor.Get_pairs(1, 0, pairs);

    mutual_info.resize(num_outputs);
    search.mutualInfo(pairs, num_outputs, &mutual_info.at(0));
    cudaDeviceSynchronize();

    statistics.End_timer(timer_label);
    statistics.Add_value(analysis_label, myTotalAnal);
}
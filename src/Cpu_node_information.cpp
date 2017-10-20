//
// Created by christian on 18/10/17.
//

#include "Cpu_node_information.h"
#include <cstring>
#include <climits>
#include <mpi.h>
#include <unistd.h>
#include <memory>

std::string Cpu_node_information::get_cpu_info() {
    std::string cpu;
    std::array<char, 256> buffer;
    std::shared_ptr<FILE> pipe(popen(cpu_name_bash_command, "r"), pclose);
    if (!pipe) throw std::runtime_error("popen() failed!");
    while (!feof(pipe.get())) {
        if (fgets(buffer.data(), 256, pipe.get()) != nullptr)
            cpu.append(buffer.data());
    }
    cpu.resize(cpu.length() - 1);
    return cpu;
}

long Cpu_node_information::get_physical_memory() {
    std::string size;
    std::array<char, 256> buffer;
    std::shared_ptr<FILE> pipe(popen(physical_memory_command, "r"), pclose);
    if (!pipe) throw std::runtime_error("popen() failed!");
    while (!feof(pipe.get())) {
        if (fgets(buffer.data(), 128, pipe.get()) != nullptr)
            size.append(buffer.data());
    }
    return atol(size.c_str()) * 1024;
}

Cpu_node_information::Cpu_node_information() : Node_information() {
    int len;
    char hostname_buff[HOST_NAME_MAX];
    gethostname(hostname_buff, HOST_NAME_MAX);
    hostname_str.append(hostname_buff);
    cpu_str = get_cpu_info();
    memory_size = get_physical_memory();
    char mpi_v[MPI_MAX_LIBRARY_VERSION_STRING];
    MPI_Get_library_version(mpi_v, &len);
    mpi_library.append(mpi_v, (unsigned long) len - 1);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    process_list.push_back(rank);
}

// TODO: Función template que construye el objeto a partir del puntero en memoria
Cpu_node_information::Cpu_node_information(const void *pointer) : Node_information() {
    auto *ptr = (char *) pointer;
    size_t size, offset = 0;

    // Hardware ID string
    memcpy(&size, ptr, sizeof(size_t));
    offset += sizeof(size_t);
    hardware_id.resize(size);
    memcpy(&hardware_id[0], ptr + offset, size);
    offset += size;

    // Hostname string
    memcpy(&size, ptr + offset, sizeof(size_t));
    offset += sizeof(size_t);
    hostname_str.resize(size);
    memcpy(&hostname_str[0], ptr + offset, size);
    offset += size;

    // CPU name string
    memcpy(&size, ptr + offset, sizeof(size_t));
    offset += sizeof(size_t);
    cpu_str.resize(size);
    memcpy(&cpu_str[0], ptr + offset, size);
    offset += size;

    // Memory size integer
    memcpy(&memory_size, ptr + offset, sizeof(long));
    offset += sizeof(long);

    // MPI library string
    memcpy(&size, ptr + offset, sizeof(size_t));
    offset += sizeof(size_t);
    mpi_library.resize(size);
    memcpy(&mpi_library[0], ptr + offset, size);
    offset += size;

    // Process ID vector
    memcpy(&size, ptr + offset, sizeof(size_t));
    offset += sizeof(size_t);
    process_list.resize(size);
    memcpy(&process_list[0], ptr + offset, size * sizeof(int));
    offset += size * sizeof(int);
}

std::string Cpu_node_information::hostname() const {
    return hostname_str;
}

std::string Cpu_node_information::cpu() const {
    return cpu_str;
}

long Cpu_node_information::memory() const {
    return memory_size;
}

std::string Cpu_node_information::mpi_library_version() const {
    return mpi_library;
}

std::vector<int> Cpu_node_information::processes() const {
    return process_list;
}

std::vector<std::string> Cpu_node_information::gpus() const {
    return std::vector<std::string>(0);
}

std::string Cpu_node_information::to_string() const {
    std::string output;
    output += "Hostname: " + hostname_str + "\n";
    output += "CPU: " + cpu_str + "\n";
    output += "Memory: " + std::to_string(memory_size / (1024 * 1024)) + " MB\n";
    output += "MPI Library: " + mpi_library + "\n";
    output += "Processes: ";
    for (auto p : process_list){
        output += "[" + std::to_string(p) + "] ";
    }
    output += "\n";
    return output;
}

size_t Cpu_node_information::to_byteblock(void **pointer) const {
    const size_t buffer_size =  5 * sizeof(size_t) + hardware_id.length() + hostname_str.length() + cpu_str.length() +
            sizeof(unsigned int) + mpi_library.length() + process_list.size() * sizeof(int);
    *pointer = new char[buffer_size];
    auto *ptr = (char *) *pointer;
    size_t temp, offset = 0;

    // Hardware ID string
    temp = hardware_id.length();
    memcpy(ptr + offset, &temp, sizeof(size_t));
    offset += sizeof(size_t);
    memcpy(ptr + offset, &hardware_id[0], hardware_id.length());
    offset += hardware_id.length();

    // Hostname string
    temp = hostname_str.length();
    memcpy(ptr + offset, &temp, sizeof(size_t));
    offset += sizeof(size_t);
    memcpy(ptr + offset, &hostname_str[0], hostname_str.length());
    offset += hostname_str.length();

    // CPU name string
    temp = cpu_str.length();
    memcpy(ptr + offset, &temp, sizeof(size_t));
    offset += sizeof(size_t);
    memcpy(ptr + offset, &cpu_str[0], cpu_str.length());
    offset += cpu_str.length();

    // Memory size integer
    memcpy(ptr + offset, &memory_size, sizeof(long));
    offset += sizeof(long);

    // MPI library string
    temp = mpi_library.length();
    memcpy(ptr + offset, &temp, sizeof(size_t));
    offset += sizeof(size_t);
    memcpy(ptr + offset, &mpi_library[0], mpi_library.length());
    offset += mpi_library.length();

    // Process ID vector
    temp = process_list.size();
    memcpy(ptr + offset, &temp, sizeof(size_t));
    offset += sizeof(size_t);
    memcpy(ptr + offset, &process_list[0], process_list.size() * sizeof(int));
    offset += process_list.size() * sizeof(int);

    return offset;
}

void Cpu_node_information::add_processes(std::vector<int> processes) {
    process_list.insert(process_list.end(), processes.begin(), processes.end());
}

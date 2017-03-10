//
// Created by christian on 10/03/17.
//

#include "IOMpi.h"

IOMpi::IOMpi() {
    MPI_Comm_dup(MPI_COMM_WORLD, &io_comm);
    MPI_Comm_size(io_comm, &comm_size);
    MPI_Comm_rank(io_comm, &my_rank);
    io_rank = MPI_PROC_NULL;
    cprintf_tag = 0;
    io_buff = NULL;
}

IOMpi::~IOMpi() {
    MPI_Comm_free(io_comm);
    if (io_buff != NULL){
        delete io_buff;
    }
}

int IOMpi::Get_io_rank() {
    // Determine IO process
    int mpi_io, flag, io_rank;
    MPI_Attr_get(MPI_COMM_WORLD, MPI_IO, &mpi_io, &flag);
    if (flag == 0) {
        // Attribute not cached
        io_rank = MPI_PROC_NULL;
    } else if (mpi_io == MPI_PROC_NULL) {
        // No process can carry IO
        io_rank = MPI_PROC_NULL;
    } else if (mpi_io == MPI_ANY_SOURCE) {
        io_rank = 0;
    } else {
        // Multiple IO processes
        MPI_Allreduce(mpi_io, &io_rank, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    }
    return io_rank;
}

int IOMpi::Cprintf(char *format, ...) {
    va_list args;
    int i;

    if (io_rank == MPI_PROC_NULL){
        io_rank = Get_io_rank();
    }

    if (io_buff == NULL){
        io_buff = new char[BUFFER_SIZE];
    }

    if (my_rank == io_rank){
        for (i=0; i<my_rank; i++){
            if (MPI_Recv(io_buff, BUFFER_SIZE, MPI_CHAR, i, cprintf_tag, io_comm, NULL) != MPI_SUCCESS){
                return -1;
            }
            printf("Process %d > %s\n", i, io_buff);
            fflush(stdout);
        }
        va_start(args, format);
        vsprintf(io_buff, format, args);
        va_end(args);
        printf("Process %d > %s\n", my_rank, io_buff);
        for (i=my_rank+1; i<comm_size; i++){
            if (MPI_Recv(io_buff, BUFFER_SIZE, MPI_CHAR, i, cprintf_tag, io_comm, NULL) != MPI_SUCCESS){
                return -1;
            }
            printf("Process %d > %s\n", i, io_buff);
            fflush(stdout);
        }
    } else {
        va_start(args, format);
        vsprintf(io_buff, format, args);
        va_end(args);
        if (MPI_Send(io_buff, strlen(io_buff) + 1, MPI_CHAR, io_rank, cprintf_tag, io_comm) != MPI_SUCCESS){
            return -1;
        }
    }
    cprintf_tag++;
    /* TODO:
     - return number of characters printed
    */
    return 0;
}
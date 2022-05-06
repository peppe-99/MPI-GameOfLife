#include<stdio.h>
#include<stdlib.h>
#include<time.h>
#include"mpi.h"

#define LIVE "\xF0\x9F\x91\xBE"
#define DEATH "\xF0\x9F\x94\xB2"

void swap(int **current_matrix, int **new_matrix);

int neighbors_alive(int i, int j, int row, int col, int *process_matrix, int *top_row, int *bottom_row);

void update_not_chained_rows(int start_row, int end_row, int row, int col, int *process_matrix, int *new_process_matrix);

void update_chained_row(int chained_row, int row, int col, int *process_matrix, int *new_process_matrix, int *top_row, int *bottom_row);

int main(int argc, char **argv) {

    int np, rank;
    double start_time, end_time;

    MPI_Request send_first_row, send_last_row, receive_next_row, receive_prev_row;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &np);

    if (argc != 4) {
        if (rank == 0) fprintf(stderr, "Inserire parametri righe, colonne e generazioni\n");
        MPI_Finalize();
        return 0;
    }

    int row = atoi(argv[1]);
    int col = atoi(argv[2]);
    int generations = atoi(argv[3]);

    if (row < np) {
        if (rank == 0) fprintf(stderr, "The number of rows must be at least equal to the number of processors\n");
        MPI_Finalize();
        return 0;
    }

    int* matrix = (int*)malloc((row * col) * sizeof(int));
    int* send_counts = (int*)malloc(np * sizeof(int));
    int* displs = (int*)malloc(np * sizeof(int));

    int divisione = row / np;
    int resto = row % np;

    for (int i = 0; i < np; i++) {
        send_counts[i] = (i < resto) ? (divisione + 1) * col : divisione * col;
        displs[i] = (i == 0) ? 0 : displs[i-1] + send_counts[i-1];
    }

    int process_size = send_counts[rank];
    int local_row = process_size / col;

    int* process_matrix = (int*)malloc(process_size * sizeof(int));
    int* new_process_matrix = (int*)malloc(process_size * sizeof(int));
    int* top_row = NULL;
    if (rank > 0) {
        top_row = (int*)malloc(col * sizeof(int));
    }
    int* bottom_row = NULL;
    if (rank < np-1) {
        bottom_row = (int*)malloc(col * sizeof(int));
    }

    MPI_Barrier(MPI_COMM_WORLD);
    start_time = MPI_Wtime();

    if (rank == 0) {
        srand(time(NULL));
        // printf("Original Matrix\n");
        for (int i = 0; i < row; i++) {
            for (int j = 0; j < col; j++) {
                matrix[i * col + j] = rand() % 2;
                // if (matrix[i * col + j] == 1) printf(LIVE);
                // else printf(DEATH);  
            }
            // printf("\n");
        }
    }

    MPI_Scatterv(matrix, send_counts, displs, MPI_INT, process_matrix, process_size, MPI_INT, 0, MPI_COMM_WORLD);

    for (int generation = 0; generation < generations; generation++) {
        /*Tutti i processi tranne 0: 
            inviano al precedente la propria prima riga 
            richiedono al precedente la sua ultima riga */
        if (rank > 0) {
            MPI_Isend(&process_matrix[0], col, MPI_INT, rank-1, rank-1, MPI_COMM_WORLD, &send_first_row);
            MPI_Irecv(top_row, col, MPI_INT, rank-1, rank, MPI_COMM_WORLD, &receive_prev_row);
        }
        /*Tutti i processi tranne np-1: 
            inviano al successivo la propria ultima riga 
            richiedono al successivo la sua prima riga  */
        if (rank < np-1) {
            MPI_Isend(&process_matrix[(local_row-1) * col], col, MPI_INT, rank+1, rank+1, MPI_COMM_WORLD, &send_last_row);
            MPI_Irecv(bottom_row, col, MPI_INT, rank+1, rank, MPI_COMM_WORLD, &receive_next_row);
        }

        /* ogni processo inizia a computare le righe non vincolate */
        int start_row = (rank == 0) ? 0 : 1;
        int end_row = (rank == np-1) ? local_row : local_row-1;
        update_not_chained_rows(start_row, end_row, local_row, col, process_matrix, new_process_matrix);

        /* se la sottomatrice di un processo è composta da una sola riga allora sarà doppiamente vincolata */
        if (local_row == 1) {
            if (rank > 0) MPI_Wait(&receive_prev_row, MPI_STATUS_IGNORE);
            if (rank < np-1) MPI_Wait(&receive_next_row, MPI_STATUS_IGNORE);

            int chained_row = 0;
            update_chained_row(chained_row, local_row, col, process_matrix, new_process_matrix, top_row, bottom_row);
        }
        else {
            if (rank > 0) {
                MPI_Wait(&receive_prev_row, MPI_STATUS_IGNORE);
                
                int chained_row = 0;
                update_chained_row(chained_row, local_row, col, process_matrix, new_process_matrix, top_row, bottom_row);
            }
            
            if (rank < np-1) {
                MPI_Wait(&receive_next_row, MPI_STATUS_IGNORE);
                
                int chained_row = local_row-1;
                update_chained_row(chained_row, local_row, col, process_matrix, new_process_matrix, top_row, bottom_row);
            }   
        }
        /* swap dei puntatori */
        swap(&process_matrix, &new_process_matrix);
    }

    MPI_Gatherv(process_matrix, process_size, MPI_INT, matrix, send_counts, displs, MPI_INT, 0, MPI_COMM_WORLD);

    end_time = MPI_Wtime();

    if (rank == 0) {
        printf("Time in s: %f, with %d processors, %dx%d matrix and %d generations\n", end_time-start_time, np, row, col, generations);
    }
    
    free(matrix);
    free(displs);
    free(send_counts);
    free(process_matrix);
    free(new_process_matrix);
    if (rank > 0) {
        free(top_row);
    }
    if (rank < np-1) {
        free(bottom_row);
    }

    MPI_Finalize();

    return 0;
}

void swap(int **current_matrix, int **new_matrix) {
    int *temp = *current_matrix;
    *current_matrix = *new_matrix;
    *new_matrix = temp;
}

int neighbors_alive(int i, int j, int row, int col, int *process_matrix, int *top_row, int *bottom_row) {
    int top = 0;
    int tl_corner = 0;
    int tr_corner = 0;
    if (i > 0) {
        top = process_matrix[(i-1) * col + j];        
        if (j > 0) {
            tl_corner = process_matrix[(i-1) * col + (j-1)];
        }
        if (j < col-1) {
            tr_corner = process_matrix[(i-1) * col + (j+1)];
        }
    } 
    else if(top_row != NULL) {
        top = top_row[j];
        if (j > 0) {
            tl_corner = top_row[j-1];
        }
        if (j < col-1) {
            tr_corner = top_row[j+1];
        }
    }

    int left = (j > 0) ? process_matrix[i * col + (j-1)] : 0;
    int right = (j < col-1) ? process_matrix[i * col + (j+1)] : 0;

    int bottom = 0;
    int bl_corner = 0;
    int br_corner = 0;
    if (i < row-1) {
        bottom = process_matrix[(i+1) * col + j];
        if (j > 0) {
            bl_corner = process_matrix[(i+1) * col + (j-1)];
        }
        if (j < col-1) {
            br_corner = process_matrix[(i+1) * col + (j+1)];
        }
    }
    else if(bottom_row != NULL) {
        bottom = bottom_row[j];
        if (j > 0) {
            bl_corner = bottom_row[j-1];
        }
        if (j < col-1) {
            br_corner = bottom_row[j+1];
        }
    }

    return top + left + right + bottom + tl_corner + tr_corner + bl_corner + br_corner;
}

void update_not_chained_rows(int start_row, int end_row, int row, int col, int *process_matrix, int *new_process_matrix) {
    for (int i = start_row; i < end_row; i++) {
        for (int j = 0; j < col; j++) {
            int alives = neighbors_alive(i, j, row, col, process_matrix, NULL, NULL);

            if (process_matrix[i * col + j] == 1 && (alives == 2 || alives == 3)) {
                new_process_matrix[i * col + j] = 1;
            }
            else if (process_matrix[i * col + j] == 0 && alives == 3) {
                new_process_matrix[i * col + j] = 1;
            }
            else new_process_matrix[i * col + j] = 0;
        }
    }
}

void update_chained_row(int chained_row, int row, int col, int *process_matrix, int *new_process_matrix, int *top_row, int *bottom_row) {
    int i = chained_row;
    for (int j = 0; j < col; j++) {
        int alives = neighbors_alive(i, j, row, col, process_matrix, top_row, bottom_row);

        if (process_matrix[i * col + j] == 1 && (alives == 2 || alives == 3)) {
            new_process_matrix[i * col + j] = 1;
        }
        else if (process_matrix[i * col + j] == 0 && alives == 3) {
            new_process_matrix[i * col + j] = 1;
        }
        else new_process_matrix[i * col + j] = 0;
    }
}
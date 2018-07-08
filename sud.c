/* File:
 *    sud.c
 *
 * Purpose:
 *    Solve a correctly defined Sudoku puzzle using parallel
 *    processing
 *
 * Compile:
 *    gcc -o sud sud.c -lpthread
 *
 * Input:
 *     1. Elements of Sudoku puzzle matrix in string format with column traversal
            2. Number of threads to used


 * Output:
 *    Solved sudoku puzzle sent through sockets to connected server
 *

 */


#include <unistd.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <arpa/inet.h>
#include <time.h>


#define BOARDSIZE (81)
#define GRIDSIZE (9)
#define LENGTH (3)
#define PORT (7120)



struct timespec g_start; // for measuring execution time
struct timespec g_finish;
double g_elapsed;
bool g_finished = 0; // execution status of a thread
char buff[256]; // solve puzzle to be sent to server
int sockfd;   // file descritor for created socket
pthread_mutex_t mutex;  // prevents race conditions for signaling variable
pthread_cond_t is_fin;  // signal for main thread to finish execution
pthread_rwlockattr_t mylock_attr; // attribute for rwlock lock
pthread_rwlock_t lock;  // rwlock for variable finished



/* Function Prototypes */
void *solveSudoku(void *);
bool isValid(int number, int puzzle[GRIDSIZE][GRIDSIZE], int row, int column);
bool sudokuHelper(int puzzle[GRIDSIZE][GRIDSIZE], int row, int column,
                  int startV, int nTimes);

char* buffSudoku(int puzzle[GRIDSIZE][GRIDSIZE], double timeo);


/* Structure to hold data passed to a thread */
typedef struct
{
    bool completed;  // execution status of thread
    int board[GRIDSIZE][GRIDSIZE];  // Sudoku matrix passed to a thread
    int start;  // Starting used in brute-force
    int row;    // Starting row position to use
    int col;    // Starting column position to use
} boardz;


/*-------------------------------------------------------------------
 * Purpose:     Checks if an entry does not violate any of the rules of sudoku
 * In arg:      numbers        Entry to check
                puzzle[][]    Matrix containing Sudoku problem
                rows          Row number on which to check
                column        Column number on which to check

 * Return val:  A bool which is true if number is allowed at the given position
 */

bool isValid(int number, int puzzle[GRIDSIZE][GRIDSIZE], int row, int column) {
    int i = 0;

    for (i = 0; i < GRIDSIZE; i++) {
        if (puzzle[i][column] == number) return 0;
        if (puzzle[row][i] == number) return 0;
        if ( number ==
             puzzle[row/LENGTH*LENGTH+i%LENGTH]
             [column/LENGTH*LENGTH+i/LENGTH] ) {
            return 0;
        }
    }
    return 1;
}

/*-------------------------------------------------------------------
 * Purpose:     Calculates solution of puzzle by assigning starting value startV
                                and using recursive backtracking algorithm
 * In arg:      puzzle[][]    Matrix containing Sudoku problem
                                rows      Row number of element
                                column      Column number of element
                                startV      Starting value from which to find
                                            legal entries
                                nTimes      Depth of recursion
 * Return val:  A bool which is true if legal entry found at this location
 */

bool sudokuHelper(int puzzle[GRIDSIZE][GRIDSIZE], int row, int col,
                  int startV, int nTimes)
{
    pthread_rwlock_rdlock(&lock);

    if (1 == g_finished) {
        return 1;
    }
    pthread_rwlock_unlock(&lock);

    // If depth of recursion is 81, then board solved
    if (BOARDSIZE == nTimes) return 1;
    // Do a loop of rows and columns
    col++;
    if (GRIDSIZE == col){
        col = 0;
        row++;
        if (GRIDSIZE == row ) row = 0;
    }


    if (0 != puzzle[row][col]){
        // recursion
        return sudokuHelper(puzzle ,row, col, startV, nTimes+1);
    }


    for (int val = 1; val <= GRIDSIZE; ++val) {
        if (++startV == 10) {
            startV = 1;
        }
        if (isValid(startV, puzzle, row, col)) {
            puzzle[row][col] = startV;
            if (sudokuHelper(puzzle, row, col, startV, nTimes+1))
                return 1;
        }
    }
    // If no match found then backtrack to previus block

    puzzle[row][col] = 0;
    return 0;
} //End of function






/*-------------------------------------------------------------------
 * Purpose:     Each thread solves a sudoku puzzle according to the given
                starting value
 * In arg:      boardz structure
 * Return val:  Ignored
 */
void *solveSudoku(void * params) {

    boardz *data = (boardz *) params;
    data->completed = 0;

    /* Passing puzzle and start value to recursive function sudokuHelper
        to find solution */
    sudokuHelper(data->board, data->row, data->col, data->start, 0);

    // apply write lock so as to change value of finished
    pthread_rwlock_wrlock(&lock);

    // If any other thread has not finished
    if (g_finished == 0) {
        pthread_mutex_lock(&mutex);
        data->completed = 1;
        g_finished = 1;
        pthread_mutex_unlock(&mutex);


        // calculate time taken
        clock_gettime(CLOCK_MONOTONIC, &g_finish);
        g_elapsed = (g_finish.tv_sec - g_start.tv_sec);
        g_elapsed += (double)(g_finish.tv_nsec - g_start.tv_nsec) / 1000000000;


        char *b1; // stores puzzle as string to be sent

        // Converting solved puzzle to string b1
        b1 = buffSudoku(data->board, g_elapsed);
        // Send b1 to server
        send(sockfd , b1 , strlen(b1) , 0 );

        pthread_rwlock_unlock(&lock);

        // Signaling main function to continue execution and terminate process
        pthread_cond_signal(&is_fin);
    }

    return 0;
}






/*-------------------------------------------------------------------
 * Purpose:     Converts sudoku board to string for socket transmission
 * In arg:      puzzle[][]    Matrix containing Sudoku problem
                                timeo          Elapsed time


 * Return val:  A string comprising of the elements of the puzzle
 */
char* buffSudoku(int puzzle[GRIDSIZE][GRIDSIZE], double timeo) {
    char *buff2 = (char *)malloc(sizeof(buff));
    int i = 0;
    int j = 0;
    int cx = 0, dx;
    for (i = 0; i < GRIDSIZE; i++) {
        for (j = 0; j < GRIDSIZE; j++) {
            dx = snprintf(buff2 + cx, 256 - cx, "%d ", puzzle[i][j]);
            cx = cx + dx;
        }

    }
    dx = snprintf(buff2 + cx, 256 - cx, "%f ", timeo);
    cx = cx + dx;
    return buff2;
}



int main(int argc, char** argv) {

    int thread_num;
    g_finished = 0;
    int puzzle[GRIDSIZE][GRIDSIZE] = { 0 }; // Array to store problem


    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&is_fin, NULL);
    pthread_rwlockattr_init (&mylock_attr);
    // Preferring writer-locks for rwlocks
    pthread_rwlockattr_setkind_np(&mylock_attr,
          PTHREAD_RWLOCK_PREFER_WRITER_NP);
    pthread_rwlock_init(&lock , &mylock_attr);


    // Converting problem from **argv to 2d integer array
    int c3 = 1;
    for (int c = 0; c < GRIDSIZE; c++) {
        for (int c2 = 0; c2 < GRIDSIZE; c2++, c3++) {
            puzzle[c][c2] = atoi(argv[c3]);
        }
    }

    // Getting number of threads to use
    thread_num = atoi(argv[c3]);
    // thread_num = 1;

    // Initializing socket for client side
    struct sockaddr_in serv_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, '0', sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);
    connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));



    srand(time(NULL));

    boardz *p[thread_num];

    // Allocating memory and initializing structures for thread parameters
    for (int i = 0; i < thread_num; i++) {
        p[i] = (boardz *) malloc (sizeof(boardz));

        memcpy(p[i]->board, puzzle, GRIDSIZE * GRIDSIZE * sizeof(int));

        p[i]->completed = 0;
        p[i]->start = (float)GRIDSIZE/thread_num * i;
        p[i]->row = rand() % 9;
        p[i]->col = rand() % 9;
    }


    // Start measuring time
    clock_gettime(CLOCK_MONOTONIC, &g_start);

    pthread_t t[thread_num];
    // Starting threads
    for (int i = 0; i < thread_num; i++) {

        pthread_create(&t[i], NULL, solveSudoku, (void *) p[i]);
    }


    pthread_mutex_lock(&mutex);

    // Waiting till one of the threads has completed execution
    while (0 == g_finished)  pthread_cond_wait(&is_fin, &mutex);


    pthread_mutex_unlock(&mutex);
    pthread_mutex_destroy(&mutex);
    pthread_rwlock_destroy(&lock);
    pthread_cond_destroy(&is_fin);
    return 0;
    // Main function finishes execution and all other threads terminated
}
# sudoku_pthread

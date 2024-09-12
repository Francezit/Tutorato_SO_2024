// GESTIONE DEI PUNTATORI IN OS

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <ctype.h>

typedef struct
{
    int c;
    float d;

} data_t;

// MEMORIA STATICA
int x;
data_t shared_data;

void fun()
{
    // MEMORIA STACK
    int t = 10;
    // MEMORIA DINAMICA (O HEAP)
    int* v=(int*)malloc(sizeof(int));
}

void *thread_function(void *args)
{
    data_t *dt = (data_t *)args;
    while (1)
    {
        sleep(1);
        printf("%d\n", dt->c);
    }
}

pthread_t crea_thread()
{
    data_t data_in_stack;
    data_in_stack.c = 10;

    pthread_t id;
    pthread_create(&id, NULL, thread_function, &data_in_stack);
    sleep(10);
    return id;
}

void main(int argc, char **argv)
{
    // Perchè dopo 10 secondi non stampa piu 10?
    pthread_t id = crea_thread();
    pthread_join(id, NULL);
}
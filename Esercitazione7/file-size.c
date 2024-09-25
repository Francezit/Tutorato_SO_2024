#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>

#define MAX_PATH_SIZE 100
#define BUFFER_CAPACITY 10

typedef struct
{
    char file_path[MAX_PATH_SIZE];
    long file_size;
} record_t;

typedef struct
{
    char buffer[BUFFER_CAPACITY][MAX_PATH_SIZE];
    int buffer_head;
    int buffer_tail;
    pthread_mutex_t buffer_mutex;
    sem_t buffer_empty;
    sem_t buffer_full;

    record_t file_info;
    pthread_mutex_t file_read_mutex;
    pthread_mutex_t file_write_mutex;

    int *status_dir_threads;
    int n_dirs;
    int exit;

} shared_data_t;

typedef struct
{
    int id;
    char dir_path[MAX_PATH_SIZE];
    shared_data_t *shared_data;
    pthread_t thread_id;

} dir_thread_args_t;

int is_regular_file(char *file_path)
{
    struct stat file_stat;
    stat(file_path, &file_stat);
    return S_ISREG(file_stat.st_mode);
}

int all(int *status, int size)
{
    for (int i = 0; i < size; i++)
    {
        if (status[i] == 0)
            return 0;
    }
    return 1;
}

void *dir_thread_function(void *args)
{
    dir_thread_args_t *dt = (dir_thread_args_t *)args;
    shared_data_t *shared_data = dt->shared_data;

    printf("[D-%d] scansione della cartella '%s'\n", dt->id + 1, dt->dir_path);

    DIR *dir = opendir(dt->dir_path);
    if (dir == NULL)
    {
        printf("[D-%d] non e' stato possibile aprire la cartella '%s'\n", dt->id + 1, dt->dir_path);
        pthread_exit(NULL);
    }

    char full_file_path[1024];
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, "/..") != 0 && strcmp(entry->d_name, "/.") != 0)
        {
            snprintf(full_file_path, 1024, "%s/%s", dt->dir_path, entry->d_name);
            if (is_regular_file(full_file_path))
            {
                sem_wait(&shared_data->buffer_empty);
                pthread_mutex_lock(&shared_data->buffer_mutex);

                printf("[D-%d] trovato il file '%s' in '%s'\n", dt->id + 1, full_file_path, dt->dir_path);
                strcpy(shared_data->buffer[shared_data->buffer_tail], full_file_path);
                shared_data->buffer_tail = (shared_data->buffer_tail + 1) % BUFFER_CAPACITY;

                pthread_mutex_unlock(&shared_data->buffer_mutex);
                sem_post(&shared_data->buffer_full);
            }
        }
    }

    closedir(dir);

    printf("[D-%d] La cartella '%s' e' stata processata\n", dt->id + 1, dt->dir_path);
    shared_data->status_dir_threads[dt->id] = 1;
}

void *stat_thread_function(void *args)
{
    shared_data_t *shared_data = (shared_data_t *)args;

    char file_path[MAX_PATH_SIZE];
    long file_size;
    do
    {
        sem_wait(&shared_data->buffer_full);
        pthread_mutex_lock(&shared_data->buffer_mutex);

        strcpy(file_path, shared_data->buffer[shared_data->buffer_head]);
        shared_data->buffer_head = (shared_data->buffer_head + 1) % BUFFER_CAPACITY;

        pthread_mutex_unlock(&shared_data->buffer_mutex);
        sem_post(&shared_data->buffer_empty);

        struct stat file_stat;
        stat(file_path, &file_stat);
        file_size = file_stat.st_size;

        printf("[STAT] il file '%s' ha dimensione di %ld bytes\n", file_path, file_size);

        pthread_mutex_lock(&shared_data->file_write_mutex);
        shared_data->file_info.file_size = file_size;
        strcpy(shared_data->file_info.file_path, file_path);
        pthread_mutex_unlock(&shared_data->file_read_mutex);
        
    } while (!(all(shared_data->status_dir_threads, shared_data->n_dirs) && shared_data->buffer_head == shared_data->buffer_tail));

    printf("[STAT] fine\n");
    shared_data->exit = 1;
    pthread_mutex_unlock(&shared_data->file_read_mutex);
}

void main(int argc, char **argv)
{
    int n_dirs = argc - 1;
    if (n_dirs == 0)
    {
        perror("Argomenti non validi");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < n_dirs; i++)
    {
        char *dir_path = argv[i + 1];
        struct stat dir_stat;
        if (stat(dir_path, &dir_stat) < 0 || !S_ISDIR(dir_stat.st_mode))
        {
            perror("Errore negli argomenti");
            exit(EXIT_FAILURE);
        }
    }

    shared_data_t *data = (shared_data_t *)malloc(sizeof(shared_data_t));
    data->buffer_head = 0;
    data->buffer_tail = 0;
    data->n_dirs = n_dirs;
    data->status_dir_threads = (int *)calloc(n_dirs, sizeof(int));
    if (sem_init(&data->buffer_empty, 0, BUFFER_CAPACITY) < 0 || sem_init(&data->buffer_full, 0, 0) < 0)
    {
        perror("Errore buffer_sem");
        exit(EXIT_FAILURE);
    }

    if (pthread_mutex_init(&data->buffer_mutex, NULL) < 0)
    {
        perror("Errore buffer_mutex");
        exit(EXIT_FAILURE);
    }

    if (pthread_mutex_init(&data->file_read_mutex, NULL) < 0 || pthread_mutex_init(&data->file_write_mutex, NULL) < 0)
    {
        perror("Errore file_mutex");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_lock(&data->file_read_mutex);

    pthread_t stat_thread_id;
    if (pthread_create(&stat_thread_id, NULL, stat_thread_function, data) < 0)
    {
        perror("Errore nella creazione del stat thread");
        exit(EXIT_FAILURE);
    }

    dir_thread_args_t **dir_threads = (dir_thread_args_t **)malloc(sizeof(dir_thread_args_t *) * n_dirs);
    for (int i = 0; i < n_dirs; i++)
    {
        dir_thread_args_t *dir_thread = (dir_thread_args_t *)malloc(sizeof(dir_thread_args_t));
        strcpy(dir_thread->dir_path, argv[i + 1]);
        dir_thread->shared_data = data;
        dir_thread->id = i;

        if (pthread_create(&dir_thread->thread_id, NULL, dir_thread_function, dir_thread) < 0)
        {
            perror("Errore nella creazione del dir thread");
            exit(EXIT_FAILURE);
        }

        dir_threads[i] = dir_thread;
    }

    long total_size = 0;
    while (data->exit == 0)
    {
        pthread_mutex_lock(&data->file_read_mutex);
        if (data->exit != 0)
        {
            break;
        }
        total_size += data->file_info.file_size;
        printf("[MAIN] con il file '%s' il totale parziale e' %ld\n", data->file_info.file_path, total_size);
        pthread_mutex_unlock(&data->file_write_mutex);
    }

    printf("[MAIN] la dimensione totale e' %ld bytes\n", total_size);

    pthread_mutex_destroy(&data->buffer_mutex);
    pthread_mutex_destroy(&data->file_read_mutex);
    pthread_mutex_destroy(&data->file_write_mutex);

    sem_destroy(&data->buffer_empty);
    sem_destroy(&data->buffer_full);

    for (int i = 0; i < n_dirs; i++)
    {
        free(dir_threads[i]);
    }
    free(dir_threads);
    free(data->status_dir_threads);
    free(data);

    exit(EXIT_SUCCESS);
}
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>

#define PATH_MAX_SIZE 100
#define BUFFER_SIZE 1024
#define STACK_CAPACITY 10

typedef struct
{
    char buffer[BUFFER_SIZE];
    char filename[PATH_MAX_SIZE];
    long file_size;
    long offset;
    int buffer_size;
    int end_of_file;
} record_t;

typedef struct
{
    record_t stack[STACK_CAPACITY];
    int stack_ptr;
    int exit;

    pthread_mutex_t mutex; // accesso allo stack
    sem_t empty;           // semaforo per bloccare il writer se non c'è nulla nello stack
    sem_t full;            //  semaforo per bloccare il reader se lo stack è pieno

} shared_data_t;

typedef struct
{
    int id;
    char filename[PATH_MAX_SIZE];
    shared_data_t *data;
} reader_thread_arg_t;

typedef struct
{
    char dest_folder[PATH_MAX_SIZE];
    shared_data_t *data;

} writer_thread_arg_t;

void path_join(char *dir, char *name, char *dest)
{
    strcpy(dest, dir);
    size_t len = strlen(dest);
    if (len > 0 && dest[len - 1] == '/')
    {
        dest[len - 1] = '\0';
    }
    if (name[0] != '/')
    {
        strcat(dest, "/");
    }
    strcat(dest, name);
}

void *reader_thread(void *args)
{
    reader_thread_arg_t *dt = (reader_thread_arg_t *)args;
    shared_data_t *shared_data = dt->data;

    // apro il file
    int file_id = open(dt->filename, O_RDONLY);
    if (file_id < 0)
    {
        printf("[READER-%d] impossibile leggere il file '%s'\n", dt->id + 1, dt->filename);
        return NULL;
    }

    // recupero le statisitiche per recuperarmi la dimensione
    struct stat stat_file;
    if (fstat(file_id, &stat_file) < 0)
    {
        printf("[READER-%d] fstat error\n", dt->id + 1);
        close(file_id);
        return NULL;
    }
    printf("[READER-%d] lettura del file '%s di %ld byte\n", dt->id + 1, dt->filename, stat_file.st_size);

    // creo la mappatura in memoria
    char *file_map = mmap(NULL, stat_file.st_size, PROT_READ, MAP_SHARED, file_id, 0);
    if (file_map == MAP_FAILED)
    {
        printf("[READER-%d] nmap error\n", dt->id + 1);
        close(file_id);
        return NULL;
    }

    long offset = 0;
    while (offset < stat_file.st_size)
    {
        // STACK PUSH
        sem_wait(&shared_data->empty);
        pthread_mutex_lock(&shared_data->mutex);

        record_t *record = &shared_data->stack[++shared_data->stack_ptr];

        // verifico quanti byte leggere e scrivo il blocco nel record
        int len = 0;
        if (offset + BUFFER_SIZE > stat_file.st_size)
        {
            len = stat_file.st_size - offset;
            record->end_of_file = 1;
        }
        else
        {
            len = BUFFER_SIZE;
            record->end_of_file = 0;
        }
        record->buffer_size = len;
        record->file_size = stat_file.st_size;
        record->offset = offset;
        strcpy(record->filename, dt->filename);
        strncpy(record->buffer, file_map + offset, len);
        printf("[READER-%d] lettura del blocco di offset %ld di %d byte\n", dt->id + 1, record->offset, len);

        pthread_mutex_unlock(&shared_data->mutex);
        sem_post(&shared_data->full);

        offset += len;
    }

    // chiudo il file
    munmap(file_map, stat_file.st_size);
    close(file_id);

    printf("[READER-%d] lettura del file '%s' completata\n", dt->id + 1, dt->filename);

    return NULL;
}

void *writer_thread(void *args)
{
    writer_thread_arg_t *dt = (writer_thread_arg_t *)args;
    shared_data_t *shared_data = dt->data;

    struct stat folder_stat;
    if (stat(dt->dest_folder, &folder_stat) && S_ISDIR(folder_stat.st_mode))
    {
        printf("[WRITER] la cartella è gia presente\n");
    }
    else
    {
        mkdir(dt->dest_folder, 0755);
        printf("[WRITER] la cartella è stata creata\n");
    }

    char temp_path[PATH_MAX_SIZE];
    while (1)
    {
        // STACK POP
        sem_wait(&shared_data->full);
        pthread_mutex_lock(&shared_data->mutex);
        if (shared_data->exit != 0 && shared_data->stack_ptr < 0)
        {
            break;
        }

        record_t *record = &shared_data->stack[shared_data->stack_ptr--];

        // get path
        path_join(dt->dest_folder, basename(record->filename), temp_path);

        // se il file non esiste (controllo se devo scrivere il primo blocco) lo creo e
        // riservo il blocco di memoria, se esiste lo apro
        int file_id;
        if (record->offset == 0)
        {
            // creo un file con una dimensione pari al file da copiare.
            file_id = open(temp_path, O_RDWR | O_CREAT | O_TRUNC, 0666);
            if (file_id < 0)
            {
                printf("[WRITER] errore nella creazione del file '%s'\n", temp_path);
            }
            else
            {
                lseek(file_id, record->file_size - 1, SEEK_SET);
                write(file_id, "", 1);
                close(file_id);
                printf("[WRITER] creazione del file '%s' di dimensione %ld byte\n", temp_path, record->file_size);
            }
        }

        file_id = open(temp_path, O_RDWR);
        if (file_id < 0)
        {
            printf("[WRITER] errore nella apertura del file '%s' per la scrittura del blocco di offset %ld di %d byte\n", temp_path, record->offset, record->buffer_size);
        }
        else
        {
            // scrivo il blocco
            char *file_map = mmap(NULL, record->file_size, PROT_WRITE, MAP_SHARED, file_id, 0);
            if (file_map == MAP_FAILED)
            {
                printf("[WRITER] nmap error\n");
                close(file_id);
                return NULL;
            }

            strncpy(file_map + record->offset, record->buffer, record->buffer_size);
            if (msync(file_map, record->file_size, MS_SYNC) < 0)
            {
                printf("[WRITER] errore nella scrittura nel blocco di offset %ld di %d byte sul file '%s'\n", record->offset, record->buffer_size, temp_path);
            }
            else
            {
                printf("[WRITER] scrittura nel blocco di offset %ld di %d byte sul file '%s'\n", record->offset, record->buffer_size, temp_path);
            }

            munmap(file_map, record->file_size);
            close(file_id);
        }

        pthread_mutex_unlock(&shared_data->mutex);
        sem_post(&shared_data->empty);
    }

    return NULL;
}

void main(int argc, char **argv)
{
    // Controllo gli argomenti
    if (argc <= 2)
    {
        perror("Argomenti non validi");
        exit(EXIT_FAILURE);
    }

    int n_files = argc - 2;
    printf("[MAIN] duplicazione di %d file\n", n_files);

    // creo le strutture dati condivise
    shared_data_t *shared_data = (shared_data_t *)malloc(sizeof(shared_data_t));
    shared_data->stack_ptr = -1;
    shared_data->exit = 0;
    if (pthread_mutex_init(&shared_data->mutex, NULL) < 0)
    {
        perror("Errore nella creazione del mutex");
        exit(EXIT_FAILURE);
    }
    if (sem_init(&shared_data->empty, 0, STACK_CAPACITY) < 0 || sem_init(&shared_data->full, 0, 0) < 0)
    {
        perror("Errore nella creazione del sem");
        exit(EXIT_FAILURE);
    }

    // creo il writer
    pthread_t writer_thread_id;
    writer_thread_arg_t *writer = (writer_thread_arg_t *)malloc(sizeof(writer_thread_arg_t));
    writer->data = shared_data;
    strcpy(writer->dest_folder, argv[argc - 1]);
    if (pthread_create(&writer_thread_id, NULL, writer_thread, writer) < 0)
    {
        perror("Errore nella creazione del thread");
        exit(EXIT_FAILURE);
    }

    // creo i readers
    pthread_t *reader_thread_ids = (pthread_t *)malloc(sizeof(pthread_t) * n_files);
    reader_thread_arg_t **readers = (reader_thread_arg_t **)malloc(sizeof(reader_thread_arg_t *) * n_files);
    for (int i = 0; i < n_files; i++)
    {
        reader_thread_arg_t *reader = (reader_thread_arg_t *)malloc(sizeof(reader_thread_arg_t));
        reader->data = shared_data;
        reader->id = i;
        strcpy(reader->filename, argv[i + 1]);
        if (pthread_create(&reader_thread_ids[i], NULL, reader_thread, reader) < 0)
        {
            perror("Errore nella creazione del thread");
            exit(EXIT_FAILURE);
        }
        printf("Thread creato %s\n", reader->filename);

        readers[i] = reader;
    }

    // aspetto che i thread terminano
    for (int i = 0; i < n_files; i++)
    {
        pthread_join(reader_thread_ids[i], NULL);
        free(readers[i]);
    }
    free(readers);

    shared_data->exit = 1;
    sem_post(&shared_data->full);
    pthread_join(writer_thread_id, NULL);
    free(writer);

    pthread_mutex_destroy(&shared_data->mutex);
    sem_destroy(&shared_data->empty);
    sem_destroy(&shared_data->full);
    free(shared_data);
}
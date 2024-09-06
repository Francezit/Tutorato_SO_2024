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

#define KEYS_SIZE 30
#define BUFFER_SIZE 100

typedef struct shared_data
{
    char buffer[BUFFER_SIZE];
    sem_t buffer_sem; // ha la funzione di gestire l'accesso al buffer
    int exit;
} shared_data_t;

typedef struct thread_data
{
    char key[KEYS_SIZE];
    int index;
    shared_data_t *data;
    sem_t read_sem;
    sem_t write_sem;

} thread_data_t;

char *decrypt(char *text, char *keys)
{
    char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int n = strlen(text);
    int v = strlen(keys);

    char *dec_text = (char *)malloc(sizeof(char) * n);
    for (int i = 0; i < n; i++)
    {
        // strchr trova la prima occurenza di un carattere è restituisce un puntatore a tale char
        char *index_c = strchr(keys, text[i]);
        if (index_c != NULL)
        {
            int index = index_c - keys;
            dec_text[i] = alphabet[index];
        }
        else
        {
            dec_text[i] = text[i];
        }
    }
    return dec_text;
}

int parse_text(char *text, char *cifar_text, int *index_key)
{
    int sep_index = -1;
    int len = strlen(text);
    for (int i = 0; i < len; i++)
    {
        if (text[i] == ':')
        {
            sep_index = i;
            break;
        }
    }
    if (sep_index < 0)
        return -1;

    char *index_text = (char *)malloc(sizeof(char) * sep_index);
    for (int i = 0; i < sep_index; i++)
    {
        index_text[i] = text[i];
    }

    for (int i = sep_index + 1, j = 0; i < len - 1; i++, j++)
    {
        cifar_text[j] = text[i];
    }

    *index_key = atoi(index_text);
    free(index_text);
    return 0;
}

void *thread_function(void *args)
{
    thread_data_t *dt = (thread_data_t *)args;
    printf("[K%d] chiave assegnata : %s\n", dt->index, dt->key);
    char buffer[BUFFER_SIZE];
    while (dt->data->exit == 0)
    {
        // blocco il thread
        sem_wait(&dt->read_sem);
        if (dt->data->exit != 0)
            break;

        sem_wait(&dt->data->buffer_sem);

        // leggo la stringa cifrata dal buffer globale
        strcpy(buffer, dt->data->buffer);

        int d = strlen(buffer);
        printf("[K%d] sto decifando la frase di %d  caratteri passata dal main\n", dt->index, d);

        // decifro e copio il riusltato nel  buffer globale
        char *dec = decrypt(buffer, dt->key);
        strcpy(dt->data->buffer, dec);
        free(dec);

        sem_post(&dt->data->buffer_sem);

        //  notificare al main che ha completato
        sem_post(&dt->write_sem);
    }
    return NULL;
}

void main(int argc, char **argv)
{
    if (argc < 3)
    {
        perror("Numero di parametri non valido");
        exit(EXIT_FAILURE);
    }

    char *keys_input_filename = argv[1];
    char *cifar_input_filename = argv[2];
    char *ouput_filename = argc > 3 ? argv[3] : "output.txt";

    printf("[M] Leggo il file delle chiavi");
    FILE *key_fp = fopen(keys_input_filename, "r");
    if (key_fp == NULL)
    {
        perror("File delle chiavi non valido");
        exit(EXIT_FAILURE);
    }

    // recupero il numero delle chiavi
    int num_keys = 0;
    while (!feof(key_fp))
    {
        if (fgetc(key_fp) == '\n')
        {
            num_keys++;
        }
    }
    fseek(key_fp, 0, SEEK_SET);
    printf("[M] trovate %d chiavi, creo i thread k-i necessari\n", num_keys);

    // inizializzo le strutture dati
    shared_data_t *data = (shared_data_t *)malloc(sizeof(shared_data_t));
    thread_data_t **thread_data_array = (thread_data_t **)malloc(sizeof(thread_data_t *) * num_keys);

    int j = 0;
    char key_buffer[KEYS_SIZE];
    while (fgets(key_buffer, KEYS_SIZE, key_fp) != NULL)
    {
        thread_data_t *item = (thread_data_t *)malloc(sizeof(thread_data_t));
        item->data = data;
        item->index = j;
        strcpy(item->key, key_buffer);
        thread_data_array[j] = item;

        // viene inizializzato a 0 cosi il thread è bloccato inizialmente
        if (sem_init(&item->read_sem, 0, 0) < 0)
        {
            perror("Errore nella creazione del semaforo read_sem");
            exit(EXIT_FAILURE);
        }
        // viene inizializzato a 0 cosi il thread è bloccato inizialmente
        if (sem_init(&item->write_sem, 0, 0) < 0)
        {
            perror("Errore nella creazione del semaforo write_sem");
            exit(EXIT_FAILURE);
        }

        j++;
    }
    fclose(key_fp);

    // viene inizializzato a 1 cosi il primo thread che arriva puo procedere
    if (sem_init(&data->buffer_sem, 0, 1) < 0)
    {
        perror("Errore nella creazione del semaforo buffer_sem");
        exit(EXIT_FAILURE);
    }
    data->exit = 0;

    // creo i threads
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * num_keys);
    for (int i = 0; i < num_keys; i++)
    {
        if (pthread_create(&threads[i], NULL, thread_function, thread_data_array[i]) < 0)
        {
            perror("Errore nella creazione del thread");
            exit(EXIT_FAILURE);
        }
    }

    printf("[M] Processo il file cifrato\n");
    FILE *cif_fp = fopen(cifar_input_filename, "r");
    if (cif_fp == NULL)
    {
        perror("Errore nell'apertura del file cifrato");
        exit(EXIT_FAILURE);
    }

    char buffer[BUFFER_SIZE];
    char text[BUFFER_SIZE];
    int key_index;
    while (fgets(buffer, BUFFER_SIZE, cif_fp) != NULL)
    {
        if (parse_text(buffer, text, &key_index) < 0)
        {
            perror("File cifrato non valido");
            exit(EXIT_FAILURE);
        }

        printf("[M] la riga '%s' deve essere decifrata con la chiave n. %d\n", text, key_index);

        sem_wait(&data->buffer_sem);                        // blocco la lettura del testo
        sem_post(&thread_data_array[key_index]->read_sem);  // risveglio il thread
        strcpy(data->buffer, text);                         // inserisco il testo da decifrare
        sem_post(&data->buffer_sem);                        // libero la lettura del testo cosi il thread puo processare l'elemento
        sem_wait(&thread_data_array[key_index]->write_sem); // aspetto che il thread finisce di processare

        // sem_wait(&data->buffer_sem);
        strcpy(buffer, data->buffer);
        // sem_post(&data->buffer_sem);

        printf("[M] la riga e' stata decifrata in %s\n", buffer);
    }
    fclose(cif_fp);

    // chiusura pulita del programma
    printf("[M] Termino i thread\n");
    data->exit = 1;
    printf("[M] Aspetto che tutti i thread terminano e dealloco tutte le risorse\n");
    for (int i = 0; i < num_keys; i++)
    {
        sem_post(&thread_data_array[i]->read_sem);
        pthread_join(threads[i], NULL);

        sem_destroy(&thread_data_array[i]->read_sem);
        sem_destroy(&thread_data_array[i]->write_sem);
        free(thread_data_array[i]);
    }

    free(thread_data_array);
    sem_destroy(&data->buffer_sem);
    free(data);
}
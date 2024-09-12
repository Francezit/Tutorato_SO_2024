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

#define N_OPERATIONS 3
#define OP_CODE_ADD 0
#define OP_CODE_SUB 1
#define OP_CODE_MULT 2
#define OPERATION_SYMBOLS {'+', '-', 'x'}
#define OPERATION_CODES {OP_CODE_ADD, OP_CODE_SUB, OP_CODE_MULT}
#define OPERATION_NAMES {"ADD", "SUB", "MULT"}

#define STATUS_SUCCESS 0
#define STATUS_FAILURE 1
#define STATUS_PROCESSING 2

#define BUFFER_SIZE 100

int get_op_code(char op)
{
    char ops[] = OPERATION_SYMBOLS;
    for (int i = 0; i < N_OPERATIONS; i++)
    {
        if (ops[i] == op)
        {
            return i;
        }
    }
    return -1;
}

typedef struct
{
    long long operando_1;
    long long operando_2;
    long long risultato;
    int op_code;
    int richiedente;
    int exit;

    pthread_mutex_t read_mutex;  // usato per leggere questa struttura dati
    pthread_mutex_t write_mutex; // usato per scrivere in questa struttura dati
    sem_t *op_sem;               // è presente un semaforo per ogni op threads
    sem_t *calc_sem;             // è presente un semaforo per ogni calc threads;

} shared_data_t;

typedef struct
{
    int codice;
    char *filename;
    shared_data_t *shared_data;
    pthread_t thread_id;
    int status;

} calc_thread_arguments_t;

typedef struct
{
    char op_symbol;
    char *op_name;
    int op_code;
    shared_data_t *shared_data;
    pthread_t thread_id;

} op_thread_arguments_t;

void *op_thread_function(void *args)
{
    op_thread_arguments_t *dt = (op_thread_arguments_t *)args;
    shared_data_t *shared_data = dt->shared_data;

    while (shared_data->exit == 0)
    {
        sem_wait(&shared_data->op_sem[dt->op_code]);
        if (shared_data->exit != 0)
        {
            break;
        }

        pthread_mutex_lock(&shared_data->write_mutex);
        switch (dt->op_code)
        {
        case OP_CODE_ADD:
            shared_data->risultato = shared_data->operando_1 + shared_data->operando_2;
            break;
        case OP_CODE_MULT:
            shared_data->risultato = shared_data->operando_1 * shared_data->operando_2;
            break;
        case OP_CODE_SUB:
            shared_data->risultato = shared_data->operando_1 - shared_data->operando_2;
            break;
        default:
            shared_data->risultato = -1;
            break;
        }
        printf("[%s] calcolo effettuato: %lld %c %lld = %lld\n", dt->op_name, shared_data->operando_1, dt->op_symbol, shared_data->operando_2, shared_data->risultato);
        pthread_mutex_unlock(&shared_data->write_mutex);

        sem_post(&shared_data->calc_sem[shared_data->richiedente]);
    }
}

void *calc_thread_function(void *args)
{
    calc_thread_arguments_t *dt = (calc_thread_arguments_t *)args;
    shared_data_t *shared_data = dt->shared_data;

    printf("[CALC-%d] file da verifica %s\n", dt->codice + 1, dt->filename);

    // apro il file
    FILE *fp = fopen(dt->filename, "r");
    if (fp == NULL)
    {
        dt->status = STATUS_FAILURE;
        return NULL;
    }
    else
    {
        dt->status = STATUS_PROCESSING;
    }

    // leggo il valore base
    long long base_value;
    if (fscanf(fp, "%lld\n", &base_value) != 1)
    {
        fclose(fp);
        dt->status = STATUS_FAILURE;
        return NULL;
    }
    printf("[CALC-%d] il valore iniziale della computazione: %lld\n", dt->codice + 1, base_value);

    // processo il file
    char buffer[BUFFER_SIZE];
    char op_temp;
    int op_code_temp;
    long long value_temp;
    long long current_value = base_value;
    while (fgets(buffer, BUFFER_SIZE, fp) != NULL)
    {
        if (sscanf(buffer, "%lld\n", &value_temp) == 1)
        {
            if (current_value == value_temp)
            {
                fclose(fp);
                printf("[CALC-%d] computazione terminata in modo corretto: %lld\n", dt->codice + 1, current_value);
                dt->status = STATUS_SUCCESS;
                return NULL;
            }
            else
            {
                fclose(fp);
                printf("[CALC-%d] computazione terminata in modo errato: calcolato %lld previsto %lld\n", dt->codice + 1, current_value, value_temp);
                dt->status = STATUS_FAILURE;
                return NULL;
            }
        }
        else if (sscanf(buffer, "%c %lld\n", &op_temp, &value_temp) == 2)
        {
            op_code_temp = get_op_code(op_temp);
            printf("[CALC-%d] prossima operazione: '%c %lld'\n", dt->codice + 1, op_temp, value_temp);

            // blocco l'accesso alla memoria condivisa
            pthread_mutex_lock(&shared_data->read_mutex);

            pthread_mutex_lock(&shared_data->write_mutex);
            shared_data->richiedente = dt->codice;
            shared_data->operando_1 = current_value;
            shared_data->operando_2 = value_temp;
            // risveglio op_thread corrispondente permettendolo di procedere
            sem_post(&shared_data->op_sem[op_code_temp]);
            // libero la memoria condivisa cosi il op thread puo leggere ed effettuare il calcolo
            pthread_mutex_unlock(&shared_data->write_mutex);
            // aspetto che op thread finisca il calcolo
            sem_wait(&shared_data->calc_sem[dt->codice]);
            // aggiorno il valore corrente
            printf("[CALC-%d] risultato ricevuto: %lld\n", dt->codice + 1, shared_data->risultato);
            current_value = shared_data->risultato;

            pthread_mutex_unlock(&shared_data->read_mutex);
        }
        else
        {
            fclose(fp);
            dt->status = STATUS_FAILURE;
            return NULL;
        }
    }

    return NULL;
}

void main(int argc, char **argv)
{
    // processo gli argomenti
    int n_calc_file = argc - 1;
    if (n_calc_file == 0)
    {
        perror("Argomenti non validi");
        exit(EXIT_FAILURE);
    }

    // inizializzo la struttura dati condivisa
    shared_data_t *shared_data = (shared_data_t *)malloc(sizeof(shared_data_t));
    shared_data->exit = 0;

    shared_data->calc_sem = (sem_t *)malloc(sizeof(sem_t) * n_calc_file);
    for (int i = 0; i < n_calc_file; i++)
    {
        if (sem_init(&shared_data->calc_sem[i], 0, 0) < 0)
        {
            perror("Errore semaforo calc_sem");
            exit(EXIT_FAILURE);
        }
    }

    shared_data->op_sem = (sem_t *)malloc(sizeof(sem_t) * N_OPERATIONS);
    for (int i = 0; i < N_OPERATIONS; i++)
    {
        if (sem_init(&shared_data->op_sem[i], 0, 0) < 0)
        {
            perror("Errore semaforo op_sem");
            exit(EXIT_FAILURE);
        }
    }

    if (pthread_mutex_init(&shared_data->write_mutex, NULL) < 0 || pthread_mutex_init(&shared_data->read_mutex, NULL) < 0)
    {
        perror("Errore nella creazione del mutex");
        exit(EXIT_FAILURE);
    }

    //  creo i thread cal con gli elementi di sincronizzazione
    calc_thread_arguments_t **calc_thread_args = (calc_thread_arguments_t **)malloc(sizeof(calc_thread_arguments_t *) * n_calc_file);
    for (int i = 0; i < argc - 1; i++)
    {
        calc_thread_arguments_t *arg = (calc_thread_arguments_t *)malloc(sizeof(calc_thread_arguments_t));
        arg->filename = argv[i + 1];
        arg->codice = i;
        arg->shared_data = shared_data;

        if (pthread_create(&arg->thread_id, NULL, calc_thread_function, arg) < 0)
        {
            perror("Errore nella creazione del thread");
            exit(EXIT_FAILURE);
        }

        calc_thread_args[i] = arg;
    }

    // creo il thread delle operazione
    char op_codes[] = OPERATION_CODES;
    char *op_names[] = OPERATION_NAMES;
    char op_symbols[] = OPERATION_SYMBOLS;
    op_thread_arguments_t **op_thread_args = (op_thread_arguments_t **)malloc(sizeof(op_thread_arguments_t) * N_OPERATIONS);
    for (int i = 0; i < N_OPERATIONS; i++)
    {
        op_thread_arguments_t *op_thread = (op_thread_arguments_t *)malloc(sizeof(op_thread_arguments_t));
        op_thread->op_code = op_codes[i];
        op_thread->op_name = op_names[i];
        op_thread->op_symbol = op_symbols[i];
        op_thread->shared_data = shared_data;

        if (pthread_create(&op_thread->thread_id, NULL, op_thread_function, op_thread) < 0)
        {
            perror("Errore nella creazione del thread");
            exit(EXIT_FAILURE);
        }

        op_thread_args[i] = op_thread;
    }

    // aspetto che tutti i thread terminano spontaneamente
    int success_counter = 0;
    for (int i = 0; i < n_calc_file; i++)
    {
        pthread_join(calc_thread_args[i]->thread_id, NULL);
        if (calc_thread_args[i]->status == STATUS_SUCCESS)
            success_counter++;
    }
    printf("[MAIN] verifiche completate con successo %d/%d\n", success_counter, n_calc_file);

    shared_data->exit = 1;
    for (int i = 0; i < N_OPERATIONS; i++)
    {
        sem_post(&shared_data->op_sem[i]);
        pthread_join(op_thread_args[i]->thread_id, NULL);
    }

    // tutti i thread sono arrestati, posso procedere a deallocare tutte le risorse
    pthread_mutex_destroy(&shared_data->read_mutex);
    pthread_mutex_destroy(&shared_data->write_mutex);
    for (int i = 0; i < N_OPERATIONS; i++)
        sem_destroy(&shared_data->op_sem[i]);
    for (int i = 0; i < n_calc_file; i++)
        sem_destroy(&shared_data->calc_sem[i]);
    free(shared_data);

    for(int i=0;i<n_calc_file;i++){
        free(calc_thread_args[i]);
    }
    free(calc_thread_args);

     for(int i=0;i<N_OPERATIONS;i++){
        free(op_thread_args[i]);
    }
    free(op_thread_args);
}
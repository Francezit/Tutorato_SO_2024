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
#define STRING_TEMP_SIZE 20
#define BUFFER_SIZE 100

typedef struct
{
    char current_name[STRING_TEMP_SIZE];
    char current_present[STRING_TEMP_SIZE];

    pthread_mutex_t read_mutex;
    pthread_mutex_t write_mutex;

    pthread_barrier_t barrier;

    int *es_status;
    int n_letters;
    int exit;
} bn_data_t;

typedef struct
{
    char letter_filename[MAX_PATH_SIZE];
    bn_data_t *bn_data;
    int id;

    pthread_t thread_id;

} es_data_t;

typedef struct
{
    char goods_bads_filename[MAX_PATH_SIZE];
    bn_data_t *bn_data;
    pthread_t thread_id;

    char **child_names;
    int *child_status;
    int child_len;

    char current_child_name[STRING_TEMP_SIZE];
    int current_child_status;
    pthread_mutex_t mutex;
    pthread_cond_t cond1_mutex;
    pthread_cond_t cond2_mutex;

} ei_data_t;

typedef struct
{
    bn_data_t *bn_data;
    pthread_t thread_id;

    int n_letters;
    int n_child_goods;
    int n_child_bads;
    int total_cost;

    pthread_mutex_t read_mutex;
    pthread_mutex_t write_mutex;

    int current_child_status;
    int current_cost;

} ec_data_t;

typedef struct
{
    char presents_filename[MAX_PATH_SIZE];
    bn_data_t *bn_data;
    ec_data_t *ec_data;
    pthread_t thread_id;

    char **presents;
    int *costs;
    int presents_len;

    int current_child_status;
    char current_present_name[STRING_TEMP_SIZE];
    char current_child_name[STRING_TEMP_SIZE];
    pthread_mutex_t mutex;
    pthread_cond_t cond1_mutex;
    pthread_cond_t cond2_mutex;

} ep_data_t;

int read_line(FILE *fp, char *str1, char *str2)
{
    char line[BUFFER_SIZE];
    if (fgets(line, BUFFER_SIZE, fp) == NULL)
        return -1;

    int n = strcspn(line, "\n");
    line[n] = '\0';

    char *context = NULL;
    char *temp = strtok_r(line, ";", &context);
    if (temp == NULL)
        return -1;
    strcpy(str1, temp);
    temp = strtok_r(NULL, ";", &context);
    if (temp == NULL)
        return -1;
    strcpy(str2, temp);
    return 0;
}

void *es_thread_function(void *args)
{
    es_data_t *dt = (es_data_t *)args;

    printf("[ES%d] leggo le letterine dal file '%s'\n", dt->id + 1, dt->letter_filename);

    pthread_barrier_wait(&dt->bn_data->barrier);

    FILE *fp = fopen(dt->letter_filename, "r");
    if (fp == NULL)
    {
        printf("[ES%d] errore nell'apertura del file", dt->id + 1);
        pthread_exit(NULL);
    }

    char name_temp[STRING_TEMP_SIZE];
    char present_temp[STRING_TEMP_SIZE];
    while (read_line(fp, name_temp, present_temp) == 0)
    {
        pthread_mutex_lock(&dt->bn_data->write_mutex);
        strcpy(dt->bn_data->current_name, name_temp);
        strcpy(dt->bn_data->current_present, present_temp);
        pthread_mutex_unlock(&dt->bn_data->read_mutex);
    }
    fclose(fp);

    printf("[ES%d] non ho piu letterine da consegnare\n", dt->id + 1);
    dt->bn_data->es_status[dt->id] = 1;
    pthread_mutex_unlock(&dt->bn_data->read_mutex);
}

int all(int *v, int s)
{
    for (int i = 0; i < s; i++)
    {
        if (v[i] == 0)
        {
            return 0;
        }
    }
    return 1;
}

void *ei_thread_function(void *args)
{
    ei_data_t *dt = (ei_data_t *)args;

    FILE *fp = fopen(dt->goods_bads_filename, "r");
    if (fp == NULL)
    {
        printf("[EI] errore apertura file\n");
        pthread_exit(NULL);
    }

    int size = 0;
    char buffer[BUFFER_SIZE];
    while (fgets(buffer, BUFFER_SIZE, fp) != NULL)
    {
        size++;
    }
    rewind(fp);

    dt->child_names = (char **)malloc(sizeof(char *) * size);
    dt->child_status = (int *)malloc(sizeof(int) * size);

    int i = 0;
    char temp_name[STRING_TEMP_SIZE];
    char temp_status[STRING_TEMP_SIZE];
    while (read_line(fp, temp_name, temp_status) == 0)
    {
        dt->child_names[i] = malloc(sizeof(char) * strlen(temp_name));
        strcpy(dt->child_names[i], temp_name);

        if (strcmp(temp_status, "buono") == 0)
            dt->child_status[i] = 1;
        else
            dt->child_status[i] = -1;
        i++;
    }
    dt->child_len = i;
    fclose(fp);

    printf("[EI] Pronto, letti %d elementi\n", dt->child_len);
    pthread_barrier_wait(&dt->bn_data->barrier);

    while (dt->bn_data->exit == 0)
    {
        pthread_cond_wait(&dt->cond1_mutex, &dt->mutex);
        if (dt->bn_data->exit != 0)
        {
            break;
        }

        int search_index = -1;
        for (int i = 0; i < dt->child_len; i++)
        {
            if (strcmp(dt->child_names[i], dt->current_child_name) == 0)
            {
                search_index = i;
                break;
            }
        }

        if (search_index < 0)
        {
            dt->current_child_status = -1;
            printf("[EI] il bambino '%s' non è stato trovato\n", dt->current_child_name);
        }
        else
        {
            dt->current_child_status = dt->child_status[search_index];
            if (dt->current_child_status > 0)
            {
                printf("[EI] il bambino '%s' è stato buono\n", dt->current_child_name);
            }
            else
            {
                printf("[EI] il bambino '%s' è stato cattivo\n", dt->current_child_name);
            }
        }
        pthread_cond_signal(&dt->cond2_mutex);
    }

    printf("[EI] Terminato\n");
}

void *ep_thread_function(void *args)
{
    ep_data_t *dt = (ep_data_t *)args;

    FILE *fp = fopen(dt->presents_filename, "r");
    if (fp == NULL)
    {
        printf("[EP] errore apertura file\n");
        pthread_exit(NULL);
    }

    int size = 0;
    char buffer[BUFFER_SIZE];
    while (fgets(buffer, BUFFER_SIZE, fp) != NULL)
    {
        size++;
    }
    rewind(fp);

    dt->presents = (char **)malloc(sizeof(char *) * size);
    dt->costs = (int *)malloc(sizeof(int) * size);

    int i = 0;
    char temp_presents[STRING_TEMP_SIZE];
    char temp_cost[STRING_TEMP_SIZE];
    while (read_line(fp, temp_presents, temp_cost) == 0)
    {
        dt->presents[i] = malloc(sizeof(char) * strlen(temp_presents));
        strcpy(dt->presents[i], temp_presents);

        dt->costs[i] = atoi(temp_cost);
        i++;
    }
    dt->presents_len = i;
    fclose(fp);

    printf("[EP] Pronto\n");
    pthread_barrier_wait(&dt->bn_data->barrier);

    while (1)
    {
        pthread_cond_wait(&dt->cond1_mutex, &dt->mutex);
        if (dt->bn_data->exit != 0)
        {
            break;
        }

        int search_index = -1;
        for (int i = 0; i < dt->presents_len; i++)
        {
            if (strcmp(dt->presents[i], dt->current_present_name) == 0)
            {
                search_index = i;
                break;
            }
        }

        int cost = dt->costs[search_index];
        printf("[EP] creo il regalo '%s' per il bambino '%s' al costo di %d\n", dt->current_present_name, dt->current_child_name, cost);

        pthread_mutex_lock(&dt->ec_data->write_mutex);
        if (dt->bn_data->exit != 0)
        {
            break;
        }
        dt->ec_data->current_child_status = dt->current_child_status;
        dt->ec_data->current_cost = cost;
        pthread_mutex_unlock(&dt->ec_data->read_mutex);

        pthread_cond_signal(&dt->cond2_mutex);
    }

    printf("[EP] Terminato\n");
}

void *ec_thread_function(void *args)
{
    ec_data_t *dt = (ec_data_t *)args;

    dt->n_child_bads = 0;
    dt->n_child_goods = 0;
    dt->n_letters = 0;
    dt->total_cost = 0;

    printf("[EC] Pronto\n");
    pthread_barrier_wait(&dt->bn_data->barrier);

    while (1)
    {
        pthread_mutex_lock(&dt->read_mutex);
        if (dt->bn_data->exit != 0)
        {
            printf("[EC] Quest'anno abbiamo ricevuto %d richieste da %d bambini buoni e da %d cattivi con un costo totale di produzione di %d euro\n", dt->n_letters, dt->n_child_goods, dt->n_child_bads, dt->total_cost);
            break;
        }

        dt->n_letters++;
        dt->total_cost += dt->current_cost;
        if (dt->current_child_status > 0)
        {
            dt->n_child_goods++;
            printf("[EC] Aggiornate le statistiche dei bambini cattivi (%d)\n", dt->n_child_bads);
        }
        else
        {
            dt->n_child_bads++;
            printf("[EC] Aggiornate le statistiche dei bambini buoni (%d), costo totale %d\n", dt->n_child_goods, dt->total_cost);
        }

        pthread_mutex_unlock(&dt->write_mutex);
    }

    printf("[EC] Terminato\n");
}

void main(int argc, char **argv)
{
    if (argc < 4)
    {
        perror("Errore argomenti");
        exit(EXIT_FAILURE);
    }

    char *present_filename = argv[1];
    char *goods_bads_filename = argv[2];
    int n_letters = argc - 3;

    bn_data_t *bn_data = (bn_data_t *)malloc(sizeof(bn_data_t));
    bn_data->es_status = (int *)calloc(n_letters, sizeof(int));
    bn_data->n_letters = n_letters;
    bn_data->exit = 0;
    if (pthread_barrier_init(&bn_data->barrier, NULL, n_letters + 4) < 0)
    {
        perror("Errore barrier");
        exit(EXIT_FAILURE);
    }
    if (pthread_mutex_init(&bn_data->read_mutex, NULL) < 0 || pthread_mutex_init(&bn_data->write_mutex, NULL) < 0)
    {
        perror("Errore mutex");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_lock(&bn_data->read_mutex);

    ec_data_t *ec_data = (ec_data_t *)malloc(sizeof(ec_data_t));
    ec_data->bn_data = bn_data;
    if (pthread_create(&ec_data->thread_id, NULL, ec_thread_function, ec_data) < 0)
    {
        perror("Errore ec thread");
        exit(EXIT_FAILURE);
    }
    if (pthread_mutex_init(&ec_data->read_mutex, NULL) < 0 || pthread_mutex_init(&ec_data->write_mutex, NULL) < 0)
    {
        perror("Errore mutex");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_lock(&ec_data->read_mutex);

    ep_data_t *ep_data = (ep_data_t *)malloc(sizeof(ep_data_t));
    ep_data->bn_data = bn_data;
    ep_data->ec_data = ec_data;
    strcpy(ep_data->presents_filename, present_filename);
    if (pthread_create(&ep_data->thread_id, NULL, ep_thread_function, ep_data) < 0)
    {
        perror("Errore ep thread");
        exit(EXIT_FAILURE);
    }
    if (pthread_mutex_init(&ep_data->mutex, NULL) < 0 || pthread_cond_init(&ep_data->cond1_mutex, NULL) < 0 || pthread_cond_init(&ep_data->cond2_mutex, NULL) < 0)
    {
        perror("Errore ep mutex and cond");
        exit(EXIT_FAILURE);
    }

    ei_data_t *ei_data = (ei_data_t *)malloc(sizeof(ei_data_t));
    ei_data->bn_data = bn_data;
    strcpy(ei_data->goods_bads_filename, goods_bads_filename);
    if (pthread_create(&ei_data->thread_id, NULL, ei_thread_function, ei_data) < 0)
    {
        perror("Errore ei thread");
        exit(EXIT_FAILURE);
    }
    if (pthread_mutex_init(&ei_data->mutex, NULL) < 0 || pthread_cond_init(&ei_data->cond1_mutex, NULL) < 0 || pthread_cond_init(&ei_data->cond2_mutex, NULL) < 0)
    {
        perror("Errore ei mutex and cond");
        exit(EXIT_FAILURE);
    }

    es_data_t **es_list = (es_data_t **)malloc(sizeof(es_data_t *) * n_letters);
    for (int i = 0; i < n_letters; i++)
    {
        es_data_t *es_data = (es_data_t *)malloc(sizeof(es_data_t));
        es_data->id = i;
        es_data->bn_data = bn_data;
        strcpy(es_data->letter_filename, argv[i + 3]);
        if (pthread_create(&es_data->thread_id, NULL, es_thread_function, es_data) < 0)
        {
            perror("Errore nella creazione di es thread");
            exit(EXIT_FAILURE);
        }
        es_list[i] = es_data;
    }

    pthread_barrier_wait(&bn_data->barrier);
    printf("[BN] Pronto\n");

    char current_name[STRING_TEMP_SIZE];
    char current_present[STRING_TEMP_SIZE];
    int current_status;
    do
    {
        pthread_mutex_lock(&bn_data->read_mutex);
        if (all(bn_data->es_status, bn_data->n_letters))
        {
            break;
        }
        strcpy(current_name, bn_data->current_name);
        strcpy(current_present, bn_data->current_present);
        pthread_mutex_unlock(&bn_data->write_mutex);

        printf("[BN] come è stato il bambino '%s' ?\n", current_name);

        strcpy(ei_data->current_child_name, current_name);
        pthread_cond_signal(&ei_data->cond1_mutex);
        pthread_cond_wait(&ei_data->cond2_mutex, &ei_data->mutex);
        current_status = ei_data->current_child_status;

        if (current_status > 0)
        {
            ep_data->current_child_status = current_status;
            strcpy(ep_data->current_present_name, current_present);
            strcpy(ep_data->current_child_name, current_name);
            pthread_cond_signal(&ep_data->cond1_mutex);
            pthread_cond_wait(&ep_data->cond2_mutex, &ep_data->mutex);
        }
        else
        {
            pthread_mutex_lock(&ec_data->write_mutex);
            ec_data->current_child_status = -1;
            ec_data->current_cost = 0;
            pthread_mutex_unlock(&ec_data->read_mutex);
        }
    } while (all(bn_data->es_status, bn_data->n_letters) == 0);
    bn_data->exit = 1;

    pthread_mutex_unlock(&ec_data->read_mutex);
    pthread_join(ec_data->thread_id, NULL);

    pthread_cond_signal(&ep_data->cond1_mutex);
    pthread_mutex_unlock(&ep_data->mutex);
    pthread_mutex_unlock(&ec_data->write_mutex);
    pthread_join(ep_data->thread_id, NULL);

    pthread_cond_signal(&ei_data->cond1_mutex);
    pthread_mutex_unlock(&ei_data->mutex);
    pthread_join(ei_data->thread_id, NULL);

    pthread_mutex_destroy(&bn_data->read_mutex);
    pthread_mutex_destroy(&bn_data->write_mutex);
    pthread_barrier_destroy(&bn_data->barrier);
    free(bn_data->es_status);
    free(bn_data);

    for (int i = 0; i < n_letters; i++)
        free(es_list[i]);
    free(es_list);

    free(ei_data->child_names);
    free(ei_data->child_status);
    pthread_mutex_destroy(&ei_data->mutex);
    pthread_cond_destroy(&ei_data->cond1_mutex);
    pthread_cond_destroy(&ei_data->cond2_mutex);
    free(ei_data);

    pthread_mutex_destroy(&ec_data->read_mutex);
    pthread_mutex_destroy(&ec_data->write_mutex);
    free(ec_data);


    free(ep_data->presents);
    free(ep_data->costs);
    pthread_mutex_destroy(&ep_data->mutex);
    pthread_cond_destroy(&ep_data->cond1_mutex);
    pthread_cond_destroy(&ep_data->cond2_mutex);
    free(ep_data);

    printf("[BN] Terminato\n");
}
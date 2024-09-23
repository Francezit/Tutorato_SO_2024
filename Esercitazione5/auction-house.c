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

#define BUFFER_SIZE 100

typedef struct
{
    int auction_index;
    char object_description[BUFFER_SIZE];
    int minimum_offer;
    int maximum_offer;
    int current_offer;
    int current_bidder_index;
    pthread_cond_t cond_1;
    pthread_cond_t cond_2;
    pthread_mutex_t mutex;
    int exit;

} shared_data_t;

typedef struct
{
    int index;
    shared_data_t *shared_data;
    pthread_t thread_id;

} bidders_thread_args_t;

int parse_line(char *line, char *name, int *min, int *max)
{
    char *token = strtok(line, ",");
    if (token != NULL)
    {
        strcpy(name, token);

        token = strtok(NULL, ",");
        if (token != NULL)
        {
            *min = atoi(token);

            token = strtok(NULL, ",");
            if (token != NULL)
            {
                *max = atoi(token);
                return 0;
            }
        }
    }
    return -1;
}

int get_winner(int *values, int *ranking, int *valid, int size, int *best_value, int *n_valid)
{
    int index = -1;
    int max = -1;
    int n = 0;
    for (int i = 0; i < size; i++)
    {
        if (valid[i])
        {
            n++;
            if (values[i] > max)
            {
                max = values[i];
                index = i;
            }
            else if (values[i] == max)
            {
                if (ranking[i] < ranking[index])
                {
                    index = i; // è arrivato prima
                }
            }
        }
    }
    *best_value = max;
    *n_valid = n;
    return index;
}

void *bidders_thread(void *args)
{
    bidders_thread_args_t *dt = (bidders_thread_args_t *)args;
    shared_data_t *shared_data = dt->shared_data;

    printf("[B%d] offerente pronto\n", dt->index + 1);

    while (shared_data->exit == 0)
    {
        pthread_cond_wait(&shared_data->cond_1, &shared_data->mutex);
        if (shared_data->exit != 0)
        {
            pthread_mutex_unlock(&shared_data->mutex);
            break;
        }
        int offer = 1 + rand() % shared_data->maximum_offer;
        printf("[B%d] invio offerta di %d EUR per asta n. %d\n", dt->index + 1, offer, shared_data->auction_index);
        shared_data->current_offer = offer;
        shared_data->current_bidder_index = dt->index;
        pthread_cond_signal(&shared_data->cond_2);
    }
}

void main(int argc, char **argv)
{
    // controllo gli argomenti
    if (argc < 3)
    {
        perror("Argomenti non validi");
        exit(EXIT_FAILURE);
    }

    char *auction_filename = argv[1];
    int num_bidders = atoi(argv[2]);

    // creo la struttura data condivisa
    shared_data_t *shared_data = (shared_data_t *)malloc(sizeof(shared_data_t));
    if (pthread_cond_init(&shared_data->cond_1, NULL) < 0 || pthread_cond_init(&shared_data->cond_2, NULL) < 0)
    {
        perror("Errore inizializzazione cond");
        exit(EXIT_FAILURE);
    }
    if (pthread_mutex_init(&shared_data->mutex, NULL) < 0)
    {
        perror("Errore inizializzazione mutex");
        exit(EXIT_FAILURE);
    }

    bidders_thread_args_t **bidders = (bidders_thread_args_t **)malloc(sizeof(bidders_thread_args_t *) * num_bidders);
    for (int i = 0; i < num_bidders; i++)
    {
        bidders_thread_args_t *bidder = (bidders_thread_args_t *)malloc(sizeof(bidders_thread_args_t));
        bidder->index = i;
        bidder->shared_data = shared_data;

        if (pthread_create(&bidder->thread_id, NULL, bidders_thread, bidder) < 0)
        {
            perror("Errore nella creazione del thread");
            exit(EXIT_FAILURE);
        }

        bidders[i] = bidder;
    }

    // apro il file delle offerte, leggo riga per riga e faccio partire l'asta per quell'oggetto
    FILE *auction_fp = fopen(auction_filename, "r");
    if (auction_fp == NULL)
    {
        perror("Impossibile aprire il file");
        exit(EXIT_FAILURE);
    }

    // alloco variabili temporanee
    int *offers = malloc(sizeof(int) * num_bidders);
    int *sorting_offers = malloc(sizeof(int) * num_bidders);
    int *valid_offers = malloc(sizeof(int) * num_bidders);

    char buffer[BUFFER_SIZE];
    int auction_index = 1;
    while (fgets(buffer, BUFFER_SIZE, auction_fp) != NULL)
    {
        if (parse_line(buffer, shared_data->object_description, &shared_data->minimum_offer, &shared_data->maximum_offer) < 0)
        {
            perror("Formato nel file non supportato");
            exit(EXIT_FAILURE);
        }
        shared_data->auction_index = auction_index;

        printf("[J] lancio asta n. %d per %s con offerta minima %d EUR e massima di %d EUR\n", auction_index, shared_data->object_description, shared_data->minimum_offer, shared_data->maximum_offer);

        for (int i = 0; i < num_bidders; i++)
        {
            pthread_cond_signal(&shared_data->cond_1);
            pthread_cond_wait(&shared_data->cond_2, &shared_data->mutex);

            offers[shared_data->current_bidder_index] = shared_data->current_offer;
            sorting_offers[shared_data->current_bidder_index] = i;
            valid_offers[shared_data->current_bidder_index] = shared_data->current_offer <= shared_data->maximum_offer && shared_data->current_offer >= shared_data->minimum_offer;
            printf("[J] ricevuta offerta da B%d\n", shared_data->current_bidder_index + 1);
        }

        int best_offer;
        int n_valid;
        int winner_index = get_winner(offers, sorting_offers, valid_offers, num_bidders, &best_offer, &n_valid);
        if (winner_index >= 0)
        {
            printf("[J] l'asta n. %d per %s si e' conclusa con %d offerte valide su %d, il vincitore e' B%d che si aggiudica l'oggetto per %d EUR\n", auction_index, shared_data->object_description, n_valid, num_bidders, winner_index + 1, best_offer);
        }
        else
        {
            printf("[J] l'asta n. %d per %s si e' conclusa senza alcuna offerta valida pertanto l'oggetto non risulta assegnato\n", auction_index, shared_data->object_description);
        }

        auction_index++;
    }
    fclose(auction_fp);

    // dealloco tutte le risorse;
    free(offers);
    free(sorting_offers);
    free(valid_offers);

    shared_data->exit = 1;

    // aspetto che tutti i thread terminano
    pthread_mutex_unlock(&shared_data->mutex);
    pthread_cond_broadcast(&shared_data->cond_1);
    for (int i = 0; i < num_bidders; i++)
    {
        pthread_join(bidders[i]->thread_id, NULL);
        free(bidders[i]);
    }
    free(bidders);

    pthread_cond_destroy(&shared_data->cond_1);
    pthread_cond_destroy(&shared_data->cond_2);
    pthread_mutex_destroy(&shared_data->mutex);
    free(shared_data);
}
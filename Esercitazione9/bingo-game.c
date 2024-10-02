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
#include <pthread.h>
#include <semaphore.h>

#define CARD_COLS 5
#define CARD_ROWS 3
#define MAX_NUMBERS 74

typedef struct
{
    int **card;
    int current_number;
    int player_id_bingo;
    int player_id_quintet;
    int is_quintet;

    sem_t *read_sems;
    sem_t write_sem;
    int exit;
} shared_data_t;

typedef struct
{
    int id;
    int n_cards;
    shared_data_t *shared_data;
    pthread_t thread_id;
} player_data_t;

typedef struct
{
    int n_players;
    int n_cards;
    shared_data_t *shared_data;
    pthread_t thread_id;
} dealer_data_t;

void shuffle(int *array, int n)
{
    if (n > 1)
    {
        for (int i = 0; i < n; i++)
        {
            int j = rand() % n;
            int t = array[j];
            array[j] = array[i];
            array[i] = t;
        }
    }
}

void generate_card(int **card)
{
    int numbers[MAX_NUMBERS];
    for (int i = 0; i < MAX_NUMBERS; i++)
        numbers[i] = i + 1;
    shuffle(numbers, MAX_NUMBERS);

    int k = 0;
    for (int i = 0; i < CARD_ROWS; i++)
    {
        for (int j = 0; j < CARD_COLS; j++)
        {
            card[i][j] = numbers[k++];
        }
    }
}

void copy_card(int **src_card, int **dest_card)
{
    for (int i = 0; i < CARD_ROWS; i++)
    {
        for (int j = 0; j < CARD_COLS; j++)
        {
            dest_card[i][j] = src_card[i][j];
        }
    }
}

int set_number_card(int **card, int number)
{
    int count_quintet = 0;
    for (int i = 0; i < CARD_ROWS; i++)
    {
        int count_zero = 0;
        for (int j = 0; j < CARD_COLS; j++)
        {
            if (card[i][j] == number)
                card[i][j] = 0;
            if (card[i][j] == 0)
                count_zero++;
        }

        if (count_zero == CARD_COLS)
            count_quintet++;
    }

    if (count_quintet == CARD_ROWS)
        return 1; // BINGO
    else if (count_quintet > 0)
        return 0; // CINQUINA
    else
        return -1;
}

void print_card(int **card)
{
    for (int i = 0; i < CARD_ROWS; i++)
    {
        if (i == 0)
            printf("(");
        else
            printf(" / (");

        for (int j = 0; j < CARD_COLS; j++)
        {
            if (j == 0)
                printf("%d", card[i][j]);
            else
                printf(",%d", card[i][j]);
        }
        printf(")");
    }
}

int **inizialize_card()
{
    int **card = (int **)malloc(sizeof(int *) * CARD_ROWS);
    for (int i = 0; i < CARD_ROWS; i++)
    {
        card[i] = calloc(CARD_COLS, sizeof(int));
    }
    return card;
}

void free_card(int **card)
{
    for (int i = 0; i < CARD_ROWS; i++)
    {
        free(card[i]);
    }
    free(card);
}

void *player_thread_function(void *args)
{
    player_data_t *dt = (player_data_t *)args;
    shared_data_t *shared_data = dt->shared_data;

    int ***cards = (int ***)malloc(sizeof(int **) * dt->n_cards);
    int ***flag_cards = (int ***)malloc(sizeof(int **) * dt->n_cards);
    for (int i = 0; i < dt->n_cards; i++)
    {
        cards[i] = inizialize_card();
        flag_cards[i] = inizialize_card();
    }

    for (int i = 0; i < dt->n_cards; i++)
    {
        sem_wait(&shared_data->read_sems[dt->id]);
        copy_card(shared_data->card, cards[i]);
        copy_card(shared_data->card, flag_cards[i]);
        sem_post(&shared_data->write_sem);

        printf("[P%d]: ricevuta card ", dt->id + 1);
        print_card(cards[i]);
        printf("\n");
    }

    int current_status = -1;
    while (shared_data->exit == 0)
    {
        sem_wait(&shared_data->read_sems[dt->id]);
        if (shared_data->exit != 0)
        {
            break;
        }

        for (int i = 0; i < dt->n_cards; i++)
        {
            int status = set_number_card(flag_cards[i], shared_data->current_number);
            if (status != current_status)
            {
                if (status == 1)
                {
                    printf("[P%d] card con Bingo ", dt->id + 1);
                    print_card(cards[i]);
                    printf("\n");
                    shared_data->player_id_bingo = 1;
                    copy_card(cards[i], shared_data->card);
                    break;
                }
                else if (status == 0 && shared_data->is_quintet == 0)
                {
                    printf("[P%d] card con cinquina ", dt->id + 1);
                    print_card(cards[i]);
                    printf("\n");
                    shared_data->player_id_quintet = 1;
                    copy_card(cards[i], shared_data->card);
                }
                current_status = status;
            }
        }
        sem_post(&dt->shared_data->write_sem);
    }

    for (int i = 0; i < dt->n_cards; i++)
    {
        free_card(cards[i]);
        free_card(flag_cards[i]);
    }
    free(cards);
    free(flag_cards);
}

void *dealer_thread_function(void *args)
{
    dealer_data_t *dt = (dealer_data_t *)args;

    printf("[D] ci saranno %d giocatori con %d card ciascuno\n", dt->n_players, dt->n_cards);

    int k = 1;
    for (int i = 0; i < dt->n_players; i++)
    {
        for (int j = 0; j < dt->n_cards; j++)
        {
            generate_card(dt->shared_data->card);
            printf("[D]  genero e distribuisco la card n.%d ", k++);
            print_card(dt->shared_data->card);
            printf("\n");
            sem_post(&dt->shared_data->read_sems[i]);
            sem_wait(&dt->shared_data->write_sem);
        }
    }

    printf("[D] fine della distribuzione delle card e inizio di estrazione dei numeri\n");

    int numbers[MAX_NUMBERS];
    for (int i = 0; i < MAX_NUMBERS; i++)
        numbers[i] = i + 1;
    shuffle(numbers, MAX_NUMBERS);

    k = 0;
    while (dt->shared_data->exit == 0)
    {

        dt->shared_data->player_id_bingo = -1;
        dt->shared_data->player_id_quintet = -1;
        dt->shared_data->current_number = numbers[k++];
        printf("[D] numero estratto %d\n", dt->shared_data->current_number);

        for (int i = 0; i < dt->n_players; i++)
        {
            sem_post(&dt->shared_data->read_sems[i]);
            sem_wait(&dt->shared_data->write_sem);

            if (dt->shared_data->player_id_bingo >= 0)
            {
                printf("[D] il giocatore n.%d ha vinto il bingo con la scheda ", i + 1);
                print_card(dt->shared_data->card);
                printf("\n");
                dt->shared_data->exit = 1;
                break;
            }
            else if (dt->shared_data->is_quintet == 0 && dt->shared_data->player_id_quintet >= 0)
            {
                printf("[D] il giocatore n.%d ha vinto la cinquina con la scheda", i + 1);
                print_card(dt->shared_data->card);
                printf("\n");
                dt->shared_data->is_quintet = 1;
            }
        }
    }

    printf("[D] Fine");
    for (int i = 0; i < dt->n_players; i++)
    {
        sem_post(&dt->shared_data->read_sems[i]);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        perror("Argomenti non validi");
        exit(EXIT_FAILURE);
    }

    int n_players = atoi(argv[1]);
    int n_cards = atoi(argv[2]);

    shared_data_t *data = (shared_data_t *)malloc(sizeof(shared_data_t));
    data->exit = 0;
    data->card = inizialize_card();
    data->current_number = -1;
    data->is_quintet = 0;
    data->read_sems = (sem_t *)malloc(sizeof(sem_t) * n_players);
    if (sem_init(&data->write_sem, 0, 0) < 0)
    {
        perror("Errore semaforo");
        exit(EXIT_FAILURE);
    }

    player_data_t **players = (player_data_t **)malloc(sizeof(player_data_t *) * n_players);
    for (int i = 0; i < n_players; i++)
    {
        player_data_t *player = (player_data_t *)malloc(sizeof(player_data_t));
        player->id = i;
        player->shared_data = data;
        player->n_cards = n_cards;

        if (sem_init(&data->read_sems[i], 0, 0) < 0)
        {
            perror("Errore semaforo");
            exit(EXIT_FAILURE);
        }
        if (pthread_create(&player->thread_id, NULL, player_thread_function, player) < 0)
        {
            perror("Errore nella creazione del thread player");
            exit(EXIT_FAILURE);
        }

        players[i] = player;
    }

    dealer_data_t *dealer = (dealer_data_t *)malloc(sizeof(dealer_data_t));
    dealer->n_cards = n_cards;
    dealer->n_players = n_players;
    dealer->shared_data = data;
    if (pthread_create(&dealer->thread_id, NULL, dealer_thread_function, dealer) < 0)
    {
        perror("Errore nella creazione del thread dealer");
        exit(EXIT_FAILURE);
    }

    pthread_join(dealer->thread_id, NULL);
    for (int i = 0; i < n_players; i++)
    {
        pthread_join(players[i]->thread_id, NULL);
    }

    free_card(data->card);
    for (int i = 0; i < n_players; i++)
    {
        sem_destroy(&data->read_sems[i]);
        free(players[i]);
    }
    sem_destroy(&data->write_sem);
    free(players);
    free(data);
}
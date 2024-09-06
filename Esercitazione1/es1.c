//Scrivere un programma in C che permetta di copiare un numero arbitrario di file regolari su una directory di destinazione preesistente. Il programma dovrà accettare una sintassi del tipo:

//./main file1.txt path/file2.txt file3.txt 


#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#define BUFFER_SIZE 2048
#define MODE 0600
#define PATH_MAX 4096

void copy(char *source, char *dest)
{
    /*
        input="src/file1.txt"
        dest_folder="destinazione"

        output="dest/file.txt"

    */

    char path_destination[PATH_MAX];
    sprintf(path_destination, "%s/%s", dest, basename(source));

    /*
    basename("src/file1.txt") ->"file1.txt"
    dirname("src/file1.txt") ->"src"
    */
    int fin;
    int fout;
    int size;
    char buffer[BUFFER_SIZE];
    if ((fin = open(source, O_RDONLY)) < 0)
    {
        perror("File non trovato");
        exit(EXIT_FAILURE);
    }

    if ((fout = open(path_destination, O_CREAT | O_TRUNC | O_WRONLY, MODE)) < 0)
    {
        perror("Errore nel creare il file");
        exit(EXIT_FAILURE);
    }

    do
    {
        size = read(fin, buffer, BUFFER_SIZE);
        if (size < 0)
        {
            perror("Errore nella lettura del file");
            exit(EXIT_FAILURE);
        }
        else if (size > 0)
        {
            if (write(fout, buffer, size) < 0)
            {
                perror("Errore nella scrittura del file");
                exit(EXIT_FAILURE);
            }
        }
    } while (size == BUFFER_SIZE);

    close(fin);
    close(fout);
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        perror("Argomenti non validi");
        exit(EXIT_FAILURE);
    }

    for (int i = 1; i < argc - 1; i++)
    {
        copy(argv[i], argv[argc - 1]);
    }

    exit(EXIT_SUCCESS);
}
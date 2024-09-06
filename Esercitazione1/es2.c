//Estendere l'esercizio 1 affinché operi correttamente anche nel caso in cui tra le sorgenti è indicata una directory, copiandone il contenuto ricorsivamente. Eventuali link simbolici incontrati dovranno essere replicati come tali (dovrà essere creato un link e si dovranno preservare tutti permessi di accesso originali dei file e directory).

//./main file1.txt dir1 file3.txt <directory-dest>



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

char *join(char *path1, char *path2)
{
    int n = strlen(path1) + strlen(path2) + 1;
    char *buffer = (char *)calloc(n, sizeof(char));
    sprintf(buffer, "%s/%s", path1, path2);
    return buffer;
}

void copy_item(char *src, char *dest)
{

    /*
       src="src/cartella"
       dest="destinazione"

       output="destinazione/cartella"

   */

    int size;
    char buffer[BUFFER_SIZE];
    char *path_source_temp;
    char *path_dest_temp;
    char *path_destination = join(dest, basename(src));

    struct stat statbuf;
    if (lstat(src, &statbuf) < 0)
    { 
        perror("Errore");
        exit(EXIT_FAILURE);
    }


    if (S_ISDIR(statbuf.st_mode))
    {

        if (mkdir(path_destination, 0777) < 0)
        {
            perror("Errore nella creazione della cartella");
            exit(EXIT_FAILURE);
        }

        DIR *dp;
        struct dirent *entry;
        if ((dp = opendir(src)) == NULL)
        {
            perror("Errore nell'apertura della cartella");
            exit(EXIT_FAILURE);
        }

        while ((entry = readdir(dp)) != NULL)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue; // non bisogna copiare questi elementi

            // calcolo il percorso sorgente
            path_source_temp = join(src, entry->d_name);
            printf("\n%s", path_source_temp);

            // calcolo il percorso della nuova cartella di destinazione
            if (strcmp(dirname(entry->d_name), ".") == 0)
                // se il percorso non è disponibile allora la funzione ritorna il punto.
                // questo succede quando l'elemento si trova nel primo livello
                path_dest_temp = path_destination;
            else
                path_dest_temp = join(path_destination, dirname(entry->d_name));
            printf("->%s\n", path_dest_temp);

            copy_item(path_source_temp, path_dest_temp);
        }
    }
    else if (S_ISREG(statbuf.st_mode))
    {

        printf("%s->%s\n", src, path_destination);

        int fpint;
        int fpout;
        if ((fpint = open(src, O_RDONLY)) < 0)
        {
            perror("Errore");
            exit(EXIT_FAILURE);
        }

        if ((fpout = open(path_destination, O_CREAT | O_TRUNC | O_WRONLY, MODE)) < 0)
        {
            perror("Errore");
            exit(EXIT_FAILURE);
        }

        do
        {
            size = read(fpint, buffer, BUFFER_SIZE);
            if (size < 0)
            {
                perror("Errore nella lettura");
                exit(EXIT_FAILURE);
            }
            if (size > 0)
            {
                if (write(fpout, buffer, size) < 0)
                {
                    perror("Errore nella scrittura");
                    exit(EXIT_FAILURE);
                }
            }

        } while (size == BUFFER_SIZE);
    }
    else if (S_ISLNK(statbuf.st_mode))
    {
        // è un link
        if ((size = readlink(src, buffer, BUFFER_SIZE)) < 0)
        {
            perror("errore nella lettura del link");
            exit(EXIT_FAILURE);
        }

        if (symlink(path_destination, buffer) < 0)
        {
            perror("Errore nella creazione del link");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        perror("Tipo non valido");
        exit(EXIT_FAILURE);
    }
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
        copy_item(argv[i], argv[argc - 1]);
    }

    exit(EXIT_SUCCESS);
}
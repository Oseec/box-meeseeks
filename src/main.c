/*
 * Proyecto: Box de Mr. Meeseeks
 * Autor: Isaac Gamba Ureña
 * Carné: 2022437592
 * Institución: Instituto Tecnológico de Costa Rica
 * Curso: Sistemas Operativos
 * Plataforma: Linux
 *
 * Primera etapa:
 * Crear un proceso hijo, mostrar sus identificadores
 * y esperar su terminación desde el proceso padre.
 */
 
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

int main(void)
{
    unsigned int task_count = 0;

    while (1) {
        char request[256];

        printf("\nEscriba una solicitud (salir o adios para terminar): ");
        fflush(stdout);

        if (fgets(request, sizeof(request), stdin) == NULL) {
            if (ferror(stdin)) {
                perror("Error al leer la solicitud");
                return EXIT_FAILURE;
            }

            break; 
        }

        if (strchr(request, '\n') == NULL && !feof(stdin)) {
            int character;

            /*descarta lo que no cupo para limpiar la siguiente lectura.*/
            while ((character = getchar()) != '\n' && character != EOF) {
            }

            fprintf(stderr, "La solicitud es demasiado larga.\n");
            continue;
        }

        request[strcspn(request, "\n")] = '\0';

        if (request[0] == '\0') {
            fprintf(stderr, "La solicitud no puede estar vacia.\n");
            continue;
        }

        if (strcmp(request, "salir") == 0 ||
            strcmp(request, "adios") == 0 ||
            strcmp(request, "adiós") == 0) {
            break;
        }  
    

    int task_pipe[2];

    /*ambos procesos heredaran el pipe al ejecutar fork. */
    if (pipe(task_pipe) == -1) {
        perror("Error al crear el pipe");
        return EXIT_FAILURE;
    }

    pid_t child_pid = fork();

    if (child_pid < 0) {
        perror("Error al crear el Meeseeks");
        close(task_pipe[0]);
        close(task_pipe[1]);
        return EXIT_FAILURE;
    }

    if (child_pid == 0) {
        /*El hijo recibe la solicitud: solamete necesita leer.*/
        close(task_pipe[1]);

        char task[256];
        size_t total = 0;

        /*Acumula los bytes recibidos hasta encontrar el final.*/
        while (total < sizeof(task) - 1) {
            ssize_t received = read(
                task_pipe[0],
                task + total,
                sizeof(task) - 1 - total
            );

            if (received == -1) {
                if (errno == EINTR) {
                    continue;
                }

                perror("Error al leer la solicitud");
                close(task_pipe[0]);
                return EXIT_FAILURE;
            }

            if (received == 0) {
                break;
            }

            total += (size_t)received;
        }

        task[total] = '\0';
        close(task_pipe[0]);

        printf(
            "Hi, I'm Mr. Meeseeks! Look at Meeeee. "
            "(pid=%ld, ppid=%ld, N=1, i=1)\n",
            (long)getpid(),
            (long)getppid()
        );

        printf(
            "[pid=%ld ppid=%ld] Solicitud recibida: %s\n",
            (long)getpid(),
            (long)getppid(),
            task
        );

        return EXIT_SUCCESS;
    }

    /*box envia la solicitud: solamente necesita escribir.*/
    close(task_pipe[0]);

    size_t length = strlen(request);

    if (write(task_pipe[1], request, length) != (ssize_t)length) {
        fprintf(stderr, "Box: no se envio la solicitud completa.\n");
        close(task_pipe[1]);

        while (waitpid(child_pid, NULL, 0) == -1 && errno == EINTR) {
        }

        return EXIT_FAILURE;
    }

    /*esto permite que el hijo detecte el final del mensaje.*/
    close(task_pipe[1]);

    while (waitpid(child_pid, NULL, 0) == -1) {
        if (errno == EINTR) {
            continue;
        }

        perror("Error al esperar al Meeseeks");
        return EXIT_FAILURE;
    }

        printf("Box: el Meeseeks termino; recupere el control.\n");

        task_count++;
    }

    printf(
        "\nBox: solicitudes enviadas a Meeseeks: %u. Adios.\n",
        task_count
    );

    return EXIT_SUCCESS;
}
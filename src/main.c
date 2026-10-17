/*
 * Tarea: Box de Mr. Meeseeks
 * Autor: Isaac Gamba Ureña
 * Carné: 2022437592
 * Institución: Instituto Tecnológico de Costa Rica
 * Curso: Sistemas Operativos
 *
 * Primera etapa:
 * Crear un proceso hijo, mostrar sus identificadores
 * y esperar su terminación desde el proceso padre.
 */


#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

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
        
        struct timespec started_at;

        if (clock_gettime(CLOCK_MONOTONIC, &started_at) == -1) {
            perror("Error al iniciar la medicion");
            return EXIT_FAILURE;
        }

    int task_pipe[2];

    /*ambos procesos heredaran el pipe al ejecutar fork. */
    if (pipe(task_pipe) == -1) {
        perror("Error al crear el pipe");
        return EXIT_FAILURE;
    }

    int result_pipe[2];

    if (pipe(result_pipe) == -1) {
        perror("Error al crear el pipe de respuesta");
        close(task_pipe[0]);
        close(task_pipe[1]);
        return EXIT_FAILURE;
    }

    pid_t child_pid = fork();

    if (child_pid < 0) {
        perror("Error al crear el Meeseeks");

        close(task_pipe[0]);
        close(task_pipe[1]);
        close(result_pipe[0]);
        close(result_pipe[1]);

        return EXIT_FAILURE;
    }

    if (child_pid == 0) {
        /*El hijo recibe la solicitud: solamete necesita leer.*/
        close(task_pipe[1]);
        close(result_pipe[0]);

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

        char confirmation = 'R';
        ssize_t sent;

        /* El hijo envia la confirmacion a Box. */
        do {
            sent = write(
                result_pipe[1],
                &confirmation,
                sizeof(confirmation)
            );
        } while (sent == -1 && errno == EINTR);

        close(result_pipe[1]);

        if (sent != (ssize_t)sizeof(confirmation)) {
            perror("Error al enviar la confirmacion");
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }

    /*box envia la solicitud: solamente necesita escribir.*/
    close(task_pipe[0]);
    close(result_pipe[1]);

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

    char confirmation = '\0';
    ssize_t received;

    do {
        received = read(
            result_pipe[0],
            &confirmation,
            sizeof(confirmation)
        );
    } while (received == -1 && errno == EINTR);

    close(result_pipe[0]);

    if (received == sizeof(confirmation) && confirmation == 'R') {
        printf(
            "Box (pid=%ld): confirmacion recibida del Meeseeks pid=%ld.\n",
            (long)getpid(),
            (long)child_pid
        );
    } else {
        fprintf(stderr, "Box: no se recibio una confirmacion valida.\n");
    }

    while (waitpid(child_pid, NULL, 0) == -1) {
        if (errno == EINTR) {
            continue;
        }

        perror("Error al esperar al Meeseeks");
        return EXIT_FAILURE;
    }

        struct timespec finished_at;

        if (clock_gettime(CLOCK_MONOTONIC, &finished_at) == -1) {
            perror("Error al finalizar la medicion");
            return EXIT_FAILURE;
        }

        double elapsed_seconds =
            (double)(finished_at.tv_sec - started_at.tv_sec) +
            (double)(finished_at.tv_nsec - started_at.tv_nsec) / 1000000000.0;

        printf(
            "Box: duracion de la solicitud %u: %.6f segundos.\n",
            task_count + 1,
            elapsed_seconds
        );

        printf("Box: el Meeseeks termino; recupere el control.\n");

        task_count++;
    }

    printf(
        "\nBox: solicitudes enviadas a Meeseeks: %u. Adios.\n",
        task_count
    );

    return EXIT_SUCCESS;
}
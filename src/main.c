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
#include <ctype.h>
#include <math.h>

typedef struct {
    char text[256];
    double ease;
} TaskMessage;

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

        char ease_input[64];

        printf("Facilidad de la tarea (0=imposible, 100=muy facil): ");
        fflush(stdout);

        if (fgets(ease_input, sizeof(ease_input), stdin) == NULL) {
            if (ferror(stdin)) {
                perror("Error al leer la facilidad");
                return EXIT_FAILURE;
            }

            break;
        }

        if (strchr(ease_input, '\n') == NULL && !feof(stdin)) {
            int character;

            while ((character = getchar()) != '\n' && character != EOF) {
            }

            fprintf(stderr, "El valor ingresado es demasiado largo.\n");
            continue;
        }

        char *end;
        errno = 0;

        double ease = strtod(ease_input, &end);

        if (end == ease_input) {
            fprintf(stderr, "Debe ingresar un numero.\n");
            continue;
        }

        /* Permite espacios o el salto de linea despues del numero. */
        while (isspace((unsigned char)*end)) {
            end++;
        }

        if (*end != '\0' || errno == ERANGE ||
            !isfinite(ease) || ease < 0 || ease > 100) {
            fprintf(stderr, "La facilidad debe ser un numero entre 0 y 100.\n");
            continue;
        }

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

        TaskMessage task = {0};
        size_t total = 0;

        /* Recibe todos los bytes que forman la estructura. */
        while (total < sizeof(task)) {
            ssize_t received = read(
                task_pipe[0],
                (char *)&task + total,
                sizeof(task) - total
            );

            if (received == -1) {
                if (errno == EINTR) {
                    continue;
                }

                perror("Error al leer la solicitud");
                close(task_pipe[0]);
                close(result_pipe[1]);
                return EXIT_FAILURE;
            }

            if (received == 0) {
                break;
            }

            total += (size_t)received;
        }

        close(task_pipe[0]);

        if (total != sizeof(task)) {
            fprintf(
                stderr,
                "[pid=%ld ppid=%ld] Se recibio un mensaje incompleto.\n",
                (long)getpid(),
                (long)getppid()
            );

            close(result_pipe[1]);
            return EXIT_FAILURE;
        }

        task.text[sizeof(task.text) - 1] = '\0';

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
            task.text
        );

        printf(
            "[pid=%ld ppid=%ld] Facilidad recibida: %.4f\n",
            (long)getpid(),
            (long)getppid(),
            task.ease
        );

        char result;

        if (task.ease > 85.0) {
            result = 'S';

            printf(
                "[pid=%ld ppid=%ld] Puedo resolver esta solicitud sin ayuda.\n",
                (long)getpid(),
                (long)getppid()
            );
        } else {
            result = 'H';

            printf(
                "[pid=%ld ppid=%ld] Necesito ayudantes para esta solicitud.\n",
                (long)getpid(),
                (long)getppid()
            );
        }

        ssize_t sent;

        do {
            sent = write(result_pipe[1], &result, sizeof(result));
        } while (sent == -1 && errno == EINTR);

        close(result_pipe[1]);

        if (sent != (ssize_t)sizeof(result)) {
            perror("Error al enviar el resultado");
            return EXIT_FAILURE;
        }

        printf(
            "[pid=%ld ppid=%ld] Finalizo este intento. Adios.\n",
            (long)getpid(),
            (long)getppid()
        );

        return EXIT_SUCCESS;
    }

    /*box envia la solicitud: solamente necesita escribir.*/
    close(task_pipe[0]);
    close(result_pipe[1]);

    TaskMessage outgoing = {0};

    snprintf(outgoing.text, sizeof(outgoing.text), "%s", request);
    outgoing.ease = ease;

    ssize_t sent;

    do {
        sent = write(task_pipe[1], &outgoing, sizeof(outgoing));
    } while (sent == -1 && errno == EINTR);

    if (sent != (ssize_t)sizeof(outgoing)) {
        fprintf(stderr, "Box: no se envio el mensaje completo.\n");

        close(task_pipe[1]);
        close(result_pipe[0]);

        while (waitpid(child_pid, NULL, 0) == -1 && errno == EINTR) {
        }

        return EXIT_FAILURE;
    }

    /*esto permite que el hijo detecte el final del mensaje.*/
    close(task_pipe[1]);

    char result = '\0';
    ssize_t received;

    do {
        received = read(
            result_pipe[0],
            &result,
            sizeof(result)
        );
    } while (received == -1 && errno == EINTR);

    close(result_pipe[0]);

    if (received != (ssize_t)sizeof(result)) {
        fprintf(stderr, "Box: no se recibio un resultado completo.\n");
    } else if (result == 'S') {
        printf(
            "Box: el Meeseeks pid=%ld reporto la solicitud como resuelta.\n",
            (long)child_pid
        );
    } else if (result == 'H') {
        printf(
            "Box: el Meeseeks pid=%ld reporto que necesita ayudantes.\n",
            (long)child_pid
        );
    } else {
        fprintf(stderr, "Box: se recibio un codigo de resultado desconocido.\n");
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
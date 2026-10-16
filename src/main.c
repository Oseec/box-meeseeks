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
 

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    /* Crea un proceso hijo que continúa desde esta instrucción. */
    pid_t child_pid = fork();

    if (child_pid < 0) {
        perror("Error al crear el Meeseeks");
        return EXIT_FAILURE;
    }

    if (child_pid == 0) {
        /* Esta sección la ejecuta únicamente el hijo. */
        printf(
            "Hi, I'm Mr. Meeseeks! Look at Meeeee. "
            "(pid=%ld, ppid=%ld, N=1, i=1)\n",
            (long)getpid(),
            (long)getppid()
        );

        return EXIT_SUCCESS;
    }

    /* Esta sección la ejecuta únicamente el padre: Box. */
    printf(
        "Box (pid=%ld): cree al Meeseeks con pid=%ld.\n",
        (long)getpid(),
        (long)child_pid
    );

    if (waitpid(child_pid, NULL, 0) == -1) {
        perror("Error al esperar al Meeseeks");
        return EXIT_FAILURE;
    }

    printf("Box: el Meeseeks termino; recupere el control.\n");

    return EXIT_SUCCESS;
}
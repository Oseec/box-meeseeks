/*
 * Tarea: Box de Mr. Meeseeks — casos A y B.
 * Autor: Isaac Gamba Ureña. Carné: 2022437592.
 * Instituto Tecnológico de Costa Rica. Curso: Sistemas Operativos.
 * Lenguaje: C11. Plataforma: Linux. 
 * Contratos de los mensajes privados y del resumen de una solicitud.
 */
 
#ifndef SIMULATION_H
#define SIMULATION_H

#include <stdint.h>
#include <sys/types.h>

#define TASK_SIZE 256
#define DETAIL_SIZE 192
#define MAX_HELPERS 6
#define MAX_REQUESTS 1000

typedef enum {
    TASK_RESOLVED,
    TASK_LIMIT,
    TASK_CANCELLED,
    TASK_FAILED,
    TASK_ERROR
} TaskStatus;

/* Se envía por pipe a cada hijo; no se usa mmap para transmitir solicitudes. */
typedef struct {
    char kind;                /* A: simulación textual. B: cálculo. */
    char text[TASK_SIZE];
    double ease;              /* -1: sortear en el primer Meeseeks. */
    double wait_seconds;      /* -1: pausa uniforme entre 0.5 y 5. */
    unsigned int seed;
    unsigned int level;
    unsigned int instance;
    unsigned int process_limit;
    double timeout_seconds;
    double ceiling_seconds;
    int observe;
} TaskMessage;

typedef struct {
    TaskStatus status;
    pid_t solver_pid;
    pid_t solver_ppid;
    unsigned int solver_level;
    unsigned int solver_instance;
    double initial_ease;
    double value;             /* Resultado numérico válido si B se resolvió. */
    uint64_t simulated_wait_ns; /* Suma de pausas; pueden superponerse. */
    char detail[DETAIL_SIZE];
} TaskResult;

typedef struct {
    TaskMessage task;
    TaskResult result;
    unsigned int created;
    unsigned int peak_active;
    unsigned int max_level;
    unsigned int active_at_end;
    double wall_seconds;
    double cpu_seconds;        /* CPU de Box y todos los hijos recuperados. */
    int chaos;
    int forced;
} TaskReport;

uint64_t monotonic_ns(void);
const char *task_status_name(TaskStatus status);
void handle_interrupt(int signum);
int was_interrupted(void);
int run_simulation(const TaskMessage *task, TaskReport *report);

#endif

/*
 * Tarea: Box de Mr. Meeseeks — árbol de ayudantes (caso A).
 * Autor: Isaac Gamba Ureña. Carné: 2022437592.
 * Instituto Tecnológico de Costa Rica. Curso: Sistemas Operativos.
 * Lenguaje: C11. Plataforma: Linux.
 *
 * Un solo ejecutable: cada hijo entra en worker() y puede crear otros hijos.
 * Dos pipes privados por enlace: solicitud/cancelación hacia abajo y resultado
 * hacia arriba. mmap solo contiene contadores y semáforos POSIX compartidos.
 * La cancelación normal es cooperativa; el supervisor conserva un cierre
 * forzado del grupo para fallos o procesos bloqueados. Cada padre hace waitpid.
 */
#define _GNU_SOURCE
#include "simulation.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    sem_t counters;
    sem_t output;
    unsigned int created;
    unsigned int reserved;
    unsigned int active;
    unsigned int peak;
    unsigned int levels;
} SharedCounters;

typedef struct {
    pid_t pid;
    int down;                 /* Padre escribe; hijo lee. */
    int up;                   /* Padre lee; hijo escribe. */
    int done;
} Child;

/* Bandera local de señales, nunca almacena una solicitud ni un resultado. */
static volatile sig_atomic_t interrupted = 0;

void handle_interrupt(int signum)
{
    (void)signum;
    interrupted = 1;
}

int was_interrupted(void)
{
    return interrupted != 0;
}

uint64_t monotonic_ns(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) == -1) {
        perror("clock_gettime");
        _exit(EXIT_FAILURE);
    }
    return (uint64_t)value.tv_sec * 1000000000ULL + (uint64_t)value.tv_nsec;
}

const char *task_status_name(TaskStatus status)
{
    switch (status) {
    case TASK_RESOLVED: return "RESUELTA";
    case TASK_LIMIT: return "LIMITE_RECURSOS";
    case TASK_CANCELLED: return "CANCELADA";
    case TASK_ERROR: return "ERROR";
    }
    return "DESCONOCIDO";
}

static void lock_semaphore(sem_t *semaphore)
{
    while (sem_wait(semaphore) == -1) {
        if (errno != EINTR) {
            perror("sem_wait");
            _exit(EXIT_FAILURE);
        }
    }
}

static int write_all(int fd, const void *data, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        ssize_t sent = write(fd, (const char *)data + offset, length - offset);
        if (sent == -1 && errno == EINTR) {
            continue;
        }
        if (sent <= 0) {
            return -1;
        }
        offset += (size_t)sent;
    }
    return 0;
}

static int read_all(int fd, void *data, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        ssize_t received = read(fd, (char *)data + offset, length - offset);
        if (received == -1 && errno == EINTR) {
            continue;
        }
        if (received <= 0) {
            return -1;
        }
        offset += (size_t)received;
    }
    return 0;
}

static int reap(pid_t pid, int *status)
{
    while (waitpid(pid, status, 0) == -1) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return 0;
}

/* Un semáforo evita que se mezclen líneas escritas por procesos diferentes. */
static void message(SharedCounters *shared, const TaskMessage *task,
                    const char *format, ...)
{
    char content[1024];
    char line[1200];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(content, sizeof(content), format, arguments);
    va_end(arguments);
    int n = snprintf(line, sizeof(line), "[pid=%ld ppid=%ld N=%u i=%u] %s\n",
                     (long)getpid(), (long)getppid(),
                     task ? task->level : 0, task ? task->instance : 0, content);
    /* Si un proceso muere dentro del registro, Box aún debe poder cerrar. */
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 250000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    int locked;
    do {
        locked = sem_timedwait(&shared->output, &deadline);
    } while (locked == -1 && errno == EINTR);
    if (n > 0) {
        size_t length = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
        (void)write_all(STDOUT_FILENO, line, length);
    }
    if (locked == 0) sem_post(&shared->output);
}

/* Reservar el grupo completo impide exceder el límite con forks simultáneos. */
static int reserve_group(SharedCounters *shared, unsigned int count,
                         unsigned int limit)
{
    lock_semaphore(&shared->counters);
    int fits = shared->created + shared->reserved + count <= limit;
    if (fits) {
        shared->reserved += count;
    }
    sem_post(&shared->counters);
    return fits;
}

static void release_reservation(SharedCounters *shared, unsigned int count)
{
    lock_semaphore(&shared->counters);
    shared->reserved -= count;
    sem_post(&shared->counters);
}

static void confirm_fork(SharedCounters *shared)
{
    lock_semaphore(&shared->counters);
    shared->reserved--;
    shared->created++;
    sem_post(&shared->counters);
}

static void enter_worker(SharedCounters *shared, unsigned int level)
{
    lock_semaphore(&shared->counters);
    shared->active++;
    if (shared->active > shared->peak) {
        shared->peak = shared->active;
    }
    if (level > shared->levels) {
        shared->levels = level;
    }
    sem_post(&shared->counters);
}

static void leave_worker(SharedCounters *shared)
{
    lock_semaphore(&shared->counters);
    shared->active--;
    sem_post(&shared->counters);
}

/* xorshift32: cada rama recibe una semilla distinta por su pipe privado. */
static unsigned int random_next(unsigned int *seed)
{
    unsigned int x = *seed ? *seed : 0x9e3779b9U;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *seed = x;
    return x;
}

static double random_unit(unsigned int *seed)
{
    return (double)random_next(seed) / 4294967296.0;
}

/* El padre mantiene abierto down para poder enviar 'C' después de la tarea. */
static int cancellation_received(int down, int timeout_ms)
{
    struct pollfd descriptor = {down, POLLIN, 0};
    int ready;
    do {
        ready = poll(&descriptor, 1, timeout_ms);
    } while (ready == -1 && errno == EINTR && !interrupted);
    if (interrupted || ready == -1) {
        return 1;
    }
    if (ready > 0 && descriptor.revents != 0) {
        char command;
        ssize_t count;
        do {
            count = read(down, &command, 1);
        } while (count == -1 && errno == EINTR);
        return count != 1 || command == 'C';
    }
    return 0;
}

/* poll sustituye a nanosleep para despertar en cuanto llegue una cancelación. */
static int simulated_pause(int down, double seconds, uint64_t *waited)
{
    uint64_t start = monotonic_ns();
    uint64_t deadline = start + (uint64_t)(seconds * 1000000000.0);
    int cancelled = 0;
    while (monotonic_ns() < deadline) {
        uint64_t now = monotonic_ns();
        if (now >= deadline) {
            break;
        }
        uint64_t remaining = deadline - now;
        int milliseconds = (int)(remaining / 1000000ULL) + 1;
        if (milliseconds > 50) {
            milliseconds = 50;
        }
        if (cancellation_received(down, milliseconds)) {
            cancelled = 1;
            break;
        }
    }
    *waited = monotonic_ns() - start;
    return cancelled || cancellation_received(down, 0);
}

static void worker(SharedCounters *shared, int down, int up);

/* Devuelve -1 si no hizo fork y -2 si creó un hijo pero falló el envío. */
static int spawn_child(SharedCounters *shared, const TaskMessage *task,
                       Child *child, int inherited_down, int inherited_up,
                       Child *siblings, unsigned int sibling_count)
{
    int down[2];
    int up[2];
    if (pipe2(down, O_CLOEXEC) == -1) {
        return -1;
    }
    if (pipe2(up, O_CLOEXEC) == -1) {
        close(down[0]); close(down[1]);
        return -1;
    }
    pid_t pid = fork();
    if (pid == -1) {
        close(down[0]); close(down[1]); close(up[0]); close(up[1]);
        return -1;
    }
    if (pid == 0) {
        close(down[1]); close(up[0]);
        if (inherited_down >= 0) close(inherited_down);
        if (inherited_up >= 0) close(inherited_up);
        for (unsigned int j = 0; j < sibling_count; j++) {
            close(siblings[j].down);
            close(siblings[j].up);
        }
        worker(shared, down[0], up[1]);
    }
    confirm_fork(shared);
    close(down[0]); close(up[1]);
    *child = (Child){pid, down[1], up[0], 0};
    if (write_all(child->down, task, sizeof(*task)) == -1) {
        kill(pid, SIGKILL);
        (void)reap(pid, NULL);
        close(child->down); close(child->up);
        return -2;
    }
    return 0;
}

static void cancel_children(SharedCounters *shared, const TaskMessage *task,
                            Child *children, unsigned int count)
{
    for (unsigned int j = 0; j < count; j++) {
        if (!children[j].done) {
            (void)write_all(children[j].down, "C", 1);
            message(shared, task, "PIPE CANCELAR -> hijo pid=%ld.",
                    (long)children[j].pid);
        }
    }
}

static void request_help(SharedCounters *shared, const TaskMessage *task,
                         int down, int up, TaskResult *result,
                         double effective, unsigned int *seed)
{
    unsigned int minimum = effective <= 45.0 ? 5U : 2U;
    double extra_probability = (100.0 - effective) / 100.0;
    double draw = random_unit(seed);
    unsigned int count = minimum + (draw < extra_probability ? 1U : 0U);
    if (!reserve_group(shared, count, task->process_limit)) {
        if (count > minimum && reserve_group(shared, minimum, task->process_limit)) {
            count = minimum;
        } else {
            result->status = TASK_LIMIT;
            snprintf(result->detail, sizeof(result->detail),
                     "No hay cupo para el grupo minimo de %u ayudantes", minimum);
            message(shared, task, "CONTINGENCIA: limite total=%u; %s.",
                    task->process_limit, result->detail);
            return;
        }
    }
    message(shared, task,
            "AYUDA: facilidad efectiva=%.4f; minimo=%u; P(extra)=%.4f; "
            "sorteo=%.4f; creare=%u.", effective, minimum,
            extra_probability, draw, count);

    Child children[MAX_HELPERS];
    unsigned int made = 0;
    for (unsigned int j = 0; j < count; j++) {
        if (cancellation_received(down, 0)) {
            release_reservation(shared, count - made);
            result->status = TASK_CANCELLED;
            snprintf(result->detail, sizeof(result->detail), "Cancelado al crear ayudantes");
            break;
        }
        TaskMessage next = *task;
        next.level++;
        next.instance = j + 1;
        next.seed = random_next(seed) ^ ((j + 1) * 2654435761U);
        int spawned = spawn_child(shared, &next, &children[made], down, up,
                                  children, made);
        if (spawned != 0) {
            release_reservation(shared, count - made - (spawned == -2 ? 1U : 0U));
            result->status = TASK_ERROR;
            snprintf(result->detail, sizeof(result->detail),
                     "No se pudo crear/enviar al grupo: %s", strerror(errno));
            message(shared, task, "ERROR: %s.", result->detail);
            break;
        }
        message(shared, task, "PIPE SOLICITUD -> hijo pid=%ld N=%u i=%u.",
                (long)children[made].pid, next.level, next.instance);
        made++;
    }

    unsigned int pending = made;
    int stopping = made < count;
    uint64_t waits = result->simulated_wait_ns;
    if (stopping) {
        cancel_children(shared, task, children, made);
    }
    while (pending > 0) {
        struct pollfd descriptors[MAX_HELPERS + 1];
        descriptors[0] = (struct pollfd){stopping ? -1 : down, POLLIN, 0};
        for (unsigned int j = 0; j < made; j++) {
            descriptors[j + 1] = (struct pollfd){
                children[j].done ? -1 : children[j].up, POLLIN, 0};
        }
        int ready = poll(descriptors, made + 1, 50);
        if (ready == -1 && errno != EINTR) {
            result->status = TASK_ERROR;
            snprintf(result->detail, sizeof(result->detail), "poll de ayudantes fallo");
            break;
        }
        if (!stopping && (interrupted || descriptors[0].revents != 0)) {
            (void)cancellation_received(down, 0);
            result->status = TASK_CANCELLED;
            snprintf(result->detail, sizeof(result->detail), "Cancelacion recibida del padre");
            stopping = 1;
            cancel_children(shared, task, children, made);
        }
        for (unsigned int j = 0; j < made; j++) {
            if (children[j].done || descriptors[j + 1].revents == 0) {
                continue;
            }
            TaskResult received = {.status = TASK_ERROR};
            int complete = read_all(children[j].up, &received, sizeof(received)) == 0;
            close(children[j].up); close(children[j].down);
            children[j].done = 1;
            pending--;
            int exit_status = 0;
            if (reap(children[j].pid, &exit_status) == -1 || !complete ||
                !WIFEXITED(exit_status) ||
                WEXITSTATUS(exit_status) != (received.status == TASK_RESOLVED ? 0 : 1)) {
                received.status = TASK_ERROR;
                snprintf(received.detail, sizeof(received.detail),
                         "Hijo termino sin respuesta valida");
            }
            waits += received.simulated_wait_ns;
            message(shared, task, "PIPE RESULTADO <- hijo pid=%ld: %s.",
                    (long)children[j].pid, task_status_name(received.status));
            if (!stopping && received.status == TASK_RESOLVED) {
                *result = received;
                stopping = 1;
                cancel_children(shared, task, children, made);
            } else if (!stopping && received.status == TASK_LIMIT) {
                result->status = TASK_LIMIT;
                snprintf(result->detail, sizeof(result->detail),
                         "Ninguna rama resolvio antes del limite de procesos");
            }
        }
    }
    result->simulated_wait_ns = waits;

    /* Si poll falla, se pide cancelar y se recolecta. Box conserva su watchdog. */
    if (pending > 0) {
        cancel_children(shared, task, children, made);
        for (unsigned int j = 0; j < made; j++) {
            if (!children[j].done) {
                close(children[j].down); close(children[j].up);
                (void)reap(children[j].pid, NULL);
            }
        }
    }
}

static void worker(SharedCounters *shared, int down, int up)
{
    TaskMessage task;
    if (read_all(down, &task, sizeof(task)) == -1) {
        close(down); close(up);
        _exit(1);
    }
    enter_worker(shared, task.level);
    unsigned int seed = task.seed;
    if (task.ease < 0.0) {
        task.ease = 100.0 * random_unit(&seed);
    }
    TaskResult result = {.status = TASK_ERROR, .initial_ease = task.ease};
    message(shared, &task, "Hi, I'm Mr. Meeseeks! Look at Meeeee. "
            "(pid=%ld, ppid=%ld, N=%u, i=%u)",
            (long)getpid(), (long)getppid(), task.level, task.instance);
    message(shared, &task, "PIPE RECIBIDO: %s", task.text);
    double effective = task.ease == 0.0 ? 0.0 :
        100.0 - (100.0 - task.ease) * pow(0.5, (double)task.level - 1.0);
    double delay = task.wait_seconds < 0.0 ?
        0.5 + 4.5 * random_unit(&seed) : task.wait_seconds;
    message(shared, &task,
            "FACILIDAD inicial=%.4f; efectiva=%.4f; PAUSA=%.6f s (%s).",
            task.ease, effective, delay, task.wait_seconds < 0.0 ? "aleatoria" : "fija");
    if (simulated_pause(down, delay, &result.simulated_wait_ns)) {
        result.status = TASK_CANCELLED;
        snprintf(result.detail, sizeof(result.detail), "Pausa cancelada por el padre");
    } else if (effective > 85.0) {
        result.status = TASK_RESOLVED;
        result.solver_pid = getpid();
        result.solver_ppid = getppid();
        result.solver_level = task.level;
        result.solver_instance = task.instance;
        snprintf(result.detail, sizeof(result.detail), "Solicitud textual completada");
        message(shared, &task, "SOLUCION encontrada: %s", task.text);
    } else {
        request_help(shared, &task, down, up, &result, effective, &seed);
    }
    if (result.status == TASK_RESOLVED) {
        message(shared, &task, "Tarea completada. Solucionador: pid=%ld ppid=%ld N=%u i=%u.",
                (long)result.solver_pid, (long)result.solver_ppid,
                result.solver_level, result.solver_instance);
    }
    message(shared, &task, "Gracias por dejarme servirle. Adios. Estado=%s. "
            "Envio resultado por PIPE.", task_status_name(result.status));
    leave_worker(shared);
    (void)write_all(up, &result, sizeof(result));
    close(down); close(up);
    _exit(result.status == TASK_RESOLVED ? 0 : 1);
}

static double usage_seconds(const struct rusage *usage)
{
    return (double)usage->ru_utime.tv_sec + (double)usage->ru_stime.tv_sec +
        ((double)usage->ru_utime.tv_usec + (double)usage->ru_stime.tv_usec) / 1000000.0;
}

int run_simulation(const TaskMessage *task, TaskReport *report)
{
    memset(report, 0, sizeof(*report));
    report->task = *task;
    report->result.status = TASK_ERROR;
    SharedCounters *shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE,
                                  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    memset(shared, 0, sizeof(*shared));
    if (sem_init(&shared->counters, 1, 1) == -1) {
        perror("sem_init contadores");
        munmap(shared, sizeof(*shared));
        return -1;
    }
    if (sem_init(&shared->output, 1, 1) == -1) {
        perror("sem_init salida");
        sem_destroy(&shared->counters);
        munmap(shared, sizeof(*shared));
        return -1;
    }
    /* Linux: permite recuperar nietos adoptados si un padre termina anormalmente. */
    if (prctl(PR_SET_CHILD_SUBREAPER, 1) == -1) {
        perror("prctl subreaper");
        goto error;
    }
    struct rusage before_self, before_children, after_self, after_children;
    if (getrusage(RUSAGE_SELF, &before_self) == -1 ||
        getrusage(RUSAGE_CHILDREN, &before_children) == -1) {
        perror("getrusage inicial");
        goto error;
    }
    uint64_t start = monotonic_ns();
    int down[2], up[2];
    if (pipe2(down, O_CLOEXEC) == -1) {
        perror("pipe solicitud"); goto error;
    }
    if (pipe2(up, O_CLOEXEC) == -1) {
        close(down[0]); close(down[1]);
        perror("pipe resultado"); goto error;
    }
    (void)reserve_group(shared, 1, task->process_limit);
    pid_t root = fork();
    if (root == -1) {
        close(down[0]); close(down[1]); close(up[0]); close(up[1]);
        perror("fork original"); goto error;
    }
    if (root == 0) {
        if (setpgid(0, 0) == -1) _exit(1);
        signal(SIGINT, SIG_IGN); /* Box recibe Ctrl+C y cancela mediante pipes. */
        interrupted = 0;
        close(down[1]); close(up[0]);
        worker(shared, down[0], up[1]);
    }
    confirm_fork(shared);
    int group_ready = setpgid(root, root) == 0;
    if (!group_ready && getpgid(root) == root) group_ready = 1;
    close(down[0]); close(up[1]);
    int sent = 0;
    int got = 0;
    uint64_t cancelled_at = 0;
    if (!group_ready || write_all(down[1], task, sizeof(*task)) == -1) {
        report->forced = 1;
        goto cleanup;
    }
    message(shared, NULL, "BOX: PIPE SOLICITUD -> Meeseeks original pid=%ld.", (long)root);
    for (;;) {
        struct pollfd descriptor = {up[0], POLLIN, 0};
        int ready = poll(&descriptor, 1, 25);
        double elapsed = (double)(monotonic_ns() - start) / 1000000000.0;
        if (ready == -1 && errno != EINTR) {
            report->forced = 1;
            break;
        }
        if (elapsed >= task->timeout_seconds && !report->chaos) {
            report->chaos = 1;
            message(shared, NULL, "CAOS PLANETARIO: umbral %.3f s. %s",
                    task->timeout_seconds, task->observe ?
                    "Observacion: se permite continuar con limites." : "Se cancelara todo el arbol.");
        }
        if (!sent && (interrupted || (!task->observe && report->chaos) ||
                       elapsed >= task->ceiling_seconds)) {
            sent = 1;
            cancelled_at = monotonic_ns();
            (void)write_all(down[1], "C", 1);
            message(shared, NULL, "BOX: PIPE CANCELAR -> original; motivo=%s.",
                    interrupted ? "interrupcion" : "tiempo maximo");
        }
        if (ready > 0 && descriptor.revents != 0) {
            got = read_all(up[0], &report->result, sizeof(report->result)) == 0;
            if (!got) report->forced = 1;
            break;
        }
        if (sent && monotonic_ns() - cancelled_at > 2000000000ULL) {
            report->forced = 1;
            break;
        }
    }

cleanup:
    close(down[1]); close(up[0]);
    if (report->forced) {
        /* El PID raíz aún no se ha recolectado: no puede reutilizarse aquí. */
        if (group_ready) kill(-root, SIGKILL);
        kill(root, SIGKILL);
    }
    int root_status = 0;
    if (reap(root, &root_status) == -1 || !WIFEXITED(root_status) ||
        (got && WEXITSTATUS(root_status) != (report->result.status == TASK_RESOLVED ? 0 : 1))) {
        got = 0;
    }
    if (report->forced) {
        /* Recupera también los descendientes adoptados del grupo terminado. */
        while (waitpid(-1, NULL, 0) > 0 || errno == EINTR) {
            errno = 0;
        }
    }
    if (!got) {
        report->result.status = TASK_ERROR;
        report->result.initial_ease = task->ease;
        snprintf(report->result.detail, sizeof(report->result.detail),
                 "Cierre forzado o respuesta IPC incompleta");
    }
    report->wall_seconds = (double)(monotonic_ns() - start) / 1000000000.0;
    if (getrusage(RUSAGE_SELF, &after_self) == -1 ||
        getrusage(RUSAGE_CHILDREN, &after_children) == -1) {
        report->cpu_seconds = -1.0;
    } else {
        report->cpu_seconds = usage_seconds(&after_self) + usage_seconds(&after_children)
            - usage_seconds(&before_self) - usage_seconds(&before_children);
    }
    report->created = shared->created;
    report->peak_active = shared->peak;
    report->max_level = shared->levels;
    report->active_at_end = shared->active;
    sem_destroy(&shared->output);
    sem_destroy(&shared->counters);
    munmap(shared, sizeof(*shared));
    return 0;

error:
    sem_destroy(&shared->output);
    sem_destroy(&shared->counters);
    munmap(shared, sizeof(*shared));
    return -1;
}

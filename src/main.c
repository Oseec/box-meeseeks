/*
 * Traea: Box de Mr. Meeseeks — interfaz y resumen del caso A.
 * Autor: Isaac Gamba Ureña. Carné: 2022437592.
 * Instituto Tecnológico de Costa Rica. Curso: Sistemas Operativos.
 * Lenguaje: C11. Plataforma: Linux.
 * Compilar: make. Ejecutar: ./build/box o ./build/box --help.
 * Esta etapa implementa consultas textuales, no los casos de cálculo/exec.
 */
#define _POSIX_C_SOURCE 200809L
#include "simulation.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void print_help(void)
{
    puts("Box de Mr. Meeseeks — avance del caso A, Linux/C11\n"
         "Sin --task: solicita varias tareas hasta salir/adios.\n"
         "  --task TEXTO        ejecutar una sola solicitud\n"
         "  --ease N|random     facilidad 0..100; 0 imposible, 100 facil\n"
         "  --delay N|random    pausa fija 0..3600 s o aleatoria 0.5..5 s\n"
         "  --seed N            semilla reproducible 0..4294967295\n"
         "  --limit N           limite TOTAL de Meeseeks: 6..256 (defecto 96)\n"
         "  --timeout N         umbral de caos: 300..86400 s (defecto 300)\n"
         "  --observe           continuar tras el umbral; requiere --ceiling\n"
         "  --ceiling N         limite absoluto > timeout, hasta 172800 s\n"
         "  --csv RUTA          agregar resultados en CSV\n"
         "  --help              mostrar esta ayuda\n"
         "Retorno en modo --task: 0 resuelta, 1 no resuelta, 2 error de uso.\n"
         "Ejemplo: ./build/box --task Cafe --ease 60 --delay 0.5 --seed 42");
}

/* 1 línea válida; 0 EOF; 2 línea larga; -1 error. */
static int read_line(const char *prompt, char *buffer, size_t capacity)
{
    fputs(prompt, stdout);
    if (fgets(buffer, (int)capacity, stdin) == NULL) {
        return ferror(stdin) ? -1 : 0;
    }
    char *newline = strchr(buffer, '\n');
    if (newline != NULL) {
        *newline = '\0';
    } else {
        int character = getchar();
        if (character != '\n' && character != EOF) {
            while ((character = getchar()) != '\n' && character != EOF) {}
            return ferror(stdin) ? -1 : 2;
        }
        if (ferror(stdin)) return -1;
    }
    size_t length = strlen(buffer);
    if (length && buffer[length - 1] == '\r') buffer[length - 1] = '\0';
    return 1;
}

static int parse_number(const char *text, double minimum, double maximum,
                        double *value)
{
    char *end;
    errno = 0;
    double parsed = strtod(text, &end);
    if (end == text || errno == ERANGE || !isfinite(parsed)) return 0;
    while (isspace((unsigned char)*end)) end++;
    if (*end || parsed < minimum || parsed > maximum) return 0;
    *value = parsed;
    return 1;
}

static int blank_text(const char *text)
{
    while (*text) {
        if (!isspace((unsigned char)*text)) return 0;
        text++;
    }
    return 1;
}

static void print_report(const TaskReport *report, unsigned int index)
{
    printf("\nRESUMEN #%u | %s\n", index, task_status_name(report->result.status));
    printf("Solicitud: %s\nResultado: %s\n", report->task.text, report->result.detail);
    if (report->result.status != TASK_ERROR) {
        printf("Facilidad inicial usada: %.6f\n", report->result.initial_ease);
    }
    printf("Meeseeks creados=%u | pico activos=%u | nivel maximo=%u\n",
           report->created, report->peak_active, report->max_level);
    printf("Duracion total (reloj monotono)=%.6f s\n", report->wall_seconds);
    printf("CPU agregada de Box y descendientes (sin espera artificial)=%.6f s\n",
           report->cpu_seconds);
    if (!report->forced) {
        printf("Suma de pausas individuales=%.6f s (pueden superponerse)\n",
               (double)report->result.simulated_wait_ns / 1000000000.0);
        printf("Meeseeks activos al cierre=%u\n", report->active_at_end);
    } else {
        puts("Contadores de actividad/pausas incompletos por cierre forzado.");
    }
    printf("Caos=%s | cierre forzado=%s\n", report->chaos ? "si" : "no",
           report->forced ? "si" : "no");
    if (report->result.status == TASK_RESOLVED) {
        printf("Solucionador: pid=%ld ppid=%ld N=%u i=%u\n",
               (long)report->result.solver_pid, (long)report->result.solver_ppid,
               report->result.solver_level, report->result.solver_instance);
    }
}

static void csv_text(FILE *file, const char *text)
{
    fputc('"', file);
    while (*text) {
        if (*text == '"') fputc('"', file);
        fputc(*text++, file);
    }
    fputc('"', file);
}

static int append_csv(const char *path, const TaskReport *report)
{
    FILE *file = fopen(path, "a+");
    if (file == NULL) return -1;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return -1; }
    if (ftell(file) == 0) {
        fputs("task,ease,delay,seed,status,created,peak,levels,wall_s,cpu_s,"
              "sum_wait_s,chaos,forced,solver_pid,solver_level\n", file);
    }
    csv_text(file, report->task.text);
    fprintf(file, ",%.9f,%.6f,%u,%s,%u,%u,%u,%.9f,%.9f,%.9f,%d,%d,%ld,%u\n",
            report->result.initial_ease, report->task.wait_seconds, report->task.seed,
            task_status_name(report->result.status), report->created,
            report->peak_active, report->max_level, report->wall_seconds,
            report->cpu_seconds, report->forced ? -1.0 :
            (double)report->result.simulated_wait_ns / 1000000000.0,
            report->chaos, report->forced, (long)report->result.solver_pid,
            report->result.solver_level);
    int failed = ferror(file);
    return fclose(file) == 0 && !failed ? 0 : -1;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) { perror("SIGPIPE"); return 2; }
    struct sigaction action = {0};
    action.sa_handler = handle_interrupt;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) == -1 ||
        sigaction(SIGTERM, &action, NULL) == -1) {
        perror("sigaction"); return 2;
    }
    TaskMessage base = {
        .ease = 100, .wait_seconds = -1, .level = 1, .instance = 1,
        .seed = (unsigned int)time(NULL) ^ (unsigned int)getpid(),
        .process_limit = 96, .timeout_seconds = 300, .ceiling_seconds = 300
    };
    int single = 0;
    int ceiling_given = 0;
    const char *csv = NULL;
    for (int j = 1; j < argc; j++) {
        if (!strcmp(argv[j], "--help")) { print_help(); return 0; }
        if (!strcmp(argv[j], "--observe")) { base.observe = 1; continue; }
        if (j + 1 >= argc) {
            fprintf(stderr, "Falta el valor de %s.\n", argv[j]); return 2;
        }
        const char *option = argv[j];
        const char *argument = argv[++j];
        double value;
        if (!strcmp(option, "--task") && !blank_text(argument) && strlen(argument) < TASK_SIZE) {
            snprintf(base.text, sizeof(base.text), "%s", argument);
            single = 1;
        } else if (!strcmp(option, "--ease")) {
            if (!strcmp(argument, "random")) base.ease = -1;
            else if (parse_number(argument, 0, 100, &value)) base.ease = value;
            else goto invalid;
        } else if (!strcmp(option, "--delay")) {
            if (!strcmp(argument, "random")) base.wait_seconds = -1;
            else if (parse_number(argument, 0, 3600, &value)) base.wait_seconds = value;
            else goto invalid;
        } else if (!strcmp(option, "--limit") &&
                   parse_number(argument, 6, 256, &value) && floor(value) == value) {
            base.process_limit = (unsigned int)value;
        } else if (!strcmp(option, "--timeout") && parse_number(argument, 300, 86400, &value)) {
            base.timeout_seconds = value;
        } else if (!strcmp(option, "--ceiling") && parse_number(argument, 300, 172800, &value)) {
            base.ceiling_seconds = value;
            ceiling_given = 1;
        } else if (!strcmp(option, "--seed") &&
                   parse_number(argument, 0, 4294967295.0, &value) && floor(value) == value) {
            base.seed = (unsigned int)value;
        } else if (!strcmp(option, "--csv") && *argument) {
            csv = argument;
        } else {
invalid:
            fprintf(stderr, "Opcion o valor invalido: %s %s\n", option, argument);
            return 2;
        }
    }
    if (!base.observe) base.ceiling_seconds = base.timeout_seconds;
    if (base.observe && (!ceiling_given || base.ceiling_seconds <= base.timeout_seconds)) {
        fputs("--observe requiere --ceiling mayor que --timeout.\n", stderr);
        return 2;
    }

    TaskReport *reports = calloc(MAX_REQUESTS, sizeof(*reports));
    if (reports == NULL) { perror("calloc"); return 2; }
    printf("BOX pid=%ld | limite total=%u | caos=%.1f s | observacion=%s\n",
           (long)getpid(), base.process_limit, base.timeout_seconds, base.observe ? "si" : "no");
    unsigned int count = 0;
    unsigned int resolved = 0;
    int exit_code = 0;
    while (count < MAX_REQUESTS && !was_interrupted()) {
        TaskMessage task = base;
        task.seed += count * 2654435761U;
        if (!single) {
            int input = read_line("\nEscriba una solicitud (salir o adios para terminar): ",
                                  task.text, sizeof(task.text));
            if (input <= 0) {
                if (input < 0 && !was_interrupted()) {
                    perror("Error al leer solicitud"); exit_code = 2;
                }
                break;
            }
            if (input == 2) { puts("La solicitud es demasiado larga."); continue; }
            if (blank_text(task.text)) { puts("La solicitud no puede estar vacia."); continue; }
            if (!strcmp(task.text, "salir") || !strcmp(task.text, "adios") ||
                !strcmp(task.text, "adiós")) break;
            char buffer[64];
            input = read_line("Facilidad de la tarea (0..100 o azar): ", buffer, sizeof(buffer));
            if (input <= 0) {
                if (input < 0 && !was_interrupted()) {
                    perror("Error al leer facilidad"); exit_code = 2;
                }
                break;
            }
            if (input == 1 && !strcmp(buffer, "azar")) task.ease = -1;
            else if (input == 2 || !parse_number(buffer, 0, 100, &task.ease)) {
                puts("La facilidad debe estar entre 0 y 100, o escriba azar."); continue;
            }
            input = read_line("Pausa simulada (Enter=azar 0.5..5; numero 0..3600=fija): ",
                              buffer, sizeof(buffer));
            if (input <= 0) {
                if (input < 0 && !was_interrupted()) {
                    perror("Error al leer pausa"); exit_code = 2;
                }
                break;
            }
            task.wait_seconds = -1;
            if (input == 2 || (*buffer && !parse_number(buffer, 0, 3600, &task.wait_seconds))) {
                puts("La pausa debe estar entre 0 y 3600, o dejarse vacia."); continue;
            }
        }
        if (run_simulation(&task, &reports[count]) == -1) { exit_code = 2; break; }
        print_report(&reports[count], count + 1);
        if (csv && append_csv(csv, &reports[count]) == -1) {
            perror("CSV"); exit_code = 2;
        }
        if (reports[count].result.status == TASK_RESOLVED) resolved++;
        else if (exit_code == 0) exit_code = 1;
        int stop = reports[count].chaos || reports[count].forced ||
                   reports[count].result.status == TASK_CANCELLED;
        count++;
        if (single || stop) break;
    }

    puts("\nINFORME FINAL BOX");
    for (unsigned int j = 0; j < count; j++) {
        printf("%u. %s | %s | %u Meeseeks | total=%.6f s | CPU sin pausas=%.6f s\n",
               j + 1, reports[j].task.text, task_status_name(reports[j].result.status),
               reports[j].created, reports[j].wall_seconds, reports[j].cpu_seconds);
    }
    printf("Solicitudes=%u | resueltas=%u | no resueltas=%u. Adios.\n",
           count, resolved, count - resolved);
    free(reports);
    return exit_code;
}

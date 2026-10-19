/*
 * Box de Mr. Meeseeks — interfaz del evaluador del caso B.
 * Isaac Gamba Ureña, 2022437592. ITCR. Sistemas Operativos.
 * C11 / Linux. 
 */
#ifndef CALCULATOR_H
#define CALCULATOR_H

#include <stddef.h>

#define CALC_MAX_LENGTH 255
#define CALC_MAX_DEPTH 64

/* La traza es opcional: el motor la usa para registrar pasos con PID/PPID. */
typedef void (*CalcTrace)(void *context, const char *step);

/* 0: evaluación correcta (incluso si vale 0); -1: error descrito en error.
 * Usa double. No modifica expression. value solo cambia si hubo éxito.
 * error debe ser un buffer válido de al menos un byte. */
int calculate(const char *expression, double *value, char *error, size_t size,
              CalcTrace trace, void *context);

#endif

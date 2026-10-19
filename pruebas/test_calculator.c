/*
 * Box de Mr. Meeseeks — pruebas del evaluador B en C.
 * Isaac Gamba Ureña, 2022437592. ITCR. Sistemas Operativos.
 * C11 / Linux. Fecha: 18/10/2026. Ejecutar con make test.
 */
#include "calculator.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *expression;
    double value;
    const char *error; /* NULL si debe evaluarse correctamente. */
} Case;

static unsigned int trace_steps;
static void count_step(void *context, const char *step)
{
    (void)context;
    if (strstr(step, "CALCULO paso") != NULL) trace_steps++;
}

int main(void)
{
    Case cases[] = {
        {"2+3*4", 14, NULL}, {"(2+3)*4", 20, NULL},
        {"8/4/2", 1, NULL}, {"10-3-2", 5, NULL},
        {" 2.5 + .5 ", 3, NULL}, {"1e2+2.5e-1", 100.25, NULL},
        {"-3*2 + +8", 2, NULL}, {"!0+2*-3", -5, NULL},
        {"7%3", 1, NULL}, {"5.5%2", 1.5, NULL}, {"-7%3", -1, NULL},
        {"(8>3)&&!(2==5)", 1, NULL}, {"3<2", 0, NULL},
        {"2<=2 && 3>=2", 1, NULL}, {"1+2==3", 1, NULL},
        {"3!=4", 1, NULL}, {"!!-2", 1, NULL},
        {"1 || 0 && 0", 1, NULL}, {"2 && -3", 1, NULL},
        {"0 || 5", 1, NULL}, {"3<2<1", 1, NULL},
        {"0 && (1/0)", 0, NULL}, {"1 || (1/0)", 1, NULL},
        {"0 && (7%0)", 0, NULL}, {"1 || (1e308*1e308)", 1, NULL},
        {"0 && (1/0) || 1", 1, NULL},
        {"10/0", 0, "Division entre cero"}, {"7%0", 0, "Resto entre cero"},
        {"1 && (1/0)", 0, "Division entre cero"},
        {"0 || (1/0)", 0, "Division entre cero"},
        {"1+", 0, "Se esperaba"}, {"(2+3", 0, "Falta parentesis"},
        {"2+3)", 0, "Texto u operador"}, {"", 0, "Se esperaba"},
        {"   ", 0, "Se esperaba"}, {"1 2", 0, "Texto u operador"},
        {"2^3", 0, "Texto u operador"}, {"2**3", 0, "Se esperaba"},
        {"hola", 0, "Se esperaba"}, {"nan", 0, "Se esperaba"},
        {"inf", 0, "Se esperaba"}, {"0x10", 0, "Solo se admiten"},
        {"1e999", 0, "Numero fuera de rango"},
        {"1e-999", 0, "Numero fuera de rango"},
        {"(1e308*1e308)>0", 0, "Resultado fuera de rango"},
        {"1e308+1e308", 0, "Resultado fuera de rango"},
        {"1 || (2+)", 0, "Se esperaba"},
        {"0 && (", 0, "Se esperaba"}, {"1 || 1e999", 0, "Numero fuera de rango"},
        {"1; echo hola", 0, "Texto u operador"},
    };
    unsigned int passed = 0;
    for (size_t j = 0; j < sizeof(cases) / sizeof(cases[0]); j++) {
        double value = 12345;
        char error[192];
        int result = calculate(cases[j].expression, &value, error, sizeof(error), NULL, NULL);
        if (cases[j].error == NULL) {
            if (result != 0 || fabs(value - cases[j].value) > 1e-12) {
                fprintf(stderr, "FALLO: %s -> %.12g; error=%s\n", cases[j].expression, value, error);
                return 1;
            }
        } else if (result == 0 || strstr(error, cases[j].error) == NULL || value != 12345) {
            fprintf(stderr, "FALLO error: %s -> %s\n", cases[j].expression, error);
            return 1;
        }
        passed++;
    }

    char nested[160];
    memset(nested, '(', 64);
    nested[64] = '1';
    memset(nested + 65, ')', 64);
    nested[129] = '\0';
    char error[192];
    double value;
    if (calculate(nested, &value, error, sizeof(error), NULL, NULL) != 0 || value != 1) return 1;
    passed++;
    memset(nested, '!', 65);
    nested[65] = '0'; nested[66] = '\0';
    if (calculate(nested, &value, error, sizeof(error), NULL, NULL) == 0 ||
        strstr(error, "Anidamiento") == NULL) return 1;
    passed++;
    char long_expression[257];
    memset(long_expression, '1', 256); long_expression[256] = '\0';
    if (calculate(long_expression, &value, error, sizeof(error), NULL, NULL) == 0) return 1;
    passed++;
    char tiny_error[1];
    if (calculate("1/0", &value, tiny_error, sizeof(tiny_error), NULL, NULL) == 0) return 1;
    passed++;
    if (calculate("2+3*4", &value, error, sizeof(error), count_step, NULL) != 0 ||
        trace_steps != 2 || value != 14) return 1;
    passed++;

    printf("PASS: %u pruebas del evaluador C; precedencia, logica, limites y errores.\n", passed);
    return 0;
}

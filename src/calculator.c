/*
 * Tarea: Box de Mr. Meeseeks — calculadora del caso B.
 * Autor: Isaac Gamba Ureña. Carné: 2022437592.
 * Instituto Tecnológico de Costa Rica. Curso: Sistemas Operativos.
 * Lenguaje: C11. Plataforma: Linux. 
 *
 * Analizador por descenso recursivo con precedencia, asociatividad izquierda
 * y cortocircuito lógico. No ejecuta programas auxiliares. Se evalúan números
 * double finitos. Se limita la longitud y la recursión para entradas erróneas.
 * Una rama lógica omitida se analiza sintácticamente pero no se calcula.
 */
#include "calculator.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *begin;
    const char *cursor;
    char *error;
    size_t error_size;
    unsigned int depth;
    unsigned int steps;
    int failed;
    CalcTrace trace;
    void *context;
} Parser;

static double parse_or(Parser *parser, int evaluate);

static void fail(Parser *parser, const char *reason)
{
    if (!parser->failed) {
        parser->failed = 1;
        snprintf(parser->error, parser->error_size, "%s (posicion %td)",
                 reason, parser->cursor - parser->begin + 1);
    }
}

static void step(Parser *parser, const char *format, ...)
{
    if (parser->trace == NULL || parser->failed) return;
    char detail[144];
    char line[192];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(detail, sizeof(detail), format, arguments);
    va_end(arguments);
    snprintf(line, sizeof(line), "CALCULO paso %u: %s", ++parser->steps, detail);
    parser->trace(parser->context, line);
}

static void skip_spaces(Parser *parser)
{
    while (isspace((unsigned char)*parser->cursor)) parser->cursor++;
}

static int take(Parser *parser, const char *token)
{
    skip_spaces(parser);
    size_t length = strlen(token);
    if (strncmp(parser->cursor, token, length) != 0) return 0;
    parser->cursor += length;
    return 1;
}

static int enter_nested(Parser *parser)
{
    if (parser->depth >= CALC_MAX_DEPTH) {
        fail(parser, "Anidamiento mayor a 64 niveles");
        return 0;
    }
    parser->depth++;
    return 1;
}

static double primary(Parser *parser, int evaluate)
{
    skip_spaces(parser);
    if (take(parser, "(")) {
        if (!enter_nested(parser)) return 0;
        double value = parse_or(parser, evaluate);
        if (!take(parser, ")")) fail(parser, "Falta parentesis de cierre");
        parser->depth--;
        if (evaluate) step(parser, "Parentesis -> %.12g", value);
        return evaluate ? value : 0;
    }
    if (!isdigit((unsigned char)*parser->cursor) && *parser->cursor != '.') {
        fail(parser, "Se esperaba un numero o parentesis");
        return 0;
    }
    if (parser->cursor[0] == '0' &&
        (parser->cursor[1] == 'x' || parser->cursor[1] == 'X')) {
        fail(parser, "Solo se admiten numeros decimales");
        return 0;
    }
    char *end;
    errno = 0;
    double value = strtod(parser->cursor, &end);
    if (end == parser->cursor) {
        fail(parser, "Numero invalido");
        return 0;
    }
    parser->cursor = end;
    if (errno == ERANGE || !isfinite(value)) fail(parser, "Numero fuera de rango");
    return evaluate ? value : 0;
}

static double unary(Parser *parser, int evaluate)
{
    const char *operator = NULL;
    if (take(parser, "!")) operator = "!";
    else if (take(parser, "+")) operator = "+";
    else if (take(parser, "-")) operator = "-";
    if (operator == NULL) return primary(parser, evaluate);
    if (!enter_nested(parser)) return 0;
    double operand = unary(parser, evaluate);
    parser->depth--;
    if (!evaluate || parser->failed) return 0;
    double value = operator[0] == '!' ? (operand == 0) :
                   operator[0] == '-' ? -operand : operand;
    step(parser, "%s %.12g = %.12g", operator, operand, value);
    return value;
}

static double arithmetic(Parser *parser, const char *operator, double left, double right)
{
    if (parser->failed) return 0;
    if ((operator[0] == '/' || operator[0] == '%') && right == 0.0) {
        fail(parser, operator[0] == '/' ? "Division entre cero" : "Resto entre cero");
        return 0;
    }
    double value;
    switch (operator[0]) {
    case '+': value = left + right; break;
    case '-': value = left - right; break;
    case '*': value = left * right; break;
    case '/': value = left / right; break;
    default: value = fmod(left, right); break;
    }
    /* Verifica también resultados intermedios, aunque luego haya comparación. */
    if (!isfinite(value)) {
        fail(parser, "Resultado fuera de rango");
        return 0;
    }
    step(parser, "%.12g %s %.12g = %.12g", left, operator, right, value);
    return value;
}

static double product(Parser *parser, int evaluate)
{
    double left = unary(parser, evaluate);
    while (!parser->failed) {
        const char *operator;
        if (take(parser, "*")) operator = "*";
        else if (take(parser, "/")) operator = "/";
        else if (take(parser, "%")) operator = "%";
        else break;
        double right = unary(parser, evaluate);
        if (evaluate) left = arithmetic(parser, operator, left, right);
    }
    return left;
}

static double sum(Parser *parser, int evaluate)
{
    double left = product(parser, evaluate);
    while (!parser->failed) {
        const char *operator;
        if (take(parser, "+")) operator = "+";
        else if (take(parser, "-")) operator = "-";
        else break;
        double right = product(parser, evaluate);
        if (evaluate) left = arithmetic(parser, operator, left, right);
    }
    return left;
}

static double relation(Parser *parser, int evaluate)
{
    double left = sum(parser, evaluate);
    while (!parser->failed) {
        const char *operator;
        if (take(parser, "<=")) operator = "<=";
        else if (take(parser, ">=")) operator = ">=";
        else if (take(parser, "<")) operator = "<";
        else if (take(parser, ">")) operator = ">";
        else break;
        double right = sum(parser, evaluate);
        if (evaluate && !parser->failed) {
            double value;
            if (!strcmp(operator, "<=")) value = left <= right;
            else if (!strcmp(operator, ">=")) value = left >= right;
            else if (!strcmp(operator, "<")) value = left < right;
            else value = left > right;
            step(parser, "%.12g %s %.12g = %.12g", left, operator, right, value);
            left = value;
        }
    }
    return left;
}

static double equality(Parser *parser, int evaluate)
{
    double left = relation(parser, evaluate);
    while (!parser->failed) {
        const char *operator;
        if (take(parser, "==")) operator = "==";
        else if (take(parser, "!=")) operator = "!=";
        else break;
        double right = relation(parser, evaluate);
        if (evaluate && !parser->failed) {
            double value = operator[0] == '=' ? left == right : left != right;
            step(parser, "%.12g %s %.12g = %.12g", left, operator, right, value);
            left = value;
        }
    }
    return left;
}

static double parse_and(Parser *parser, int evaluate)
{
    double left = equality(parser, evaluate);
    while (!parser->failed && take(parser, "&&")) {
        int skip_right = evaluate && left == 0;
        if (skip_right) step(parser, "Cortocircuito &&: izquierda=0; no se calcula la derecha");
        double right = equality(parser, evaluate && left != 0);
        if (evaluate && !parser->failed) {
            double value = left != 0 && right != 0;
            if (skip_right) step(parser, "&& -> 0 (derecha omitida)");
            else step(parser, "%.12g && %.12g = %.12g", left, right, value);
            left = value;
        }
    }
    return left;
}

static double parse_or(Parser *parser, int evaluate)
{
    double left = parse_and(parser, evaluate);
    while (!parser->failed && take(parser, "||")) {
        int skip_right = evaluate && left != 0;
        if (skip_right) step(parser, "Cortocircuito ||: izquierda distinta de 0; no se calcula la derecha");
        double right = parse_and(parser, evaluate && left == 0);
        if (evaluate && !parser->failed) {
            double value = left != 0 || right != 0;
            if (skip_right) step(parser, "|| -> 1 (derecha omitida)");
            else step(parser, "%.12g || %.12g = %.12g", left, right, value);
            left = value;
        }
    }
    return left;
}

int calculate(const char *expression, double *value, char *error, size_t size,
              CalcTrace trace, void *context)
{
    if (error == NULL || size == 0) return -1;
    error[0] = '\0';
    if (expression == NULL || value == NULL) {
        snprintf(error, size, "Argumentos invalidos del evaluador");
        return -1;
    }
    if (strlen(expression) > CALC_MAX_LENGTH) {
        snprintf(error, size, "Expresion mayor a %d caracteres", CALC_MAX_LENGTH);
        return -1;
    }
    Parser parser = {
        .begin = expression, .cursor = expression,
        .error = error, .error_size = size, .trace = trace, .context = context
    };
    double result = parse_or(&parser, 1);
    skip_spaces(&parser);
    if (*parser.cursor != '\0') fail(&parser, "Texto u operador no admitido");
    if (parser.failed) return -1;
    *value = result;
    return 0;
}

/* reetfps.h - tipos compartilhados da reconstrucao do ReetFPS. */
#ifndef REETFPS_H
#define REETFPS_H

/* Um ajuste do otimizador: uma linha de shell e a categoria logica a que
 * pertence (ver reetfps.c e catalogo_comandos.md). */
typedef struct {
    const char *comando;    /* linha executada (reg add / sc / powercfg / ...) */
    const char *categoria;  /* SERVICES, POWER, NETWORK, ... */
} Tweak;

#endif /* REETFPS_H */

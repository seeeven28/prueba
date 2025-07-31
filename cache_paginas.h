#ifndef CACHE_PAGINAS_H
#define CACHE_PAGINAS_H
#include <stdbool.h>
#include <sys/socket.h>
#include <semaphore.h>
#include <pthread.h>


#define TAM_PAGINA 64 
extern int entradas_por_pagina;
#define SOLICITAR_PAGINA 13


typedef struct {
    int pid;
    int pagina;
    char* contenido;//[TAM_PAGINA]; //
    bool bit_uso;
    bool bit_modificado;
    bool ocupada;
    int frame;
} entrada_cache_t;

typedef struct {
    entrada_cache_t* entradas;
    int cantidad_entradas;
    char algoritmo[8]; // "CLOCK" o "CLOCK-M"
    int puntero_reemplazo;
} cache_paginas_t;

void cache_inicializar(cache_paginas_t* cache, int entradas, const char* algoritmo);
char* cache_leer(cache_paginas_t* cache, int pid, int pagina, int conexion_memoria, int entradas_por_pagina);
void cache_escribir(cache_paginas_t* cache, int pid, int pagina, const char* datos, int conexion_memoria);
void cache_cargar(cache_paginas_t* cache, int pid, int pagina, int conexion_memoria);
void cache_limpiar_proceso(cache_paginas_t* cache, int pid, int conexion_memoria);
void cache_acceder_pagina(cache_paginas_t* cache, int pid, int pagina, bool es_write, int conectar_memoria);
void cache_liberar_proceso(cache_paginas_t* cache, int pid, int conexion_memoria);

char* leer_pagina_de_memoria1(int pid, int pagina, int socket_memoria);

void escribir_pagina_en_memoria(int pid, int pagina, const char* contenido, int socket_memoria);
int buscar_algoritmo(cache_paginas_t* cache);
char* leer_parte_pagina(int pid, int direccion_logica, int tamanio, int socket_memoria);
void escribir_parte_pagina(int pid, int direccion_logica, const char* datos, int socket_memoria);

#endif
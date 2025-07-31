#include "cache_paginas.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <cpu.h>


int puntero_clock = 0;
int entradas_por_pagina;
pthread_mutex_t mutex_entradas_por_pagina;


void cache_inicializar(cache_paginas_t* cache, int entradas, const char* algoritmo) {
    cache->entradas = malloc(sizeof(entrada_cache_t) * entradas);
    cache->cantidad_entradas = entradas;
    
    pthread_mutex_lock(&mutex_entradas_por_pagina);
    entradas_por_pagina = entradas;
    pthread_mutex_unlock(&mutex_entradas_por_pagina);

    strncpy(cache->algoritmo, algoritmo, sizeof(cache->algoritmo));
    cache->puntero_reemplazo = 0;

    for(int i=0; i<entradas; i++) {
        cache->entradas[i].ocupada = false;
        cache->entradas[i].bit_uso = false;
        cache->entradas[i].bit_modificado = false;
    }
}

char* cache_leer(cache_paginas_t* cache, int pid, int pagina, int conexion_memoria, int entradas_por_pagina) {
    if (cache->cantidad_entradas == 0) {
        int direccion_logica = pagina * entradas_por_pagina; // offset = 0
        return leer_parte_pagina(pid, direccion_logica, TAM_PAGINA, conexion_memoria);  //si la cache esta apagada no solicitamos pagina, hacemos INSTR_READ directamente
    }

    // HIT
    for (int i = 0; i < cache->cantidad_entradas; i++) {
        entrada_cache_t* entrada = &cache->entradas[i];
        if (entrada->ocupada && entrada->pid == pid && entrada->pagina == pagina) {
            entrada->bit_uso = true;
            log_info(logger, "PID: %d - Cache Hit - Pagina: %d", pid, pagina);
            return strdup(entrada->contenido);
        }
    }

    // MISS
    log_info(logger, "PID: %d - Cache Miss - Pagina: %d", pid, pagina);
    char* contenido_mem = leer_pagina_de_memoria1(pid, pagina, conexion_memoria);

    
    log_info(logger, "memoria leida");

    // Espacio libre
    for (int i = 0; i < cache->cantidad_entradas; i++) {
        entrada_cache_t* entrada = &cache->entradas[i];
        if (!entrada->ocupada) {
            entrada->ocupada = true;
            entrada->pid = pid;
            entrada->pagina = pagina;
            entrada->contenido = contenido_mem;
            entrada->bit_uso = true;
            entrada->bit_modificado = false;
            log_info(logger, "PID: %d - Cache Add - Pagina: %d", pid, pagina);
            return strdup(entrada->contenido);
        }
    }

    // Reemplazo
    int reemplazado = buscar_algoritmo(cache);
    entrada_cache_t* reemplazo = &cache->entradas[reemplazado];

    if (reemplazo->bit_modificado) {
        escribir_pagina_en_memoria(reemplazo->pid, reemplazo->pagina, reemplazo->contenido, conexion_memoria);
        log_info(logger, "PID: %d - Memory Update - Página: %d - Frame: %d", reemplazo->pid, reemplazo->pagina, reemplazo->frame);
    }

    free(reemplazo->contenido);
    reemplazo->pid = pid;
    reemplazo->pagina = pagina;
    reemplazo->contenido = contenido_mem;
    reemplazo->bit_uso = true;
    reemplazo->bit_modificado = false;
    reemplazo->ocupada = true;

    log_info(logger, "PID: %d - Cache Add - Pagina: %d", pid, pagina);

    return strdup(reemplazo->contenido);
}

char* leer_parte_pagina(int pid, int direccion_logica, int tamanio, int socket_memoria) {
    int op = INSTR_READ;

    if (send(socket_memoria, &op, sizeof(int), 0) <= 0) {
        perror("Error al enviar código de operación INSTR READ");
    }

    char* direccion_str = string_itoa(direccion_logica);
    enviar_param_string(direccion_str, socket_memoria);
    send(socket_memoria, &tamanio, sizeof(int), 0);
    free(direccion_str);

    char* buffer = malloc(tamanio + 1);

    //log_info(logger, "Esperando recibir buffer ");
    recv(socket_memoria, buffer, tamanio, MSG_WAITALL);

    log_info(logger, "buffer recibido");
    buffer[tamanio] = '\0';

    return buffer;
}

void cache_escribir(cache_paginas_t* cache, int pid, int pagina, const char* datos, int socket_memoria) {
    if (cache->cantidad_entradas == 0) {
        // Cache deshabilitada entonces usar INSTR_WRITE directamente
        int direccion_logica = pagina * entradas_por_pagina; // offset 0
        escribir_parte_pagina(pid, direccion_logica, datos, socket_memoria);
        log_info(logger, "PID: %d - Escritura directa sin caché - Página: %d", pid, pagina);
        return;
    }

    // Buscar entrada ya existente (HIT)
    for (int i = 0; i < cache->cantidad_entradas; i++) {
        entrada_cache_t* entrada = &cache->entradas[i];
        if (entrada->ocupada && entrada->pid == pid && entrada->pagina == pagina) {
            free(entrada->contenido);
            entrada->contenido = strdup(datos);
            entrada->bit_uso = true;
            entrada->bit_modificado = true;
            log_info(logger, "PID: %d - Cache Write Hit - Página: %d", pid, pagina);
            return;
        }
    }

    // MISS
    log_info(logger, "PID: %d - Cache Write Miss - Página: %d", pid, pagina);
    char* contenido_mem = leer_pagina_de_memoria1(pid, pagina, socket_memoria);

    log_info(logger, "CONTENIDO MEM: %s", contenido_mem);

    // Buscar espacio libre
    for (int i = 0; i < cache->cantidad_entradas; i++) {
        entrada_cache_t* entrada = &cache->entradas[i];
        if (!entrada->ocupada) {
            entrada->ocupada = true;
            entrada->pid = pid;
            entrada->pagina = pagina;
            free(contenido_mem); // se pisa con nuevos datos
            entrada->contenido = strdup(datos);
            entrada->bit_uso = true;
            entrada->bit_modificado = true;
            log_info(logger, "PID: %d - Cache Write Add - Página: %d", pid, pagina);
            return;
        }
    }

    // Reemplazo

    log_info(logger, "Buscamos algoritmo");

    int reemplazado = buscar_algoritmo(cache);
    entrada_cache_t* reemplazo = &cache->entradas[reemplazado];

    if (reemplazo->bit_modificado) {
        escribir_pagina_en_memoria(reemplazo->pid, reemplazo->pagina, reemplazo->contenido, socket_memoria);
        log_info(logger, "PID: %d - Cache Evict - Página: %d - Frame: %d", reemplazo->pid, reemplazo->pagina, reemplazo->frame);
    }

    free(reemplazo->contenido);
    reemplazo->pid = pid;
    reemplazo->pagina = pagina;
    reemplazo->contenido = strdup(datos);
    reemplazo->bit_uso = true;
    reemplazo->bit_modificado = true;
    reemplazo->ocupada = true;

    log_info(logger, "PID: %d - Cache Write Replace - Página: %d", pid, pagina);
}

void escribir_parte_pagina(int pid, int direccion_logica, const char* datos, int socket_memoria) {
    int op = INSTR_WRITE;
    if (send(socket_memoria, &op, sizeof(int), 0) <= 0) {
        perror("Error al enviar código de operación INST WRITE");
    }

    char* direccion_str = string_itoa(direccion_logica);
    enviar_param_string(direccion_str, socket_memoria);

    int largo = strlen(datos);
    send(socket_memoria, &largo, sizeof(int), 0);
    send(socket_memoria, datos, largo, 0);

    free(direccion_str);
}

static int buscar_clock(cache_paginas_t* cache) {
    while(true) {
        entrada_cache_t* entrada = &cache->entradas[cache->puntero_reemplazo];

        if(!entrada->bit_uso) {
            return cache->puntero_reemplazo;
        }

        entrada->bit_uso = false;
        cache->puntero_reemplazo = (cache->puntero_reemplazo + 1) % cache->cantidad_entradas;
    }
}

static int buscar_clockM(cache_paginas_t* cache) {
    for(int vuelta=0; vuelta<2; vuelta++){
        for(int i=0; i<cache->cantidad_entradas; i++){
            entrada_cache_t* entrada = &cache->entradas[cache->puntero_reemplazo];

            bool uso = entrada->bit_uso;
            bool modificado = entrada->bit_modificado;

            if(!uso && (!modificado || vuelta == 1)) {
                int reemplazo = cache->puntero_reemplazo;
                cache->puntero_reemplazo = (cache->puntero_reemplazo + 1) % cache->cantidad_entradas;
                return reemplazo;
            }

            if(uso) {
                entrada->bit_uso = false;
            }

            cache->puntero_reemplazo = (cache->puntero_reemplazo + 1) % cache->cantidad_entradas;
        }
    }
    // si llego hasta aca devuelve a lo que esta apuntando
    return cache->puntero_reemplazo;
}

int buscar_algoritmo(cache_paginas_t* cache){
    if(strcmp(cache->algoritmo, "CLOCK") == 0) {
        return buscar_clock(cache);
    } else {
        return buscar_clockM(cache);
    }
}

void cache_cargar(cache_paginas_t* cache, int pid, int pagina, int conexion_memoria) {
    if(cache->cantidad_entradas == 0){
        return; // cache deshabilitada
    }

    for(int i=0; i<cache->cantidad_entradas; i++){ // hit
        entrada_cache_t* entrada = &cache->entradas[i];
        if(entrada->ocupada && entrada->pid == pid && entrada->pagina == pagina) {
            entrada->bit_uso = true;
            log_info(logger, "PID: %d - Cache Hit - Pagina: %d", pid, pagina); 
            return;
        }
    }

    log_info(logger, "PID: %d - Cache Miss - Pagina: %d", pid, pagina); // miss

    char* contenido = leer_pagina_de_memoria1(pid, pagina, conexion_memoria); // leer contenido de la memoria

    for(int i=0; i<cache->cantidad_entradas; i++) { // buscar espacio libre
         if(!cache->entradas[i].ocupada) {
            entrada_cache_t* nueva = &cache->entradas[i];
            nueva->ocupada = true;
            nueva->pid = pid;
            nueva->pagina = pagina;
            nueva->contenido = contenido;
            nueva->bit_uso = true;
            nueva->bit_modificado = false;

            log_info(logger, "PID: %d - Cache Add - Pagina: %d", pid, pagina);
            return;
         }
    }

    int reemplazado_busq = buscar_algoritmo(cache);
    entrada_cache_t* reemplazado = &cache->entradas[reemplazado_busq];

    if(reemplazado->bit_modificado) {
        escribir_pagina_en_memoria(reemplazado->pid, reemplazado->pagina, reemplazado->contenido, conexion_memoria);
        log_info(logger, "PID: %d - Memory Update - Pagina: %d - Frame: %d", reemplazado->pid, reemplazado->pagina, reemplazado->frame);
    }

    free(reemplazado->contenido);
    reemplazado->pid = pid;
    reemplazado->pagina = pagina;
    reemplazado->contenido = contenido;
    reemplazado->bit_uso = true;
    reemplazado->bit_modificado = false;

    log_info(logger, "PID: %d - Cache Add - Pagina: %d", pid, pagina);
}

void cache_acceder_pagina(cache_paginas_t* cache, int pid, int pagina, bool es_write, int conexion_memoria){
    if(cache->cantidad_entradas == 0){
        // cache deshabilitada, directo a la memoria
        return;
    }

    for(int i=0; i<cache->cantidad_entradas; i++){ // para el hit
        entrada_cache_t* entrada = &cache->entradas[i];
        if(entrada->ocupada && entrada->pid == pid && entrada->pagina == pagina) {
            entrada->bit_uso = true;
            if(es_write){
                entrada->bit_modificado = true;
            }
            log_info(logger, "PID: %d - Cache Hit - Pagina: %d", pid, pagina);
            return;
        }
    }

    log_info(logger, "PID: %d - Cache Miss - Pagina: %d", pid, pagina);  // si llega aca es porque es un miss

    char* contenido = leer_pagina_de_memoria1(pid, pagina, conexion_memoria); // suponiendo que devuelve malloc

    for(int i=0; i<cache->cantidad_entradas; i++) { // para buscar un espacio libre en cache
        if(!cache->entradas[i].ocupada){
            entrada_cache_t* nueva = &cache->entradas[i];

            nueva->ocupada = true;
            nueva->pid = pid;
            nueva->pagina = pagina;
            nueva->contenido = contenido;
            nueva->bit_uso = true;
            nueva->bit_modificado = es_write;

            log_info(logger, "PID: %d - Cache Add - Pagina: %d", pid, pagina);
            return;
        }
    }

    int reemplazado = buscar_algoritmo(cache);
    entrada_cache_t* reemplazo = &cache->entradas[reemplazado]; // si no hay espacio libre usa el algoritmo de reemplazo

    if(reemplazo->bit_modificado) {
        escribir_pagina_en_memoria(reemplazo->pid, reemplazo->pagina, reemplazo->contenido, conexion_memoria);      //COMPLETAR!!! creo que no tenemos esta funcion hecha
        log_info(logger, "PID: %d - Memory Update - Pagina: %d - Frame: %d", reemplazo->pid, reemplazo->pagina, reemplazo->frame);

    }
    free(reemplazo->contenido);
    reemplazo->pid = pid;
    reemplazo->pagina = pagina;
    reemplazo->contenido = contenido;
    reemplazo->bit_uso = true;
    reemplazo->bit_modificado = es_write;
    reemplazo->ocupada = true;

    log_info(logger, "PID: %d - Cache Add - Pagina: %d", pid, pagina);

}

void cache_liberar_proceso(cache_paginas_t* cache, int pid, int conexion_memoria) {
    if(cache->cantidad_entradas == 0){
        return; // cache deshabilitada
    }

    for(int i=0; i<cache->cantidad_entradas; i++){
        entrada_cache_t* entrada = &cache->entradas[i];

        if(entrada->ocupada && entrada->pid == pid){
            if(entrada->bit_modificado) {
                escribir_pagina_en_memoria(entrada->pid, entrada->pagina, entrada->contenido, conexion_memoria);
                log_info(logger, "PID: %d - Memory Update - Pagina: %d - Frame: %d", entrada->pid, entrada->pagina, entrada->frame);
            }

            free(entrada->contenido);
            entrada->ocupada = false;
            entrada->pid = -1;
            entrada->pagina = -1;
            entrada->contenido = NULL;
            entrada->bit_uso = false;
            entrada->bit_modificado = false;
            entrada->frame = -1;
        }
    }

    log_info(logger, "PID: %d - Cache vaciada.", pid);
}

char* leer_pagina_de_memoria1(int pid, int pagina, int socket_memoria) {
    int op = SOLICITAR_PAGINA;  // Este código lo deben definir en `shared/include/protocolo.h` o donde uses los códigos de operación
    // Enviamos código de operación


    if (send(socket_memoria, &op, sizeof(int), 0) <= 0) {
        perror("Error al enviar código de operación LEER_PAGINA");
        return NULL;
    }

    // Enviamos los parámetros necesarios: PID y Página
    if (send(socket_memoria, &pid, sizeof(int), 0) <= 0) {
        perror("Error al enviar PID");
        return NULL;
    }

    if (send(socket_memoria, &pagina, sizeof(int), 0) <= 0) {
        perror("Error al enviar número de página");
        return NULL;
    }

    // Recibimos la página

    log_info(logger, "mandamos de todo a memoria y esperamos recibir un buffer");

    char* buffer = malloc(TAM_PAGINA + 1);
    
    if (recv(socket_memoria, buffer, TAM_PAGINA, MSG_WAITALL) <= 0) {
        perror("Error al recibir contenido de página");
        free(buffer);
        return NULL;
    }
    log_info(logger, "Se recibio buffer");

    buffer[TAM_PAGINA] = '\0'; // Seguridad por si recibimos texto
    return buffer;
}



void escribir_pagina_en_memoria(int pid, int pagina, const char* contenido, int socket_memoria) {
    int op = ESCRIBIR_PAGINA;

    log_info(logger, "vamos a escribir pagina");

    if (send(socket_memoria, &op, sizeof(int), 0) <= 0) {
        perror("Error al enviar código de operación ESCRIBIR_PAGINA");
    }

    int largo = strlen(contenido) + 1;
    send(socket_memoria, &pid, sizeof(int), 0);
    send(socket_memoria, &pagina, sizeof(int), 0);
    send(socket_memoria, &largo, sizeof(int), 0);
    send(socket_memoria, contenido, largo, 0);
}


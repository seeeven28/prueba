#include "tlb.h"
#include <string.h>
#include <stdio.h>
#include <utils/hello.h>

static long contador_uso_global = 0;

void tlb_inicializar(tlb_t* tlb, int cantidad_entradas, const char* algoritmo) {
    tlb->entradas = malloc(sizeof(entrada_tlb) * cantidad_entradas);
    tlb->cantidad_entradas = cantidad_entradas;
    strncpy(tlb->algoritmo, algoritmo, sizeof(tlb->algoritmo));
    tlb->proxima_reemplazo = 0;
    contador_uso_global = 0;

    for(int i=0; i<cantidad_entradas; i++){
        tlb->entradas[i].pagina = -1;
        tlb->entradas[i].marco = -1;
        tlb->entradas[i].contador_usos = 0;
    }
}

int tlb_buscar(tlb_t* tlb, int pagina) {
    for(int i=0; i<tlb->cantidad_entradas; i++){
        if(tlb->entradas[i].pagina == pagina) {
            contador_uso_global++;
            if(strcmp(tlb->algoritmo, "LRU") == 0){
                tlb->entradas[i].contador_usos = contador_uso_global;
            }
            return tlb->entradas[i].marco;
        }
    }
    return -1; // no encontro
}

void tlb_agregar(tlb_t* tlb, int pagina, int marco) {

    for(int i=0; i<tlb->cantidad_entradas; i++){
        if(tlb->entradas[i].pagina == -1) {
            tlb->entradas[i].pagina = pagina;
            tlb->entradas[i].marco = marco; 
            tlb->entradas[i].contador_usos = contador_uso_global;
            return;
        }
    }

    int indice_reemplazo = 0;
    
    if(strcmp(tlb->algoritmo, "FIFO") == 0) {
        indice_reemplazo = tlb->proxima_reemplazo;
        tlb->proxima_reemplazo = (tlb->proxima_reemplazo + 1) % tlb->cantidad_entradas;
    } else if(strcmp(tlb->algoritmo, "LRU") == 0) {
        long menor_uso = LONG_MAX;
        for(int i=0; i<tlb->cantidad_entradas; i++){
            if(tlb->entradas[i].contador_usos < menor_uso) {
                menor_uso = tlb->entradas[i].contador_usos;
                indice_reemplazo = i;
            }
        }
    }


    tlb->entradas[indice_reemplazo].pagina = pagina;
    tlb->entradas[indice_reemplazo].marco = marco;
    tlb->entradas[indice_reemplazo].contador_usos = contador_uso_global;
}

void tlb_limpiar(tlb_t* tlb) {
    for(int i=0; i<tlb->cantidad_entradas; i++) {
        tlb->entradas[i].pagina = -1;
        tlb->entradas[i].marco = -1;
        tlb->entradas[i].contador_usos = 0;
    }
    tlb->proxima_reemplazo = 0;
    contador_uso_global = 0;
}

void actualizar_uso_tlb(entrada_tlb* entrada) {
    contador_uso_global++;
    entrada->contador_usos = contador_uso_global;
}

int buscar_menor_usada(tlb_t* tlb) {
    int indice_menor = 0;
    long menor_usada = LONG_MAX;
    for(int i=0; i<tlb->cantidad_entradas; i++) {
        if(tlb->entradas[i].contador_usos < menor_usada) {
            menor_usada = tlb->entradas[i].contador_usos;
            indice_menor = i;
        }
    }
    return indice_menor;
}
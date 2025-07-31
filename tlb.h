#ifndef TLB_H
#define TLB_H

#include <stdbool.h>


#define TAM_PAGINA 64
#define LONG_MAX 64


typedef struct { 
    int pagina;  // numero de pagina virtual
    int marco;   // numero de marco fisico
    long contador_usos; // para LRU, numero mas bajo quiere decir que esta menos recientemente usado
} entrada_tlb;


typedef struct {
    entrada_tlb* entradas;
    int cantidad_entradas;
    char algoritmo[5]; // FIFO o LRU
    int proxima_reemplazo; // indice para FIFO
} tlb_t;

extern tlb_t tlb_cpu;

void tlb_inicializar(tlb_t* tlb, int cantidad_entradas, const char* algoritmo);

int tlb_buscar(tlb_t* tlb, int pagina); // busca la pagina en la TLB, devuelve marco si la encuentra, -1 si no

void tlb_agregar(tlb_t* tlb, int pagina, int marco);  // agrega o reemplaza una entrada en la TLB

void tlb_limpiar(tlb_t* tlb);

void actualizar_uso_tlb(entrada_tlb* entrada);


#endif
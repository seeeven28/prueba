#ifndef CPU_H
#define CPU_H_
#include<stdio.h>
#include<stdlib.h>
#include<commons/log.h>
#include<commons/string.h>
#include<commons/config.h>
#include<readline/readline.h>
//#include "tlb.h"

#define TAM_PAGINA 64  //OJOOOO esto lo usamos en varios lados y es dificil de cambiar a la hora de trabajarlo. valor hardcodeado, lo usamos en cache y tlb
//#define TAMANIO_PAGINA 64


//comunicacion con kernel:
#define MOTIVO_EXIT 9
#define MOTIVO_REPLANIFICAR 10

//comunicacion con memoria:
#define MOTIVO_IO 8        //OJO: defini esto como 8, espero que no hayamos usado ese numero
#define PEDIR_INSTRUCCION 11    //si no anda fijarse de cambiarle el numero tanto aca como en el planif_corto.h
#define SOLICITAR_MARCO 12

#define INSTR_NOOP        501
#define INSTR_WRITE       502
#define INSTR_READ        503
#define INSTR_GOTO        504
#define INSTR_IO          505   
#define INSTR_INIT_PROC   506
#define INIT_PROC         1
#define INSTR_DUMP_MEMORY 507
#define INSTR_EXIT        508
#define ESCRIBIR_PAGINA 509
#define DEVOLUCION_PROCESO  510

#define MEM_OK 100

extern t_log* logger;
extern t_config* config;

extern bool interrumpir;
extern pthread_mutex_t mutex_interrupcion;

typedef struct {
    int *socket_procesos;
    int *socket_interrupciones;     //OJO probablemente sean solo INT sin asteriscos
} argumentos_planificador_t;

typedef struct {    //esto me lo manda el kernel
    int pid;
    int pc;
    float estimacion;
} t_paquete_proceso;

typedef struct {    //esto es lo que le devuelvo al kernel
    int pid;
    int pc;
    int motivo;
} t_devolucion_proceso;

typedef struct {
    int pid;
    int cantidad_paginas;
    char** paginas;  // array de strings (contenido por página)
} proceso_memoria_t;

//t_list* procesos_en_memoria;


t_log* iniciar_logger(void);
t_config* iniciar_config(char* ruta);
int conectar_kernel_procesos(t_config* config);
int conectar_kernel_interrupciones(t_config* config);
int conectar_memoria(t_config* config);
bool get_interrumpir(void);


void ejecutar_noop();
void ejecutar_write(int pid, const char* direccion, const char* valor, int conexion_memoria);
void ejecutar_read(int pid, const char* direccion, int tamanio, int conexion_memoria);
void ejecutar_goto(int* pc, int valor);
void ejecutar_io(int pid, const char* dispositivo, int tiempo, int conexion_kernel);
void ejecutar_init_proc(const char* archivo_instrucciones, int tamanio, int conexion_memoria);
// void ejecutar_dump_memory(int conexion_memoria);
void ejecutar_exit(int conexion_procesos);
char* recv_param_string(int socket);

void recv_param_int(int socket, int* destino);

void enviar_param_string(const char* string, int socket);

void enviar_param_int(int socket, int valor);

void ciclo_de_instruccion(int pid, int pc, int conexion_memoria, int conexion_procesos);

void* hilo_escucha_interrupciones(void* args);

int recibir_proceso_a_ejecutar(int socket_procesos);

void devolver_proceso_a_kernel(int pid, int pc, int *conexion_procesos);

void set_interrumpir(bool valor);

void pedir_instruccion_memoria(int pid, int pc, char* buffer_instruccion, int conexion_memoria);

void decode_instruccion(char* instruccion, char* operacion, char* param1, char* param2);

void ejecutar_instruccion(int pid, char* operacion, char* param1, char* param2, int* pc_actual, bool* termino_proceso, int conexion_memoria, int conexion_procesos);

int mmu_traducir(int pid, int direccion_logica, int conexion_memoria);

char* leer_parte_pagina(int pid, int direccion_logica, int tamanio, int socket_memoria);

#endif

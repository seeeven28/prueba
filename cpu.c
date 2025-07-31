#include <utils/hello.h>
#include <cpu.h>
#include <socket_cpu.h>
#include <tlb.h>
#include <cache_paginas.h>
#include <math.h>

t_log* logger;
t_config* config;
tlb_t tlb_cpu;
t_list* tlb;
int entradas_maximas_tlb;
extern int entradas_por_pagina;
cache_paginas_t cache_cpu;

//para el socket de escucha interrupciones
bool interrumpir = false;
pthread_mutex_t mutex_interrupcion = PTHREAD_MUTEX_INITIALIZER;

t_log* iniciar_logger(void){
    t_log* nuevo_logger= log_create("cpu.log", "CPU logger", 1, LOG_LEVEL_INFO);
    if(nuevo_logger == NULL){
        perror("KERNEL: No se pudo crear el log \n");
        exit(EXIT_FAILURE);
    }
    return nuevo_logger;
}

t_config* iniciar_config(char* ruta){
    t_config* nuevo_config = config_create(ruta); //leo IP y PUERTO de la CPU

    if(nuevo_config==NULL){
        perror("KENEL: Error al obtener el config \n");
        exit(EXIT_FAILURE);
    }
    
    return nuevo_config;    
}

int conectar_kernel_procesos(t_config* config) {
    char* ip = config_get_string_value(config, "IP_KERNEL");
    char* puerto = config_get_string_value(config, "PUERTO_KERNEL_DISPATCH");

    return iniciar_cliente(ip, puerto);
}

int conectar_kernel_interrupciones(t_config* config) {
    char* ip = config_get_string_value(config, "IP_KERNEL");
    char* puerto = config_get_string_value(config, "PUERTO_KERNEL_INTERRUPT");

    return iniciar_cliente(ip, puerto);
}

int conectar_memoria(t_config* config) {
    char* ip = config_get_string_value(config, "IP_MEMORIA");
    char* puerto = config_get_string_value(config, "PUERTO_MEMORIA");

    return iniciar_cliente(ip, puerto);
}

void set_interrumpir(bool valor){
    pthread_mutex_lock(&mutex_interrupcion);
    interrumpir = valor;
    pthread_mutex_unlock(&mutex_interrupcion);
}

bool get_interrumpir(void){
    bool valor;
    pthread_mutex_lock(&mutex_interrupcion);
    valor = interrumpir;
    pthread_mutex_unlock(&mutex_interrupcion);
    return valor;
}



void ejecutar_noop() {
    int tiempo_total = 100; // simula el tiempo que dura un ciclo de instruccion
    int tiempo_step = 5;
    
    for (int i = 0; i < tiempo_total / tiempo_step; i++) {  //hago que pueda ser interrumpido por desalojo y que no tenga q esperar los 100ms completos, 
        if (get_interrumpir()) {                            //sino que haga escaloncitos de 5ms para checkear si debo interrumpirlo antes
            log_info(logger, "[NOOP] Interrumpido");
            break;
        }
        usleep(tiempo_step * 1000); // 5ms
    }
}

void ejecutar_write(int pid, const char* direccion, const char* valor, int conexion_memoria) {
    int direccion_logica = atoi(direccion);
    log_info(logger, "WRITE direccion logica: %d",direccion_logica);

    int pagina = direccion_logica / entradas_por_pagina;
    log_info(logger, "WRITE pagina: %d", pagina);

    int offset = direccion_logica % entradas_por_pagina;
    log_info(logger, "WRITE offset: %d", offset);

    int marco = mmu_traducir(pid, direccion_logica, conexion_memoria);
    log_info(logger, "WRITE marco: %d", marco);


    int direccion_fisica = marco * entradas_por_pagina + offset;
    log_info(logger, "WRITE direccion fisica: %d", direccion_fisica);


    // Escribir usando la caché (o directamente a memoria si está deshabilitada)
    cache_escribir(&cache_cpu, pid, pagina, valor, conexion_memoria);  

    log_info(logger, "PID %d - Acción: ESCRIBIR - Dirección Física: %d - Valor: %s", pid, direccion_fisica, valor);
}

void ejecutar_read(int pid, const char* direccion, int tamanio, int conexion_memoria) {
    int direccion_logica = atoi(direccion);
    int marco = mmu_traducir(pid, direccion_logica, conexion_memoria);
    int pagina = direccion_logica / entradas_por_pagina;
    int offset = direccion_logica % entradas_por_pagina;
    int direccion_fisica = marco * entradas_por_pagina + offset;

    char* contenido = cache_leer(&cache_cpu, pid, pagina, conexion_memoria, entradas_por_pagina);

    log_info(logger, "PID: %d - Acción: LEER - Dirección Física: %d - Tamaño: %d", pid, direccion_fisica, tamanio);

    // Simular que "leemos" desde el contenido devuelto (offset y tamaño)
    char* datos = malloc(tamanio + 1);
    strncpy(datos, contenido + offset, tamanio);
    datos[tamanio] = '\0';

    log_info(logger, "[CPU] Datos leídos: %s", datos);

    free(contenido); // strdup() desde cache
    free(datos);
}

void ejecutar_goto(int* pc, int nuevo_valor) {
    log_info(logger, "[CPU] Ejecutando GOTO de PC=%d a PC=%d", *pc, nuevo_valor);
    *pc = nuevo_valor;
}


void ejecutar_io(int pid, const char* dispositivo, int tiempo, int conexion_kernel) {
    log_info(logger, "[CPU] Ejecutando IO en %s por %d unidades de tiempo", dispositivo, tiempo);

    // Enviamos al Kernel que el proceso está bloqueado por IO
    enviar_codigo_operacion(INSTR_IO, conexion_kernel);     //COMPLETAR: DONDE SE RECIBE ESTO? NO LO SE. HAY QUE HACER SEGUIMIENTO CON LAS PRUEBAS QUE HAGAN IO
    
    send(conexion_kernel, &pid, sizeof(int), 0);
    send(conexion_kernel, &tiempo, sizeof(int), 0);
    //send(conexion_kernel, dispositivo, sizeof(char*), 0);
    
    
    enviar_param_string(dispositivo, conexion_kernel);  //Conexion kernel, pero es la de dispatch o interrupciones?
    //enviar_param_int(tiempo, conexion_kernel);
    //enviar_param_int(pid, conexion_kernel);
    log_info(logger, "Envie instruccion, dispositivo %s, tiempo %d, y PID %d", dispositivo, tiempo, pid);
}


// void ejecutar_init_proc(const char* archivo_instrucciones, int tamanio, int conexion_memoria) {
//     log_info(logger, "[CPU] Ejecutando INIT_PROC archivo: %s, tamaño: %d", archivo_instrucciones, tamanio);

//     // enviar_codigo_operacion(INIT_PROC, conexion_memoria);
//     // enviar_param_string(archivo_instrucciones, conexion_memoria);
//     // enviar_param_int(tamanio, conexion_memoria);

//     // Esperás la lista de instrucciones o un OK
// }

// void ejecutar_dump_memory(int conexion_procesos, int pid) {
//     log_info(logger, "[CPU] Ejecutando DUMP MEMORY");

//     enviar_codigo_operacion(INSTR_DUMP_MEMORY, conexion_procesos);
//     int pid = paquete.pid; // lo que estés usando como pid actual
//     send(conexion_procesos, &pid, sizeof(int), 0);

//     // Esperás un código de resultado
//     int resultado;
//     recv(conexion_procesos, &resultado, sizeof(int), 0);

//     if (resultado == MEM_OK) {
//         log_info(logger, "[CPU] Dump completado correctamente");
//     } else {
//         log_error(logger, "[CPU] Error al hacer dump de memoria");
//     }
// }

void ejecutar_exit(int conexion_kernel) {
    log_info(logger, "[CPU] Ejecutando EXIT de proceso");
    
    //enviar_codigo_operacion(MOTIVO_EXIT, conexion_kernel);

}

char* recv_param_string(int socket) {
    int longitud;

    //log_info(logger, "Esperando recibir longitud");
    if (recv(socket, &longitud, sizeof(int), MSG_WAITALL) <= 0) {
        perror("[recv_param_string] Error recibiendo longitud del string");
        exit(EXIT_FAILURE);
    }
    //log_info(logger, "Longitud recibida");

    char* buffer = malloc(longitud + 1);

    //log_info(logger, "Esperando recibir buffer");
    if (recv(socket, buffer, longitud, MSG_WAITALL) <= 0) {
        perror("[recv_param_string] Error recibiendo string");
        free(buffer);
        exit(EXIT_FAILURE);
    }
    //log_info(logger, "Buffer recibido jeje");

    buffer[longitud] = '\0';
    return buffer;
}

void recv_param_int(int socket, int* destino) {
    log_info(logger, "Esperando recibir bytes");
    int bytes_recibidos = recv(socket, destino, sizeof(int), 0);
    
    if (bytes_recibidos <= 0) {
        perror("Error al recibir int");
        // Opcional: salir o manejar el error de forma adecuada
        close(socket);
    }
    log_info(logger, "Bytes recibidos");
}

void enviar_param_string(const char* string, int socket) {
    int longitud = strlen(string) + 1; // el mas 1 por el '\0'

    // enviamos la longitud al socket
    if (send(socket, &longitud, sizeof(int), 0) <= 0) {
        perror("Error enviando la longitud del string");
        return;
    }

    // enviamos el contenido del socket
    if (send(socket, string, longitud, 0) <= 0) {
        perror("Error enviando el contenido del string");
        return;
    }
}

void enviar_param_int(int socket, int valor) {
    send(socket, &valor, sizeof(int), 0);
}

void* hilo_escucha_interrupciones(void* args) {
    int socket_interrupt = *(int*) args;
    free(args);
    int motivo;
    while (1) {
        int interrupt_code;
        log_info(logger, "Esperando recibir codigo de INTERRUPCION");

        int bytes_recibidos = recv(socket_interrupt, &interrupt_code, sizeof(int), MSG_WAITALL);

        if (bytes_recibidos <= 0) {
            log_error(logger, "Se cerró la conexión de interrupciones.");
            break; 
        }

        if (interrupt_code == MOTIVO_REPLANIFICAR) {
            
            enviar_codigo_operacion(interrupt_code, socket_interrupt);
            log_info(logger, "[INTERRUPCIONES] Código REPLANIFICAR recibido");
            set_interrumpir(true);
        } else {
            log_warning(logger, "Código de interrupción desconocido: %d", interrupt_code);
        }
    }

    return NULL;
}

void devolver_proceso_a_kernel(int pid, int pc, int *socket_procesos){
    if(send(*socket_procesos, &(pid), sizeof(int), 0)){
        perror("Error al devolver PID al kernel");
    }
    if(send(*socket_procesos, &(pc), sizeof(int), 0)){
        perror("Error al devolver PC al kernel");
    }
}

int recibir_proceso_a_ejecutar(int socket_procesos){
    int valor;

    log_info(logger, "Esperando recibir valor");
    int recibido = recv(socket_procesos, &valor, sizeof(int), MSG_WAITALL);        //esperamos PID del kernel
    if (recibido <= 0) {
        log_warning(logger, "Conexion cerrada o error");
        close(socket_procesos);
    }

    log_info(logger, "Valor recibido");
    return recibido;
}



int mmu_traducir(int pid, int direccion_logica, int conexion_memoria) {

    //entradas_por_pagina = config_get_string_value(config, )
    
    // int pagina = direccion_logica / entradas_por_pagina;    //hallo direccion fisica
    // int instrucciones_por_pagina = tamanio_pagina / sizeof(char*);
    // int pagina = direccion_logica/instrucciones_por_pagina;
    // int offset = direccion_logica % instrucciones_por_pagina;

    int tamanio_pagina = 64;
    int cant_entradas_tabla = 4;
    int N = 2;
    int X = 2;  // Nivel actual

    int pagina = floor(direccion_logica / tamanio_pagina);

    // pot = cant_entradas_tabla ^ (N - X)  → usamos pow correctamente
    int divisor = (int)pow(cant_entradas_tabla, N - X);

    int entrada_nivel_X = (pagina / divisor) % cant_entradas_tabla;

    int offset = direccion_logica % tamanio_pagina;


    int marco = tlb_buscar(&tlb_cpu, pagina);               //busco si esta en la TLB
    

    if(marco != -1){
        log_info(logger, "[TLB] HIT - PID: %d - Pagina: %d -> Marco %d", pid, pagina, marco);
    } 
    else {
        log_info(logger, "[TLB] MISS - PID: %d - Pagina: %d", pid, pagina);

        int cod_op_traduccion = SOLICITAR_MARCO;
        send(conexion_memoria, &cod_op_traduccion, sizeof(int), 0);
        send(conexion_memoria, &pid, sizeof(int), 0);
        send(conexion_memoria, &pagina, sizeof(int), 0);

        //log_info(logger, "Esperando recibir marco de memoria");
        recv(conexion_memoria, &marco, sizeof(int), MSG_WAITALL);

        //log_info(logger, "[CPU] Marco recibido de Memoria: %d", marco);
        if (marco != -1) {
            tlb_agregar(&tlb_cpu, pagina, marco);
        }
    }
    log_info(logger, "El PID: %d - PC:%d - MARCO: %d", pid, direccion_logica, marco);
    return marco;
}




void pedir_instruccion_memoria(int pid, int pc, char* buffer_instruccion, int conexion_memoria) {
    
    
    int marco = mmu_traducir(pid, pc, conexion_memoria);
    int pagina = pc / 4;  //entradas_por_pagina;
    int offset = pc % 4;//entradas_por_pagina;

    if (marco == -1) {
        log_error(logger, "[CPU] Memoria devolvió marco inválido para PID %d - PC %d (Página %d)", pid, pc, pagina);
        strcpy(buffer_instruccion, "INVALID");  // CAMBIO: ahora usamos esto como clave de terminación
        //interrumpir = true; 
        return;
    }

    int cod_op = PEDIR_INSTRUCCION;
    send(conexion_memoria, &cod_op, sizeof(int), 0);

    // recv(conexion_memoria, &TAM_PAGINA, sizeof(int), MSG_WAITALL);
    // recv(conexion_memoria, &entradas_por_pagina, sizeof(int), MSG_WAITALL);
    // recv(conexion_memoria, &cantidad_niveles_tablas, sizeof(int), MSG_WAITALL);
    // recv(conexion_memoria, &nivel_actual, sizeof(int), MSG_WAITALL);


    
    send(conexion_memoria, &pid, sizeof(int), 0);
    send(conexion_memoria, &pagina, sizeof(int), 0);
    send(conexion_memoria, &offset, sizeof(int), 0);

    //send(conexion_memoria, &entradas_por_pagina, sizeof(int), 0);

    char* instruccion = recv_param_string(conexion_memoria);

    // Validar lo recibido
    if (instruccion == NULL || strcmp(instruccion, "") == 0 || strcmp(instruccion, "INVALID") == 0) {
        log_warning(logger, "[CPU] Instrucción inválida recibida de Memoria para PID %d PC %d", pid, pc);
        strcpy(buffer_instruccion, "INVALID");
    } else {
        strcpy(buffer_instruccion, instruccion);
    }

    free(instruccion);

    //log_info(logger, "[CPU] FETCH -> PID %d PC %d (Página %d, Offset %d, Marco %d) -> Instrucción: '%s'", pid, pc, pagina, offset, marco, buffer_instruccion);
}


// void pedir_instruccion_memoria(int pid, int pc, char* buffer_instruccion, int conexion_memoria) {
//     int cod_op = PEDIR_INSTRUCCION;
//     send(conexion_memoria, &cod_op, sizeof(int), 0);
//     send(conexion_memoria, &pid, sizeof(int), 0);
//     send(conexion_memoria, &pc, sizeof(int), 0);

//     char* instruccion = recv_param_string(conexion_memoria);

//     if (instruccion == NULL || strcmp(instruccion, "") == 0 || strcmp(instruccion, "INVALID") == 0) {
//         log_warning(logger, "[CPU] Instrucción inválida recibida de Memoria para PID %d PC %d", pid, pc);
//         strcpy(buffer_instruccion, "INVALID");
//     } else {
//         strcpy(buffer_instruccion, instruccion);
//     }

//     free(instruccion);
// }




void decode_instruccion(char* instruccion, char* operacion, char* nombre_archivo, char* param2) {
    operacion[0] = '\0';
    nombre_archivo[0] = '\0';
    param2[0] = '\0';

    if (instruccion == NULL || strlen(instruccion) == 0) {
        log_warning(logger, "[CPU] DECODE: instrucción vacía o NULL");
        return;
    }

    sscanf(instruccion, "%s %s %s", operacion, nombre_archivo, param2);

    log_info(logger, "[CPU] DECODE -> Operacion: %s nombre_archivo: %s Param2: %s", operacion, nombre_archivo, param2);
}


void ejecutar_instruccion(int pid, char* operacion, char* param1, char* param2, int* pc_actual, bool* termino_proceso, int conexion_memoria, int conexion_procesos) {
    log_info(logger, "[CPU] EXECUTE -> PID %d - %s %s %s", pid, operacion, param1, param2);

    if (strcmp(operacion, "NOOP") == 0) {
        ejecutar_noop();
        log_info(logger, "EJECUTO NOOP");

    } else if (strcmp(operacion, "WRITE") == 0) {
        log_info(logger, "tenemos el param1: %s y el param2: %s", param1, param2);
        ejecutar_write(pid, param1, param2, conexion_memoria);
        log_info(logger, "EJECUTO WRITE");

    } else if (strcmp(operacion, "READ") == 0) {
        int tamanio = atoi(param2);
        ejecutar_read(pid, param1, tamanio, conexion_memoria);
        log_info(logger, "EJECUTO READ");

    } else if (strcmp(operacion, "GOTO") == 0) {
        int nuevo_pc = atoi(param1);
        *pc_actual = nuevo_pc;
        log_info(logger, "EJECUTO GOTO");

    } else if (strcmp(operacion, "EXIT") == 0) {
        log_info(logger, "[CPU] Se recibió EXIT, terminando proceso PID %d", pid);
        *termino_proceso = true;

    } else if (strcmp(operacion, "INIT_PROC") == 0) {
        log_info(logger, "[CPU] Syscall INIT_PROC detectada - Archivo: %s, Tamaño: %s", param1, param2);
        (*pc_actual)++;  

    } else if (strcmp(operacion, "IO") == 0) {
        log_info(logger, "[CPU] Syscall IO detectada - Dispositivo: %s, Tiempo: %s", param1, param2);
        (*pc_actual)++;  

    } else if (strcmp(operacion, "DUMP_MEMORY") == 0) {
        log_info(logger, "[CPU] Syscall DUMP_MEMORY detectada para PID %d", pid);
        // ejecutar_dump_memory(pid, conexion_memoria);
        // Si necesitás sumar el PC, también:
        (*pc_actual)++;  // Opcional si DUMP_MEMORY interrumpe

    } else {
        log_warning(logger, "[CPU] Instrucción desconocida: %s", operacion);
    }
}
#include <utils/hello.h>
#include <cpu.h>
#include <socket_cpu.h>
#include <tlb.h>
#include <cache_paginas.h>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <identificador_de_la_cpu> <ruta_archivo_configuracion>\n", argv[0]);
        return EXIT_FAILURE;
    }

    logger = iniciar_logger();
    t_config* config = iniciar_config(argv[2]);

    // Configuración de TLB y Cache
    int entradas_tlb = config_get_int_value(config, "ENTRADAS_TLB");
    char* algoritmo_tlb = config_get_string_value(config, "REEMPLAZO_TLB");
    int entradas_cache = config_get_int_value(config, "ENTRADAS_CACHE"); 
    char* algoritmo_cache = config_get_string_value(config, "REEMPLAZO_CACHE");

    tlb_inicializar(&tlb_cpu, entradas_tlb, algoritmo_tlb);
    extern cache_paginas_t cache_cpu;
    cache_inicializar(&cache_cpu, entradas_cache, algoritmo_cache);

    // Conexiones
    log_info(logger, "Inicializando CPU: Conectando con MEMORIA...");
    int conexion_memoria = conectar_memoria(config);

    log_info(logger, "Inicializando CPU: Conectando con KERNEL...");
    int conexion_procesos = conectar_kernel_procesos(config);
    int conexion_interrupciones = conectar_kernel_interrupciones(config);

    // Hilo de interrupciones
    pthread_t hilo_interrupciones;
    int* arg_interrupt = malloc(sizeof(int));
    *arg_interrupt = conexion_interrupciones;
    pthread_create(&hilo_interrupciones, NULL, hilo_escucha_interrupciones, arg_interrupt);
    pthread_detach(hilo_interrupciones);

    // Bucle principal
    while (true) {
        log_info(logger, "_________________________________________");
        char syscall_nombre_archivo[64] = {0};
        char syscall_tamanio_str[64] = {0};
        t_paquete_proceso paquete;

        log_info(logger, "Esperando proceso del Kernel...");
        int recibido = recv(conexion_procesos, &paquete, sizeof(t_paquete_proceso), MSG_WAITALL);
        if (recibido <= 0) {
            log_info(logger, "[CPU DISPATCH] Kernel desconectado.");
            break;
        }

        log_info(logger, "[CPU] Proceso recibido: PID %d - PC %d - Estimación %.2f", paquete.pid, paquete.pc, paquete.estimacion);

        int pc_actual = paquete.pc;
        bool interrumpido = false;
        bool termino_proceso = false;
        int motivo = -1;

        set_interrumpir(false);

        char nombre_archivo[64] = {0};
        char tamanio_str[64] = {0};

        while (!termino_proceso) {

            // FETCH
            char instruccion[128] = {0};
            //log_info(logger, "## PID: %d - FETCH - PC: %d", paquete.pid, pc_actual);
            pedir_instruccion_memoria(paquete.pid, pc_actual, instruccion, conexion_memoria);

            
            if (strcmp(instruccion, "") == 0 || strcmp(instruccion, "INVALID") == 0) {
                log_warning(logger, "[CPU] Instrucción vacía o inválida. Finalizando PID %d", paquete.pid);
                termino_proceso = true;
                motivo = MOTIVO_EXIT;
                break;
            }

            // DECODE
            char operacion[32];
            decode_instruccion(instruccion, operacion, nombre_archivo, tamanio_str);

            

            // EXECUTE
            log_info(logger, "OPERACION %s", operacion);
            ejecutar_instruccion(paquete.pid, operacion, nombre_archivo, tamanio_str, &pc_actual, &termino_proceso, conexion_memoria, conexion_procesos);

            // SYSCALLS o instrucciones que requieren devolución
            if (strcmp(operacion, "IO") == 0) {
                motivo = INSTR_IO;
                interrumpido = true;
                strncpy(syscall_nombre_archivo, nombre_archivo, sizeof(syscall_nombre_archivo));
                strncpy(syscall_tamanio_str, tamanio_str, sizeof(syscall_tamanio_str));
            } else if (strcmp(operacion, "INIT_PROC") == 0) {
                motivo = INIT_PROC;
                interrumpido = true;
                strncpy(syscall_nombre_archivo, nombre_archivo, sizeof(syscall_nombre_archivo));
                strncpy(syscall_tamanio_str, tamanio_str, sizeof(syscall_tamanio_str));
            } else if (strcmp(operacion, "DUMP_MEMORY") == 0) {
                motivo = INSTR_DUMP_MEMORY;
                interrumpido = true;
            } else if (strcmp(operacion, "EXIT") == 0) {
                motivo = MOTIVO_EXIT;
                interrumpido = true;
            }

            if (!termino_proceso && strcmp(operacion, "GOTO") != 0 && !interrumpido)
                pc_actual++;

            //CHECK INTERRUPT
            if (get_interrumpir()) {
                log_info(logger, "[CPU] Interrupción externa detectada para PID %d", paquete.pid);
                motivo = MOTIVO_REPLANIFICAR;
                set_interrumpir(false);
                break;
            }

            if (interrumpido)
                break;
        }

        // DEVOLVER PROCESO AL KERNEL
        t_devolucion_proceso devolucion;
        devolucion.pid = paquete.pid;
        devolucion.pc = pc_actual;
        devolucion.motivo = motivo;

        //enviar_codigo_operacion(motivo, conexion_procesos);
        send(conexion_procesos, &devolucion, sizeof(t_devolucion_proceso), 0);

        switch (motivo) {
            case INIT_PROC:
                enviar_param_string(syscall_nombre_archivo, conexion_procesos);
                enviar_param_int(conexion_procesos, atoi(syscall_tamanio_str));
                break;
            case INSTR_IO:
                enviar_param_int(conexion_procesos, atoi(syscall_tamanio_str));
                enviar_param_string(syscall_nombre_archivo, conexion_procesos);
                break;
            case INSTR_DUMP_MEMORY:
                // Aquí podrías agregar lógica si fuera necesario
                break;
            case MOTIVO_EXIT:
                //send(conexion_memoria, &devolucion.pid, sizeof(int), 0);
            default:  
                break;
        }

        cache_liberar_proceso(&cache_cpu, devolucion.pid, conexion_memoria);
        log_info(logger, "[CPU] PID %d devuelto al Kernel. PC=%d, Motivo=%d", devolucion.pid, devolucion.pc, devolucion.motivo);
    }

    close(conexion_procesos);
    close(conexion_interrupciones);
    close(conexion_memoria);
    config_destroy(config);
    log_destroy(logger);
    return EXIT_SUCCESS;
}


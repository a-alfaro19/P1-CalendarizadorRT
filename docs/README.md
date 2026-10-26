# Sistema Host y Planificador Real Time

## Compilación y Ejecución del Host

### 1. Compilación

Para compilar todas las bibliotecas estáticas y generar el ejecutable principal del Host (`bin/host_app`):

```bash
make clean
make host
```

### 2. Ejecución de la Aplicación Principal

El binario recibe únicamente el archivo de configuración. El proceso planificador
solicita la calibración de los nodos y utiliza sus velocidades medidas para
calcular los costos y asignar las tareas.

```bash
./bin/host_app config/config.txt
```

Para un nodo, configurar `node2_port=none`; para dos, indicar ambos puertos.
Ya no se aceptan velocidades por argumentos ni la opción `--fail`, que no tenía
una implementación operativa. La simulación de fallas se incorporará con la
recuperación durante la ejecución.

El arranque espera las calibraciones, publica las asignaciones en memoria
compartida, envía tareas y verifica cada ACK. Solo después de confirmar todas
las tareas envía START a ambos nodos. Cada intercambio tiene un plazo de 30 s;
una calibración inválida, rechazo o timeout cancela el arranque y solicita STOP
a todos los nodos, con espera limitada y confirmación por separado. Si el STOP
no se confirma, el host no puede asegurar que esa placa se haya detenido.
El campo `assigned` indica confirmación por el nodo, no solo admisión matemática.

START usa un objetivo común de tiempo del host. El log muestra el desfase entre
los envíos y un intervalo conservador para la diferencia entre los ticks cero.
Son observaciones del host; la medición física en las placas sigue siendo una
validación necesaria.

El proceso de reporte consume la cola y muestra admisiones y eventos en consola.
El Gantt, la persistencia del reporte, las tareas manuales, la recuperación de
fallas y el cierre por `q` siguen pendientes. Ctrl+C termina los procesos y libera
la memoria; todavía no realiza el intercambio STOP/ACK con las placas.

### 3. Guardar los logs de ejecución

Para ver la salida del host en la terminal y guardarla a la vez en `host.log`
(en el directorio desde donde ejecutas el comando), usa `tee`:

```bash
./bin/host_app config/config.txt 2>&1 | tee host.log
```

Este ejemplo ejecuta el host con un nodo; asegúrate de tener `node2_port=none`
y de que `node1_port` apunte al puerto serial de la placa. Para ejecutar con dos
nodos, cambia el comando por el correspondiente y conserva `2>&1 | tee host.log`.
El archivo se sobrescribe cada vez que ejecutas el comando. Para agregar la
salida al final del archivo existente, usa `tee -a host.log`.

Detén el host con `Ctrl+C`. La salida que se haya generado hasta entonces queda
guardada en el archivo de log.

---

## Pruebas

```bash
make host
make test-scheduler_runtime test-node_driver test-ipc_queue test-shared_memory
```

Las pruebas del planificador comprueban velocidades medidas, respuestas fuera de
orden, correlación de ACK, timeout, calibración inválida, rechazo de despliegue y
límites de admisión del nodo. Las pruebas seriales conectan dos controladores y
el planificador mediante pseudoterminales y verifican el arranque completo.

## Firmware para NodeMCU ESP8266

Para compilar y cargar el firmware en una NodeMCU V3:

```bash
pio run -d src/node -e esp8266
pio run -d src/node -e esp8266 -t upload --upload-port /dev/ttyUSB0
```

En la NodeMCU el protocolo usa el UART integrado por USB a 115200 baudios.
Mientras el host esté conectado, no abras el monitor serial en ese mismo puerto:
los mensajes de depuración compartirían el canal binario del protocolo. Para
cargar el firmware, detén primero el host.

Para probar solo la NodeMCU, establece `node2_port=none` en la configuración
del host. Así se ejecuta un solo driver y se calibra el
nodo conectado:

```bash
make host
./bin/host_app config/config.txt
```

Usa el puerto USB de la NodeMCU como `node1_port` (por ejemplo,
`/dev/ttyUSB0`). El modo habitual con dos nodos permanece disponible cuando
`node2_port` contiene el puerto serial del segundo nodo.

## Integración con el módulo de fallos

Cuando el otro módulo detecte que un nodo se desconectó, solo necesita llamar:

```c
TaskAssignment results[MAX_TASKS_PER_NODE];
int result_count = 0;

int relocated = planner_handle_node_failure(
    &planner,
    failed_node_id,
    results,
    &result_count
);
```

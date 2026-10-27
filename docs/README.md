# Sistema Host y Planificador Real Time

## Compilación y Ejecución del Host

### 1. Compilación

Para compilar todas las bibliotecas estáticas y generar el ejecutable principal del Host (`bin/host_app`):

```bash
make clean
make host
```

### 2. Ejecución de la Aplicación Principal

El binario recibe el archivo de configuración y, opcionalmente, `--fail 1|2`
para simular la caída de un nodo en cuanto el sistema esté en ejecución.
El planificador solicita la calibración de los nodos y utiliza sus velocidades
medidas para calcular los costos y asignar las tareas.

```bash
./bin/host_app config/config.txt
./bin/host_app config/config_failure.txt --fail 2
```

Para un nodo, configurar `node2_port=none`; para dos, indicar ambos puertos.

#### Procesos del host

| Proceso | Función |
|---|---|
| Supervisor (`main`, `host.c`) | Crea los demás procesos, atiende la consola y las señales, y coordina el cierre |
| Planificador | Calibración, admisión (First-Fit / Worst-Fit), despliegue con ACK, START, detección de nodos caídos y replanificación |
| Controlador por nodo | Un proceso por puerto serial: tramas con CRC, órdenes del planificador y eventos del nodo |
| Reporte | Consume la cola de eventos, muestra la actividad en vivo y genera el reporte final |

Se comunican por memoria compartida POSIX con semáforos. Las colas son
acotadas: órdenes por nodo, respuestas (controladores → planificador), control
(consola → planificador) y eventos (controladores y planificador → reporte).

#### Consola

| Orden | Efecto |
|---|---|
| `add id,P\|S,W,T,D,criticidad` | Agrega una tarea sin detener los nodos (vuelve a pasar la admisión) |
| `spor nodo` | Libera la tarea esporádica del nodo, igual que su botón físico |
| `fail nodo` | Simula la caída del nodo 1 o 2 |
| `status` | Muestra asignaciones y nodos activos |
| `q` | Cierre ordenado (también `Ctrl+C`) |

#### Cierre y reporte

Con `q` o `Ctrl+C` el planificador envía STOP a cada nodo y espera su ACK; luego
el supervisor pide terminar a los controladores y, cuando ya no quedan, cierra la
cola de eventos para que el reporte procese todo lo pendiente. Al final no
quedan procesos ni objetos en `/dev/shm`, y se cierran los puertos seriales.
Los hijos ignoran `SIGINT`: solo el supervisor reacciona a `Ctrl+C`.

El reporte se guarda en `reports/` (relativo al directorio de ejecución):

- `reporte_<fecha>.txt`: Gantt por nodo, liberaciones/plazos/fallas, estadísticas
  por tarea (con la latencia de las esporádicas), utilización teórica y medida,
  cambios de tarea, y tareas aceptadas/rechazadas con su motivo.
- `gantt_nodoN_<fecha>.csv`: tramos `tarea,tick_inicio,tick_fin` para graficar.

Límites del reporte: el texto muestra los primeros 400 ticks del Gantt; se
almacenan hasta 8192 tramos por nodo y 4096 eventos en la línea de tiempo. Si se
superan, el reporte lo indica, pero las estadísticas y la utilización siguen
exactas. La utilización medida llega hasta el último tick observado del nodo.

START usa un objetivo común de tiempo del host. El log muestra el desfase entre
los envíos y un intervalo conservador para la diferencia entre los ticks cero.
Son observaciones del host; la medición física en las placas sigue siendo una
validación necesaria.

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

Detén el host con `q` o `Ctrl+C`. La salida que se haya generado hasta entonces queda
guardada en el archivo de log.

---

## Pruebas

```bash
make host
make test-scheduler_runtime test-node_driver test-ipc_queue test-shared_memory \
     test-report test-host_e2e
```

`test-host_e2e` ejecuta `bin/host_app` con dos nodos simulados por
pseudoterminales, pide el cierre con `q` y con `Ctrl+C`, y comprueba el reporte,
la salida con código 0 y la liberación de la memoria compartida.

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

# Sistema Host y Planificador Real Time

## Compilación y Ejecución del Host

### 1. Compilación

Para compilar todas las bibliotecas estáticas y generar el ejecutable principal del Host (`bin/host_app`):

```bash
make clean
make host
```

### 2. Ejecución de la Aplicación Principal

El binario requiere la ruta del archivo de configuración y las velocidades asociadas a cada nodo:

**Uso general:**

```bash
./bin/host_app <config_path> <vel_nodo1> [vel_nodo2] [--fail nodo]
```

- **Prueba con un nodo** (configurar `node2_port=none`):

  ```bash
  ./bin/host_app config/config.txt 10
  ```

- **Dos nodos** (ambos puertos deben existir):

  ```bash
  ./bin/host_app config/config_edf.txt 20 12
  ```

- **Modo Simulación de Falla** (configurado para dos nodos):

  Para simular la caída de un nodo (por ejemplo, el Nodo 1) durante la ejecución:
  ```bash
  ./bin/host_app config/config_edf.txt 20 12 --fail 1
  ```

---

## Pruebas

```bash
make clean
make
make run
make test-failure
make test-edf
make test-rms
make test-ff
make test-wf
```

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
del host. Así se ejecuta un solo driver y solo se necesita la velocidad del
nodo conectado:

```bash
make host
./bin/host_app config/config.txt 10
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

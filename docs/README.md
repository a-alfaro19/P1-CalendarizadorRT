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
./bin/host_app <config_path> <vel_nodo1> <vel_nodo2> [--fail nodo]
```

- **Modo Estándar:**

  ```bash
  ./bin/host_app config/config.txt 20 12
  ```

- **Modo Simulación de Falla:**
  Para simular la caída de un nodo (por ejemplo, el Nodo 1) durante la ejecución:
  ```bash
  ./bin/host_app config/config.txt 20 12 --fail 1
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

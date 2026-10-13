## Integracion con comunicacion

La persona encargada de comunicacion puede llenar:

```c
NodeInfo nodes[2] = {
    {1, velocidad_nodo_1},
    {2, velocidad_nodo_2}
};
```

y luego llamar:

```c
plan_tasks(tasks, task_count, nodes, 2, &config, assignments);
```

Cada elemento de `assignments` indica si la tarea fue aceptada, el nodo asignado, el costo en ticks, la utilizacion y el motivo en caso de rechazo.

## Prueba temporal de calibracion

Mientras no este integrada la comunicacion, main recibe s1 y s2 por linea de comandos:

```bash
./host config/config.txt 20 12
```

## Compilar y probar

```bash
make
make run
make run-rms
```

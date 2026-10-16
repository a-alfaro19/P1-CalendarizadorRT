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

## Integracion con el modulo de fallos

Cuando el otro modulo detecte que un nodo se desconecto, solo necesita llamar:

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




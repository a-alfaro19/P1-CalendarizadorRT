## Compilar

```bash
make
```

## Ejecutar

```bash
make run
```

o directamente:

```bash
./host config/config.txt
```


## Formato del archivo de tareas

Cada línea tiene el formato:

```text
id,tipo,W,T,D,criticidad
```

Donde:

- `id`: identificador de la tarea;
- `tipo`: `P` para periodica o `S` para esporadica
- `W`: unidades de trabajo
- `T`: periodo o intervalo min
- `D`: deadline relativo
- `criticidad`: nivel de criticidad

Ejemplo:

```text
1,P,40,20,20,2
```

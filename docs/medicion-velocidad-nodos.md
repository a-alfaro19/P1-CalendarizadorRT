# Medición de la velocidad de los nodos (`s_n`)

Esta prueba mide en la placa real cuántas unidades de trabajo (UT, la función
`unidad_trabajo()` de `ut.c`) caben en un tick. Ese valor es `s_n`, el que el
nodo le responde al host en el mensaje `0x01` y con el que el host calcula
`C(i,n) = ⌈W(i) / s_n⌉`.

No hace falta cablear nada: basta la placa conectada por USB.

## Qué hace la prueba

1. Informa la placa, la frecuencia nominal del CPU y el margen usado.
2. Mide `s_n` diez veces seguidas con un tick de 10 ms y revisa que la
   diferencia entre la mayor y la menor sea de 2 % o menos.
3. Reporta `s_n` para ticks de 1, 5, 10, 20, 50 y 100 ms.

Cada medición ejecuta UT durante 1 s, medido con el temporizador de hardware
(`micros()`). Después se escala a la duración del tick y se le aplica un
margen del 80 %, para dejar tiempo al calendarizador y al envío de eventos. En
total tarda unos 20 s.

## Pasos

Requisitos: PlatformIO (la extensión de VS Code o `pio` en la terminal) y git.

```bash
git clone --branch feature/calendarizador-nodo https://github.com/a-alfaro19/P1-CalendarizadorRT.git
cd P1-CalendarizadorRT/node
```

Conectar **una sola placa** y correr el comando que le corresponde:

```bash
pio test -e esp32     # con el ESP32 conectado
pio test -e pico      # con el Pico conectado
```

El comando compila, carga el programa en la placa, lo ejecuta y muestra los
resultados. La primera vez descarga la plataforma y tarda unos minutos.

### Si se usa Linux

- El usuario tiene que estar en el grupo `dialout`:
  `sudo usermod -aG dialout $USER`. Después hay que cerrar sesión y volver a
  entrar.
- Hay que instalar las reglas udev de PlatformIO, para que la carga funcione
  sin `sudo`:

  ```bash
  curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
  sudo udevadm control --reload-rules && sudo udevadm trigger
  ```

### Primera carga en el Pico

Si el Pico nunca tuvo un programa de Arduino, la primera carga puede fallar
porque no lo encuentra. En ese caso:

1. Desconectarlo.
2. Conectarlo manteniendo apretado el botón **BOOTSEL**.
3. Repetir `pio test -e pico`.

## Qué devolver

La salida trae líneas como estas (los números son de ejemplo):

```text
placa: ESP32, CPU nominal: 240 MHz, margen: 80 %, ventana: 1000 ms
medicion 1: s_n = 1234 (tick 10 ms)
...
dispersion: 0.41 % (min 1230, max 1235)
RESULTADO ESP32 tick_ms=1 s_n=123
RESULTADO ESP32 tick_ms=10 s_n=1234
...
4 Tests 0 Failures 0 Ignored
```

**Copiar la salida completa**, desde `placa:` hasta el resumen final, y
mandarla. Esos valores van en la documentación del proyecto.

## Si algo falla

| Síntoma | Qué hacer |
| :--- | :--- |
| `la dispersion supera el 2 %` | Mandar la salida igual: es un dato útil. Repetir con la placa conectada directo a la computadora, sin hub USB. |
| Carga bien pero no aparece ningún resultado | Correr `pio test -e pico --without-uploading` (o `-e esp32`) para solo leer la salida, o apretar el botón de reset de la placa. |
| `Could not find a port` o se elige el puerto equivocado | Indicar el puerto: `pio test -e esp32 --upload-port /dev/ttyUSB0 --test-port /dev/ttyUSB0`. En el Pico suele ser `/dev/ttyACM0`; en Windows es `COMx`. |
| `Permission denied` en `/dev/tty...` (Linux) | Falta el grupo `dialout` o las reglas udev (ver arriba). |

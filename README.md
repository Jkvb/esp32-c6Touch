| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-H21 | ESP32-H4 | ESP32-P4 | ESP32-S2 | ESP32-S3 | ESP32-S31 | Linux |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | --------- | -------- | -------- | -------- | -------- | --------- | ----- |

# Hello World Example

Starts a FreeRTOS task to print "Hello World".

(See the README.md file in the upper level 'examples' directory for more information about examples.)

## How to use example

Follow detailed instructions provided specifically for this example.

Select the instructions depending on Espressif chip installed on your development board:

- [ESP32 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/index.html)
- [ESP32-S2 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/get-started/index.html)


## Example folder contents

The project **hello_world** contains one source file in C language [hello_world_main.c](main/hello_world_main.c). The file is located in folder [main](main).

ESP-IDF projects are built using CMake. The project build configuration is contained in `CMakeLists.txt` files that provide set of directives and instructions describing the project's source files and targets (executable, library, or both).

Below is short explanation of remaining files in the project folder.

```
├── CMakeLists.txt
├── pytest_hello_world.py      Python script used for automated testing
├── main
│   ├── CMakeLists.txt
│   └── hello_world_main.c
└── README.md                  This is the file you are currently reading
```

For more information on structure and contents of ESP-IDF projects, please refer to Section [Build System](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html) of the ESP-IDF Programming Guide.

## Troubleshooting

* Program upload failure

    * Hardware connection is not correct: run `idf.py -p PORT monitor`, and reboot your board to see if there are any output logs.
    * The baud rate for downloading is too high: lower your baud rate in the `menuconfig` menu, and try again.

## Technical support and feedback

Please use the following feedback channels:

* For technical queries, go to the [esp32.com](https://esp32.com/) forum
* For a feature request or bug report, create a [GitHub issue](https://github.com/espressif/esp-idf/issues)

We will get back to you as soon as possible.


## PowerShell rápido (Windows) para este repo

> Importante: en PowerShell **no uses** los símbolos `< >` literalmente.

### 1) Actualizar tu rama correctamente

```powershell
cd C:\Users\jcarl\esp32\esp32-c6Touch
git fetch origin
git branch
git checkout nombre-real-de-tu-rama
git pull --rebase origin nombre-real-de-tu-rama
```

Ejemplo real:

```powershell
git checkout work
git pull --rebase origin work
```

### 2) Flashear cuidando el puerto COM

```powershell
idf.py -p COM5 flash monitor
```

Si no sabes el puerto:

```powershell
mode
```

### 3) Pines usados en esta implementación

- Display SPI (Waveshare): MOSI=GPIO4, SCK=GPIO5, DC=GPIO6, CS=GPIO7, RST=GPIO14, BL=GPIO15.
- Acelerómetro QMI8658 por I2C: SDA=GPIO20, SCL=GPIO21, direcciones probadas: `0x6A` y `0x6B`.

## Hora automática por WiFi (SNTP)

Este proyecto ya puede sincronizar hora real por internet (NTP) usando WiFi del celular/hotspot.

### Configuración rápida

1. Abre configuración:

```powershell
idf.py menuconfig
```

2. Ve a **IAWICHU Clock** y configura:
   - `WiFi SSID`
   - `WiFi Password`
   - `NTP server` (default: `pool.ntp.org`)
   - `Timezone TZ string`

3. Para México (CDMX), usa por ejemplo:

```text
CST6CDT,M4.1.0/2,M10.5.0/2
```

4. Flashea y monitorea:

```powershell
idf.py -p COM6 flash monitor
```

Si no hay WiFi o credenciales, la UI se queda en `--:--:--` hasta que haya hora válida.


## VS Code: errores "cannot open source file" (IntelliSense)

Si ves errores como `cannot open source file "esp_log.h"` o `driver/gpio.h` en VS Code, normalmente **no es un error real de compilación**: falta que VS Code cargue el entorno ESP-IDF y la base de compilación.

### Pasos recomendados (Windows)

1. Abre VS Code desde una terminal ESP-IDF o carga entorno primero:

```powershell
C:\esp\esp-idf\export.ps1
```

2. En el proyecto, genera/re-genera archivos de build (incluye `compile_commands.json`):

```powershell
cd C:\Users\jcarl\esp32\esp32-c6Touch
idf.py reconfigure
```

3. Si usas extensión C/C++ de Microsoft, en `.vscode/settings.json` apunta a:

```json
{
  "C_Cpp.default.compileCommands": "${workspaceFolder}/build/compile_commands.json",
  "C_Cpp.default.configurationProvider": "espressif.esp-idf-extension"
}
```

4. Recarga VS Code (`Developer: Reload Window`).

Con eso desaparecen los falsos positivos de includes en `esp_*`, `driver/*`, `lvgl.h`, etc.


## Si tu repo local no coincide (ejemplo: commit `1811f02` en `main`)

Si ves que `git show --stat --oneline HEAD` te da un commit distinto al esperado, estás en otra historia de rama.

### Flujo seguro para traer cambios nuevos

```powershell
cd C:\Users\jcarl\esp32\esp32-c6Touch
git fetch --all --prune
git branch -a
git checkout main
git pull --rebase origin main
```

Si existe rama remota de trabajo (por ejemplo `origin/work`):

```powershell
git checkout -B work origin/work
```

Si **no existe** `origin/work`, entonces los cambios nuevos aún no están en remoto y debes pedir el hash/branch exacto para hacer `cherry-pick` o `pull`.

### Comandos equivalentes en PowerShell (no Linux)

- Último commit:

```powershell
git show --stat --oneline HEAD
```

- Ver últimas líneas de README:

```powershell
Get-Content README.md -Tail 70
```

- Ver README con número de línea:

```powershell
$ln=0; Get-Content README.md | ForEach-Object { $ln++; "{0,4}: {1}" -f $ln, $_ }
```

## Configuración WiFi desde la pantalla (panel lateral)

Ahora puedes configurar red sin recompilar:

1. En la pantalla del reloj, toca el botón lateral **W**, toca la barra lateral `≡` o arrastra desde el borde derecho hacia la izquierda (estilo smartwatch).
2. Se abre el panel **Config WiFi**.
3. Escribe `SSID` y `Password`.
4. Toca **Guardar**.
5. El firmware aplica credenciales, reconecta WiFi y vuelve a sincronizar hora por SNTP.

Logs esperados en monitor:
- `EVENTO_WIFI_CONECTADO ...`
- `EVENTO_FECHA_ACTUALIZADA ...`

## Flujo recomendado SIEMPRE para actualizar y cargar (PowerShell)

```powershell
cd C:\Users\jcarl\esp32\esp32-c6Touch
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
C:\esp\esp-idf\export.ps1

git fetch --all --prune
git checkout work
git pull --rebase origin work

idf.py fullclean
idf.py reconfigure
idf.py -p COM6 flash monitor
```

Si `idf.py` no existe en la terminal, vuelve a correr `C:\esp\esp-idf\export.ps1` en esa misma ventana.


## PowerShell: errores comunes (rg y símbolos ✅)

Si en Windows te aparece:
- `rg : The term 'rg' is not recognized...`
- `✅ : The term '✅' is not recognized...`

usa estos equivalentes nativos de PowerShell:

### Buscar texto sin `rg`

```powershell
Select-String -Path .\main\ui_clock.c, .\README.md -Pattern "screen_gesture_open_cb|btn_swipe_hint|barra lateral|LV_EVENT_GESTURE"
```

### Estado y commit actual (sin emojis al inicio)

```powershell
git status --short
git rev-parse --short HEAD
git show --stat --oneline HEAD
```

> Nota: los prefijos `✅`, `⚠️`, `❌` se usan solo en reportes/documentación, **no** se escriben en la terminal de PowerShell.

### Si ves `cebd79d` y esperabas cambios nuevos

Eso indica que estás en una historia vieja/local. Sincroniza así:

```powershell
git fetch --all --prune
git branch -a
git checkout main
git pull --rebase origin main
```

Si existe rama remota de trabajo:

```powershell
git checkout -B work origin/work
```


## PowerShell vs comandos Linux (chuleta rápida)

En PowerShell de Windows estos comandos de Linux **no** existen por defecto:
- `tail`
- `nl`
- `rg` (si no instalaste ripgrep)
- prefijos visuales como `✅` al inicio de una línea

Usa estas equivalencias:

| Si intentaste | En PowerShell usa |
| --- | --- |
| `✅ git status --short --branch` | `git status --short --branch` |
| `tail -n 80 README.md` | `Get-Content README.md -Tail 80` |
| `nl -ba README.md | tail -n 120` | `$ln=0; Get-Content README.md | ForEach-Object { $ln++; "{0,4}: {1}" -f $ln, $_ } | Select-Object -Last 120` |
| `rg -n "patron" archivo` | `Select-String -Path .\archivo -Pattern "patron"` |

### Verificación rápida (copiar/pegar)

```powershell
git status --short --branch
git rev-parse --short HEAD
Get-Content README.md -Tail 80
$ln=0; Get-Content README.md | ForEach-Object { $ln++; "{0,4}: {1}" -f $ln, $_ } | Select-Object -Last 120
Select-String -Path .\main\ui_clock.c, .\README.md -Pattern "screen_gesture_open_cb|btn_swipe_hint|barra lateral|LV_EVENT_GESTURE"
```


## Script de 1 comando para actualizar repo + flashear (PowerShell)

Si quieres "descargar/subir cambios y actualizar la tarjeta" en una sola corrida:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\update_and_flash.ps1 -Port COM6 -Branch main
```

Opcional (sin `fullclean`):

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\update_and_flash.ps1 -Port COM6 -Branch main -NoClean
```

El script hace:
1. Activa `C:\esp\esp-idf\export.ps1`
2. `git fetch` + `checkout` + `pull --rebase`
3. `idf.py reconfigure`
4. `idf.py -p COM# flash`
5. `idf.py -p COM# monitor`


### Si PowerShell dice que `.\tools\update_and_flash.ps1` no existe

Ese error significa que tu copia local aún no trae ese archivo. Haz esto:

```powershell
cd C:\Users\jcarl\esp32\esp32-c6Touch
git fetch --all --prune
git checkout main
git pull --rebase origin main
Test-Path .\tools\update_and_flash.ps1
```

Si `Test-Path` devuelve `True`, ya puedes correr:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\update_and_flash.ps1 -Port COM6 -Branch main
```

También puedes usar el wrapper en la raíz del repo:

```powershell
powershell -ExecutionPolicy Bypass -File .\update_and_flash.ps1 -Port COM6 -Branch main
```


## Subir cambios a remoto (PowerShell, sin errores comunes)

Si quieres "subir cambios" y te salen errores por scripts faltantes, primero sincroniza:

```powershell
cd C:\Users\jcarl\esp32\esp32-c6Touch
git fetch --all --prune
git checkout main
git pull --rebase origin main
```

Verifica que los scripts existan:

```powershell
Test-Path .\update_and_flash.ps1
Test-Path .\tools\update_and_flash.ps1
```

Ambos deben dar `True`. Luego, para subir tus cambios:

```powershell
git status --short
git add -A
git commit -m "tu mensaje"
git push origin main
```

> Importante: en PowerShell **no** escribas `✅` al inicio del comando.


### Si `origin/main` está "up to date" pero los `.ps1` siguen en `False`

Eso significa que esos commits todavía no están en tu `origin/main` (aún no mergeados).

Opciones:

1) Cambiarte a la rama que sí los trae (si existe):

```powershell
git fetch --all --prune
git branch -a
git checkout work
```

2) O traer commits por hash **solo si existen en tu remoto**:

```powershell
git fetch --all --prune
git log --oneline --all -- update_and_flash.ps1
# toma los hashes reales que te aparezcan
git checkout main
git cherry-pick <HASH_1> <HASH_2>
```

Si sale `fatal: bad revision`, ese hash no existe en tu remoto todavía.
En ese caso usa plan B (crear scripts localmente) y sigue trabajando:

```powershell
@'
param(
  [string]$Port = "COM6",
  [string]$Branch = "main",
  [switch]$NoClean
)
$ErrorActionPreference = "Stop"
$scriptPath = Join-Path $PSScriptRoot "tools\update_and_flash.ps1"
if (-not (Test-Path $scriptPath)) {
  throw "No se encontró $scriptPath"
}
& $scriptPath -Port $Port -Branch $Branch -NoClean:$NoClean
'@ | Set-Content -Path .\update_and_flash.ps1 -Encoding UTF8

New-Item -ItemType Directory -Force .\tools | Out-Null
@'
param(
  [string]$Port = "COM6",
  [string]$Branch = "main",
  [switch]$NoClean
)
$ErrorActionPreference = "Stop"
& "C:\esp\esp-idf\export.ps1"
Set-Location "C:\Users\jcarl\esp32\esp32-c6Touch"
git fetch --all --prune
git checkout $Branch
git pull --rebase origin $Branch
if (-not $NoClean) { idf.py fullclean }
idf.py reconfigure
idf.py -p $Port flash
idf.py -p $Port monitor
'@ | Set-Content -Path .\tools\update_and_flash.ps1 -Encoding UTF8
```

Verifica:

```powershell
Test-Path .\update_and_flash.ps1
Test-Path .\tools\update_and_flash.ps1
```


## Fix definitivo cuando NO existe `origin/work` (copiar/pegar)

Si `git branch -a` no muestra `origin/work`, ejecuta esto tal cual en PowerShell para crear scripts locales y flashear:

```powershell
cd C:\Users\jcarl\esp32\esp32-c6Touch
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
C:\esp\esp-idf\export.ps1

@'
param(
  [string]$Port = "COM6",
  [string]$Branch = "main",
  [switch]$NoClean
)
$ErrorActionPreference = "Stop"
$scriptPath = Join-Path $PSScriptRoot "tools\update_and_flash.ps1"
if (-not (Test-Path $scriptPath)) {
  throw "No se encontró $scriptPath"
}
& $scriptPath -Port $Port -Branch $Branch -NoClean:$NoClean
'@ | Set-Content -Path .\update_and_flash.ps1 -Encoding UTF8

New-Item -ItemType Directory -Force .\tools | Out-Null
@'
param(
  [string]$Port = "COM6",
  [string]$Branch = "main",
  [switch]$NoClean
)
$ErrorActionPreference = "Stop"
& "C:\esp\esp-idf\export.ps1"
Set-Location "C:\Users\jcarl\esp32\esp32-c6Touch"
git fetch --all --prune
git checkout $Branch
git pull --rebase origin $Branch
if (-not $NoClean) { idf.py fullclean }
idf.py reconfigure
idf.py -p $Port flash
idf.py -p $Port monitor
'@ | Set-Content -Path .\tools\update_and_flash.ps1 -Encoding UTF8

Test-Path .\update_and_flash.ps1
Test-Path .\tools\update_and_flash.ps1
powershell -ExecutionPolicy Bypass -File .\update_and_flash.ps1 -Port COM6 -Branch main
```


## Menú tipo Smartwatch para WiFi (nuevo)

La UI ahora incluye un **Smart Menu**:
- Abre con botón `W`, barra lateral `≡` o gesto horizontal.
- Pantalla de apps con tarjeta **WiFi**.
- Formulario `SSID`/`Password` con teclado en pantalla.
- Botón `Guardar` aplica credenciales en runtime (callback de `main.c`).


### Logs de touch/menú WiFi para diagnóstico

Si el touch no responde, en monitor debes ver trazas como:
- `UI_CLOCK: ui_clock_create init`
- `UI_CLOCK: Gesture detectado dir=...`
- `UI_CLOCK: Tap en abrir menu (W/handle)`
- `UI_CLOCK: Tap app WiFi`
- `UI_CLOCK: Focus en input WiFi`
- `UI_CLOCK: Guardar WiFi desde UI (ssid_len=... pass_len=...)`

Si no aparece ninguna, revisa driver/calibración touch del display.


## Fix rápido: `idf.py` no reconocido en PowerShell

Si te sale `idf.py : The term 'idf.py' is not recognized...`, tu sesión perdió el entorno ESP-IDF.

Ejecuta en **esa misma** ventana de PowerShell:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
C:\esp\esp-idf\export.ps1
idf.py --version
idf.py -p COM6 flash monitor
```

Si cerraste la terminal o restauraste historial, debes repetir `export.ps1` antes de usar `idf.py`.

### Push correcto (evitar typo `git pu;;`)

```powershell
git status --short
git push origin main
```


## Script anti-error: `idf.py` no reconocido

Si PowerShell te marca `idf.py` como comando no reconocido, usa este script (siempre re-exporta entorno):

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_monitor.ps1 -Port COM6
```

Este script hace:
1. Ejecuta `C:\esp\esp-idf\export.ps1`
2. Verifica `idf.py --version`
3. Ejecuta `idf.py -p COM6 flash monitor`


## Caso real: `main` al día pero faltan scripts y `idf.py`

Si te pasa esto:
- `git pull` => `Already up to date`
- `.\tools\flash_monitor.ps1` no existe
- `idf.py` no reconocido

usa este flujo (sin depender de scripts):

```powershell
cd C:\Users\jcarl\esp32\esp32-c6Touch

# 1) Detecta si hay rama remota de trabajo con cambios

git fetch --all --prune
git branch -r

# Si existe, cámbiate (ejemplo real visto en tus logs)
# git checkout -B codex/add-accelerometer-controlled-clock origin/codex/add-accelerometer-controlled-clock

# 2) Activa ESP-IDF en ESTA misma terminal
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
C:\esp\esp-idf\export.ps1
idf.py --version

# 3) Flashea directo (aunque no existan scripts .ps1)
idf.py -p COM6 flash monitor
```

Si `idf.py --version` falla, vuelve a ejecutar `C:\esp\esp-idf\export.ps1` en la misma ventana.


## UI extra: pantalla WiFi por swipe


- Nueva pantalla completa de WiFi por swipe (izquierda), independiente del drawer.
- Permite abrir configuración rápida sin entrar al menú de apps.


## Seleccionar red WiFi desde UI (scan + teclado)

Ahora hay dos formas dentro de la UI para configurar WiFi:
1. **Drawer Smart Menu** (botón `W` / swipe derecha-arriba)
2. **Pantalla Swipe WiFi** (swipe izquierda)

En ambas puedes:
- tocar **Scan** para escanear redes disponibles,
- seleccionar una red de la lista (dropdown),
- escribir/ajustar password con teclado virtual,
- tocar **Guardar** para conectar.


### Color magenta en pantalla (fix aplicado)

Si el blanco se ve magenta en ST7789, se ajustó el orden de color del panel a **BGR** en el driver.
Además, el reloj ahora cambia color dinámicamente para confirmar que el pipeline de color está bien.

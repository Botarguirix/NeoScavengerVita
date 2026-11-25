# Entorno Vita para NeoScavenger (20-11-2026)

Leyenda: ✅ hecho en el PC · 🙋 lo haces tú · ⬜ pendiente

## PC (estado actual)

| Pieza | Estado |
|---|---|
| VitaSDK (GCC 15.2) con vitaGL, vitashark, mathneon, SceShaccCgExt, vorbis, zlib, taihen | ✅ `C:\vitasdk` |
| mbedtls y libk añadidas con vdpm | ✅ |
| MSYS2 UCRT64 (GCC, CMake 4.5, Ninja, SDL2) | ✅ |
| vita-agent-use, commit `ae140b9` (el mismo de Valdis) | ✅ `tools/vita/vita-agent-use` |
| Plugins del agente compilados con `-O2` (con `-O3`, `vita-elf-create` se cae con 0xC0000374, el bug conocido) | ✅ `tools/vita/vita-agent-use/build_o2/`: `vita_agent_kernel.skprx`, `vita_agent_loader.suprx`, `vita_agent_shell.suprx` |
| DMR v1.00, CapUnlocker v1.4, PSVshell v1.1 | ✅ `tools/vita/plugins/` |
| Cliente del agente | ⬜ necesita **WSL + Ubuntu** (usa `fcntl`, que no existe en Windows) |

### 🙋 1. Instalar WSL con Ubuntu (PowerShell como administrador)

```powershell
wsl --install -d Ubuntu
```

Reinicia y crea el usuario de Ubuntu cuando lo pida. Después, Claude hace el resto: instalar Python en Ubuntu, el entorno con `cryptography`, `~/wsl-agent.sh` y el emparejamiento.

## Vita nueva (ya tiene HENkaku/Ensō)

### 🙋 2. Copiar archivos a `ur0:tai/` (VitaShell, por FTP o USB)

- `DMR.skprx`, `CapUnlocker.skprx`, `PSVshell.skprx`
- `vita_agent_kernel.skprx`, `vita_agent_loader.suprx`, `vita_agent_shell.suprx`

### 🙋 3. Editar `ur0:tai/config.txt`

Haz antes una copia de seguridad (`config_backup.txt`). Esta edición la haces tú, porque Claude no toca `config.txt` sin tu permiso explícito. Añade:

```
*KERNEL
ur0:tai/DMR.skprx
ur0:tai/CapUnlocker.skprx
ur0:tai/PSVshell.skprx
ur0:tai/vita_agent_kernel.skprx

*main
ur0:tai/vita_agent_loader.suprx
```

Las líneas van debajo de las secciones `*KERNEL` y `*main` que ya existen; no se duplican las cabeceras. Reinicia la Vita.

### 🙋 4. `libshacccg.suprx` (obligatorio para vitaGL)

Extráelo de tu propia consola con **ShaRKBR33D** (lo deja en `ur0:data/libshacccg.suprx`). Sin él, cualquier app hecha con vitaGL se cierra al arrancar.

### 🙋 5. Relojes

Con PSVshell, sube a **500 MHz de CPU y 222 MHz de GPU** (lo predeterminado del proyecto). Más reloj da más calor y puede acortar la vida útil de la consola. NeoScavenger a 30 fps probablemente necesite menos; se medirá.

### ✅ Verificado en la Vita nueva (20-11-2026)

- Firmware real 3.65 (reporta 3.74), HENkaku, Wi-Fi 100 %, en la misma red que el PC (el PC va por cable a `192.168.1.x`; con la Vita en otra subred no hay conexión).
- Módulos cargados: **DMR, CapUnlocker, vita_agent_kernel, vita_agent_loader, PSVshell, vitamemio_kernel**. `libshacccg.suprx` presente en `ur0:data/` (3,4 MB).
- Agente emparejado desde Ubuntu (WSL): `system snapshot` y `screen capture` funcionan; captura 960×544 guardada en `work/vita_prueba1.jpg` (LiveArea de VitaShell).
- ⚠️ `ux0:` tiene solo **6,5 GB libres de 256 GB**: hay que vigilarlo con los datos convertidos del juego.
- La IP de la Vita puede cambiar (DHCP): si el agente deja de responder, mirar la IP y actualizar `~/vita-agent-config/config.json`.
- Sin verificar aún: relojes 500/222 MHz, cuarto núcleo activo en un juego real (se mide con el primer build).

### 6. Comprobar (lo hace Claude cuando esté WSL)

- `~/wsl-agent.sh bootstrap` y luego `session pair` (🙋 tocar OK en la Vita; la Vita y el PC en la misma red).
- `system snapshot` debe responder y mostrar DMR, CapUnlocker y los 4 núcleos.
- Screenshot de prueba.

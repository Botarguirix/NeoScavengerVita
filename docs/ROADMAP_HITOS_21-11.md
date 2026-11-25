# NEO Scavenger PSVITA — Roadmap hacia el juego completo 1:1

Actualizado: 2026-11-21. Objetivo: **el juego original completo, idéntico en reglas y contenido, a 30 fps fijos en PS Vita**, con la UI adaptada a 960×544, controles táctiles/mando y **soporte de mods de NexusMods**.

Leyenda: ✅ hecho y verificado (dónde) · 🖥️ hecho, solo probado en PC · 🔶 en curso · ⬜ pendiente. Detalle técnico y mediciones anteriores: [docs/ROADMAP_NOTAS.md](docs/ROADMAP_NOTAS.md).

## Estrategia (decidida)
**Ruta híbrida:** el juego original es ActionScript 3 sobre Flixel (310 clases, ~54 000 líneas, 40 MB de SWF). No se reescribe la lógica: se **traduce a C++ con `tools/as3cpp`** y se ejecuta sobre un **runtime propio** (`runtime/as3rt.h`, `flash.h`) y un **backend de Vita** (vitaGL, sceCtrl/táctil, audio por software). Así las reglas son *las del original*, no una imitación. La paridad se demuestra con datos y pruebas (hito 7), no a ojo.

## Estado actual (verificado)
| Pieza | Estado |
|---|---|
| Entorno Vita nueva: VitaSDK, vitaGL, agente (WSL), DMR/CapUnlocker/PSVshell cargados | ✅ en la Vita |
| Esqueleto 00.02: vitaGL, pak de texturas, **30,0 fps bloqueados**, hilo cargador en el núcleo 2 | ✅ en la Vita |
| SWF extraído y decompilado (JPEXS + desofuscado), 296 clases del juego | 🖥️ |
| Traductor AS3→C++ y runtime AS3: **296/296 clases compilan y enlazan** en un ejecutable de PC (0 símbolos sin resolver), 42/42 pruebas con código traducido real | 🖥️ |
| Gestor de mods + instalador de zip (NexusMods): **24/24 pruebas** con zips reales y los XML reales del juego | 🖥️ |
| Datos del port anterior en la Vita (`ux0:data/neoscavvita`: 24 XML, `img.pak`) y su log como referencia de cifras | ✅ |

---
## HITO 1 — Todo el código traducido compila y enlaza (PC)
*Meta: `296/296` clases compilan, se enlazan en un ejecutable de PC sin símbolos sin resolver.*
- 1.1 ✅ 🖥️ Las 296 clases compilan (`tools/as3cpp/check_all.sh` → `compilan: 296 de 296`).
- 1.2 🖥️ Cadenas cifradas: el descifrador RC4 traducido corre dentro del juego y los nombres de XML y rutas salen en claro (no hace falta volcar tabla).
- 1.3 🖥️ Estáticos en `as3_init_statics()` (generado, orden por dependencias): el arranque en PC termina sin excepciones.
- 1.4 🖥️ Inferencia de tipos de locales `*` (contadores → `int`, acumuladores → `double`, bool/String/clase): hecha; los bucles revisados ya usan enteros nativos. Falta repasar las 10 clases más pesadas y medir.
- 1.5 🖥️ Enlazado completo hecho (`tools/as3cpp/build_pc.sh`: cabecera precompilada + 296 objetos + `port/pc/main_pc.cpp`). Falta el backend real de PC.

## HITO 2 — Datos: el juego carga sus 24 XML igual que el original  (🖥️ PC: carga completa al 100 %)
*Meta: el log de arranque coincide con el del port anterior y con el original.*
- 2.1 🖥️ `XML`/`XMLList` (E4X mínimo) con parser nativo: los 24 XML (6,7 MB) se leen. Falta medir el tiempo y binarizar al instalar para cargar rápido en la Vita.
- 2.2 🖥️ `DataHandler` sobre archivos locales (`URLLoader`, `Loader` con PNG propio): **«Done loading data», 100 % en 58 fotogramas** con el propio código del juego (`tools/as3cpp/run_pc.sh`).
- 2.3 🖥️ **Resultado medido (PC, 21-11):** tablas de tesoro **764 ✓**, encuentros **2264 ✓**, disparadores **133 ✓**, ingredientes **128 ✓**, recetas **105 ✓**, movimientos de combate **63 ✓**, modos de ataque **61 ✓**, condiciones **872 ✓** (de 2198 posiciones: el array se indexa por id), criaturas **28 ✓**, fuentes de criaturas **32 ✓**, campamentos **14 ✓**, lugares DMC **7 ✓**, titulares 48, tipos de hexágono 37, contenedores 39, facciones 14, hexágonos prohibidos 16, variables 19, perfiles de carga 32, imágenes **2326 ✓**. **Objetos: 743**, no 537: el código del juego añade clones (`UpdateTemplateItems` clona los grupos 12 y 36, y otros pasos clonan más). No se da por verificado hasta compararlo con el original (hito 7.1).
- **Aceptación (números del log original):** 28 criaturas/32 fuentes, **537 tipos de objeto**, **764 tablas de tesoro**, **2264 encuentros**, 133 disparadores, 128 ingredientes, 105 recetas, 63 movimientos de combate, 61 modos de ataque, 872 condiciones, 14 tipos de campamento, 7 lugares DMC. Tiempo de carga en Vita ≤ 2,5 s (el port anterior: 2,45 s con XML crudo).

## HITO 3 — Gráficos: Flixel/Flash dibujan en pantalla
*Plan decidido (21-11): Flixel es un motor de *blitting* por software: dibuja cada cámara en un `BitmapData` (`FlxCamera.buffer`). Primero se implementan las operaciones de `BitmapData` en CPU (copyPixels con alfa, draw, fillRect, colorTransform, scroll...) y el texto con las fuentes reales; el resultado del fotograma es un único bitmap que el backend sube como textura. Así se pueden comparar capturas en PC sin GPU, y luego se mueve el cuello de botella a GPU/NEON en el hito 11.*
*Meta: menú principal y mapa se ven igual que en el original.*
- 3.1 🖥️ **Menú principal dibujado por el código original en PC** (`work/captura_menu.png`, 23-11): copyPixels con máscara y mezcla, draw de bitmaps/texto/sprites anidados con transformaciones, colorTransform, scroll, Graphics (rectángulos/círculos). Pendiente: comparar con el original píxel a píxel y el resto de pantallas. Detalle original: `copyPixels`, `draw`, `colorTransform`, `getPixel`, `fillRect`, `lock/unlock`. El juego es un motor de *blitting*: se implementan los blits sobre texturas de GPU (vitaGL) y los accesos de píxeles en CPU donde el juego los exige (`MapUtils`, colisión por píxel).
- 3.2 🖥️ Fuentes reales: `tools/extract_fonts.py` saca las 4 `DefineFont3` del SWF y `runtime/font.h` las rasteriza (contornos, 4×4, caché); `TextField` usa sus métricas. Falta HTML/varios formatos por campo si el juego los usa. Texto: `TextField`/`TextFormat` con las fuentes `pixeltiny`, `pixelmid`, `pixelsupertiny`, `system` (`.df3` ya extraídas en el port anterior) — medir texto con las mismas métricas del original.
- 3.3 ⬜ Formato de texturas **NSVPAK02 + DXT5/LZ4** y carga bajo demanda por escena (las 2326 imágenes decodificadas pesan **389 MB**; presupuesto: ≤ 112 MB de VRAM). Lectura de disco ≥ 40 MB/s con bloques grandes (hoy ~3 MB/s con `fread`).
- 3.4 ⬜ Filtros/mezclas que usa el juego (`BlendMode`, `perlinNoise`, `applyFilter`) o su equivalente.
- **Aceptación:** capturas de menú principal, selección de personaje, mapa, inventario y combate iguales al original (comparación visual lado a lado + diferencia de imagen en PC).

## HITO 4 — Bucle de juego y entrada
- 4.1 🔶 `FlxGame`: **el bucle del juego original ya corre en PC sin pantalla**: tras la carga, `Preloader` → `FlxGame.create` → `MenuState.create` (construye el menú de escape, textos y botones) y se sostienen **300 fotogramas en MenuState sin errores** (`work/run32.log`). Faltan: 30 fps fijos/`FlxG.elapsed` coherente y cámaras/`FlxTilemap`/`FlxHexmap` con dibujo real.
- 4.2 ⬜ Entrada: ratón → **táctil frontal** (toque = clic, arrastrar = arrastrar objetos), mando (stick = cursor, ✕ = clic, ○ = clic derecho/cancelar, △□ atajos, L/R pestañas, START = menú), teclado del juego mapeado.
- **Aceptación (PC con ratón, luego Vita):** de «Nuevo juego» a la primera casilla y moverse 10 turnos sin errores.

## HITO 5 — Audio
- 5.1 ⬜ `Sound`/`SoundChannel`/`SoundTransform`: 154 sonidos embebidos (WAV/MP3 del SWF) + 16 pistas de la banda sonora (`OST/*.mp3`). Mezclador por software < 1 ms/frame; música en streaming.
- 5.2 ⬜ Conversión MP3 → formato barato de decodificar en Vita (OGG/AAC por hardware, regla del proyecto) en el primer arranque.
- **Aceptación:** misma música/efectos por escena; ningún tirón de audio a 30 fps.

## HITO 6 — Guardado y estado persistente
- 6.1 ⬜ `SharedObject` → archivo en `ux0:data/neoscavvita/save/`; compatibilidad con partidas del original (importar desde PC).
- 6.2 ⬜ Autoguardado, suspender/reanudar de la Vita, cierre seguro.
- **Aceptación:** guardar → cerrar → cargar reproduce exactamente el estado (huella).

## HITO 7 — Paridad 1:1 demostrable
*«1:1» se define como: mismas reglas, mismos datos, mismos resultados.*
- 7.1 ⬜ **Datos:** cuentas del hito 2 + comparación de cada XML cargado (hash del modelo en memoria) contra el original.
- 7.2 ⬜ **Determinismo:** `FlxG.random` con semilla fija; volcado de «huella» del estado (condiciones, inventario, hex, hora, clima) cada turno. Escenarios guionizados (nueva partida con los 8 orígenes, 100 turnos de movimiento/rebusca/descanso/combate) ejecutados en el original (PC, Flash) y en el port; las huellas deben coincidir turno a turno.
- 7.3 ⬜ **Cobertura:** lista de funcionalidades del juego (movimiento, rebusca, crafteo 105 recetas, combate, campamento, comercio, condiciones, clima, noche/día, muerte, DMC, finales) marcada una a una en el original y en el port.
- 7.4 ⬜ Diferencias aceptadas documentadas (si las hay) con su motivo.
- **Aceptación:** 0 divergencias en los escenarios guionizados; lista de cobertura 100 %.

## HITO 8 — Primer ejecutable completo en la Vita
- 8.1 ⬜ Portar backend a Vita (vitaGL, entrada, audio) y desplegar con `tools/vita/deploy_port.sh`.
- 8.2 ⬜ Arranque → menú → nueva partida → 10 turnos, con capturas y log `[perf]`.
- **Aceptación:** 30,0 fps en menú y mapa; RAM < 381 MiB y VRAM < 112 MiB medidas y comunicadas.

## HITO 9 — UI limpia en 960×544 (pantalla de la Vita)
*El juego soporta 5 diseños de GUI (800×450, 1024×768, 1360×768, 1600×900, 2400×1350). La Vita es 960×544.*
- 9.1 ⬜ Elegir y medir en pantalla real entre **800×450 escalado ×1,2 (= 960×540)** y **1600×900 reducido ×0,6**. Criterio: texto legible a 5" (altura mínima de letra ≈ 8 px reales), bordes nítidos, sin solapes. Barras de 2 px arriba/abajo (544 − 540).
- 9.2 ⬜ Ajustes de layout propios de Vita (sin tocar reglas): zona segura, paneles que no se tapen, tamaño de objetivos táctiles ≥ 44 px, cursor visible para stick, tooltips por pulsación larga (no hay «hover»).
- 9.3 ⬜ Iconos de botones de Vita (✕ ○ □ △) en lugar de teclas; menú de opciones con mapeo de controles y escala de UI.
- **Aceptación:** revisión con capturas de todas las pantallas (menús, mapa, inventario, combate, crafteo, comercio, opciones, game over); lista de problemas = 0.

## HITO 10 — Soporte de mods de NexusMods
*El juego ya tiene su sistema: `getmods.php` (nombre + carpeta) y cada mod aporta `data/*.xml` (contenido nuevo) o es un mod «0» (sustituye datos originales); también imágenes (`getimages.php`).*
- 10.3 🖥️ **hecho en PC** (`port/src/modmgr.h`, 24/24 pruebas): lector ZIP propio (zlib, deflate/almacenado), detección de la raíz del mod (carpeta, plano o anidado), protección contra rutas peligrosas (`..`), límites (64 MB por archivo, 256 MB por mod), nombre de mod saneado, validación de XML (mod roto → desactivado con motivo), `modorder.txt`, lectura del `getmods.php` original. Falta integrarlo en el menú y probarlo en la Vita.
- 10.1 ⬜ Carga de mods en el orden del original (`ParseModList`, `LoadMod`, fusión «nuevo contenido» vs «sustituir»), con registro de arranque idéntico (el juego copia sus mensajes de carga).
- 10.2 ⬜ Carpeta `ux0:data/neoscavvita/mods/<Mod>/` con detección automática (cualquier carpeta con `data/` o `getimages.php`), `modorder.txt` para el orden y activar/desactivar desde el menú del port.
- 10.3 ⬜ **Instalador de zip en la Vita:** dejar el `.zip` descargado de NexusMods en `ux0:data/neoscavvita/mods_inbox/` y el port lo descomprime (lector ZIP propio con zlib), detecta la raíz del mod y lo registra. `.rar`/`.7z`: herramienta de PC `tools/install_mod.py` (convierte y copia) — la Vita no descomprime esos formatos.
- 10.4 ⬜ Validación y aislamiento: XML mal formado → mod desactivado con mensaje (no cuelga el juego); límites de memoria; lista de mods con versión y autor (`mod.txt` opcional).
- 10.5 ⬜ Imágenes de mods (PNG): conversión a NSVPAK02 al instalar (la Vita no decodifica PNG rápido).
- **Aceptación:** el mod de ejemplo `SampleModFolder` (bolsa «fancy shopping bag») funciona; 3 mods reales de NexusMods probados (contenido nuevo, sustitución y con imágenes).

## HITO 11 — Rendimiento y memoria
- 11.1 ⬜ 30 fps fijos en todas las escenas; presupuesto de frame 33,3 ms; cuellos de botella con `[CPU-bound]`/`[GPU-bound]`/... en el log.
- 11.2 ⬜ Multihilo en los 4 núcleos (carga de texturas/audio/IO fuera del hilo principal); informe de carga por núcleo.
- 11.3 ⬜ Memoria: estrategia para el no-GC (arena por escena/partida o conteo de referencias), sin fugas en sesión larga (2 h).
- 11.4 ⬜ Relojes 500/222 MHz como objetivo (los sube el usuario con PSVshell; avisar de calor y desgaste); medir cuánto margen hay a 333/166.
- **Aceptación:** 2 h de juego sin caer de 30 fps ni crecer en memoria.

## HITO 12 — QA completa y entrega
- 12.1 ⬜ Partida completa de principio a fin en la Vita (los orígenes y finales), con registro de lo verificado en hardware (lista «verificado / no verificado»).
- 12.2 ⬜ Suspender/reanudar, batería, apagado, tarjeta SD llena.
- 12.3 ⬜ LiveArea e icono propios sin arte del juego; README; guía de instalación (copiar datos de Steam; conversión en la Vita al primer arranque).
- 12.4 ⬜ Publicación: solo el motor (sin datos ni código derivado); el VPK con datos derivados es de uso personal.

---
## Reglas del proyecto
- Solo se trabaja con la copia legal del usuario; nada del juego se sube a repositorios.
- Se anuncia cada descarga (nombre, origen, tamaño) y se pide permiso. No se toca `ur0:tai/config.txt` sin permiso explícito.
- «Funciona» solo si se ha visto en la Vita; listas honestas de verificado / no verificado.
- Relojes por defecto 500/222 MHz: los sube el usuario; avisar de calor y desgaste.
- Audio < 1 ms/frame; logs con etiquetas de cuello de botella.
- Mover/borrar archivos en la Vita: nunca `fs trash` sobre rutas que se reutilizan.

## Riesgos principales
| Riesgo | Mitigación |
|---|---|
| Rendimiento del código traducido (variables `*`, `Any`, sin GC) | Hito 1.4 y 11.3 antes de medir en Vita |
| Texturas (389 MB decodificadas) | DXT5+LZ4 y carga por escena (3.3) |
| Legibilidad de la UI en 5" | Hito 9 con capturas en la consola real |
| Paridad «1:1» difícil de demostrar | Determinismo + huellas contra el original en PC (7.2) |
| Mods con datos mal formados | Validación y aislamiento (10.4) |

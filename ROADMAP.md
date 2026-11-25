# NEO Scavenger PS Vita — Roadmap alcanzable hacia el juego completo 1:1

Actualizado: 2026-11-24 (fusión del roadmap por hitos del 21-11 con el de pasos alcanzables del 23-11). Meta final: **el juego original completo, idéntico en reglas y contenido, a 30 fps fijos en PS Vita**, con la interfaz adaptada a 960×544, controles táctiles/mando y **mods de NexusMods**.

**Cómo leer este roadmap.** Cada paso es pequeño (de una a pocas sesiones), va en orden y tiene una condición de «hecho» comprobable. Leyenda: ✅ hecho y comprobado en la Vita · 🖥️ hecho, solo probado en PC · 🔶 en curso · ⬜ pendiente. Entre paréntesis, el hito equivalente del roadmap del 21-11.

Documentación común a todos los ports: **[D:\Proyectos\_VitaComun](../_VitaComun/README.md)**. Historial: [docs/ROADMAP_HITOS_21-11.md](docs/ROADMAP_HITOS_21-11.md), [docs/ROADMAP_NOTAS.md](docs/ROADMAP_NOTAS.md). Controles: [docs/CONTROLES_VITA.md](docs/CONTROLES_VITA.md).

## Estrategia (decidida)
**Ruta híbrida:** el original es ActionScript 3 sobre Flixel (310 clases, ~54 000 líneas, SWF de 40 MB). No se reescribe la lógica: se **traduce a C++ con `tools/as3cpp`** y corre sobre un **runtime propio** (`runtime/as3rt.h`, `flash.h`) y un **backend de Vita** (vitaGL, sceCtrl/táctil, audio por software). Las reglas son *las del original*; la paridad se demuestra con datos y pruebas (fase H), no a ojo. Flixel dibuja por software en un `BitmapData` por cámara: primero se hace todo en CPU (comparable en PC) y luego se mueve a GPU/NEON lo que cueste (F2).

---
## Ya conseguido
| Logro | Dónde |
|---|---|
| Entorno: VitaSDK, vitaGL, agente en WSL emparejado, DMR/CapUnlocker/PSVshell cargados | ✅ |
| Esqueleto 00.02: vitaGL, paquete de texturas, **30,0 fps bloqueados**, hilo cargador en el núcleo 2 | ✅ |
| Datos del port anterior en la Vita (`ux0:data/neoscavvita`) y su log como referencia | ✅ |
| SWF decompilado (JPEXS + desofuscado); cadenas RC4 descifradas por el propio código traducido | 🖥️ |
| Traductor + runtime: **296/296 clases compilan y enlazan**, 42/42 pruebas; estáticos en orden de dependencias; inferencia de tipos de locales `*` | 🖥️ |
| **Carga de datos al 100 %** (58 fotogramas): 764 tesoros, 2264 encuentros, 133 disparadores, 128 ingredientes, 105 recetas, 63 movimientos, 61 modos de ataque, 872 condiciones, 28 criaturas, 32 fuentes, 14 campamentos, 7 DMC, 2326 imágenes | 🖥️ |
| **Menú principal dibujado** por el código original con las 4 fuentes reales (`work/captura_menu.png`) | 🖥️ |
| Bucle de Flixel: 300 fotogramas en MenuState sin errores; un clic simulado en «NEW GAME» entra en PlayState | 🖥️ |
| Gestor de mods con instalador de zip (raíz, protección `..`, límites, validación XML, `modorder.txt`): 24/24 pruebas | 🖥️ |

---
## Fase A — Partida jugable en PC (hitos 3–4)
| # | Paso | Hecho cuando |
|---|---|---|
| A1 | 🖥️ Partida nueva sin excepciones hasta la pantalla de habilidades (`work/captura_newgame.png`). Arreglados: ordenación con huecos, `stage.loaderInfo`, orden de evaluación de argumentos (tecla F1), `stage.align` | El guion «NEW GAME» llega a la primera pantalla de partida sin excepciones y se guarda su captura |
| A2 | 🖥️ Script de compilación que **se detiene si algo no compila** (probado con un archivo roto) | Un error de compilación hace fallar `run_pc.sh` con mensaje claro |
| A3 | 🔶 Guiones (`NEOS_SCRIPT`, `tests/guiones/`): habilidades → despertar → saltar por la ventana → **mapa** → mover un hexágono → fin de turno, sin excepciones. Arreglados: `hasOwnProperty` dinámico (condiciones), `BitmapData.hitTest`, huecos en `for each`. Pendiente: hexágono revelado que deja de dibujarse, «Moves Left 100/1», inventario | Cada guion termina sin excepciones y deja captura por pantalla |
| A4 | 🖥️ Ventana SDL2 (`port/pc/main_sdl.cpp`, `work/gen17/neoscav_sdl.exe`): 960×544 como la Vita, ratón, rueda y teclado, 30 fps, recolector activo; menú a ~7,5 ms/fotograma. Falta jugar a mano un rato | Se puede jugar con ratón en PC: menú → partida → mover → inventario |
| A5 | ⬜ Gráficos restantes: `FlxTilemap`/hexmapa, `BlendMode`, `perlinNoise`, `applyFilter`, texto HTML si el juego lo usa | Capturas de selección de personaje, mapa, inventario y combate comparadas lado a lado con el original; diferencias graves corregidas |

## Fase B — Primera versión en la Vita (hito 8)
| # | Paso | Hecho cuando |
|---|---|---|
| B1 | ✅ VPK del juego traducido instalado y lanzado con el agente (`build_vita/neoscavgame.vpk`, 5 MB) | VPK generado; tamaño y memoria estática anotados |
| B2 | ✅ Datos en un solo `data.pak` (60 MB, subido con el agente). Carga al 100 % en **41 s** en la consola (objetivo ≤ 2,5 s: pendiente, ver F1) | La carga de datos termina al 100 % en la Vita |
| B3 | ✅ Menú y partida nueva **en la Vita**: la cámara de Flixel se dibuja por GPU (textura BGRA) y el resto por software; táctil frontal = ratón (toque → mover, pulsar 2 fotogramas después) | Captura del menú principal en la Vita con el agente |
| B4 | 🔶 Medido en la Vita (333/222 MHz): **30,0 fps** en despertar y mapa (lógica 13,1 ms, subida 0,9 ms); carga 34 s; memoria: DMR +64 MB, heap 105/208 MB, zona de píxeles 72 MB. Menú del título también a 30,0 fps (lógica 3,2 ms). Falta inventario y combate | Cifras en el log con etiquetas |

## Fase C — Controles (hito 4.2, ver [docs/CONTROLES_VITA.md](docs/CONTROLES_VITA.md))
| # | Paso | Hecho cuando |
|---|---|---|
| C1 | ✅ Táctil frontal en la Vita: toque = clic, mover = arrastrar, pulsación larga = clic derecho (menú contextual del juego) | En la Vita: pulsar NEW GAME y arrastrar un objeto con el dedo |
| C2 | ✅ Stick izquierdo = cursor con zona muerta del 28 % (sin deriva al 25 %, probado con el agente); ✕ = clic | Menú navegado solo con mando, sin deriva en reposo |
| C3 | ✅ Stick derecho = flechas del juego (mover el mapa); un dedo atrás = arrastrar el mapa (`MoveCamera`). Dos dedos atrás (cursor fino) sin probar | Desplazar el mapa sin tocar la pantalla |
| C4 | 🔶 △ y pulsación larga = clic derecho → **menú contextual del propio juego**. Probado en PC (Take/Drop, Use, Destroy); en la Vita llega el clic derecho, falta verlo sobre un objeto | Consumir, equipar y soltar un objeto desde el menú |
| C5 | 🔶 L = inventario (I), R = fin de turno (ESPACIO), ○ = volver (TAB), □ = rebuscar (E), START = menú (F1): atajos del propio juego. L, R y ○ probados en la Vita | Tabla de controles final verificada en la consola |

## Fase D — Sonido y guardado (hitos 5–6)
| # | Paso | Hecho cuando |
|---|---|---|
| D1 | 🔶 Mezclador por software con minimp3 (los 154 MP3 incrustados del juego, en streaming) en un hilo del núcleo 2, puerto BGM 44,1 kHz. **En la Vita: 0,85 ms por bloque de 23 ms en otro núcleo, el juego sigue a 30 fps.** En PC el volcado a WAV confirma la mezcla. Falta que el usuario confirme que se oye bien | Se oyen los efectos del menú y de la partida en PC y en la Vita |
| D2 | 🔶 La música del juego son pistas incrustadas (`cueExploreTrack*`, `cueEnding01`…), en el mismo mezclador por streaming; la carpeta `OST` de Steam es la banda sonora aparte y el juego no la usa | Música del menú y del mapa sin cortes a 30 fps |
| D3 | ✅ `SharedObject` → `ux0:data/neoscavvita/save/<nombre>.sol` (serializador propio como AMF: campos públicos, vectores tipados, diccionarios, referencias; escritura segura). **En la Vita**: autoguardado al terminar turno, cerrar, reabrir y CONTINUE carga la partida. Falta importar partidas del original de PC | Guardar, cerrar, cargar y continuar la misma partida |
| D4 | ⬜ Suspender/reanudar, cierre seguro | La partida sigue tras suspender |
| D5 | ✅ (25-11) LiveArea con arte propio del autor (icono, `pic0`, fondo, inicio; sin símbolo de PlayStation). Icono verificado en el menú de la Vita | Icono y LiveArea propios en la consola |

## Fase E — Interfaz limpia en 960×544 (hito 9)
| # | Paso | Hecho cuando |
|---|---|---|
| E1 | 🔶 Elegido en PC: escenario 960×544 → el juego usa su diseño 800×450 y su opción «Stretch» (×1,2). Memoria en PC con la partida nueva: 691 → 370 MB. Falta verlo en la consola (letra ≥ 8 px reales, sin solapes) | Elegido con capturas de menú, mapa e inventario |
| E2 | ⬜ Ajustes sin tocar reglas: zona segura, objetivos ≥ 44 px, cursor visible, tooltips por pulsación larga | Revisión de todas las pantallas (menús, mapa, inventario, combate, fabricación, comercio, opciones, game over) sin problemas abiertos |

## Fase F — Rendimiento y memoria (hitos 3.3, 11)
| # | Paso | Hecho cuando |
|---|---|---|
| F1 | ✅ Paquete `data.pak` (NSDPAK02) con las 2.347 imágenes **ya decodificadas y con LZ4**: 391 MB → 16,6 MB (4 %), sin PNG en la consola y sin leer el archivo entero para saber su tamaño. Lectura por bloques. Carga total 41 → 34 s (el resto es el XML del juego); falta DXT5 para ahorrar RAM | Carga inicial ≤ 10 s |
| F2 | ✅ Flixel dibuja con la GPU: copias de bitmaps de TODAS las cámaras anotadas como órdenes (modo diferido) y ejecutadas por vitaGL con caché de texturas; relleno completo perezoso. En la Vita: 8,4 → **30,0 fps estables** en la partida (333/222 MHz). Capturas de PC idénticas píxel a píxel | 30 fps estables en menú, mapa e inventario |
| F3 | ⬜ Carga de texturas, audio y E/S fuera del hilo principal; informe por núcleo | Ningún tirón al cambiar de escena |
| F4 | 🔶 Recolector entre fotogramas funcionando en la Vita, espaciado por volumen de memoria y presión del heap. Pasada de ~0,76 s con 240.000 objetos vivos (un tirón de ~0,5 s en las transiciones; en reposo no recolecta). Pendiente: reducir los objetos vivos (el mapa de 12.757 hexágonos y el XML) o hacerlo incremental; 2 h sin crecer | 2 h de juego sin caer de 30 fps ni crecer en memoria |
| F5 | ⬜ Medir el margen a relojes por defecto y a 500/222 (los sube el usuario; aviso de calor y desgaste) | Cifras anotadas para ambos relojes |
| F6 | ✅ (25-11) **Memoria en la partida**: sin el fotograma de 13–14 s ni `bad_alloc` al mover un hexágono. Copia al escribir en `BitmapData::touched` (en vez de reproducir la cámara entera por software); fondos del título y barras *letterbox* de 1360×768 fuera; copia de fotograma de la cámara a 1×1; imágenes del paquete sin modificar se sueltan al pasar del 85 % de la zona de píxeles (20 MB en 1,4 ms). Heap 188 → 129,5 MB, zona 70 → 58 de 72 MB. Herramientas: desglose de bitmaps vivos en el log y rastreo de dueños del recolector (PC) | Mover y terminar turno sin fotogramas > 2 s ni cierres |
| F7 | 🔶 Entrada a la partida: 5,3 → **4,5 s** (código del juego 4,8 → 4,0 s + recolector 0,45 s). `framePixels` de `FlxSprite` comparte `_pixels` cuando el fotograma es la imagen entera y sin color (parche `patch_frame_pixels` del traductor; Flixel ya contempla ese caso) y los bitmaps nuevos se rellenan de forma perezosa: bitmaps creados 6081 → 2424, heap 135 → 121 MB. Quedan `FlxSound.play` al arrancar la música (~0,5 s), las heridas del inventario (~0,5 s) y el resto de `resetHelpers`. Carga 28,8 s (`ResourceLoader.load`; la E/S ya no es el cuello). Perfilador en la consola arreglado (el muestreador ya no reserva memoria) y cronómetros `[lento]` por fase | Entrada ≤ 2 s; carga ≤ 10 s |
| F8 | ⬜ **Camino a 60 fps** (el original declara `nFPS = 60`): dibujo por lotes (prueba NEOS00002: CPU 20,9 → 3,2 ms con 5000 cuadros), menos pausas del recolector. Repetir la prueba de dibujo con la GPU a 111 MHz (perfil de PSVshell para NEOS00002) | 60 fps en menú y mapa |

**VPK de pruebas `port/memtest` (NEOS00002)**, medido 24-11: `fread` 2,9 MB/s frente a `sceIoRead` 10,6–13,6 MB/s; LZ4 114–157 MB/s; las 1052 imágenes `x2_` son 277 de los 391 MB; texturas base en BGRA8 = 84 MB VRAM + 32 MB RAM, en RGBA4444 = 58 MB (caben en VRAM); comprimir DXT5 en la consola tarda 33 s (habría que hacerlo en `mkdatapak`).

## Fase G — Mods de NexusMods (hito 10)
| # | Paso | Hecho cuando |
|---|---|---|
| G1 | ⬜ Cargar `ux0:data/neoscavvita/mods/<Mod>/` con el sistema del juego (`getmods.php`, `ParseModList`, `LoadMod`, contenido nuevo frente a sustituir) | `SampleModFolder` añade su bolsa |
| G2 | ⬜ `.zip` en `mods_inbox/` (gestor ya probado) + menú para activar/ordenar; `.rar`/`.7z` con `tools/install_mod.py` en PC | Un mod real de NexusMods instalado desde la Vita |
| G3 | ⬜ Imágenes de mods convertidas a NSVPAK02 al instalar; mod roto → desactivado con motivo | 3 mods reales probados: contenido nuevo, sustitución y con imágenes |

## Fase H — Paridad 1:1 y entrega (hitos 7, 12)
| # | Paso | Hecho cuando |
|---|---|---|
| H1 | ⬜ Datos: hash del modelo cargado frente al original (pendiente: 743 tipos de objeto frente a 537 del log original, por los clones de `UpdateTemplateItems`) | Cifras y hashes iguales, o diferencia explicada |
| H2 | ⬜ Huella del estado por turno con semilla fija, comparada con el original en PC | 100 turnos guionizados sin diferencias en los 8 orígenes |
| H3 | ⬜ Cobertura: movimiento, rebusca, 105 recetas, combate, campamento, comercio, condiciones, clima, día/noche, muerte, DMC, finales | Lista al 100 % en el original y en el port; diferencias aceptadas documentadas |
| H4 | ⬜ Partida completa en la Vita; batería, apagado, tarjeta llena | Lista verificado / no verificado en hardware |
| H5 | ⬜ LiveArea propia sin arte del juego, README, guía de instalación; publicar solo el motor | VPK final probado; `_VitaComun` actualizado |

---
## Reglas
Las de [_VitaComun/README.md](../_VitaComun/README.md): solo la copia legal del usuario y nada del juego en repositorios; permiso para cada descarga; `config.txt` solo con permiso; relojes los sube el usuario; «funciona» solo si se vio en la Vita; audio < 1 ms/frame; logs con etiquetas; nunca `fs trash` sobre rutas reutilizadas.

## Riesgos y respuesta
| Riesgo | Respuesta |
|---|---|
| Dibujo por software lento en la Vita | F2: medir primero; GPU/NEON solo donde cueste |
| Código traducido lento (`Any`, variables `*`) | Inferencia de tipos (hecha) + F2 |
| Texturas (389 MB decodificadas) | F1: DXT5 + LZ4 por escena |
| Memoria sin recolector | F4 antes de sesiones largas |
| Fallos de semántica del traductor | Guiones A3 + rastreo por sentencia; se corrige en runtime/traductor, nunca en el juego |
| Texto ilegible en 5" | E1 con capturas en la consola |
| Paridad difícil de demostrar | H2: huellas deterministas frente al original |
| Mods mal formados | G3: validación y aislamiento |

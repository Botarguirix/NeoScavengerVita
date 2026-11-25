# NEO Scavenger PSVITA — Roadmap

Creado: 2026-11-20. Objetivo: port **1:1** de NEO Scavenger (Blue Bottle Games) a PS Vita, **30 fps fijos**, con extras de Vita (táctil, panel trasero, giroscopio, guardado/suspender, LiveArea).

Leyenda: ✅ verificado EN LA VITA · 🖥️ solo PC · 🔎 observado en los archivos, sin confirmar · ⬜ pendiente

## 0. Qué se sabe del juego (de los archivos, no de memoria)

| Hecho | Estado |
|---|---|
| Copia Steam en `C:\Program Files (x86)\Steam\steamapps\common\NEO Scavenger` (188 MB) | 🔎 |
| `NEOScavenger.exe` de 47 MB; contiene cadenas "Adobe Flash" y "SWF" → probable **proyector de Flash/AIR con el juego en ActionScript 3** | 🔎 falta extraer el SWF y confirmarlo |
| Datos del juego en **XML** (`data/*.xml`: creatures, itemtypes 1,1 MB, encounters, recipes, maps, conditions...) y **2326 PNG** en `img/` | 🔎 |
| Música: 16 MP3 en `OST/` | 🔎 |
| **Confirmado (20-11):** el `.exe` es un proyector de Flash con un SWF v15 de 39,8 MB (en el offset 0x9FC2B0) con **ActionScript 3** (flag AS3 activo), 2 bloques DoABC, 154 sonidos, 21 bitmaps y 3 DefineBinaryData. Extraído a la carpeta temporal de la sesión | ✅ análisis en PC (no es hardware) |
| Soporta mods (`SampleModFolder`, `_readme.txt`) | 🔎 |

**Consecuencia clave:** a diferencia de Valdis Story (bytecode GameMaker que el runner ejecutaba), aquí la lógica probablemente es AS3. Un port 1:1 exige o (A) un intérprete/AOT de AVM2 o (B) reimplementar la lógica en C++ a partir del SWF decompilado y de los XML. Se decide en la fase 1.

### 0b. Decompilación (20-11, PC; JPEXS 26.5.0 + Temurin JRE 17 portables en `tools/`, código en `work/as3/`, no se publica)
- **310 clases AS3, ~54 000 líneas (2,4 MB).** Las mayores: GUIInventory (250 KB), PlayState (156 KB), DataHandler (139 KB), Creature (99 KB), GUIValues, GUIEscMenu, AICreature, ItemInstance.
- **Motor: Flixel** (`org.flixel.*`, p. ej. `FlxTilemap`): un motor 2D de *blitting* por software sobre `BitmapData`, **sin Stage3D**. Es lo más favorable posible para una Vita.
- **API de Flash usada, muy acotada:** BitmapData, Bitmap, Sprite/Graphics, Rectangle/Point/Matrix/ColorTransform, TextField/TextFormat (texto y fuentes embebidas), Dictionary, ByteArray, Sound/SoundAsset (134 sonidos embebidos), eventos de ratón/teclado, `URLRequest`/`navigateToURL` y `Clipboard` (irrelevantes en Vita; hay que recortarlos), XML de datos.
- **Consecuencia:** ya no hace falta un AVM2 completo. Ruta recomendada = **C (híbrida)**: reimplementar en C++ la parte usada de Flixel + el subconjunto de `flash.*` anterior, y **traducir el AS3 del juego a C++ con un traductor propio** (como `vsaot` en Valdis), en lugar de reescribirlo a mano. Mantiene la lógica 1:1 y la validación por huellas contra el original.
- Riesgos concretos: texto/fuentes (TextField), `Dictionary`/`Vector`/`Object` dinámicos, `getDefinitionByName`, clases gigantes (GUIInventory 250 KB), rendimiento del blitting a 960×544 en la Vita (conviene pasar el blit a texturas de GPU).

### 0c. Prototipo del traductor AS3 → C++ (20-11, ruta híbrida elegida por el usuario)
- `tools/as3cpp/as3cpp.py` (léxico + parser + emisor C++17) y `runtime/as3rt.h` (Object, String nulable, Any, Number = NaN por defecto, stubs de Date/Point/Rectangle/BitmapData).
- 🖥️ **Prueba:** FlxPoint, GameStats, Weather y ChargeProfile se traducen, compilan con g++ (`-Wall`) y pasan `runtime/tests/test_proto.cpp` (valores por defecto, NaN, `return this` encadenado, constructores con Date, `destroy()` → null, cálculo de `GetQuantity`).
- **Cobertura sobre las 310 clases: 15 traducidas.** Primer bloqueo de cada una que falla: tipos aún sin runtime (`Array` el más frecuente, `Function`, `Class`, `TextField`, `Sound`, `FlxCamera`...), clases base aún no traducidas (FlxWindow, FlxButton, FlxGame, Input), metadatos `[Embed(...)]` de los ~134 sonidos y bitmaps, `is`, `get`/`set`, literales de array, y unos pocos archivos ofuscados (`§_a_...§`) y de `mx_internal`.
- Siguiente orden de trabajo: `[Embed]` → `Array`/`Vector`/`Dictionary` → `get`/`set`, `is`/`as`, `for each` → herencia de Flixel (FlxBasic → FlxObject → FlxSprite → FlxButton/FlxWindow) → `Function`/cierres → `TextField`. Medir la cobertura tras cada paso.
- **Actualización (20-11, noche) — traductor v2:** genera `.h` + `.cpp` por clase (las clases se referencian entre sí), modelo `__construct` + `as3::make<T>()`, tabla global de clases para resolver tipos por la jerarquía, `var` elevadas al inicio de la función (ámbito AS3), `[Embed]` (182 recursos → `embeds.txt`), regex, Array/Vector/Dictionary/objeto literal, `get`/`set`, `is`/`as`/`in`, `for each`/`for in`, `switch` (con `===` como AS3), `try`/`catch`/`finally`, `throw`, etiquetas con `break`/`continue`, cierres (lambdas), E4X mínimo (`.@atributo`, el juego solo lo usa 2 veces), inicializadores estáticos.
- **Fuente desofuscada:** el SWF tiene ofuscación de flujo en 6 clases (`§_a_...§`, `DataLoader`, `ResourceLoader`, `DM`), y JPEXS dejaba `§§pop()`/`§§goto`. Con `ffdec -deobfuscate max` la exportación sale limpia (310 scripts, 0 errores). Fuente de trabajo: `work/as3_deobf/`.
- **Qué es la ofuscación:** `§_a_-_---§.§_a_--_--§(n)` es un **cifrado de cadenas** (nombres de XML, URL) a partir de un blob `.bin` incrustado. Es ofuscación de textos, no una comprobación de licencia, y se traduce como código normal del juego. El *sitelock* de la versión web (`Preloader`, "juega en mi web") **ya viene desactivado por el propio juego** en la build de Steam (`_loc3_ = false`): no hay que saltarse nada.
- 🖥️ **Cobertura de traducción: 308 de 308 clases** → ~52 000 líneas de C++. **Aún no compila:** falta el runtime (`as3::make`, `bind`, `Array`, `Vector`, `Dictionary`, `Any` completo, `dyn`/`setDyn`/`callDyn`, conversiones, `xmlAttr`, regex...) y **109 clases de flash.*/mx.*** (lista en `work/gen/stubs.txt`). Siguiente medida: clases que compilan.
- `runtime/tests/test_proto.cpp` usa el modelo v1 (`new X()`); hay que pasarlo a `as3::make<X>()` cuando el runtime esté.
- Gestión de memoria todavía sin decidir (sin GC; `new` crudo en el prototipo). Se decide al traducir clases con ciclos de referencias.

### 0d. Port anterior (VPK 00.01) y datos en la Vita — análisis (20-11, noche)
Carpeta en la Vita: `ux0:data/neoscavvita/` (con doble "v") → `data/*.xml` (24 XML del juego sin tocar, 6,7 MB), `img.pak` (13,7 MB), `log.txt`, `save.dat`. Copias de trabajo en `work/vita_old/`.
- **Qué era el port anterior:** una **reescritura nativa en C++**, no la lógica original. Cargaba los 24 XML (6,7 MB en 2,45 s: 1,3 s leyendo + 0,8 s procesando), el mapa hex a partir de `MapMiniMichigan.png` (60×212, con semilla), condiciones, objetos, clima, encuentros, criaturas, combate, recetas, fuentes, guardado y audio a 44,1 kHz; en su log da **57–60 fps** con 116 texturas (24 MB), pero decodificando cada PNG en la Vita (**12,9 ms c/u**). Terminaba una partida de prueba con "Froze to death" a las 6 h de juego.
- **Formato `img.pak` (NSVPAK01) — reutilizable:** `'NSVPAK01'`, `u32` cantidad (2330), tabla `[u16 largo][nombre][u32 offset][u32 tamaño]`, datos contiguos. Son los **2326 PNG tal cual + 4 fuentes `.df3`** (`fonts/pixelmid|pixelsupertiny|pixeltiny|system`), sin comprimir (13,6 MB, igual que la carpeta `img/`). Sirve de contenedor con acceso aleatorio; lo malo es que obliga a decodificar PNG en la consola.
- **Decisión:** formato nuevo **NSVPAK02** (`tools/mkpak2.py`): mismo esquema, pero con ancho/alto/formato y píxeles ya decodificados (0 = RGBA8; reservados DXT5 y LZ4). Los 24 XML y las fuentes `.df3` se reutilizan tal cual.
- **Medido: todas las texturas decodificadas pesan 389 MB** (1794 RGBA + 532 RGB; 241 mayores de 512²; máx. 1360×768). No caben en la Vita: hace falta **DXT5 (~97 MB) + LZ4 + carga bajo demanda** por escena.
- ⚠️ El código fuente del port anterior no se conserva (solo el VPK con el `eboot.bin` cifrado), así que se reaprovechan los datos, el formato y las cifras, no el código.

### 0e. Esqueleto 00.02 en la Vita (`port/`) — VERIFICADO EN HARDWARE
Archivos: `port/src/main_vita.cpp`, `port/CMakeLists.txt`, `tools/vita/deploy_port.sh` (sube, instala, lanza, captura y recoge el log), VPK `port/build/neoscavvita.vpk` (483 KB, Title ID `NEOS00001`, LiveArea del VPK anterior). Capturas y logs en `work/shot_b.jpg`, `work/log_b.txt`.

| Qué | Resultado en la Vita |
|---|---|
| Arranque con vitaGL (pipeline fija, `libshacccg` presente) | ✅ |
| Lectura de NSVPAK02 (5 texturas, 12,3 MB) y dibujo (fondo parallax, hoja de hexágonos, cursor) | ✅ capturado |
| **30,0 fps bloqueados**, frames de 33,4 ms máx., 0 frames lentos (CPU del hilo principal 0,2 ms) | ✅ (cap por vcount: 2 vblanks; `vglSwapBuffers` no bloquea, el primer intento daba 59,9) |
| Subida de textura a la GPU | ✅ 7,5–17,8 ms por textura de hasta 4 MB (repartida entre frames, máx. 4 ms/frame) |
| Hilo cargador en el núcleo 2 (máscara `0x40000`) | ✅ arranca; ⚠️ la **carga por núcleo no está medida** |
| **Lectura de disco** | ❌ **~3 MB/s** (4 MB en 1,37 s): usa `fread` de newlib; hay que probar `sceIoRead` con bloques grandes y/o comprimir |
| Reloj durante la prueba | 333/222 MHz (sin overclock: el 500/222 lo pones tú en PSVshell) |
| Memoria | sonda: **mayor bloque USER_RW ~176 MB** (+64 MB de heap newlib ya tomados); `vglInit` reserva ~117 MB de RAM y ~81 MB de VRAM; con 5 texturas quedan **71,5 MB de VRAM libres** |
| Táctil frontal y stick | ⬜ código escrito, sin probar (hay que tocar la consola) |
| DMR → 381 MiB | ⬜ **sin demostrar**: en este arranque la suma libre ronda los 250 MB |

**Para la memoria:** el presupuesto real se confirma con la build siguiente; la sonda debe superar ~256 MB si DMR está activo para este proceso.

### 0f. Runtime AS3 y traductor v3 (21-11) — 🖥️ solo PC, nada de esto se ha probado en la Vita todavía
Archivos: `runtime/as3rt.h` (núcleo), `runtime/flash.h` (API de Flash que usa el juego), `tools/as3cpp/as3cpp.py` + `flashtypes.py` (traductor), `tools/as3cpp/fix_decomp.py` (correcciones al decompilador), `tools/as3cpp/check_all.sh` y `run_tests.sh`, `runtime/tests/test_v2.cpp`. Salida en `work/gen2/` (no se publica: es código derivado del juego).

| Qué | Resultado |
|---|---|
| Clases traducidas | **296 de 296** (se excluyen las 12 de Flex `mx.*`, sustituidas por stubs en `flash.h`) |
| **Clases que pasan el compilador** (`g++ -std=c++17 -fsyntax-only`) | **273 de 296 (92 %)** |
| Prueba ejecutada (código traducido real + runtime): FlxPoint, ChargeProfile, Weather, GameStats; ToInt32, `+` con String, `==`/`===`, NaN, Array/Vector/Dictionary, String, regex, Date | ✅ **42 de 42** (tres expectativas mías estaban mal y se corrigieron; ninguna era fallo del runtime) |
| Lo que NO pasa aún (23 clases) | casi todo capa de motor: `Mouse`, `DataHandler`, `FlxG`, `FlxGame`, `VCR`/`FlxReplay`, `FlxSound`, `FlxGroup`, `FlxTilemap`, `FlxText`, `FlxSprite`, `Preloader`; y de lógica: `PlayState` (5), `MenuState` (4), `PlayerCondition` (3, interfaz `IConditionOwner`), `GameOverState` (3), `EndState` (3), `GUIInventory` (2), `Creature` (2), `MapUtils`, `GUIInventorySlot`, `WatchEntry` (1) |
| Enlazado y ejecución del juego entero | ⬜ no hecho: solo se comprueba la sintaxis de las 273 y se enlazan 4 |

**Novedades del traductor:** tipos de `flash.h` leídos automáticamente (la API vive en un solo sitio), resolución de nombres por `import` (el juego tiene su propio `Mouse`, distinto de `flash.ui.Mouse`), interfaces reales con herencia múltiple, `super.propiedad`, `static` y de instancia con el mismo nombre (prefijo `s_`), `new` sobre variables que contienen una clase, `x is variable`, `a[i]++`, accesos dinámicos resueltos por C++ (`AS3GET/AS3SET/AS3CALL`, con TypeError #1009 si el objeto es null), locales del inicializador estático.

**Correcciones al decompilador (`fix_decomp.py`):** 6 casos donde JPEXS pierde `var length = members.length` (FlxObject, FlxHexTile, FlxTilemap). La ofuscación de flujo de 6 clases se resuelve con `ffdec -deobfuscate max`.

**Riesgos y deudas que ya se ven (ver también sección 7):**
1. **Rendimiento:** el decompilador deja muchas variables locales como `*` (p. ej. `var _loc3_:* = 0` como índice de bucle), y eso se traduce a `as3::Any` y comparaciones dinámicas (`cmp_lt`) en bucles calientes. Hace falta **inferencia de tipos para locales** antes de medir en la Vita.
2. **Memoria:** no hay recolector de basura; los objetos AS3 nunca se liberan (se decide estrategia: arena por escena/partida o conteo de referencias).
3. **Orden de inicialización de estáticos:** hoy son inicializadores de C++ por archivo (orden no definido entre clases); AS3 los ejecuta al cargar cada clase. Hay que sustituirlo por una función de arranque con orden explícito.
4. **Cierres:** capturan por copia (`[=]`), AS3 por referencia; un cierre que modifique una variable local capturada se comportará distinto.
5. **String** es UTF-8 con índices por byte (el texto del juego es ASCII, pero hay que revisar acentos).
6. **Cadenas cifradas:** `§_a_-_---§.§_a_--_--§(n)` descifra los nombres de los XML y URL; esas clases (`obf_*`) aún no compilan. Alternativa: ejecutar el descifrador una vez en PC y volcar la tabla.
7. `DataHandler` (carga de XML con `URLLoader`/`XML`) y la capa Flixel de dibujo/entrada/sonido necesitan **backend propio** (vitaGL + sceCtrl/táctil + mezclador), igual que en Valdis Story.

## 1. Fase 0 — Reconocimiento (antes de escribir el port)
- 1.1 ⬜ Resumen de documentación actual: VitaSDK, vitaGL, psp2-tooling, wiki, specs (CPU ARMv7 4 núcleos, GPU PowerVR TBDR, VMEM 112 MiB, NEON). Entregado en lenguaje llano.
- 1.2 ⬜ Verificar que el juego es legal y no está crackeado (copia Steam).
- 1.3 ⬜ Extraer el SWF del `.exe` y listar clases, `Stage3D`/display list vs. `BitmapData`, uso de fuentes, audio, guardado y red (`getimages.php`/`getmods.php` son restos web).
- 1.4 ⬜ **Decisión A/B** (AVM2 vs. reimplementación) con tabla de coste/riesgo y elección del usuario.
- 1.5 ⬜ Elegir ruta de render con justificación: **vitaGL** (esperado; juego 2D con sprites y hexágonos) frente a sceGxm nativo. vkvita solo si el proyecto ya fuese Vulkan (no lo es).

## 2. Qué se reaprovecha de Valdis Story (traspaso del 19-11-2026)
Fuente: `%USERPROFILE%\Documents\TRASPASO.md`. Lo transferible, sea cual sea la opción A/B:

| Pieza | Origen en Valdis | Uso aquí |
|---|---|---|
| Proyecto CMake para VitaSDK + empaquetado VPK, LiveArea por script (PNG 8 bits con paleta, si no error 0x8010113D) | `tools/make_livearea*.py`, `docs/LIVEAREA.md` | directo |
| Capa gráfica vitaGL: texturas DXT + LZ4, carga en hilo del núcleo 2, re-subida entre frames de páginas subidas a mitad de frame, tiras >4096 px partidas | `runner/src/gfx.*` | adaptar; el atlas de 2326 PNG se empaqueta en VDX4 |
| Audio: mezclador por software sin lock durante la mezcla, WAV en memoria, OGG en streaming, máx. 64 voces, <1 ms | `runner/src/audio.*` | MP3 → OGG/AAC (decidir; ver fase 3) |
| Conversión de datos en la propia Vita en el primer arranque (el juego no se distribuye) | `convert*.cpp`, `build_vsdata.py` | XML/PNG → formato propio |
| Determinismo: huellas (`--det --hash`) para validar cada optimización contra el PC | runner PC | mismo método |
| Herramientas de prueba que acabarán como TRUCOS (warp, tour, stress, godmode, monkey, perfiles) | `debugtour.*` | readaptar a los mapas hex de NEO Scavenger |
| Control de la Vita por vita-agent (`va.sh`, `deploy.sh`) y reglas de seguridad de partidas (nunca `fs trash` sobre rutas reutilizadas; no leer el log con el juego abierto) | `tools/` | directo |
| Log con etiquetas `[CPU-bound]` etc., `[memoria]` cada minuto | `main_vita.cpp` | directo |
| Reglas: solo copias legales, nada del juego en el repo, correo noreply, VPK con datos derivados solo personal | TRASPASO §3, §7 | directo |

**Lección de Valdis:** "no digas funciona si no lo viste en la Vita"; medir en consola (la Vita es ~20–30× más lenta que el PC por objeto activo).

## 3. Fase 1 — Núcleo jugable
- 3.1 ⬜ Cargador de datos: parsear los XML a estructuras binarias (creatures, items, encounters, recipes, maps, conditions...). Verificación 🖥️ contra el original.
- 3.2 ⬜ Render del mapa hexagonal, sprites, UI (inventario con paperdoll, combate por movimientos, crafting).
- 3.3 ⬜ Lógica de supervivencia (temperatura, hambre, heridas, encuentros) 1:1 con el original; test de huellas.
- 3.4 ⬜ Audio: MP3 → OGG/AAC en el primer arranque; música en streaming.
- 3.5 ⬜ Guardado/carga compatible con la lógica original, en `ux0:data/neoscavenger/save`.

## 4. Fase 2 — Extras de Vita
- 4.1 ⬜ **Táctil frontal**: tocar hexágono para mover/seleccionar, arrastrar objetos en el inventario, pellizco para zoom del mapa. (El juego es de ratón: este es el mayor valor de la Vita.)
- 4.2 ⬜ **Panel trasero**: atajos (p. ej. inventario/mapa), como en Valdis.
- 4.3 ⬜ Botones: ✕ aceptar, ○ cancelar, □/△ acciones, L/R cambiar pestañas, sticks para cursor.
- 4.4 ⬜ Tamaño de texto/UI legible en 960×544 (el original es de ratón en monitor grande).
- 4.5 ⬜ Suspender/reanudar, autoguardado, LiveArea propia sin arte del juego, trofeos opcionales, ahorro de batería.

## 5. Fase 3 — Rendimiento y estabilidad (objetivo 30 fps fijos)
- 5.1 ⬜ Presupuesto: 33,3 ms/frame. RAM < 381 MiB (DMR), VMEM < 112 MiB; informar en cada build.
- 5.2 ⬜ Relojes por defecto 500/222 MHz (los sube el usuario con PSVshell; avisar de calor y desgaste). Opciones 444/166 y 333/166 a petición, con el coste.
- 5.3 ⬜ Usar los 4 núcleos (CapUnlocker): carga de texturas/audio/IO fuera del hilo principal; informar carga por núcleo.
- 5.4 ⬜ Shaders con lowp/mediump/highp justificados; NEON donde mida (conversión de texturas, mezcla). "Togsvik": solo tras confirmar qué es.
- 5.5 ⬜ Delta time: el juego original es por turnos/tiempo real mixto; usar delta en animaciones y mantener la lógica del juego idéntica (no escalar lo que cambiaría resultados).
- 5.6 ⬜ Sesión larga de 1–2 h para fugas; recorrido de todos los mapas; suspender/reanudar.
- 5.7 ⬜ Logs `[CPU-bound]`, `[GPU-bound]`, `[VMEM-bound]`, `[RAM-bound]`, `[bandwidth-bound]` con causa concreta.

## 6. Fase 4 — Juego completo y después
- 6.1 ⬜ Probar el juego entero (inicio, supervivencia, combate, crafting, todos los orígenes, finales) y comparar con el original.
- 6.2 ⬜ Soporte de mods (el original los admite): carpeta `ux0:data/neoscavenger/mods`.
- 6.3 ⬜ Menú de TRUCOS (warp, godmode, perfiles, editor de partida/inventario).

## 7. Riesgos
| Riesgo | Detalle |
|---|---|
| Motor AS3 | Sin intérprete de AVM2 en Vita; el coste de la ruta A o B es el mayor del proyecto |
| Texturas | 2326 PNG; el límite de textura de la Vita es 4096 px y el VMEM 112 MiB → atlas comprimidos (DXT/VDX4) |
| Entrada | Pensado para ratón: sin buen diseño táctil el "1:1" se vuelve incómodo |
| Datos | XML grandes (itemtypes 1,1 MB): conviene binarizar |
| Distribución | El VPK con datos derivados del juego es solo uso personal; GPL de vkvita no aplica si no se usa |

## 8. Pendiente del usuario
1. Confirmado: **30 fps**, 1:1.
2. ✅ VPK anterior recibido: `G:\downloads\NeoScavengerVita-00.01.vpk` (820 KB). Contiene solo `eboot.bin` (836 KB, SELF comprimido/cifrado: sin cadenas legibles), `param.sfo` (TITLE_ID `NEOS00001`, versión 00.01, "NEO Scavenger Vita") y una LiveArea mínima (icon0, pic0, bg0, startup). **Sin código fuente ni datos del juego**: es un build temprano, demasiado pequeño para llevar un intérprete AS3 completo. Aprovechable: Title ID, LiveArea base y la prueba de que el empaquetado funciona; el resto se rehace. Si tienes el código fuente de esa versión, es lo realmente valioso.
3. Elegir relojes: 500/222 (por defecto, overclock tuyo), 444/166 o 333/166.

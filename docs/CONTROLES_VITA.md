# NEO Scavenger PS Vita — Controles (táctil, panel trasero y mando)

Estado (24-11-2026): **implementado y probado en la Vita con el agente**, salvo lo marcado como pendiente al final. Cada control produce lo mismo que el original: un clic, un clic derecho o uno de los atajos de teclado del propio juego (`PlayState.KeyHandler`).

## Principios
1. **Lo más cercano al original.** NEO Scavenger se juega con ratón (clic, arrastrar y soltar, clic derecho, rueda). El port no cambia reglas ni pantallas: cada control de la Vita produce las mismas acciones de ratón/teclado que el original, o llama a la misma función del juego que el gesto equivalente.
2. **Pantalla limpia.** Sin botones fijos superpuestos ni barras. Lo añadido (cursor, anillo de pulsación larga, menú contextual) es pequeño, aparece solo al usarse y desaparece.
3. **Toque directo.** Lo que tocas es lo que recibe el clic.

## Pantalla táctil frontal
| Gesto | Equivale a | Uso |
|---|---|---|
| **Toque** | Clic izquierdo en ese punto (el cursor se mueve al tocar y el clic llega al levantar el dedo) | Botones, elegir hexágono de destino, seleccionar objeto, diálogos |
| **Tocar y mover** (más de 12 px) | Arrastrar con el botón pulsado | Mover objetos entre inventario, manos, cuerpo, suelo y contenedores |
| **Pulsación larga (0,5 s) quieto** | Clic derecho: el **menú contextual del propio juego** | Acciones del objeto tocado (✅ llega en la Vita) |
| **Toque con dos dedos** | Clic derecho en el punto medio | Igual que en el original |
| **Deslizar con dos dedos** | Rueda del ratón | Listas largas: registro, recetas, comercio |

Tooltips: el «ratón» se queda donde tocaste por última vez, así que la información emergente aparece tras un toque y desaparece al tocar en otro sitio.

## Panel táctil trasero
La Vita tiene un panel trasero del mismo tamaño que la pantalla (con menos resolución). Se usa para lo que la mano no puede hacer en la pantalla sin taparla. **Activado por defecto con zona muerta en los bordes** (donde se apoyan los dedos al sujetar la consola); se puede desactivar en Opciones.

| Gesto trasero | Acción | Por qué |
|---|---|---|
| **Arrastrar un dedo** | **Mover el mapa** (`PlayState.MoveCamera`, como el teclado; ✅ en la Vita) | Explorar el mapa sin tapar los hexágonos con el dedo |
| **Arrastrar dos dedos** | **Mover el cursor** con precisión (como un panel de portátil) | Apuntar a hexágonos u objetos pequeños |
| **Toque con dos dedos** | Clic izquierdo donde está el cursor | Confirmar sin mover la mano a la pantalla |
| **Mantener un dedo (0,5 s)** | Mostrar el tooltip bajo el cursor | Consultar sin activar nada |
| **Doble toque** | Centrar el mapa en el personaje | Volver rápido tras explorar |

Zonas: el 15 % exterior del área activa del panel (x 0–1919, y 108–889, medida con el agente) se ignora para evitar pulsaciones al sujetar la consola.

## Mando
| Control | Acción | Equivale en el original a | Probado en la Vita |
|---|---|---|---|
| **Stick izquierdo** | Mover el cursor (zona muerta 28 %, curva cuadrática) | Mover el ratón | ✅ sin deriva al 25 % |
| **Stick derecho** | Mover el mapa | Flechas / WASD | ✅ |
| **Cruceta** | Cursor fino (1 px; mantenida acelera a 4 px) | — | ⬜ |
| **✕** | Clic izquierdo | Clic | ✅ llega |
| **△** | Clic derecho: **menú contextual del propio juego** (Coger/Soltar, Usar, Destruir, Fabricar…) | Clic derecho | ✅ en PC; llega en la Vita |
| **○** | Volver a la vista principal (cerrar inventario, fabricación, etc.) | TAB | ✅ |
| **□** | Rebuscar / encuentro | E | ⬜ |
| **L** | Inventario | I | ✅ |
| **R** | Fin de turno (o lo que ESPACIO haga en cada pantalla) | ESPACIO | ✅ llega al combate |
| **START** | Menú del juego | F1 | ⬜ |
| **SELECT** | (reservado: ayuda de controles) | — | ⬜ |

### Zonas muertas de los sticks
Los sticks de la Vita no vuelven exactamente al centro y tienen algo de ruido, así que sin zona muerta el cursor o el mapa se desplazan solos.
- **Zona muerta radial del 28 %** en cada stick (valores 0–255 con centro 128: se ignora todo lo que esté a menos de ~36 unidades del centro). Es agresiva a propósito: los sticks de la Vita se desgastan y con menos el cursor se va solo. Radial, no por eje, para que las diagonales no se "enganchen" a los ejes.
- **Reescalado tras la zona muerta:** el recorrido útil (28 %–93 %) se lleva a 0–1, de modo que el movimiento arranca suave justo al salir de la zona muerta, sin salto.
- **Curva de respuesta cuadrática** para el cursor del stick izquierdo: precisión al inclinar poco y velocidad al inclinar del todo. El stick derecho (mapa/desplazamiento) usa respuesta lineal.
- **Zona exterior del 93 %:** por encima se considera inclinación máxima, para que todos los sticks lleguen a la velocidad tope aunque estén algo gastados.
- Ajustable en Opciones del port (15 %–40 %), por si una consola tiene los sticks más desgastados.

Las asignaciones de **L, R y □** se confirmarán contra los botones reales del juego al revisarlo en marcha; si el original no tiene una acción equivalente, el botón no hace nada (no se inventan reglas).

## Menú contextual (△, pulsación larga o clic derecho)
No se inventa ningún menú: NEO Scavenger ya tiene uno al hacer clic derecho sobre un objeto (`GUIInventory`, `objContext`), con las acciones que el juego permite en ese momento (Coger/Soltar, Usar, Destruir, Fabricar, modos de ataque…). △ y la pulsación larga envían ese clic derecho donde está el cursor, así que las opciones y su resultado son exactamente los del original.

## Coordenadas y escala (técnico)
- El juego dibuja a **1360×768** (su `FlxGame`). En la Vita (960×544) la imagen ocupa 960×540 con 2 px de margen arriba y abajo; el diseño de interfaz definitivo se decide en el hito 9.
- Toque frontal (tx, ty) en 0–1919 × 0–1087 → juego: `gx = (tx/2) × 1360/960`, `gy = (ty/2 − 2) × 768/540`.
- El runtime entrega la entrada como Flash: `injectMouseMove(stage, gx, gy)` (actualiza `mouseX/mouseY` en cada objeto), `injectMouseButton(stage, abajo/arriba, derecho)` (`MOUSE_DOWN/UP`, `CLICK` y sus versiones de clic derecho), `injectWheel` y `injectKey`.

## Pendiente de verificar en la Vita
- [ ] Menú contextual sobre un objeto real en la consola (en PC ✅; en la Vita el clic derecho llega pero la prueba no tenía objetos en el suelo).
- [ ] Arrastrar objetos con el dedo entre inventario, suelo y cuerpo.
- [ ] Cruceta, □, START; dos dedos atrás (cursor fino y clic).
- [ ] Indicador visual de la pulsación larga y ayuda de controles con SELECT.
- [ ] Tamaño mínimo cómodo de los objetivos (≥ 44 px reales).

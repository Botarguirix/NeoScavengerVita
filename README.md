# NEO Scavenger Vita (port no oficial)

Herramientas y runtime para ejecutar en PS Vita la copia **legal** de NEO Scavenger (Blue Bottle Games) que tenga cada
usuario. Este repositorio **no contiene ningún dato del juego ni código derivado de él**: ni el SWF, ni el ActionScript
decompilado, ni el C++ traducido, ni imágenes, sonidos o textos. Todo eso se genera en local en `work/` (ignorado por git)
a partir de la copia del propio usuario.

- `tools/as3cpp/` — traductor de ActionScript 3 a C++.
- `runtime/` — runtime propio de AS3/Flash (objetos, recolector, BitmapData, sonido, guardado, plataforma).
- `port/game/` — backend de PS Vita (vitaGL, controles, audio). `port/pc/` — arnés de pruebas en PC.
- `port/memtest/` — VPK pequeño de pruebas de memoria, carga y dibujo.
- `tools/vita/` — empaquetado de datos (`mkdatapak`), despliegue y pruebas con vita-agent.
- `ROADMAP.md`, `docs/` — plan, controles e instalación.

Sin afiliación con Blue Bottle Games. NEO Scavenger es propiedad de sus autores.

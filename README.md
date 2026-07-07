# sdl_upc_engine — Motor 2D educativo sobre SDL3

Motor de videojuegos **2D ligero, estilo componentes** (similar a Unity), construido sobre
**SDL3**. Su propósito es educativo: que estudiantes que ya conocen SDL3 básico (ventana,
renderer, eventos, delta time) creen juegos **agregando componentes a objetos**, sin pelear
con la API cruda de SDL.

El motor no toma el control: tú mantienes tu propio bucle `main` de SDL y en cada frame
"bombeas" la escena (`scene->update(dt)` y `scene->render()`). El motor te entrega el sistema
de objetos/componentes que se actualiza y dibuja.

Este repositorio es la **base de un curso** de desarrollo de videojuegos. Cada sesión suma una
capacidad nueva al motor.

---

## Características

Lo que el motor ya hace hoy:

- **Sistema de componentes** estilo Unity: `GameObject` + `Transform` + componentes con ciclo
  de vida `awake` / `start` / `update` / `render` / `onCollision`. El `Transform` marca el
  **centro** del objeto.
- **Sprites**: `SpriteRenderer` con recortes, flip horizontal y anclaje al centro.
- **Animación por spritesheet**: `SpriteAnimator` con varios formatos: celdas numeradas de
  un solo sheet, una tira (un archivo) por animación, o una fila/columna de un sheet en
  grilla (para personajes direccionales).
- **Cámara**: `Camera` + `FollowCamera` con zona muerta y suavizado.
- **Física AABB**: `RigidBody2D` + `BoxCollider` con gravedad, colisiones, triggers y
  detección de "grounded".
- **Ciclo de vida**: `destroy` diferido, `Lifetime` (autodestrucción por tiempo) y `Spawner`.
- **Tilemap**: `TilemapRenderer` (grilla + tileset) con colisión de tiles, cargable desde
  **código**, desde un **archivo de texto propio** (`.map`) o desde **Tiled JSON** con tileset
  embebido (vía **nlohmann/json**, que viene incluida en el repo en `engine/third_party/`).
- **Debugger conmutable**: dibujo de colliders, zona muerta y primitivas (se prende/apaga en
  caliente).
- **`AssetManager`**: dueño de las texturas; los renderers solo las piden prestadas.
- **Audio de fondo**: música en loop vía **SDL3_mixer**, inicializada en `main.cpp`.

---

## Arquitectura

Idea general (en pocas líneas):

- **Motor por capas**, de lo más cercano al alumno a lo más cercano a SDL:
  juego/ejemplos (`game/`, `main.cpp`) → gameplay (`GameObject` + componentes) →
  subsistemas (sprites, cámara, física, assets) → núcleo (`Scene`) → SDL3 (aislado).
- **Modelo de componentes estilo Unity**: un `GameObject` no hereda comportamiento, lo
  **compone** con `addComponent<T>()` / `getComponent<T>()`. Todo objeto nace con un
  `Transform` (que marca su **centro**). Los componentes siguen el ciclo de vida
  `awake` / `start` / `update` / `render` / `onCollision`.
- **El alumno mantiene el control**: el motor no invierte el bucle. Tú escribes tu `main`
  de SDL y en cada frame "bombeas" la escena (`scene->update(dt)` y `scene->render()`).
  `Scene::update` actualiza objetos, resuelve la física AABB y barre los marcados con
  `destroy()`.
- **SDL queda escondido**: `<SDL3/SDL.h>` vive en los `.cpp` (y muy pocos headers), nunca
  en los headers de composición; donde hace falta un tipo de SDL se usa forward declaration.

---

## Estructura del proyecto

```
sdl_upc_engine/
├── engine/                 # El MOTOR (código genérico, no conoce ningún juego)
│   ├── Component.h         #   base de todos los componentes
│   ├── GameObject.h        #   objeto contenedor de componentes
│   ├── Transform.h         #   posición/escala/rotación (centro del objeto)
│   ├── Scene.{h,cpp}       #   contenedor de objetos + fase de física + render
│   ├── AssetManager.{h,cpp}#   carga y posee texturas
│   ├── SpriteRenderer.*    #   dibujo de sprites
│   ├── SpriteAnimator.*    #   animación por spritesheet
│   ├── Camera.*  FollowCamera.*
│   ├── RigidBody2D.h  BoxCollider.*   # física AABB
│   ├── TilemapRenderer.*   #   grilla de tiles (código / archivo / Tiled JSON)
│   ├── Lifetime.h  Spawner.h
│   ├── Debugger.*          #   ayudas visuales de depuración
│   └── third_party/        #   librerías de terceros incluidas (vendored)
│       └── nlohmann/json.hpp  # nlohmann/json single-include (MIT), para Tiled JSON
├── game/                   # Lógica del JUEGO (lado del juego, no del motor)
│   ├── FragmentoMemoria.{h,cpp} #   el juego entregado: "Fragmento de Memoria"
│   └── Platformer/TopDown/Shooter.{h,cpp} # ejemplos del curso (no se usan en main.cpp)
├── main.cpp                # Bucle de SDL, inicializa audio y arranca Fragmento de Memoria
├── assets/                 # Recursos junto al ejecutable (imágenes, mapas, audio)
│   ├── pixel_adventure/    #   sprites del pack Pixel Adventure
│   ├── Nivel1.tmj          #   nivel real (Tiled), cargado por FragmentoMemoria
│   └── backgroud_sound.mp3 #   música de fondo (loop)
└── sdl_upc_engine.vcxproj  # Proyecto de Visual Studio (un solo ejecutable)
```

`engine/` es **genérico**: nunca contiene nombres de un juego concreto (nada de "Player" o
"Bala"). Toda la lógica de gameplay vive en `game/` y `main.cpp`.

---

## Cómo compilar y ejecutar

El proyecto se compila con **Visual Studio 2026** (un único proyecto que produce un
ejecutable de consola). No hay solución `.sln` ni `CMakeLists.txt` en el repo: se abre el
`.vcxproj` directamente.

### Requisitos

- **Visual Studio 2026** con el toolset de C++ (PlatformToolset `v145`), C++17. (Si tu VS es
  2022, en el primer build te va a ofrecer "Retarget projects": aceptalo para pasar a `v143`.)
- **SDL3**, **SDL3_image**, **SDL3_mixer** y **nlohmann/json** **ya vienen vendorizadas en el
  repo**, en `engine/third_party/` (`SDL3/`, `SDL3_image/`, `SDL3_mixer/` y `nlohmann/json.hpp`).
  **No hace falta instalar nada ni configurar vcpkg**: al clonar el repo están listas y el
  `.vcxproj` ya apunta ahí con rutas relativas (`$(ProjectDir)engine\third_party\...`), tanto
  para Debug como para Release. Son los binarios oficiales de
  **[libsdl.org](https://www.libsdl.org/)** (paquete "VC", x64); sus licencias están en
  `engine/third_party/SDL3/LICENSE.txt`, `engine/third_party/SDL3_image/LICENSE.txt` y
  `engine/third_party/SDL3_mixer/LICENSE.txt`.

### Pasos

1. Clona el repo (con `git clone`, sin descargar nada aparte).
2. Abre `sdl_upc_engine.vcxproj` en Visual Studio.
3. Selecciona la configuración **x64** (Debug o Release; ambas ya tienen las rutas de SDL
   cableadas).
4. Compila y ejecuta (F5). El evento post-build copia automáticamente `SDL3.dll`,
   `SDL3_image.dll`, `SDL3_mixer.dll` y la carpeta `assets/` junto al ejecutable.

---

## El juego: Fragmento de Memoria

`main.cpp` arranca directamente **Fragmento de Memoria** (`game/FragmentoMemoria.cpp`): un
plataformas de precisión estilo Celeste. Un gato —el alma de un anciano con pérdida de
memoria— recorre las etapas de su vida recolectando fragmentos de recuerdos sobre el nivel
real armado en Tiled (`assets/Nivel1.tmj`). Apenas arranca la ventana suena en loop la música
de fondo (`assets/backgroud_sound.mp3`, vía SDL3_mixer).

Los ejemplos del curso (`game/Platformer.*`, `TopDown.*`, `Shooter.*`) siguen en el repo como
referencia, pero `main.cpp` ya no los invoca.

| Tecla | Acción |
|-------|--------|
| `←` / `→` | Mover |
| `↑` / `↓` | Apuntar (pistola de agua) |
| `Espacio` | Saltar (soltar corta el salto; tiene coyote time + jump buffer) |
| `X` / `Shift izq.` | Dash de Lucidez: impulso en 8 direcciones que revela temporalmente las plataformas ocultas |
| `C` | Salto Propulsado (solo nivel 3): disparo de agua hacia abajo que actúa como doble salto |
| `R` | Reiniciar el nivel |
| `Enter` | Confirmar en pantallas de menú |
| `F1` | Prende/apaga el dibujo de debug (colliders, etc.) |

---

## Cómo editar un mapa con Tiled (guía para alumnos)

El motor lee mapas exportados desde **[Tiled](https://www.mapeditor.org/)** en formato
**JSON**. El ejemplo de plataformas carga `assets/maps/platformer_level1.json` (ver
`game/Platformer.cpp`).

### 1. Instala Tiled

Descárgalo gratis desde [mapeditor.org](https://www.mapeditor.org/).

### 2. Abre el mapa incluido o crea uno nuevo

- **Abrir el incluido**: `assets/maps/platformer_level1.tmx` (también está el proyecto de
  Tiled `assets/maps/platformer_level1.tiled-project`).
- **Crear uno nuevo**, con estas opciones (las que el motor espera):
  - Orientación: **Orthogonal**
  - Formato de capa de tiles: **CSV** (¡no Base64!)
  - Tamaño de tile: **16 × 16**
  - Tileset: **embebido en el mapa** (no como archivo externo)

### 3. Marca los tiles sólidos

La física "viaja" dentro del mapa: el motor crea un collider por cada tile marcado como
sólido. Para marcar un tile:

1. Selecciona el tileset y luego el tile en el panel de tilesets.
2. En **Propiedades personalizadas** (Custom Properties), agrega una propiedad **booleana**
   llamada exactamente **`solid`** y ponla en **`true`**.
3. Repite con todos los tiles que deban colisionar (suelo, paredes, plataformas).

El parser busca en el tileset embebido cada tile con la propiedad `solid == true` y lo
registra como sólido.

### 4. Exporta a JSON en la ruta que el juego espera

Exporta el mapa como **JSON** sobre la ruta que carga el código:

```
assets/maps/platformer_level1.json
```

(Es la ruta de `buildPlatformer` en `game/Platformer.cpp`. Si usas otro nombre, cambia esa
ruta en el código.)

### ⚠️ Advertencias importantes

- **Mantén la estructura de carpetas** al clonar el repo. La imagen del tileset se referencia
  con ruta **relativa** al `.json` (`../pixel_adventure/Terrain/Terrain (16x16).png`); si
  mueves carpetas, el tileset no cargará.
- Usa **CSV**, no **Base64 ni compresión**: el parser lee la capa de tiles como lista de
  números.
- **No uses el volteo/rotación de tiles** de Tiled: el parser aún **ignora** esos bits de
  flip (enmascara los 3 bits altos del GID).
- Hoy se soporta la **capa de tiles**; la capa de objetos de Tiled todavía no se interpreta.

---

## Roadmap

Proyecto en **desarrollo activo**: el motor crece sesión a sesión a lo largo del curso.

**Hecho:**

- Núcleo: `Component`, `GameObject`, `Transform`, `Scene`, `AssetManager`.
- Render: `SpriteRenderer` (recortes, flip, anclaje al centro), `SpriteAnimator`.
- Cámara: `Camera` (zoom, mundo→pantalla) y `FollowCamera` (zona muerta + suavizado).
- Física: `RigidBody2D` (gravedad, `grounded`), `BoxCollider` (AABB, triggers) y fase de
  colisiones en `Scene`.
- Ciclo de vida: `destroy` diferido, `Lifetime`, `Spawner`.
- `TilemapRenderer` (código / archivo propio / Tiled JSON) y `Debugger` conmutable.
- Tres ejemplos del curso: platformer, top-down y shooter.
- Audio de fondo en loop (SDL3_mixer).
- Juego final: **Fragmento de Memoria**, con Dash de Lucidez, plataformas efímeras y
  Salto Propulsado.

**Pendiente (sin orden fijo):**

- `Health` / `Damageable` (vida y condición de derrota).
- Clase `Input` consultable (`isKeyDown` / `wasPressed`).
- `TextRenderer` + UI básica (SDL3_ttf): HUD, puntaje, diálogos.
- Sistema de tags o capas (reemplazar el filtro por `name`).
- Efectos de sonido puntuales (salto, dash, recolectar fragmento); mejoras de física
  (one-way platforms, broad-phase, fricción/rebote); handles seguros; parenting de
  `Transform`; partículas.

**Limitaciones conocidas:** colisiones O(n²) (bien para decenas de objetos, no miles); la
resolución por pares puede temblar con colliders apilados; no hay desregistro automático de
punteros a objetos destruidos.

---

## Créditos y licencias de terceros

### Librerías

- **[nlohmann/json](https://github.com/nlohmann/json)** de **Niels Lohmann** — librería
  *JSON for Modern C++* usada para leer los mapas de Tiled. Licencia **MIT**. Se incluye
  **vendorizada** en el repo (`engine/third_party/nlohmann/json.hpp`, single-include), con su
  cabecera de licencia MIT intacta; no requiere instalación.
- **[SDL3](https://www.libsdl.org/)**, **[SDL3_image](https://github.com/libsdl-org/SDL_image)**
  y **[SDL3_mixer](https://github.com/libsdl-org/SDL_mixer)** de **libsdl.org** — binarios
  oficiales (paquete "VC", x64) **vendorizados** en el repo (`engine/third_party/SDL3/`,
  `engine/third_party/SDL3_image/` y `engine/third_party/SDL3_mixer/`), con sus licencias
  **zlib** intactas; no requiere instalación manual.

### Assets

- **[Pixel Adventure](https://pixelfrog-assets.itch.io/pixel-adventure-1)** de **Pixel Frog**
  (itch.io) — sprites del ejemplo *platformer* y del juego final. Respeta su licencia si
  reutilizas o redistribuyes los assets.
- **[Ninja Adventure Asset Pack](https://pixel-boy.itch.io/ninja-adventure-asset-pack)** de
  **Pixel-boy y AAA** — sprites del ninja y tileset del ejemplo *top-down*. Publicado bajo
  **CC0** (dominio público; atribución no obligatoria pero apreciada).
- **Música de fondo** (`assets/backgroud_sound.mp3`): tomada de
  [este video de YouTube](https://www.youtube.com/watch?v=EaCUyNQWY2M) <!-- ponytail: verificar
  licencia/derechos de uso antes de distribuir el juego fuera del curso -->.

Se usan con fines educativos.

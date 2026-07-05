#include "FragmentoMemoria.h"

#include <SDL3/SDL.h>
#include <cmath>
#include <vector>
#include <algorithm>

#include "../engine/Scene.h"
#include "../engine/GameObject.h"
#include "../engine/Component.h"
#include "../engine/Transform.h"
#include "../engine/SpriteRenderer.h"
#include "../engine/SpriteAnimator.h"
#include "../engine/RigidBody2D.h"
#include "../engine/BoxCollider.h"
#include "../engine/TilemapRenderer.h"
#include "../engine/Camera.h"
#include "../engine/FollowCamera.h"

// ---------------------------------------------------------------------------
// LucidPlatform: plataforma OCULTA que solo existe mientras dura la "lucidez".
// Nace invisible y sin colision (collider en modo trigger). Cuando el jugador
// hace el Dash de Lucidez se llama revealAll() y todas se vuelven visibles y
// solidas durante unos segundos; al agotarse el tiempo se ocultan de nuevo.
//
// IMPORTANTE: agregar este componente AL FINAL del GameObject (despues del
// SpriteRenderer y el BoxCollider), porque en awake() captura la escala del
// Transform y el collider ya configurados.
// ---------------------------------------------------------------------------
class LucidPlatform : public Component {
public:
    static void revealAll(float seconds) {
        for (LucidPlatform* p : all) p->reveal(seconds);
    }

    void awake() override {
        collider = gameObject->getComponent<BoxCollider>();
        baseScaleX = gameObject->transform->scaleX;
        baseScaleY = gameObject->transform->scaleY;
        hide();
        all.push_back(this);
    }

    ~LucidPlatform() override {
        all.erase(std::remove(all.begin(), all.end(), this), all.end());
    }

    void update(float dt) override {
        if (timer <= 0.0f) return;
        timer -= dt;
        if (timer <= 0.0f) hide();
    }

private:
    void reveal(float seconds) {
        timer = seconds;
        gameObject->transform->scaleX = baseScaleX;
        gameObject->transform->scaleY = baseScaleY;
        if (collider) collider->isTrigger = false;
    }
    // Ocultar = escala 0 (no se dibuja) + trigger (no bloquea). No destruimos
    // el objeto: solo lo "apagamos" para poder revivirlo en el proximo dash.
    void hide() {
        timer = 0.0f;
        gameObject->transform->scaleX = 0.0f;
        gameObject->transform->scaleY = 0.0f;
        if (collider) collider->isTrigger = true;
    }

    static std::vector<LucidPlatform*> all;
    BoxCollider* collider = nullptr;
    float baseScaleX = 1.0f, baseScaleY = 1.0f;
    float timer = 0.0f;
};
std::vector<LucidPlatform*> LucidPlatform::all;

// ---------------------------------------------------------------------------
// EphemeralPlatform: recuerdo fragil. Mientras el jugador este parado encima,
// una mecha (fuse) se consume y la plataforma tiembla cada vez mas fuerte;
// al agotarse se desvanece (trigger + invisible) y reaparece tras un tiempo.
// La mecha NO se regenera al bajarse: un recuerdo gastado sigue gastado
// hasta que "vuelve" completo.
//
// Igual que LucidPlatform: agregar AL FINAL del GameObject.
// ---------------------------------------------------------------------------
class EphemeralPlatform : public Component {
public:
    float fuse = 0.7f;          // segundos de pie que aguanta antes de desvanecerse
    float respawnDelay = 2.5f;  // segundos hasta reaparecer

    void awake() override {
        collider = gameObject->getComponent<BoxCollider>();
        baseX = gameObject->transform->x;
        baseScaleX = gameObject->transform->scaleX;
        baseScaleY = gameObject->transform->scaleY;
        timer = fuse;
    }

    void onCollision(GameObject* other) override {
        // Solo cuenta si el jugador esta ENCIMA (su centro mas arriba que el
        // nuestro): rozarla desde abajo o de costado no gasta el recuerdo.
        // El contacto de la fisica parpadea sub-pixel (igual que el grounded,
        // ver el coyote del controller), asi que en vez de un bool por frame
        // guardamos una ventana corta desde el ultimo contacto real.
        if (gone) return;
        if (other->name == "Player" && other->transform->y < gameObject->transform->y)
            contact = contactLinger;
    }

    void update(float dt) override {
        Transform* t = gameObject->transform;

        if (gone) {
            respawnTimer -= dt;
            if (respawnTimer <= 0.0f) { // reaparecer, como nueva
                gone = false;
                timer = fuse;
                t->x = baseX;
                t->scaleX = baseScaleX;
                t->scaleY = baseScaleY;
                if (collider) collider->isTrigger = false;
            }
            return;
        }

        if (contact > 0.0f) {
            contact -= dt;
            timer -= dt; // la mecha solo se consume con el jugador encima
        }

        if (timer <= 0.0f) { // se desvanece
            gone = true;
            respawnTimer = respawnDelay;
            t->x = baseX;
            t->scaleX = 0.0f;
            t->scaleY = 0.0f;
            if (collider) collider->isTrigger = true;
            return;
        }

        // Temblor: crece a medida que se consume la mecha (aviso visual).
        float consumed = 1.0f - (timer / fuse);
        if (consumed > 0.0f) {
            shakePhase += 45.0f * dt;
            t->x = baseX + std::sin(shakePhase) * 3.0f * consumed;
        }
    }

private:
    BoxCollider* collider = nullptr;
    float baseX = 0.0f;
    float baseScaleX = 1.0f, baseScaleY = 1.0f;
    float timer = 0.0f;
    float respawnTimer = 0.0f;
    float shakePhase = 0.0f;
    float contact = 0.0f;                        // ventana desde el ultimo contacto
    static constexpr float contactLinger = 0.06f;
    bool  gone = false;
};

// ---------------------------------------------------------------------------
// Fragmento de recuerdo: coleccionable (collider trigger). Al tocarlo el
// jugador, desaparece. TODO: contador global + condicion de victoria + HUD.
// ---------------------------------------------------------------------------
class Fragmento : public Component {
public:
    void onCollision(GameObject* other) override {
        if (other->name != "Player") return;
        SDL_Log("Fragmento de recuerdo recogido!");
        gameObject->scene->destroy(gameObject);
    }
};

// ---------------------------------------------------------------------------
// GatoController: el movimiento es EL juego, asi que va mas alla del ejemplo
// del curso: aceleracion/friccion (no velocidad instantanea), salto variable
// (soltar Espacio corta el salto), coyote time + jump buffer, Dash de Lucidez
// en 8 direcciones y Salto Propulsado con la pistola de agua (nivel 3).
//
// Controles: flechas mueven (y apuntan el dash), Espacio salta, X o Shift
// dash, C dispara agua hacia abajo (si hasWaterGun), R reinicia al spawn.
// ---------------------------------------------------------------------------
class GatoController : public Component {
public:
    // Movimiento horizontal (px/seg y px/seg^2)
    float maxSpeed = 300.0f;
    float accel    = 2800.0f;   // que tan rapido alcanza maxSpeed
    float friction = 3400.0f;   // que tan rapido frena al soltar

    // Salto
    float jumpSpeed = 620.0f;
    float jumpCut   = 0.45f;    // multiplicador al soltar Espacio subiendo

    // Dash de Lucidez
    float dashSpeed = 780.0f;
    float dashTime  = 0.15f;
    float lucidez   = 2.0f;     // segundos que las plataformas ocultas quedan reveladas

    // Pistola de Agua (nivel 3): disparo hacia abajo = doble salto
    bool  hasWaterGun = false;
    float waterJump   = 540.0f;

    // Respawn (por ahora: caer al vacio o tecla R; luego: pinchos, etc.)
    float spawnX = 0.0f, spawnY = -150.0f;
    float killY  = 900.0f;      // debajo de esta Y se considera caida al vacio

    // Hook para que pinchos/enemigos disparen la animacion de dano. Por ahora
    // solo afecta al sprite (no hay vidas ni knockback todavia).
    void hurt() { hurtTimer = hurtDuration; }

    void update(float dt) override {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        auto rb     = gameObject->getComponent<RigidBody2D>();
        auto sprite = gameObject->getComponent<SpriteRenderer>();
        auto anim   = gameObject->getComponent<SpriteAnimator>();
        if (!rb) return;

        Transform* t = gameObject->transform;

        // --- lectura de teclas (los flancos se detectan contra el frame anterior) ---
        float moveX = 0.0f;
        if (keys[SDL_SCANCODE_LEFT])  moveX -= 1.0f;
        if (keys[SDL_SCANCODE_RIGHT]) moveX += 1.0f;
        float aimY = 0.0f;
        if (keys[SDL_SCANCODE_UP])   aimY -= 1.0f;
        if (keys[SDL_SCANCODE_DOWN]) aimY += 1.0f;
        bool jumpNow  = keys[SDL_SCANCODE_SPACE];
        bool dashNow  = keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_LSHIFT];
        bool waterNow = keys[SDL_SCANCODE_C];
        bool resetNow = keys[SDL_SCANCODE_R];

        if (moveX != 0.0f) facing = (moveX > 0.0f) ? 1.0f : -1.0f;

        // --- respawn: caida al vacio o reinicio manual ---
        if (t->y > killY || resetNow) respawn(rb, t);

        // --- tolerancias estilo Celeste ---
        // Coyote: ventana de salto tras dejar el suelo (el grounded crudo ademas
        // parpadea sub-pixel, ver el ejemplo Platformer del curso).
        if (rb->grounded) {
            coyote = coyoteTime;
            canDash = true;        // el dash se recarga al pisar suelo
            canWaterJump = true;   // el disparo de agua tambien
        } else if (coyote > 0.0f) {
            coyote -= dt;
        }
        // Buffer: si aprieta Espacio un pelo antes de aterrizar, el salto no se pierde.
        if (jumpNow && !jumpPrev) jumpBuffer = jumpBufferTime;
        else if (jumpBuffer > 0.0f) jumpBuffer -= dt;

        // --- dash en curso: velocidad fija, sin gravedad, control bloqueado ---
        if (dashing) {
            dashLeft -= dt;
            rb->velocityX = dashDirX * dashSpeed;
            rb->velocityY = dashDirY * dashSpeed;
            if (dashLeft <= 0.0f) {
                dashing = false;
                rb->gravityScale = 1.0f;
                // Corte suave: si el dash iba hacia arriba no salimos disparados.
                if (dashDirY < 0.0f) rb->velocityY *= 0.5f;
            }
            if (anim) anim->play("dash");
            if (sprite && dashDirX != 0.0f) sprite->flipX = dashDirX < 0.0f;
            jumpPrev = jumpNow; dashPrev = dashNow; waterPrev = waterNow;
            return;
        }

        // --- iniciar Dash de Lucidez ---
        if (dashNow && !dashPrev && canDash) {
            float dx = moveX, dy = aimY;
            if (dx == 0.0f && dy == 0.0f) dx = facing; // sin direccion: hacia donde mira
            float len = std::sqrt(dx * dx + dy * dy);
            dashDirX = dx / len;
            dashDirY = dy / len;
            dashing = true;
            dashLeft = dashTime;
            canDash = false;
            jumping = false;
            rb->gravityScale = 0.0f;
            LucidPlatform::revealAll(lucidez); // la lucidez ilumina los recuerdos ocultos
            jumpPrev = jumpNow; dashPrev = dashNow; waterPrev = waterNow;
            return;
        }

        // --- movimiento horizontal con aceleracion / friccion ---
        float target = moveX * maxSpeed;
        float rate   = (moveX != 0.0f) ? accel : friction;
        if (rb->velocityX < target)
            rb->velocityX = std::min(rb->velocityX + rate * dt, target);
        else
            rb->velocityX = std::max(rb->velocityX - rate * dt, target);

        // --- salto (buffer + coyote) ---
        if (jumpBuffer > 0.0f && coyote > 0.0f) {
            rb->velocityY = -jumpSpeed;
            jumpBuffer = 0.0f;
            coyote = 0.0f; // consumir la ventana: evita doble salto en el mismo apoyo
            jumping = true;
            jumpStartTimer = jumpStartDuration; // reproducir el impulso/despegue
        }
        // Salto variable: soltar Espacio mientras sube corta el impulso. Solo
        // aplica a saltos propios (no al final de un dash ni al salto de agua).
        if (jumping && !jumpNow && rb->velocityY < 0.0f) {
            rb->velocityY *= jumpCut;
            jumping = false;
        }
        if (rb->velocityY >= 0.0f) jumping = false;

        // --- Salto Propulsado (nivel 3): disparo de agua hacia abajo en el aire ---
        if (hasWaterGun && waterNow && !waterPrev && coyote <= 0.0f && canWaterJump) {
            rb->velocityY = -waterJump;
            canWaterJump = false;
            jumping = false;
            waterShootTimer = waterShootDuration; // animacion de la pistola de agua
            // TODO: spawnear el chorro de agua (sprite + Lifetime) y sonido.
        }

        // --- sprite y animacion ---
        if (sprite && moveX != 0.0f) sprite->flipX = moveX < 0.0f;

        bool onGround = coyote > 0.0f; // grounded suavizado (anti-parpadeo)
        // "Aterrizo" recien ahora (transicion real: coyote ya filtra el parpadeo
        // sub-pixel del grounded crudo, asi que este flanco no es espurio).
        if (onGround && !wasOnGround) landTimer = landDuration;
        wasOnGround = onGround;

        if (hurtTimer > 0.0f) hurtTimer -= dt;
        if (landTimer > 0.0f) landTimer -= dt;
        if (jumpStartTimer > 0.0f) jumpStartTimer -= dt;
        if (waterShootTimer > 0.0f) waterShootTimer -= dt;

        if (anim) {
            if (hurtTimer > 0.0f) {
                anim->play("hurt");
            } else if (onGround) {
                if (landTimer > 0.0f) anim->play("land");
                else anim->play(moveX != 0.0f ? "run" : "idle");
            } else {
                // En el aire: agua > despegue > subida/apice/caida (por velocidad).
                if (waterShootTimer > 0.0f) anim->play("water_shoot");
                else if (jumpStartTimer > 0.0f) anim->play("jump_start");
                else if (rb->velocityY < -apexThreshold) anim->play("jump_up");
                else if (rb->velocityY >  apexThreshold) anim->play("fall");
                else anim->play("jump_apex"); // velocidad vertical casi nula: el pico del salto
            }
        }

        jumpPrev = jumpNow; dashPrev = dashNow; waterPrev = waterNow;
    }

private:
    void respawn(RigidBody2D* rb, Transform* t) {
        t->x = spawnX; t->y = spawnY;
        rb->velocityX = rb->velocityY = 0.0f;
        rb->gravityScale = 1.0f;
        dashing = false;
        canDash = true;
        canWaterJump = true;
        hurtTimer = landTimer = jumpStartTimer = waterShootTimer = 0.0f;
        wasOnGround = false;
    }

    // estado del dash
    bool  dashing = false;
    float dashLeft = 0.0f;
    float dashDirX = 1.0f, dashDirY = 0.0f;
    bool  canDash = true;

    // estado del salto
    bool  jumping = false;      // el impulso vertical actual vino de un salto propio
    bool  canWaterJump = true;
    float facing = 1.0f;        // ultima direccion horizontal (para el dash neutro)

    // flancos de tecla
    bool jumpPrev = false, dashPrev = false, waterPrev = false;

    // tolerancias
    float coyote = 0.0f;
    float jumpBuffer = 0.0f;
    static constexpr float coyoteTime     = 0.1f;
    static constexpr float jumpBufferTime = 0.12f;

    // maquina de animacion: timers de los clips "de un solo tiro" (no loop).
    // Cada duracion = frames del sheet / fps con que se registra ese clip
    // (ver addStripAnimation en buildFragmentoMemoria), para que el timer se
    // agote justo cuando el sheet termina de reproducirse.
    bool  wasOnGround = false;  // para detectar el flanco real de aterrizaje
    float hurtTimer = 0.0f, landTimer = 0.0f, jumpStartTimer = 0.0f, waterShootTimer = 0.0f;
    static constexpr float hurtDuration       = 3.0f / 10.0f; // hurt_sheet_3x1 a 10 fps
    static constexpr float landDuration       = 2.0f / 14.0f; // land_sheet_2x1 a 14 fps
    static constexpr float jumpStartDuration  = 3.0f / 18.0f; // jump_start_sheet_3x1 a 18 fps
    static constexpr float waterShootDuration = 4.0f / 20.0f; // water_shoot_sheet_4x1 a 20 fps
    static constexpr float apexThreshold      = 60.0f;        // px/seg: "casi cero" = pico del salto
};

// ---------------------------------------------------------------------------
// Helpers de construccion de la escena
// ---------------------------------------------------------------------------

// Plataforma efimera: sprite "Falling Platform" de Pixel Adventure (32x10).
static GameObject* crearEfimera(Scene& scene, float x, float y) {
    GameObject* p = scene.createGameObject("Efimera");
    p->transform->x = x; p->transform->y = y;
    p->transform->scaleX = p->transform->scaleY = 4.0f; // 32x10 -> 128x40
    p->addComponent<SpriteRenderer>("assets/pixel_adventure/Traps/Falling Platforms/Off.png");
    auto col = p->addComponent<BoxCollider>();
    col->width = 128.0f; col->height = 40.0f;
    p->addComponent<EphemeralPlatform>(); // al final: captura collider y escala
    return p;
}

// Plataforma oculta: solo aparece durante la lucidez del dash.
static GameObject* crearLucida(Scene& scene, float x, float y) {
    GameObject* p = scene.createGameObject("Lucida");
    p->transform->x = x; p->transform->y = y;
    p->transform->scaleX = p->transform->scaleY = 4.0f; // 32x8 -> 128x32
    p->addComponent<SpriteRenderer>("assets/pixel_adventure/Traps/Platforms/Grey Off.png");
    auto col = p->addComponent<BoxCollider>();
    col->width = 128.0f; col->height = 32.0f;
    p->addComponent<LucidPlatform>(); // al final: captura collider y escala
    return p;
}

// Fragmento de recuerdo: fruta animada de Pixel Adventure como placeholder.
static GameObject* crearFragmento(Scene& scene, float x, float y) {
    GameObject* f = scene.createGameObject("Fragmento");
    f->transform->x = x; f->transform->y = y;
    f->transform->scaleX = f->transform->scaleY = 3.0f;
    f->addComponent<SpriteRenderer>();
    auto anim = f->addComponent<SpriteAnimator>(32, 32, 1);
    anim->addStripAnimation("idle", "assets/pixel_adventure/Items/Fruits/Strawberry.png", 32, 32, 20.0f);
    anim->play("idle");
    auto col = f->addComponent<BoxCollider>();
    col->width = 50.0f; col->height = 50.0f;
    col->isTrigger = true; // solo avisa: no queremos chocar con el recuerdo
    f->addComponent<Fragmento>();
    return f;
}

void buildFragmentoMemoria(Scene& scene) {
    // --- el gato: sprites propios (assets/animation_cat), frames de 64x64, mira
    //     a la derecha por defecto (flipX lo maneja el GatoController). Escala
    //     2x (64*2=128) para quedar del mismo tamano en pantalla que antes con
    //     el placeholder (32*4=128), asi el collider de abajo no cambia. ---
    GameObject* player = scene.createGameObject("Player");
    player->transform->y = -150.0f;
    player->transform->scaleX = player->transform->scaleY = 2.0f;
    player->addComponent<SpriteRenderer>();
    auto anim = player->addComponent<SpriteAnimator>(64, 64, 1);
    const std::string gato = "assets/animation_cat/";
    anim->addStripAnimation("idle",        gato + "idle_sheet_6x1.png",        64, 64, 10.0f);
    anim->addStripAnimation("run",         gato + "run_sheet_8x1.png",         64, 64, 14.0f);
    anim->addStripAnimation("jump_start",  gato + "jump_start_sheet_3x1.png",  64, 64, 18.0f, false);
    anim->addStripAnimation("jump_up",     gato + "jump_up_sheet_2x1.png",     64, 64, 10.0f);
    anim->addStripAnimation("jump_apex",   gato + "jump_apex_sheet_1x1.png",   64, 64, 1.0f);
    anim->addStripAnimation("fall",        gato + "fall_sheet_2x1.png",        64, 64, 10.0f);
    anim->addStripAnimation("land",        gato + "land_sheet_2x1.png",        64, 64, 14.0f, false);
    anim->addStripAnimation("dash",        gato + "dash_sheet_4x1.png",        64, 64, 26.0f, false);
    anim->addStripAnimation("hurt",        gato + "hurt_sheet_3x1.png",        64, 64, 10.0f, false);
    anim->addStripAnimation("water_shoot", gato + "water_shoot_sheet_4x1.png", 64, 64, 20.0f, false);
    anim->play("idle");
    player->addComponent<RigidBody2D>();
    auto col = player->addComponent<BoxCollider>();
    // Medido sobre el canal alfa real de idle/run/land (frame de 64x64, sin
    // recortar): el cuerpo visible va de x=[10,56) y=[14,56) aprox, con
    // padding transparente debajo de los pies (a diferencia del placeholder
    // anterior, que llegaba casi al borde del frame). offsetY empuja el
    // collider hacia abajo para que el piso quede a la altura de los pies
    // visibles y no "flote". Si se reemplaza el sprite, medir de nuevo.
    col->width = 88.0f; col->height = 84.0f; col->offsetY = 6.0f;
    auto ctrl = player->addComponent<GatoController>();
    ctrl->hasWaterGun = true; // SOLO nivel 3; activado aca para poder probarla ya

    // --- terreno: por ahora el nivel de ejemplo del curso; reemplazar por los
    //     mapas propios (nivel1/2/3 = infancia/adultez/vejez) hechos en Tiled ---
    GameObject* tilemap = scene.createGameObject("Tilemap");
    tilemap->transform->x = -960.0f;
    tilemap->transform->y = -262.0f;
    tilemap->transform->scaleX = tilemap->transform->scaleY = 4.0f;
    auto tm = tilemap->addComponent<TilemapRenderer>();
    if (!tm->loadFromTiledJson("assets/maps/platformer_level1.json"))
        SDL_Log("buildFragmentoMemoria: no se pudo cargar assets/maps/platformer_level1.json");

    // --- zona de prueba de mecanicas (posiciones tentativas: ajustar con F1) ---
    // Escalera de recuerdos fragiles: obliga a no quedarse parado.
    crearEfimera(scene, 250.0f, 60.0f);
    crearEfimera(scene, 430.0f, -20.0f);
    crearEfimera(scene, 610.0f, -100.0f);
    // Camino oculto: solo visible/solido tras un Dash de Lucidez.
    crearLucida(scene, 790.0f, -180.0f);
    crearLucida(scene, 960.0f, -260.0f);
    // Fragmentos de recuerdo: uno facil y otro al final del camino oculto.
    crearFragmento(scene, -200.0f, 120.0f);
    crearFragmento(scene, 960.0f, -360.0f);

    // --- camara: zona muerta chica, el genero pide precision visual ---
    GameObject* cam = scene.createGameObject("MainCamera");
    cam->addComponent<Camera>();
    auto f = cam->addComponent<FollowCamera>();
    f->setTarget(player);
    f->deadZoneWidth = 120.0f; f->deadZoneHeight = 120.0f;
}

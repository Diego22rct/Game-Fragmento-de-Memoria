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
// Estado de recuerdos recolectados (ver guia_programacion_level01.txt, seccion
// "Fragmentos y logica de memoria"): 3 grupos de 3. grupo[0]=fragment1 (hospital),
// grupo[1]=fragment2, grupo[2]=fragment3. Variable de archivo: un solo nivel a la
// vez, se resetea sola en cada corrida del programa (no hace falta mas por ahora).
// ---------------------------------------------------------------------------
struct MemoriaState {
    int total = 0;
    int grupo[3] = { 0, 0, 0 };
};
static MemoriaState g_memoria;

// ---------------------------------------------------------------------------
// MemoriaFragment: fragmento de recuerdo REAL (leido de la capa Objects del
// Nivel1.tmj), a diferencia de Fragmento (arriba), que es el placeholder de la
// zona de prueba. Suma al contador de su grupo y lo loggea; cuando el grupo
// llega a 3, ademas avisa que ese recuerdo quedo completo (la guia sugiere
// mostrar ahi la imagen "preview" completa del pack de fragmentos, pero el
// motor todavia no tiene un sistema de UI/pantallas para mostrarla).
// ---------------------------------------------------------------------------
class MemoriaFragment : public Component {
public:
    int group = 0; // 0, 1 o 2

    void onCollision(GameObject* other) override {
        if (other->name != "Player") return;
        g_memoria.total++;
        g_memoria.grupo[group]++;
        SDL_Log("Fragmento de memoria (grupo %d) recogido: %d/9 total, grupo %d en %d/3",
                group + 1, g_memoria.total, group + 1, g_memoria.grupo[group]);
        if (g_memoria.grupo[group] == 3)
            SDL_Log("Recuerdo %d COMPLETO (3/3)", group + 1);
        gameObject->scene->destroy(gameObject);
    }
};

// ---------------------------------------------------------------------------
// ExitZone: zona de salida del nivel (objeto "exit" de Objects). Al tocarla se
// evalua el final segun la guia: bueno si los 3 grupos estan completos (3/3
// cada uno), malo si no. 'reached' evita loggear el resultado en cada frame
// mientras el jugador se queda parado adentro del trigger.
// ---------------------------------------------------------------------------
class ExitZone : public Component {
public:
    void onCollision(GameObject* other) override {
        if (other->name != "Player" || reached) return;
        reached = true;
        bool good = g_memoria.grupo[0] == 3 && g_memoria.grupo[1] == 3 && g_memoria.grupo[2] == 3;
        if (good) SDL_Log("FINAL BUENO: recuperaste los 9 recuerdos.");
        else      SDL_Log("FINAL MALO: llegaste a la salida con %d/9 recuerdos.", g_memoria.total);
        // TODO: mostrar la cinematica correspondiente (assets/cinematica_final) una
        // vez que haya un sistema de pantallas/UI; por ahora el resultado queda en el log.
    }
private:
    bool reached = false;
};

// ---------------------------------------------------------------------------
// Enemy: enemigos y boss de la capa Objects. Tres comportamientos (ver la guia,
// seccion "Tamanos de sprites importantes"):
//  - Patrol: va y vuelve en X alrededor de su punto de spawn (enemigo hospital).
//  - FloatChase: flota en su lugar; si el jugador esta cerca en X, lo persigue
//    (enemigo adultez/ansiedad).
//  - Boss: igual que FloatChase pero con mas rango y velocidad (BossShadow).
// No usa RigidBody2D: no necesita gravedad ni colisionar con el piso, se mueve
// derecho por Transform. El collider es TRIGGER (isTrigger=true) para no
// empujar fisicamente al jugador: solo avisa el contacto via onCollision, que
// llama a GatoController::hurt() (el mismo hook que ya usaba el gato para la
// animacion de dano; no hay sistema de vidas todavia, ver CAMBIOS_NIVEL1.md).
// ---------------------------------------------------------------------------
enum class EnemyBehavior { Patrol, FloatChase, Boss };

class Enemy : public Component {
public:
    EnemyBehavior behavior = EnemyBehavior::Patrol;
    GameObject* target = nullptr; // el jugador; lo asigna buildFragmentoMemoria despues de crearlo

    float speed = 70.0f;
    float patrolRange = 150.0f; // Patrol: cuanto se aleja del spawn antes de dar vuelta
    float chaseRange = 260.0f;  // FloatChase/Boss: distancia (en X) para empezar a perseguir
    float floatAmplitude = 12.0f; // FloatChase en reposo: vaiven vertical suave
    float hurtCooldown = 1.0f;    // no golpear todos los frames mientras se solapa con el jugador

    void awake() override {
        baseX = gameObject->transform->x;
        baseY = gameObject->transform->y;
    }

    void update(float dt) override {
        if (cooldown > 0.0f) cooldown -= dt;
        Transform* t = gameObject->transform;
        auto sprite = gameObject->getComponent<SpriteRenderer>();
        auto anim = gameObject->getComponent<SpriteAnimator>();

        if (behavior == EnemyBehavior::Patrol) {
            t->x += dir * speed * dt;
            if (t->x > baseX + patrolRange) dir = -1.0f;
            else if (t->x < baseX - patrolRange) dir = 1.0f;
            if (sprite) sprite->flipX = dir < 0.0f;
            return;
        }

        // FloatChase y Boss: perseguir si el objetivo esta a menos de chaseRange en X.
        bool chasing = target && std::fabs(target->transform->x - t->x) < chaseRange;
        if (chasing) {
            float dx = target->transform->x - t->x;
            t->x += (dx > 0.0f ? 1.0f : -1.0f) * speed * dt;
            if (sprite) sprite->flipX = dx < 0.0f;
        } else if (behavior == EnemyBehavior::FloatChase) {
            bobPhase += dt * 2.0f;
            t->y = baseY + std::sin(bobPhase) * floatAmplitude;
        }
        if (anim && behavior == EnemyBehavior::FloatChase) anim->play(chasing ? "chase" : "float");
    }

    void onCollision(GameObject* other) override {
        if (other->name != "Player" || cooldown > 0.0f) return;
        if (auto* ctrl = other->getComponent<GatoController>()) ctrl->hurt();
        cooldown = hurtCooldown;
    }

private:
    float baseX = 0.0f, baseY = 0.0f;
    float dir = 1.0f;
    float bobPhase = 0.0f;
    float cooldown = 0.0f;
};

// ---------------------------------------------------------------------------
// Hud: contador visual de fragmentos (la guia pide "UI: contador de
// fragmentos" en el orden de dibujo). Dibuja en coordenadas de PANTALLA fijas
// arriba a la izquierda (no usa Camera ni Transform: por eso no sigue el
// mundo), 3 filas de 3 cuadrados, una fila por grupo de memoria. Relleno =
// recogido, solo el borde = todavia no. Sin texto (el motor no tiene
// TextRenderer todavia, ver README.md "Pendiente").
// ---------------------------------------------------------------------------
class Hud : public Component {
public:
    void render() override {
        SDL_Renderer* renderer = gameObject->scene->getRenderer();
        const float size = 14.0f, gap = 4.0f, marginX = 12.0f, marginY = 12.0f;
        const Uint8 colorsR[3] = { 220, 120, 180 };
        const Uint8 colorsG[3] = { 120, 180, 220 };
        const Uint8 colorsB[3] = { 120, 220, 120 };

        for (int g = 0; g < 3; ++g) {
            SDL_SetRenderDrawColor(renderer, colorsR[g], colorsG[g], colorsB[g], 255);
            for (int i = 0; i < 3; ++i) {
                SDL_FRect r{ marginX + i * (size + gap), marginY + g * (size + gap), size, size };
                if (i < g_memoria.grupo[g]) SDL_RenderFillRect(renderer, &r);
                else                        SDL_RenderRect(renderer, &r);
            }
        }
    }
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

// Enemigo hospital: patrulla horizontal. La guia dice "el nombre dice hurt,
// pero para gameplay conviene usar la animacion patrol como estado normal".
static GameObject* crearEnemigoHospital(Scene& scene, float centerX, float centerY) {
    GameObject* e = scene.createGameObject("EnemyHospital");
    e->transform->x = centerX; e->transform->y = centerY;
    e->addComponent<SpriteRenderer>();
    auto anim = e->addComponent<SpriteAnimator>(64, 64, 1);
    anim->addStripAnimation("patrol", "assets/enemies/enemy1_hospital_patrol_sheet_6x1.png", 64, 64, 8.0f);
    anim->play("patrol");
    auto col = e->addComponent<BoxCollider>();
    col->width = 56.0f; col->height = 64.0f; col->isTrigger = true;
    auto en = e->addComponent<Enemy>();
    en->behavior = EnemyBehavior::Patrol;
    return e;
}

// Enemigo adultez/ansiedad: flota en su lugar, persigue si el jugador se acerca.
static GameObject* crearEnemigoAdultez(Scene& scene, float centerX, float centerY) {
    GameObject* e = scene.createGameObject("EnemyAdulthood");
    e->transform->x = centerX; e->transform->y = centerY;
    e->addComponent<SpriteRenderer>();
    auto anim = e->addComponent<SpriteAnimator>(64, 64, 1);
    anim->addStripAnimation("float", "assets/enemies/enemy2_adulthood_float_sheet_8x1.png", 64, 64, 10.0f);
    anim->addStripAnimation("chase", "assets/enemies/enemy2_adulthood_chase_sheet_8x1.png", 64, 64, 12.0f);
    anim->play("float");
    auto col = e->addComponent<BoxCollider>();
    col->width = 56.0f; col->height = 56.0f; col->isTrigger = true;
    auto en = e->addComponent<Enemy>();
    en->behavior = EnemyBehavior::FloatChase;
    return e;
}

// Boss (sombra del protagonista): igual logica que FloatChase pero con mas
// alcance y velocidad; 128x128 segun la guia.
static GameObject* crearBoss(Scene& scene, float centerX, float centerY) {
    GameObject* e = scene.createGameObject("BossShadow");
    e->transform->x = centerX; e->transform->y = centerY;
    e->addComponent<SpriteRenderer>();
    auto anim = e->addComponent<SpriteAnimator>(128, 128, 1);
    anim->addStripAnimation("chase", "assets/enemies/boss_shadow_chase_sheet_8x1.png", 128, 128, 10.0f);
    anim->play("chase");
    auto col = e->addComponent<BoxCollider>();
    col->width = 100.0f; col->height = 120.0f; col->isTrigger = true;
    auto en = e->addComponent<Enemy>();
    en->behavior = EnemyBehavior::Boss;
    en->chaseRange = 500.0f;
    en->speed = 90.0f;
    return e;
}

// Fragmento de recuerdo REAL (grupo 0/1/2 = fragment1/2/3). indexInGroup elige
// cual de las 3 imagenes del pack usar (solo variedad visual, sin logica extra).
static const char* kFragmentImage[3][3] = {
    { "assets/photo_fragments_collectibles/level1_hospital_fragment_01.png",
      "assets/photo_fragments_collectibles/level1_hospital_fragment_02.png",
      "assets/photo_fragments_collectibles/level1_hospital_fragment_03.png" },
    { "assets/photo_fragments_collectibles/level2_adulthood_fragment_01.png",
      "assets/photo_fragments_collectibles/level2_adulthood_fragment_02.png",
      "assets/photo_fragments_collectibles/level2_adulthood_fragment_03.png" },
    { "assets/photo_fragments_collectibles/level3_childhood_fragment_01.png",
      "assets/photo_fragments_collectibles/level3_childhood_fragment_02.png",
      "assets/photo_fragments_collectibles/level3_childhood_fragment_03.png" },
};

static GameObject* crearFragmentoReal(Scene& scene, float centerX, float centerY, int group, int indexInGroup) {
    GameObject* f = scene.createGameObject("MemoriaFragment");
    f->transform->x = centerX; f->transform->y = centerY;
    f->addComponent<SpriteRenderer>(kFragmentImage[group][indexInGroup % 3]);
    auto col = f->addComponent<BoxCollider>();
    // Collider de 32x32 (el tamano del objeto en Tiled), aunque la imagen real
    // sea 64x64: la guia dice que no afecta la logica, el sprite queda centrado.
    col->width = 32.0f; col->height = 32.0f;
    col->isTrigger = true;
    auto mf = f->addComponent<MemoriaFragment>();
    mf->group = group;
    return f;
}

// Salida del nivel: zona trigger invisible (el debug F1 la muestra igual).
static GameObject* crearSalida(Scene& scene, float centerX, float centerY, float w, float h) {
    GameObject* e = scene.createGameObject("Exit");
    e->transform->x = centerX; e->transform->y = centerY;
    auto col = e->addComponent<BoxCollider>();
    col->width = w; col->height = h;
    col->isTrigger = true;
    e->addComponent<ExitZone>();
    return e;
}

// PlayerSpawn real de assets/Nivel1.tmj (capa Objects, id 1): x=41.33 y=477,
// 64x64. Tiled da la esquina superior izquierda; el Transform del engine ancla
// al CENTRO, por eso sumamos medio ancho/alto.
static constexpr float kSpawnX = 41.333333f + 32.0f;
static constexpr float kSpawnY = 477.0f + 32.0f;

void buildFragmentoMemoria(Scene& scene) {
    // --- terreno: Nivel1.tmj real (hospital/recuerdos/final), escala 1:1 con sus
    //     coordenadas de Tiled (mismo sistema que PlayerSpawn/enemigos/fragmentos
    //     de la guia). Tolera que falten hospital_tiles_32x32_extras,
    //     hospital_corners_32x32 o fondo.png: esos tiles/fondo simplemente no se
    //     ven todavia, pero el resto del mapa (y su colision) carga igual.
    //     IMPORTANTE: se crea ANTES que el Player. El render es en orden de
    //     creacion (pintor), y el mapa real trae un Fondo que cubre TODA la
    //     pantalla: si el jugador se creara despues, el fondo lo taparia
    //     (con el nivel de prueba viejo, sin fondo opaco, no se notaba). ---
    GameObject* tilemap = scene.createGameObject("Tilemap");
    auto tm = tilemap->addComponent<TilemapRenderer>();
    if (!tm->loadTiledMap("assets/Nivel1.tmj"))
        SDL_Log("buildFragmentoMemoria: no se pudo cargar assets/Nivel1.tmj");

    // --- objetos reales del nivel (capa Objects del tmj): spawn, 2 enemigos, boss,
    //     9 fragmentos y salida. Tiled da la esquina superior izquierda de cada
    //     objeto; el Transform del motor ancla al CENTRO, por eso se suma medio
    //     ancho/alto (cx/cy) al convertir. Se crean ANTES que el jugador por el
    //     mismo motivo que el Tilemap (orden de creacion = orden de dibujado; la
    //     guia pide Jugador encima de fragmentos/enemigos/boss). Los enemigos y
    //     el boss guardan su puntero en 'enemigos' para asignarles el jugador
    //     como objetivo (target) recien despues de crearlo, un poco mas abajo. ---
    float spawnX = kSpawnX, spawnY = kSpawnY; // fallback si no aparece PlayerSpawn
    std::vector<Enemy*> enemigos;
    int fragCount[3] = { 0, 0, 0 }; // cuenta por grupo: elige que imagen (01/02/03) usar

    for (const auto& obj : tm->getObjects()) {
        float cx = obj.x + obj.width * 0.5f;
        float cy = obj.y + obj.height * 0.5f;

        if (obj.type == "player") {
            spawnX = cx; spawnY = cy;
        } else if (obj.type == "enemy" && obj.name.find("hospital") != std::string::npos) {
            enemigos.push_back(crearEnemigoHospital(scene, cx, cy)->getComponent<Enemy>());
        } else if (obj.type == "enemy") {
            enemigos.push_back(crearEnemigoAdultez(scene, cx, cy)->getComponent<Enemy>());
        } else if (obj.type == "boss_shadow") {
            enemigos.push_back(crearBoss(scene, cx, cy)->getComponent<Enemy>());
        } else if (obj.type == "fragment1" || obj.type == "fragment") {
            // "fragment" (sin numero) son los que la guia recomienda corregir en
            // Tiled a fragment1; mientras tanto se cuentan como grupo 1 igual.
            crearFragmentoReal(scene, cx, cy, 0, fragCount[0]++);
        } else if (obj.type == "fragment2") {
            crearFragmentoReal(scene, cx, cy, 1, fragCount[1]++);
        } else if (obj.type == "fragment3") {
            crearFragmentoReal(scene, cx, cy, 2, fragCount[2]++);
        } else if (obj.type == "exit") {
            crearSalida(scene, cx, cy, obj.width, obj.height);
        }
        // cualquier otro type (o vacio) se ignora: getObjects() ya filtro los sin type.
    }

    // --- el gato: sprites propios (assets/animation_cat), frames de 64x64, mira
    //     a la derecha por defecto (flipX lo maneja el GatoController). Escala 1x:
    //     el nivel real (Nivel1.tmj) esta en tiles de 32x32 sin upscalear, y la
    //     guia pide un PlayerSpawn de 64x64 en esa misma escala 1:1. ---
    GameObject* player = scene.createGameObject("Player");
    player->transform->x = spawnX;
    player->transform->y = spawnY;
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
    // Mismas proporciones que antes (cuerpo real del sprite, sin el padding
    // transparente de los pies), pero a la mitad: la escala bajo de 2x a 1x.
    // ponytail: valores a ojo desde el calculo anterior; retocar viendo F1.
    col->width = 44.0f; col->height = 42.0f; col->offsetY = 3.0f;
    auto ctrl = player->addComponent<GatoController>();
    ctrl->hasWaterGun = true; // SOLO nivel 3; activado aca para poder probarla ya
    ctrl->spawnX = spawnX; ctrl->spawnY = spawnY; // respawn al caer / tecla R

    // El jugador recien existe aca: ahora si se lo asignamos como objetivo a los
    // enemigos/boss que persiguen (FloatChase y Boss; Patrol lo ignora).
    for (Enemy* en : enemigos) en->target = player;

    // --- zona de mecanicas (Efimera/Lucida/Fragmento de prueba): comentada por
    //     ahora, quedaban ubicadas para el nivel placeholder anterior y no
    //     corresponden a las coordenadas del Nivel1.tmj real. Los objetos reales
    //     (9 fragmentos, 2 enemigos, boss, salida) se leen de la capa Objects del
    //     tmj en el proximo paso: por ahora solo se carga el mapa. ---
    // crearEfimera(scene, 250.0f, 60.0f);
    // crearEfimera(scene, 430.0f, -20.0f);
    // crearEfimera(scene, 610.0f, -100.0f);
    // crearLucida(scene, 790.0f, -180.0f);
    // crearLucida(scene, 960.0f, -260.0f);
    // crearFragmento(scene, -200.0f, 120.0f);
    // crearFragmento(scene, 960.0f, -360.0f);

    // --- camara: zona muerta chica, el genero pide precision visual ---
    GameObject* cam = scene.createGameObject("MainCamera");
    cam->addComponent<Camera>();
    auto f = cam->addComponent<FollowCamera>();
    f->setTarget(player);
    f->deadZoneWidth = 120.0f; f->deadZoneHeight = 120.0f;

    // --- HUD: contador de fragmentos, ultimo para quedar dibujado encima de todo ---
    scene.createGameObject("Hud")->addComponent<Hud>();
}

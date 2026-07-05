#pragma once
#include <string>
#include "Component.h"

struct SDL_Texture; // declaracion adelantada: SDL solo aparece en el .cpp

// Dibuja una imagen segun el Transform. Puede dibujar la imagen completa o,
// si se le da un recorte (setSourceRect), solo una porcion (un frame de spritesheet).
// flipX / flipY permiten voltear el dibujo (p.ej. mirar al otro lado) sin tocar SDL.

class SpriteRenderer : public Component {
public:
    SpriteRenderer() = default;               // sin textura inicial: la asigna el animator
    explicit SpriteRenderer(std::string imagePath);

    bool flipX = false; // espejo horizontal (mirar a izquierda/derecha)
    bool flipY = false; // espejo vertical

    // Tinte de color (0-255 por canal; 255,255,255 = sin tinte, color original de
    // la textura). Se multiplica sobre los pixeles al dibujar (SDL_SetTextureColorMod
    // en el .cpp). Sirve para feedback visual (brillo del dash, flash de dano,
    // color de una particula) sin necesitar un sprite nuevo por color.
    int colorR = 255, colorG = 255, colorB = 255;
    void setColor(int r, int g, int b) { colorR = r; colorG = g; colorB = b; }

    void awake() override;
    void render() override;

    // Cambia que textura se dibuja (la usa el SpriteAnimator al cambiar de animacion).
    // La textura sigue siendo propiedad del AssetManager: solo la tomamos prestada.
    void setTexture(SDL_Texture* tex);

    // Dibujar solo una porcion de la imagen (un frame del spritesheet).
    void setSourceRect(float x, float y, float w, float h) {
        srcX = x; srcY = y; srcW = w; srcH = h; useSrcRect = true;
    }

private:
    std::string path;
    SDL_Texture* texture = nullptr; // prestada por el AssetManager (no somos dueno)
    float width = 0.0f;             // tamano de la imagen COMPLETA
    float height = 0.0f;

    bool  useSrcRect = false;       // false = imagen completa; true = solo el recorte
    float srcX = 0.0f, srcY = 0.0f, srcW = 0.0f, srcH = 0.0f;
};

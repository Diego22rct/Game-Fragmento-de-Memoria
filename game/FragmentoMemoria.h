#pragma once

class Scene;

// FRAGMENTO DE MEMORIA — plataformas de precision 2D (estilo Celeste).
// Un gato (el alma de un anciano con perdida de memoria) recorre las tres
// etapas de su vida recolectando fragmentos de recuerdos.
//
// Mecanicas:
//  - Dash de Lucidez (X / Shift): impulso en 8 direcciones que ademas revela
//    temporalmente las plataformas ocultas (LucidPlatform).
//  - Plataformas Efimeras: recuerdos fragiles; si te quedas parado sobre ellas
//    tiemblan y se desvanecen. Reaparecen despues de unos segundos.
//  - Salto Propulsado (C, solo nivel 3): disparo de agua hacia abajo en el
//    aire que actua como doble salto.
void buildFragmentoMemoria(Scene& scene);

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <memory>

#include "engine/Scene.h"
#include "engine/Debugger.h"

#include "game/FragmentoMemoria.h"

// Nota: game/Platformer, TopDown y Shooter son los ejemplos del motor del
// curso; ahora solo quedo la referenia a nuestro juego.
int main(int argc, char* argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("Error al inicializar SDL: %s", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("Fragmento de Memoria  (F1 debug)", 1280, 720, 0);
    if (!window) { SDL_Quit(); return 1; }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

    // Musica de fondo: un solo track que loopea para siempre.
    MIX_Init();
    MIX_Mixer* mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    MIX_Audio* music = mixer ? MIX_LoadAudio(mixer, "assets/backgroud_sound.mp3", false) : nullptr;
    MIX_Track* musicTrack = mixer ? MIX_CreateTrack(mixer) : nullptr;
    if (musicTrack && music) {
        MIX_SetTrackAudio(musicTrack, music);
        SDL_PropertiesID playOpts = SDL_CreateProperties();
        SDL_SetNumberProperty(playOpts, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
        MIX_PlayTrack(musicTrack, playOpts);
        SDL_DestroyProperties(playOpts);
    } else {
        SDL_Log("No se pudo iniciar la musica de fondo: %s", SDL_GetError());
    }

    auto scene = std::make_unique<Scene>(renderer);
    buildFragmentoMemoria(*scene);

    bool running = true;
    Uint64 lastTime = SDL_GetTicks();

    while (running) {
        Uint64 now = SDL_GetTicks();
        float dt = (now - lastTime) / 1000.0f;
        lastTime = now;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;

            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.scancode == SDL_SCANCODE_F1) Debug::toggle();
            }
        }

        scene->update(dt);

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        scene->render();
        Debug::drawColliders(*scene);
        SDL_RenderPresent(renderer);
    }

    if (musicTrack) MIX_DestroyTrack(musicTrack);
    if (music) MIX_DestroyAudio(music);
    if (mixer) MIX_DestroyMixer(mixer);
    MIX_Quit();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

/*
 * File: snd-sdl.c
 * Purpose: SDL sound support
 *
 * Copyright (c) 2004 Brendon Oliver <brendon.oliver@gmail.com>
 * Copyright (c) 2007 Andi Sidwell <andi@takkaria.org>
 * Copyright (c) 2016 Graeme Russ <graeme.russ@gmail.com>
 * Copyright (c) 2021 MAngband and PWMAngband Developers
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#include "c-angband.h"

#ifdef USE_SDL
# ifdef WINDOWS
#  include "..\_SDL\SDL.h"
#  include "..\_SDL\SDL_mixer.h"
# else
#  include <SDL/SDL.h>
#  include <SDL/SDL_mixer.h>
# endif


/* Supported file types */
enum
{
    SDL_NULL = 0,
    SDL_CHUNK,
    SDL_MUSIC
};


static const struct sound_file_type supported_sound_files[] =
{
    {".ogg", SDL_CHUNK},
    {".mp3", SDL_MUSIC},
    {"", SDL_NULL}
};


/*
 * Struct representing all data about an event sample
 */
typedef struct
{
    union
    {
        Mix_Chunk *chunk;   /* Sample in WAVE format */
        Mix_Music *music;   /* Sample in MP3 format */
    } sample_data;
    int sample_type;
} sdl_sample;


static bool use_init = false;


struct sound_config* get_sound_config() {
    static struct sound_config config;
    return &config;
}


int set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;

    /* Set all channels to volume */
    Mix_Volume(-1, (volume * MIX_MAX_VOLUME) / 100);
    return volume;
}


/*
 * Initialize SDL and open the mixer.
 */
static bool open_audio_sdl(void)
{
    int audio_rate;
    Uint16 audio_format;
    int audio_channels;
    int volume;

    /* Initialize variables */
    audio_rate = 22050;
    audio_format = AUDIO_S16;
    audio_channels = 2;
    volume = get_sound_config()->volume;
    
    /* Initialize the SDL library */
    if (SDL_Init(SDL_INIT_AUDIO) < 0)
    {
        plog_fmt("Couldn't initialize SDL: %s", SDL_GetError());
        return false;
    }

    /* Try to open the audio */
    if (Mix_OpenAudio(audio_rate, audio_format, audio_channels, 4096) < 0)
    {
        plog_fmt("Couldn't open mixer: %s", SDL_GetError());
        return false;
    }

    /* Set initial volume */
    set_volume(volume);

    /* Success */
    return true;
}


/*
 * Load a sound from file.
 */
static bool load_sample_sdl(const char *filename, int file_type, sdl_sample *sample)
{
    switch (file_type)
    {
        case SDL_CHUNK:
        {
            if (!use_init)
            {
                Mix_Init(MIX_INIT_OGG);
                use_init = true;
            }

            sample->sample_data.chunk = Mix_LoadWAV(filename);
            if (sample->sample_data.chunk) return true;
            break;
        }

        case SDL_MUSIC:
        {
            if (sample->sample_data.music) Mix_FreeMusic(sample->sample_data.music);
            sample->sample_data.music = Mix_LoadMUS(filename);
            if (sample->sample_data.music) return true;
            break;
        }

        default:
            plog("Oops - Unsupported file type");
            break;
    }

    return false;
}


/*
 * Load a sound and return a pointer to the associated SDL Sound data
 * structure back to the core sound module.
 */
static bool load_sound_sdl(const char *filename, int file_type, struct sound_data *data)
{
    sdl_sample *sample = (sdl_sample *)(data->plat_data);

    if (!sample) sample = mem_zalloc(sizeof(*sample));

    /* Try and load the sample file */
    data->loaded = load_sample_sdl(filename, file_type, sample);

    if (data->loaded)
        sample->sample_type = file_type;
    else
    {
        mem_free(sample);
        sample = NULL;
    }

    data->plat_data = (void *)sample;

    return (NULL != sample);
}


/*
 * Play the sound stored in the provided SDL Sound data structure.
 */
static bool play_sound_sdl(struct sound_data *data)
{
    sdl_sample *sample = (sdl_sample *)(data->plat_data);

    if (sample)
    {
        switch (sample->sample_type)
        {
            case SDL_CHUNK:
            {
                /* If another sound is currently playing, stop it */
                /*Mix_HaltChannel(-1);*/

                if (sample->sample_data.chunk)
                    return (0 == Mix_PlayChannel(-1, sample->sample_data.chunk, 0));
                break;
            }

            case SDL_MUSIC:
            {
                /* Hack -- force reload next time a sound is played */
                data->loaded = false;

                if (sample->sample_data.music)
                    return (0 == Mix_PlayMusic(sample->sample_data.music, 1));
                break;
            }

            default: break;
        }
    }

    return false;
}


/*
 * Free resources referenced in the provided SDL Sound data structure.
 */
static bool unload_sound_sdl(struct sound_data *data)
{
    sdl_sample *sample = (sdl_sample *)(data->plat_data);

    if (sample)
    {
        switch (sample->sample_type)
        {
            case SDL_CHUNK:
            {
                if (sample->sample_data.chunk)
                    Mix_FreeChunk(sample->sample_data.chunk);
                break;
            }

            case SDL_MUSIC:
            {
                if (sample->sample_data.music)
                    Mix_FreeMusic(sample->sample_data.music);
                break;
            }

            default: break;
        }

        mem_free(sample);
        data->plat_data = NULL;
        data->loaded = false;
    }

    return true;
}


/*
 * Shut down the SDL sound module and free resources.
 */
static bool close_audio_sdl(void)
{
    if (use_init) Mix_Quit();

    /*
     * Close the audio
     *
     * NOTE: All samples will have been free'd by the sound subsystem
     * calling unload_sound_sdl() for every sample that was loaded.
     */
    Mix_CloseAudio();

    /* XXX This may conflict with the SDL port */
    SDL_Quit();

    return true;
}


const struct sound_file_type *supported_files_sdl(void)
{
    return supported_sound_files;
}


/*
 * Init the SDL sound "module".
 */
errr init_sound_sdl(struct sound_hooks *hooks)
{
    hooks->open_audio_hook = open_audio_sdl;
    hooks->supported_files_hook = supported_files_sdl;
    hooks->close_audio_hook = close_audio_sdl;
    hooks->load_sound_hook = load_sound_sdl;
    hooks->unload_sound_hook = unload_sound_sdl;
    hooks->play_sound_hook = play_sound_sdl;

    /* Success */
    return (0);
}


#endif /* USE_SDL */

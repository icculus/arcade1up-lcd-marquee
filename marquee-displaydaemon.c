/**
 * arcade1up-lcd-marquee; control an LCD in a Arcade1Up marquee.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include <stdio.h>
#include "SDL.h"

#ifdef __linux__
#define USE_DBUS 1
#else
#define USE_DBUS 0
#endif

#if USE_DBUS
#include <dbus/dbus.h>
#endif

//#define STBI_SSE2 1

#if defined(__ARM_NEON) || (defined(__ARM_ARCH) && (__ARM_ARCH >= 8))  /* ARMv8 always has NEON. */
#define STBI_NEON 1
#endif

//#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ASSERT(x) SDL_assert(x)
#define STBI_MALLOC(x) SDL_malloc(x)
#define STBI_REALLOC(x,y) SDL_realloc(x, y)
#define STBI_FREE(x) SDL_free(x)
#include "stb_image.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"


static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;
static int texturew = 0;
static int textureh = 0;
static Uint32 fadems = 500;

#if USE_DBUS
static DBusConnection *dbus = NULL;
#endif

static void redraw_window(void)
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    if (texture) {  // fading in
        SDL_RenderSetLogicalSize(renderer, texturew, textureh);
        SDL_SetTextureAlphaMod(texture, 255);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
    }
    SDL_RenderPresent(renderer);
}

static void set_new_image(const char *fname)
{
    SDL_Texture *newtex = NULL;
    int w = 0;
    int h = 0;

    printf("Setting new image \"%s\"\n", fname);

    if (fname) {
        const char *ext = SDL_strrchr(fname, '.');
        if (ext && (SDL_strcasecmp(ext, ".svg") == 0)) {
            NSVGimage *image = image = nsvgParseFromFile(fname, "px", 96.0f);
            if (!image) {
                fprintf(stderr, "WARNING: couldn't load SVG image \"%s\"\n", fname);
            } else {
                NSVGrasterizer *rast = nsvgCreateRasterizer();
                if (!rast) {
                    fprintf(stderr, "WARNING: couldn't create SVG rasterizer for \"%s\"\n", fname);
                } else {
                    const int w = (int) image->width;
                    const int h = (int) image->height;
                    unsigned char *img = SDL_malloc(w * h * 4);
                    if (!img) {
                        fprintf(stderr, "WARNING: out of memory for \"%s\"\n", fname);
                    } else {
                        nsvgRasterize(rast, image, 0, 0, 1, img, w, h, w * 4);
                        newtex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                                   SDL_TEXTUREACCESS_STATIC, w, h);
                        if (!newtex) {
                            fprintf(stderr, "WARNING: couldn't create texture for \"%s\"\n", fname);
                        } else {
                            SDL_UpdateTexture(newtex, NULL, img, w * 4);
                            SDL_SetTextureBlendMode(newtex, SDL_BLENDMODE_BLEND);
                        }
                        SDL_free(img);
                    }
                    nsvgDeleteRasterizer(rast);
                }
	            nsvgDelete(image);
            }
        } else {
            int n;
            stbi_uc *img = stbi_load(fname, &w, &h, &n, 4);
            if (!img) {
                fprintf(stderr, "WARNING: couldn't load image \"%s\"\n", fname);
            } else {
                newtex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                           SDL_TEXTUREACCESS_STATIC, w, h);
                if (!newtex) {
                    fprintf(stderr, "WARNING: couldn't create texture for \"%s\"\n", fname);
                } else {
                    SDL_UpdateTexture(newtex, NULL, img, w * 4);
                    SDL_SetTextureBlendMode(newtex, SDL_BLENDMODE_BLEND);
                }
                stbi_image_free(img);
            }
        }
    }

    const Uint32 startms = SDL_GetTicks();
    const Uint32 timeout = startms + fadems;
    for (Uint32 now = startms; !SDL_TICKS_PASSED(now, timeout); now = SDL_GetTicks()) {
        const float unclamped_percent = ((float) (now - startms)) / ((float) fadems);
        const float percent = SDL_max(0.0f, SDL_min(unclamped_percent, 1.0f));

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        if (texture) {  // fading out
            SDL_RenderSetLogicalSize(renderer, texturew, textureh);
            SDL_SetTextureAlphaMod(texture, (Uint8) (255.0f * (1.0f - percent)));
            SDL_RenderCopy(renderer, texture, NULL, NULL);
        }

        if (newtex) {  // fading in
            SDL_RenderSetLogicalSize(renderer, w, h);
            SDL_SetTextureAlphaMod(newtex, (Uint8) (255.0f * percent));
            SDL_RenderCopy(renderer, newtex, NULL, NULL);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(10);
    }

    SDL_Texture *destroyme = texture;
    texture = newtex;
    texturew = w;
    textureh = h;

    // one last time, with no fade at all.
    redraw_window();

    if (destroyme) {
        SDL_DestroyTexture(destroyme);
    }
}

static void deinitialize(void)
{
    #if USE_DBUS
    if (dbus) {
        dbus_connection_unref(dbus);
        dbus = NULL;
    }
    #endif

    if (texture) {
        SDL_DestroyTexture(texture);
        texture = NULL;
    }

    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }

    if (window) {
        SDL_DestroyWindow(window);
        window = NULL;
    }

    SDL_Quit();
}

static SDL_bool initialize(const int argc, char **argv)
{
    int displayidx = 1;   // presumably a good default for our use case.
    const char *initial_image = NULL;
    Uint32 window_flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
    int width = 800;
    int height = 480;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (SDL_strcmp(arg, "--display") == 0) {
            displayidx = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(arg, "--width") == 0) {
            width = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(arg, "--height") == 0) {
            height = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(arg, "--fadems") == 0) {
            fadems = (Uint32) SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(arg, "--windowed") == 0) {
            window_flags &= ~SDL_WINDOW_FULLSCREEN_DESKTOP;
        } else if (SDL_strcmp(arg, "--fullscreen") == 0) {
            window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        } else if (SDL_strcmp(arg, "--startimage") == 0) {
            initial_image = argv[++i];
        } else {
            fprintf(stderr, "WARNING: Ignoring unknown command line option \"%s\"\n", arg);
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "ERROR! SDL_Init(SDL_INIT_VIDEO) failed: %s\n", SDL_GetError());
        return SDL_FALSE;
    }

    const char *driver = SDL_GetCurrentVideoDriver();
    const SDL_bool isRpi = (SDL_strcasecmp(driver, "rpi") == 0);
    if (!isRpi) {
        fprintf(stderr,
            "WARNING: you aren't using SDL's \"rpi\" video target.\n"
            "WARNING:  (you are using \"%s\" instead.)\n"
            "WARNING: This is probably _not_ what you wanted to do!\n",
                driver);
    }

    const int numdpy = SDL_GetNumVideoDisplays();
    if (numdpy <= displayidx) {
        if (isRpi) {
            fprintf(stderr,
                "ERROR: We want display index %d, but there are only %d displays.\n"
                "ERROR: So as not to hijack the wrong display, we are aborting now.\n",
                    displayidx, numdpy);
            return SDL_FALSE;
        } else {
            const int replacement = numdpy - 1;
            fprintf(stderr,
                "WARNING: We want display index %d, but there are only %d displays.\n"
                "WARNING: Choosing index %d instead.\n"
                "WARNING: This is probably _not_ what you wanted to do!\n",
                    displayidx, numdpy, replacement);
                displayidx = replacement;
        }
    }

    window = SDL_CreateWindow("Arcade1Up LCD Marquee",
                              SDL_WINDOWPOS_UNDEFINED_DISPLAY(displayidx),
                              SDL_WINDOWPOS_UNDEFINED_DISPLAY(displayidx),
                              width, height, window_flags);
    if (!window) {
        fprintf(stderr, "ERROR! SDL_CreateWindow failed: %s\n", SDL_GetError());
        return SDL_FALSE;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "WARNING! SDL_CreateRenderer(accel|vsync) failed: %s\n", SDL_GetError());
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer) {
            fprintf(stderr, "WARNING! SDL_CreateRenderer(accel) failed: %s\n", SDL_GetError());
            renderer = SDL_CreateRenderer(window, -1, 0);
            if (!renderer) {
                fprintf(stderr, "ERROR! SDL_CreateRenderer(0) failed: %s\n", SDL_GetError());
                fprintf(stderr, "Giving up.\n");
                return SDL_FALSE;
            }
        }
    }

    #if 0
    SDL_RendererInfo info;
    SDL_zero(info);
    SDL_GetRendererInfo(renderer, &info);
    printf("SDL renderer target: %s\n", info.name);
    #endif

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    /* on some systems, your window doesn't show up until the event queue gets pumped. */
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT)
            return SDL_FALSE;
    }

    set_new_image(initial_image);

    // if d-bus fails, we carry on, with at least a default image showing.
    #if USE_DBUS
    DBusError err;
    dbus_error_init(&err);
    dbus = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "ERROR: Can't connect to system D-Bus: %s\n", err.message);
        dbus_error_free(&err);
        dbus = NULL;
    } else if (dbus == NULL) {
        fprintf(stderr, "ERROR: Can't connect to system D-Bus\n");
    } else {
        const int rc = dbus_bus_request_name(dbus, "org.icculus.Arcade1UpMarquee", DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
        if (dbus_error_is_set(&err)) {
            fprintf(stderr, "ERROR: Couldn't acquire D-Bus service name: %s\n", err.message);
            dbus_error_free(&err);
            dbus_connection_unref(dbus);
            dbus = NULL;
        } else if (rc != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
            fprintf(stderr, "ERROR: Not the primary owner of the D-Bus service name (%d)\n", rc);
            dbus_connection_unref(dbus);
            dbus = NULL;
        }

        if (dbus) {
            dbus_bus_add_match(dbus, "type='signal',interface='org.icculus.Arcade1UpMarquee'", &err);
            dbus_connection_flush(dbus);
            if (dbus_error_is_set(&err)) {
                fprintf(stderr, "ERROR: Can't match on D-Bus interface name (%d)\n", rc);
                dbus_connection_unref(dbus);
                dbus = NULL;
            }
        }
    }
    #endif

    return SDL_TRUE;
}

static SDL_bool iterate(void)
{
    SDL_bool redraw = SDL_FALSE;
    SDL_bool saw_event = SDL_FALSE;
    char *newimage = NULL;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        saw_event = SDL_TRUE;
        if (e.type == SDL_QUIT) {
            printf("Got SDL_QUIT event, quitting now...\n");
            return SDL_FALSE;
        } else if (e.type == SDL_WINDOWEVENT) {
            if ( (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) ||
                 (e.window.event == SDL_WINDOWEVENT_EXPOSED) ) {
                redraw = SDL_TRUE;
            }
        /* you aren't going to get a DROPFILE event on the Pi, but this is
           useful for testing when building on a desktop system. */
        } else if (e.type == SDL_DROPFILE) {
            SDL_free(newimage);
            newimage = e.drop.file;
        }
    }

    #if USE_DBUS
    if (dbus) {
        dbus_connection_read_write(dbus, 0);
        DBusMessage *msg;
        while ((msg = dbus_connection_pop_message(dbus)) != NULL) {
            if (dbus_message_is_signal(msg, "org.icculus.Arcade1UpMarquee", "ShowImage")) {
                saw_event = SDL_TRUE;
                DBusMessageIter args;
                if ( dbus_message_iter_init(msg, &args) &&
                     (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING) ) {
                     char *param = NULL;
                     dbus_message_iter_get_basic(&args, &param);
                     printf("Got D-Bus request to show image \"%s\"\n", param);
                     SDL_free(newimage);
                     newimage = SDL_strdup(param);
                }
            }
            dbus_message_unref(msg);
        }
    }
    #endif

    if (!saw_event) {
        SDL_Delay(100);
    } else if (newimage) {
        set_new_image(newimage);
        SDL_free(newimage);
    } else if (redraw) {
        redraw_window();
    }

    return SDL_TRUE;
}

int main(int argc, char **argv)
{
    if (!initialize(argc, argv)) {
        deinitialize();
        return 1;
    }

    while (iterate()) {
        // spin.
    }

    deinitialize();
    return 0;
}

// end of marquee-displaydaemon.c ...


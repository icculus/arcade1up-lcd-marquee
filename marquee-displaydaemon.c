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
#define USE_LIBEVDEV 1
#else
#define USE_DBUS 0
#define USE_LIBEVDEV 0
#endif

#if USE_DBUS
#include <dbus/dbus.h>
#endif

#if USE_LIBEVDEV
#include <libevdev/libevdev-uinput.h>
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
static int screenw = 0;
static int screenh = 0;
static Uint32 fadems = 500;

#if USE_DBUS
static DBusConnection *dbus = NULL;
#endif


static int fingers_down = 0;

// virtual mouse state
static SDL_bool motion_finger_down = SDL_FALSE;
static SDL_FingerID motion_finger = 0;
static SDL_bool button_finger_down = SDL_FALSE;
static SDL_FingerID button_finger = 0;
#if USE_LIBEVDEV
static struct libevdev *evdev_mouse = NULL;
static struct libevdev_uinput *uidev_mouse = NULL;
#endif

// virtual keyboard state
typedef struct
{
    unsigned int scancode;
    SDL_Rect rect;
} virtkey;
typedef struct
{
    SDL_bool pressed;
    SDL_FingerID finger;
    int keyindex;
} keypress;
static SDL_bool keyboard_slide_cooldown = SDL_FALSE;
static float keyboard_slide_percent = 0.0f;
static int keyboardw = 0;
static int keyboardh = 0;
static SDL_Texture *keyboard_texture = NULL;
static virtkey keyinfo[64];
static keypress pressed_keys[10];
static Uint32 keyboard_slide_ms = 500;
#if USE_LIBEVDEV
static struct libevdev *evdev_keyboard = NULL;
static struct libevdev_uinput *uidev_keyboard = NULL;
#endif

static void (*handle_fingerdown)(const SDL_TouchFingerEvent *e) = NULL;
static void (*handle_fingerup)(const SDL_TouchFingerEvent *e) = NULL;
static void (*handle_fingermotion)(const SDL_TouchFingerEvent *e) = NULL;
static void (*handle_redraw)(void) = NULL;

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
    SDL_RenderSetLogicalSize(renderer, screenw, screenh);

    if (handle_redraw) {
        handle_redraw();
    }

    SDL_RenderPresent(renderer);
}


static void handle_fingerdown_keyboard(const SDL_TouchFingerEvent *e);
static void handle_fingerup_keyboard(const SDL_TouchFingerEvent *e);
static void handle_fingermotion_keyboard(const SDL_TouchFingerEvent *e);
static void handle_redraw_keyboard(void);
static void handle_fingerdown_mouse(const SDL_TouchFingerEvent *e);
static void handle_fingerup_mouse(const SDL_TouchFingerEvent *e);
static void handle_fingermotion_mouse(const SDL_TouchFingerEvent *e);


static SDL_Texture *load_image(const char *fname, int *_w, int *_h)
{
    SDL_Texture *newtex = NULL;

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
                    *_w = w;
                    *_h = h;
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
            stbi_uc *img = stbi_load(fname, _w, _h, &n, 4);
            if (!img) {
                fprintf(stderr, "WARNING: couldn't load image \"%s\"\n", fname);
            } else {
                newtex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                           SDL_TEXTUREACCESS_STATIC, *_w, *_h);
                if (!newtex) {
                    fprintf(stderr, "WARNING: couldn't create texture for \"%s\"\n", fname);
                } else {
                    SDL_UpdateTexture(newtex, NULL, img, *_w * 4);
                    SDL_SetTextureBlendMode(newtex, SDL_BLENDMODE_BLEND);
                }
                stbi_image_free(img);
            }
        }
    }
    return newtex;
}


static void set_new_image(const char *fname)
{
    int w = 0;
    int h = 0;

    printf("Setting new image \"%s\"\n", fname);

    SDL_Texture *newtex = load_image(fname, &w, &h);
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

        // !!! FIXME: move this loop to state variables and make it part of
        // !!! FIXME:  redraw_window() instead of shoving a handle_redraw
        // !!! FIXME:  call in here.
        if (handle_redraw) {
            handle_redraw();
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

static void slide_in_keyboard(void)
{
    if (!keyboard_texture) {
        return;  // no keyboard for you.
    }

    //printf("Sliding in keyboard!\n");

    // Send keyup events for anything that fingers are still touching.
    #if USE_LIBEVDEV
    if (button_finger_down && uidev_mouse) {
        libevdev_uinput_write_event(uidev_mouse, EV_KEY, BTN_LEFT, 0);
        libevdev_uinput_write_event(uidev_mouse, EV_SYN, SYN_REPORT, 0);
    }
    #endif
    motion_finger_down = SDL_FALSE;
    motion_finger = 0;
    button_finger_down = SDL_FALSE;
    button_finger = 0;

    keyboard_slide_cooldown = SDL_TRUE;
    handle_fingerup = handle_fingerup_keyboard;
    handle_fingerdown = handle_fingerdown_keyboard;
    handle_fingermotion = handle_fingermotion_keyboard;
    handle_redraw = handle_redraw_keyboard;
    keyboard_slide_percent = 0.0f;

    const Uint32 startms = SDL_GetTicks();
    const Uint32 timeout = startms + keyboard_slide_ms;
    for (Uint32 now = startms; !SDL_TICKS_PASSED(now, timeout); now = SDL_GetTicks()) {
        const float unclamped_percent = ((float) (now - startms)) / ((float) keyboard_slide_ms);
        keyboard_slide_percent = SDL_max(0.0f, SDL_min(unclamped_percent, 1.0f));
        redraw_window();
    }

    keyboard_slide_percent = 1.0f;
    redraw_window();
}

static void slide_out_keyboard(void)
{
    //printf("Sliding out keyboard!\n");

    // Send keyup events for anything that fingers are still touching.
    for (int i = 0; i < SDL_arraysize(pressed_keys); i++) {
        if (pressed_keys[i].pressed) {
            //printf("Released virtual keyboard key %u\n", keyinfo[pressed_keys[i].keyindex].scancode);
            pressed_keys[i].pressed = SDL_FALSE;

            #if USE_LIBEVDEV
            if (uidev_keyboard) {
                libevdev_uinput_write_event(uidev_keyboard, EV_KEY, keyinfo[pressed_keys[i].keyindex].scancode, 0);
                libevdev_uinput_write_event(uidev_keyboard, EV_SYN, SYN_REPORT, 0);
            }
            #endif
        }
    }

    keyboard_slide_cooldown = SDL_TRUE;
    handle_fingerup = handle_fingerup_mouse;
    handle_fingerdown = handle_fingerdown_mouse;
    handle_fingermotion = handle_fingermotion_mouse;
    handle_redraw = handle_redraw_keyboard;
    keyboard_slide_percent = 0.0f;

    const Uint32 startms = SDL_GetTicks();
    const Uint32 timeout = startms + keyboard_slide_ms;
    for (Uint32 now = startms; !SDL_TICKS_PASSED(now, timeout); now = SDL_GetTicks()) {
        const float unclamped_percent = ((float) (now - startms)) / ((float) keyboard_slide_ms);
        keyboard_slide_percent = 1.0f - SDL_max(0.0f, SDL_min(unclamped_percent, 1.0f));
        redraw_window();
    }

    keyboard_slide_percent = 0.0f;
    redraw_window();
    handle_redraw = NULL;
}


static void handle_fingerdown_mouse(const SDL_TouchFingerEvent *e)
{
    if (fingers_down == 4) {
        slide_in_keyboard();
    } else if (!motion_finger_down) {
        //printf("FINGERDOWN: This is the motion finger.\n");
        motion_finger = e->fingerId;
        motion_finger_down = SDL_TRUE;
    } else if (!button_finger_down) {
        //printf("FINGERDOWN: This is the button finger.\n");
        button_finger = e->fingerId;
        button_finger_down = SDL_TRUE;
        #if USE_LIBEVDEV
        if (uidev_mouse) {
            libevdev_uinput_write_event(uidev_mouse, EV_KEY, BTN_LEFT, 1);
            libevdev_uinput_write_event(uidev_mouse, EV_SYN, SYN_REPORT, 0);
        }
        #endif
    }
}

static void handle_fingerup_mouse(const SDL_TouchFingerEvent *e)
{
    if (motion_finger_down && (e->fingerId == motion_finger)) {
        //printf("FINGERUP: This is the motion finger.\n");
        motion_finger_down = SDL_FALSE;
        motion_finger = 0;
    } else if (button_finger_down && (e->fingerId == button_finger)) {
        //printf("FINGERUP: This is the button finger.\n");
        button_finger_down = SDL_FALSE;
        button_finger = 0;
        #if USE_LIBEVDEV
        if (uidev_mouse) {
            libevdev_uinput_write_event(uidev_mouse, EV_KEY, BTN_LEFT, 0);
            libevdev_uinput_write_event(uidev_mouse, EV_SYN, SYN_REPORT, 0);
        }
        #endif
    }
}

static void handle_fingermotion_mouse(const SDL_TouchFingerEvent *e)
{
    if (!motion_finger_down || (e->fingerId != motion_finger)) {
        return;
    }

    #if USE_LIBEVDEV
    if (uidev_mouse) {
        SDL_bool hasdata = SDL_FALSE;
            if (e->dx != 0.0f) {
                hasdata = SDL_TRUE;
                const int val = (int) (((float) screenw) * e->dx);
                //printf("X FINGERMOTION: %d\n", val);
                libevdev_uinput_write_event(uidev_mouse, EV_REL, REL_X, val);
            }
            if (e->dy != 0.0f) {
                hasdata = SDL_TRUE;
                const int val = (int) (((float) screenh) * e->dy);
                //printf("Y FINGERMOTION: %d\n", val);
                libevdev_uinput_write_event(uidev_mouse, EV_REL, REL_Y, val);
            }
            if (hasdata) {
                libevdev_uinput_write_event(uidev_mouse, EV_SYN, SYN_REPORT, 0);
            }
    }
    #endif
}

static void handle_fingerdown_keyboard(const SDL_TouchFingerEvent *e)
{
    if (fingers_down == 4) {
        slide_out_keyboard();
    } else {
        const int x = (int) (((float) screenw) * e->x);
        const int y = (int) (((float) screenh) * e->y);
        const SDL_Point pt = { x, y };
        int keyindex = -1;

        // find the key we're touching
        for (int i = 0; i < SDL_arraysize(keyinfo); i++) {
            if (SDL_PointInRect(&pt, &keyinfo[i].rect)) {
                keyindex = i;
                break;
            }
        }

        if (keyindex == -1) {
            return;  // not touching a key
        }

        int pressedindex = -1;
        for (int i = 0; i < SDL_arraysize(pressed_keys); i++) {
            if (!pressed_keys[i].pressed) {
                pressedindex = i;
            } else if (pressed_keys[i].keyindex == keyindex) {
                return; // already a finger on this key, ignore it.
            }
        }

        if (pressedindex == -1) {
            return;  // no open slots?!?!
        }

        pressed_keys[pressedindex].pressed = SDL_TRUE;
        pressed_keys[pressedindex].finger = e->fingerId;
        pressed_keys[pressedindex].keyindex = keyindex;

        //printf("Pressed virtual keyboard key %u\n", keyinfo[keyindex].scancode);

        #if USE_LIBEVDEV
        if (uidev_keyboard) {
            libevdev_uinput_write_event(uidev_keyboard, EV_KEY, keyinfo[keyindex].scancode, 1);
            libevdev_uinput_write_event(uidev_keyboard, EV_SYN, SYN_REPORT, 0);
        }
        #endif

        redraw_window();
    }
}

static void handle_fingerup_keyboard(const SDL_TouchFingerEvent *e)
{
    for (int i = 0; i < SDL_arraysize(pressed_keys); i++) {
        if (pressed_keys[i].pressed && (pressed_keys[i].finger == e->fingerId)) {
            //printf("Released virtual keyboard key %u\n", keyinfo[pressed_keys[i].keyindex].scancode);
            pressed_keys[i].pressed = SDL_FALSE;

            #if USE_LIBEVDEV
            if (uidev_keyboard) {
                libevdev_uinput_write_event(uidev_keyboard, EV_KEY, keyinfo[pressed_keys[i].keyindex].scancode, 0);
                libevdev_uinput_write_event(uidev_keyboard, EV_SYN, SYN_REPORT, 0);
            }
            #endif

            redraw_window();
            return;
        }
    }
}

static void handle_fingermotion_keyboard(const SDL_TouchFingerEvent *e)
{
    // does nothing right now.
}

static void handle_redraw_keyboard(void)
{
    const int w = screenw;
    const int h = screenh;
    const int y = screenh - ((int) (((float) h) * keyboard_slide_percent));
    const SDL_Rect dst = { 0, y, w, keyboardh };
    //SDL_SetTextureAlphaMod(keyboard_texture, 255.0f * keyboard_slide_percent);
    SDL_RenderCopy(renderer, keyboard_texture, NULL, &dst);

    SDL_SetRenderDrawColor(renderer, 175, 0, 0, 90);
    for (int i = 0; i < SDL_arraysize(pressed_keys); i++) {
        if (pressed_keys[i].pressed) {
            SDL_RenderFillRect(renderer, &keyinfo[pressed_keys[i].keyindex].rect);
        }
    }
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 0);
}


static SDL_bool iterate(void)
{
    SDL_bool redraw = SDL_FALSE;
    SDL_bool saw_event = SDL_FALSE;
    char *newimage = NULL;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        saw_event = SDL_TRUE;
        switch (e.type) {
            case SDL_FINGERDOWN:
                fingers_down++;
                //printf("FINGER DOWN! We now have %d fingers\n", fingers_down);
                if (!keyboard_slide_cooldown) {
                    handle_fingerdown(&e.tfinger);
                }
                break;

            case SDL_FINGERUP:
                fingers_down--;
                //printf("FINGER UP! We now have %d fingers\n", fingers_down);
                if (!keyboard_slide_cooldown) {
                    handle_fingerup(&e.tfinger);
                } else if (!fingers_down) {
                    keyboard_slide_cooldown = SDL_FALSE;
                }
                break;

            case SDL_FINGERMOTION:
                if (!keyboard_slide_cooldown) {
                    handle_fingermotion(&e.tfinger);
                }
                break;

            case SDL_QUIT:
                printf("Got SDL_QUIT event, quitting now...\n");
                return SDL_FALSE;

            case SDL_WINDOWEVENT:
                if ( (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) ||
                     (e.window.event == SDL_WINDOWEVENT_EXPOSED) ) {
                    redraw = SDL_TRUE;
                }
                break;

            case SDL_DROPFILE:
                /* you aren't going to get a DROPFILE event on the Pi, but this
                   is useful for testing when building on a desktop system. */
                SDL_free(newimage);
                newimage = e.drop.file;
                break;

            default: break;
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
                     //printf("Got D-Bus request to show image \"%s\"\n", param);
                     SDL_free(newimage);
                     newimage = SDL_strdup(param);
                }
            }
            dbus_message_unref(msg);
        }
    }
    #endif

    if (!saw_event && !fingers_down) {
        SDL_Delay(100);
    } else if (newimage) {
        set_new_image(newimage);
        SDL_free(newimage);
    } else if (redraw) {
        redraw_window();
    }

    return SDL_TRUE;
}

static void deinitialize(void)
{
    #if USE_DBUS
    if (dbus) {
        dbus_connection_unref(dbus);
        dbus = NULL;
    }
    #endif

    #if USE_LIBEVDEV
    if (uidev_mouse) {
        libevdev_uinput_destroy(uidev_mouse);
        uidev_mouse = NULL;
    }

    if (evdev_mouse) {
        libevdev_free(evdev_mouse);
        evdev_mouse = NULL;
    }

    if (uidev_keyboard) {
        libevdev_uinput_destroy(uidev_keyboard);
        uidev_keyboard = NULL;
    }

    if (evdev_keyboard) {
        libevdev_free(evdev_keyboard);
        evdev_keyboard = NULL;
    }
    #endif

    if (texture) {
        SDL_DestroyTexture(texture);
        texture = NULL;
    }

    if (keyboard_texture) {
        SDL_DestroyTexture(keyboard_texture);
        keyboard_texture = NULL;
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

static SDL_Texture *build_keyboard_texture(void)
{
    SDL_Texture *tex = load_image("/home/pi/arcade1up-lcd-marquee/keyboard-en.png", &keyboardw, &keyboardh);  // !!! FIXME: hardcoded
    if (!tex) {
        return NULL;
    }

    int posx = 4;
    int posy = 4;
    int keyw = 52;
    int keyh = 52;
    virtkey *k = keyinfo;
    #define ADDKEY(sc, x, y, w, h) { \
        SDL_assert((k - keyinfo) < SDL_arraysize(keyinfo)); \
        const SDL_Rect r = { x, y, w, h }; \
        SDL_memcpy(&k->rect, &r, sizeof (SDL_Rect)); \
        k->scancode = KEY_##sc; \
        k++; \
        posx += (w + 1); \
    }

    // !!! FIXME: hardcoded mess
    ADDKEY(GRAVE, posx, posy, keyw, keyh);
    ADDKEY(1, posx, posy, keyw, keyh);
    ADDKEY(2, posx, posy, keyw, keyh);
    ADDKEY(3, posx, posy, keyw, keyh);
    ADDKEY(4, posx, posy, keyw, keyh);
    ADDKEY(5, posx, posy, keyw, keyh);
    ADDKEY(6, posx, posy, keyw, keyh);
    ADDKEY(7, posx, posy, keyw, keyh);
    ADDKEY(8, posx, posy, keyw, keyh);
    ADDKEY(9, posx, posy, keyw, keyh);
    ADDKEY(0, posx, posy, keyw, keyh);
    ADDKEY(MINUS, posx, posy, keyw, keyh);
    ADDKEY(EQUAL, posx, posy, keyw, keyh);
    ADDKEY(BACKSPACE, posx, posy, 105, keyh);

    posx = 4;
    posy += (keyh + 1);
    ADDKEY(TAB, posx, posy, 78, keyh);
    ADDKEY(Q, posx, posy, keyw, keyh);
    ADDKEY(W, posx, posy, keyw, keyh);
    ADDKEY(E, posx, posy, keyw, keyh);
    ADDKEY(R, posx, posy, keyw, keyh);
    ADDKEY(T, posx, posy, keyw, keyh);
    ADDKEY(Y, posx, posy, keyw, keyh);
    ADDKEY(U, posx, posy, keyw, keyh);
    ADDKEY(I, posx, posy, keyw, keyh);
    ADDKEY(O, posx, posy, keyw, keyh);
    ADDKEY(P, posx, posy, keyw, keyh);
    ADDKEY(LEFTBRACE, posx, posy, keyw, keyh);
    ADDKEY(RIGHTBRACE, posx, posy, keyw, keyh);
    ADDKEY(BACKSLASH, posx, posy, 78, keyh);

    posx = 4;
    posy += (keyh + 1);
    ADDKEY(CAPSLOCK, posx, posy, 91, keyh);
    ADDKEY(A, posx, posy, keyw, keyh);
    ADDKEY(S, posx, posy, keyw, keyh);
    ADDKEY(D, posx, posy, keyw, keyh);
    ADDKEY(F, posx, posy, keyw, keyh);
    ADDKEY(G, posx, posy, keyw, keyh);
    ADDKEY(H, posx, posy, keyw, keyh);
    ADDKEY(J, posx, posy, keyw, keyh);
    ADDKEY(K, posx, posy, keyw, keyh);
    ADDKEY(L, posx, posy, keyw, keyh);
    ADDKEY(SEMICOLON, posx, posy, keyw, keyh);
    ADDKEY(APOSTROPHE, posx, posy, keyw, keyh);
    ADDKEY(ENTER, posx, posy, 118, keyh);

    posx = 4;
    posy += (keyh + 1);
    ADDKEY(LEFTSHIFT, posx, posy, 118, keyh);
    ADDKEY(Z, posx, posy, keyw, keyh);
    ADDKEY(X, posx, posy, keyw, keyh);
    ADDKEY(C, posx, posy, keyw, keyh);
    ADDKEY(V, posx, posy, keyw, keyh);
    ADDKEY(B, posx, posy, keyw, keyh);
    ADDKEY(N, posx, posy, keyw, keyh);
    ADDKEY(M, posx, posy, keyw, keyh);
    ADDKEY(COMMA, posx, posy, keyw, keyh);
    ADDKEY(DOT, posx, posy, keyw, keyh);
    ADDKEY(SLASH, posx, posy, keyw, keyh);
    ADDKEY(RIGHTSHIFT, posx, posy, 145, keyh);

    posx = 4;
    posy += (keyh + 1);
    ADDKEY(LEFTCTRL, posx, posy, 78, keyh);
    ADDKEY(LEFTMETA, posx, posy, 65, keyh);
    ADDKEY(LEFTALT, posx, posy, 65, keyh);
    ADDKEY(SPACE, posx, posy, 304, keyh);
    ADDKEY(RIGHTALT, posx, posy, 65, keyh);
    ADDKEY(RIGHTMETA, posx, posy, 65, keyh);
    ADDKEY(RIGHTCTRL, posx, posy, 78, keyh);

    #undef ADDKEY

    return tex;
}


static SDL_bool initialize(const int argc, char **argv)
{
    // make sure static vars are sane.
    fingers_down = 0;
    motion_finger_down = SDL_FALSE;
    motion_finger = 0;
    button_finger_down = SDL_FALSE;
    button_finger = 0;
    keyboard_slide_cooldown = SDL_FALSE;
    keyboard_slide_percent = 0.0f;
    SDL_zero(keyinfo);
    SDL_zero(pressed_keys);
    handle_fingerdown = handle_fingerdown_mouse;
    handle_fingerup = handle_fingerup_mouse;
    handle_fingermotion = handle_fingermotion_mouse;
    handle_redraw = NULL;

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
        } else if (SDL_strcmp(arg, "--keyboardms") == 0) {
            keyboard_slide_ms = (Uint32) SDL_atoi(argv[++i]);
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

    SDL_GetWindowSize(window, &screenw, &screenh);

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

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
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
    keyboard_texture = build_keyboard_texture();

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

    #if USE_LIBEVDEV
    int rc;

    evdev_mouse = libevdev_new();
    libevdev_set_name(evdev_mouse, "Icculus's LCD Marquee Mouse");
    libevdev_enable_event_type(evdev_mouse, EV_REL);
    libevdev_enable_event_code(evdev_mouse, EV_REL, REL_X, NULL);
    libevdev_enable_event_code(evdev_mouse, EV_REL, REL_Y, NULL);
    libevdev_enable_event_type(evdev_mouse, EV_KEY);
    libevdev_enable_event_code(evdev_mouse, EV_KEY, BTN_LEFT, NULL);
    //libevdev_enable_event_code(evdev_mouse, EV_KEY, BTN_MIDDLE, NULL);
    //libevdev_enable_event_code(evdev_mouse, EV_KEY, BTN_RIGHT, NULL);
    rc = libevdev_uinput_create_from_device(evdev_mouse,
                                         LIBEVDEV_UINPUT_OPEN_MANAGED,
                                         &uidev_mouse);
    if (rc != 0) {
        fprintf(stderr, "WARNING: Couldn't set up uinput; no mouse emulation for you! (rc=%d)\n", rc);
        libevdev_free(evdev_mouse);
        evdev_mouse = NULL;
    }

    evdev_keyboard = libevdev_new();
    libevdev_set_name(evdev_keyboard, "Icculus's LCD Marquee Keyboard");
    libevdev_enable_event_type(evdev_keyboard, EV_KEY);
    // we don't send all of these, but this is easier than picking them all out and maintaining a list here.
    for (unsigned int i = KEY_ESC; i < KEY_WIMAX; i++) {
        libevdev_enable_event_code(evdev_keyboard, EV_KEY, i, NULL);
    }
    rc = libevdev_uinput_create_from_device(evdev_keyboard,
                                         LIBEVDEV_UINPUT_OPEN_MANAGED,
                                         &uidev_keyboard);
    if (rc != 0) {
        fprintf(stderr, "WARNING: Couldn't set up uinput; no keyboard emulation for you! (rc=%d)\n", rc);
        libevdev_free(evdev_keyboard);
        evdev_keyboard = NULL;
    }
    #endif

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


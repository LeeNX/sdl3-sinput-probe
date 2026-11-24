/* sdl3probe -- what SDL3 sees of a gamepad: driver path, caps, sensors, touchpads, live events.
 *
 *   sdl3probe [--wait S] [--events S] [--rumble LO HI MS] [--led R G B] [--player N] [--no-hidapi] [--json]
 *
 * Prints one key=value line per fact so a test script can grep it, or with --json one JSON object per line:
 * {"fact": key, "value": v} for facts and {"ev": kind, "t": ms, "mono_ns": ns, ...} for events. A path starting
 * with /dev/hidraw means the HIDAPI driver (SInput here) owns the device; /dev/input/event* means SDL fell back to
 * evdev. "listening" marks the start of the event stream. SIGINT/SIGTERM end the stream early, still printing the
 * final.* facts.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int json = 0;

static const char *ctype(SDL_JoystickConnectionState s)
{
    switch (s) {
    case SDL_JOYSTICK_CONNECTION_WIRED: return "wired";
    case SDL_JOYSTICK_CONNECTION_WIRELESS: return "wireless";
    case SDL_JOYSTICK_CONNECTION_UNKNOWN: return "unknown";
    default: return "invalid";
    }
}

static const char *pstate(SDL_PowerState s)
{
    switch (s) {
    case SDL_POWERSTATE_ON_BATTERY: return "on_battery";
    case SDL_POWERSTATE_NO_BATTERY: return "no_battery";
    case SDL_POWERSTATE_CHARGING: return "charging";
    case SDL_POWERSTATE_CHARGED: return "charged";
    case SDL_POWERSTATE_UNKNOWN: return "unknown";
    default: return "error";
    }
}

/* A JSON string literal; NULL prints as "". */
static void jstr(const char *s)
{
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        if (*p == '"' || *p == '\\') printf("\\%c", *p);
        else if (*p < 0x20) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}

static void fact_s(const char *k, const char *v)
{
    if (!json) {
        printf("%s=%s\n", k, v ? v : "");
        return;
    }
    printf("{\"fact\":");
    jstr(k);
    printf(",\"value\":");
    jstr(v);
    printf("}\n");
}

static void fact_i(const char *k, long long v)
{
    if (json) {
        printf("{\"fact\":");
        jstr(k);
        printf(",\"value\":%lld}\n", v);
    } else {
        printf("%s=%lld\n", k, v);
    }
}

static void fact_hex(const char *k, unsigned v)
{
    char b[8];
    snprintf(b, sizeof b, "%04x", v);
    fact_s(k, b);
}

/* A space-separated list in text mode, a JSON array of strings with --json. */
static void fact_list(const char *k, const char **items, int n)
{
    if (json) {
        printf("{\"fact\":");
        jstr(k);
        printf(",\"value\":[");
        for (int i = 0; i < n; i++) {
            if (i) putchar(',');
            jstr(items[i]);
        }
        printf("]}\n");
    } else {
        printf("%s=", k);
        for (int i = 0; i < n; i++) printf("%s ", items[i]);
        putchar('\n');
    }
}

static Uint64 mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (Uint64)ts.tv_sec * 1000000000ull + (Uint64)ts.tv_nsec;
}

/* Starts an event line: SDL's own timestamp (ms since init, as before) and the same instant on CLOCK_MONOTONIC,
 * which other processes (a test script's time.monotonic_ns(), a second probe) share. The offset between the two
 * clocks is taken now, not once at start, so NTP slewing of CLOCK_MONOTONIC can't build up over a long run. */
static void ev_begin(const char *kind, Uint64 ts_ns)
{
    Uint64 now_ticks = SDL_GetTicksNS(), now_mono = mono_ns();
    Uint64 age = now_ticks > ts_ns ? now_ticks - ts_ns : 0;
    if (json) {
        printf("{\"ev\":");
        jstr(kind);
        printf(",\"t\":%llu,\"mono_ns\":%llu", (unsigned long long)(ts_ns / 1000000),
               (unsigned long long)(now_mono - age));
    } else {
        printf("ev t=%llu %s", (unsigned long long)(ts_ns / 1000000), kind);
    }
}

static void ev_end(void)
{
    puts(json ? "}" : "");
}

int main(int argc, char **argv)
{
    double wait_s = 5, events_s = 0;
    int rumble = 0, lo = 0, hi = 0, ms = 0, led = 0, r = 0, g = 0, b = 0, player = -1, hidapi = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--wait") && i + 1 < argc) wait_s = atof(argv[++i]);
        else if (!strcmp(argv[i], "--events") && i + 1 < argc) events_s = atof(argv[++i]);
        else if (!strcmp(argv[i], "--rumble") && i + 3 < argc) {
            rumble = 1; lo = atoi(argv[++i]); hi = atoi(argv[++i]); ms = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--led") && i + 3 < argc) {
            led = 1; r = atoi(argv[++i]); g = atoi(argv[++i]); b = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--player") && i + 1 < argc) player = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-hidapi")) hidapi = 0;
        else if (!strcmp(argv[i], "--json")) json = 1;
        else {
            fprintf(stderr, "usage: %s [--wait S] [--events S] [--rumble LO HI MS] [--led R G B] [--player N] "
                            "[--no-hidapi] [--json]\n", argv[0]);
            return 2;
        }
    }

    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, hidapi ? "1" : "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SINPUT, hidapi ? "1" : "0");
    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC | SDL_INIT_SENSOR)) {
        fact_s("error", SDL_GetError());
        return 1;
    }
    int v = SDL_GetVersion();
    char vbuf[32];
    snprintf(vbuf, sizeof vbuf, "%d.%d.%d", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
    fact_s("sdl_version", vbuf);
    fact_s("sdl_revision", SDL_GetRevision());
    fact_i("hidapi", hidapi);

    /* Joysticks show up as events after init; pump until one is a gamepad or the wait runs out. */
    SDL_Gamepad *pad = NULL;
    Uint64 end = SDL_GetTicks() + (Uint64)(wait_s * 1000);
    while (!pad && SDL_GetTicks() < end) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_GAMEPAD_ADDED && !pad) pad = SDL_OpenGamepad(ev.gdevice.which);
        }
        SDL_Delay(20);
    }

    int n = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&n);
    fact_i("joysticks", n);
    for (int i = 0; i < n; i++) {
        const char *jname = SDL_GetJoystickNameForID(ids[i]), *jpath = SDL_GetJoystickPathForID(ids[i]);
        const char *jtype = SDL_GetGamepadStringForType(SDL_GetGamepadTypeForID(ids[i]));
        if (json) {
            printf("{\"fact\":\"joystick\",\"value\":{\"index\":%d,\"name\":", i);
            jstr(jname);
            printf(",\"vid\":\"%04x\",\"pid\":\"%04x\",\"gamepad\":%d,\"type\":", SDL_GetJoystickVendorForID(ids[i]),
                   SDL_GetJoystickProductForID(ids[i]), SDL_IsGamepad(ids[i]));
            jstr(jtype);
            printf(",\"path\":");
            jstr(jpath);
            printf("}}\n");
        } else {
            printf("joystick.%d=%s vid=%04x pid=%04x gamepad=%d type=%s path=%s\n", i, jname,
                   SDL_GetJoystickVendorForID(ids[i]), SDL_GetJoystickProductForID(ids[i]), SDL_IsGamepad(ids[i]),
                   jtype, jpath);
        }
    }
    SDL_free(ids);
    if (!pad) {
        char e[64];
        snprintf(e, sizeof e, "no gamepad within %.1fs", wait_s);
        fact_s("error", e);
        SDL_Quit();
        return 1;
    }

    /* Let the first input reports land so the state below is the device's, not SDL's zeroed defaults. */
    Uint64 settle = SDL_GetTicks() + 500;
    while (SDL_GetTicks() < settle) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    const char *path = SDL_GetGamepadPath(pad);
    fact_s("name", SDL_GetGamepadName(pad));
    fact_s("path", path);
    fact_s("driver", path && !strncmp(path, "/dev/hidraw", 11) ? "hidapi" : "evdev");
    fact_hex("vid", SDL_GetGamepadVendor(pad));
    fact_hex("pid", SDL_GetGamepadProduct(pad));
    fact_hex("version", SDL_GetGamepadProductVersion(pad));
    fact_s("serial", SDL_GetGamepadSerial(pad));
    fact_s("type", SDL_GetGamepadStringForType(SDL_GetGamepadType(pad)));
    fact_s("real_type", SDL_GetGamepadStringForType(SDL_GetRealGamepadType(pad)));
    fact_s("connection", ctype(SDL_GetGamepadConnectionState(pad)));
    int pct = -1;
    SDL_PowerState ps = SDL_GetGamepadPowerInfo(pad, &pct);
    fact_s("power", pstate(ps));
    fact_i("battery", pct);

    SDL_PropertiesID props = SDL_GetGamepadProperties(pad);
    fact_i("cap.rumble", SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false));
    fact_i("cap.trigger_rumble", SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false));
    fact_i("cap.rgb_led", SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN, false));
    fact_i("cap.mono_led", SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_MONO_LED_BOOLEAN, false));
    fact_i("cap.player_led", SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_PLAYER_LED_BOOLEAN, false));

    const char *names[(int)SDL_GAMEPAD_BUTTON_COUNT + (int)SDL_GAMEPAD_AXIS_COUNT];
    int nn = 0;
    for (int bt = 0; bt < SDL_GAMEPAD_BUTTON_COUNT; bt++) {
        if (SDL_GamepadHasButton(pad, (SDL_GamepadButton)bt))
            names[nn++] = SDL_GetGamepadStringForButton((SDL_GamepadButton)bt);
    }
    fact_list("buttons", names, nn);
    nn = 0;
    for (int ax = 0; ax < SDL_GAMEPAD_AXIS_COUNT; ax++) {
        if (SDL_GamepadHasAxis(pad, (SDL_GamepadAxis)ax)) names[nn++] = SDL_GetGamepadStringForAxis((SDL_GamepadAxis)ax);
    }
    fact_list("axes", names, nn);
    fact_i("touchpads", SDL_GetNumGamepadTouchpads(pad));
    char key[64];
    for (int t = 0; t < SDL_GetNumGamepadTouchpads(pad); t++) {
        snprintf(key, sizeof key, "touchpad.%d.fingers", t);
        fact_i(key, SDL_GetNumGamepadTouchpadFingers(pad, t));
    }
    SDL_SensorType sensors[] = {SDL_SENSOR_ACCEL, SDL_SENSOR_GYRO};
    const char *sname[] = {"accel", "gyro"};
    for (int s = 0; s < 2; s++) {
        bool has = SDL_GamepadHasSensor(pad, sensors[s]);
        float rate = has ? SDL_GetGamepadSensorDataRate(pad, sensors[s]) : 0.0f;
        if (json) {
            printf("{\"fact\":\"sensor.%s\",\"value\":%d}\n{\"fact\":\"sensor.%s.rate\",\"value\":%.1f}\n", sname[s],
                   has, sname[s], rate);
        } else {
            printf("sensor.%s=%d rate=%.1f\n", sname[s], has, rate);
        }
        if (has && events_s > 0) SDL_SetGamepadSensorEnabled(pad, sensors[s], true);
    }
    for (int ax = 0; ax < SDL_GAMEPAD_AXIS_COUNT; ax++) {
        if (SDL_GamepadHasAxis(pad, (SDL_GamepadAxis)ax)) {
            snprintf(key, sizeof key, "state.%s", SDL_GetGamepadStringForAxis((SDL_GamepadAxis)ax));
            fact_i(key, SDL_GetGamepadAxis(pad, (SDL_GamepadAxis)ax));
        }
    }
    char *mapping = SDL_GetGamepadMapping(pad);
    fact_s("mapping", mapping);
    SDL_free(mapping);

    if (rumble) fact_i("rumble", SDL_RumbleGamepad(pad, (Uint16)lo, (Uint16)hi, (Uint32)ms));
    if (led) fact_i("led", SDL_SetGamepadLED(pad, (Uint8)r, (Uint8)g, (Uint8)b));
    if (player >= 0) fact_i("player", SDL_SetGamepadPlayerIndex(pad, player));
    if (!rumble && !led && player < 0 && events_s <= 0) goto done;

    /* Live events, one line each. Sensors report at their data rate whether or not anything moved: text mode
     * throttles them to one line per 250 ms per sensor, --json prints only a changed reading. */
    fact_i("listening", 1);
    fflush(stdout);
    Uint64 last_sensor[2] = {0, 0};
    float last_data[2][3] = {{0}};
    int seen[2] = {0, 0};
    end = SDL_GetTicks() + (Uint64)((events_s > 0 ? events_s : ms / 1000.0 + 0.2) * 1000);
    while (SDL_GetTicks() < end) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP: {
                const char *bn = SDL_GetGamepadStringForButton((SDL_GamepadButton)ev.gbutton.button);
                ev_begin("button", ev.common.timestamp);
                if (json) {
                    printf(",\"name\":");
                    jstr(bn);
                    printf(",\"down\":%s", ev.gbutton.down ? "true" : "false");
                } else {
                    printf(" %s %s", bn, ev.gbutton.down ? "down" : "up");
                }
                ev_end();
                break;
            }
            case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
                const char *an = SDL_GetGamepadStringForAxis((SDL_GamepadAxis)ev.gaxis.axis);
                ev_begin("axis", ev.common.timestamp);
                if (json) {
                    printf(",\"name\":");
                    jstr(an);
                    printf(",\"value\":%d", ev.gaxis.value);
                } else {
                    printf(" %s %d", an, ev.gaxis.value);
                }
                ev_end();
                break;
            }
            case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
            case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
            case SDL_EVENT_GAMEPAD_TOUCHPAD_UP: {
                const char *act = ev.type == SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN ? "down"
                                  : ev.type == SDL_EVENT_GAMEPAD_TOUCHPAD_UP ? "up" : "motion";
                ev_begin("touch", ev.common.timestamp);
                if (json) {
                    printf(",\"action\":\"%s\",\"pad\":%d,\"finger\":%d,\"x\":%.4f,\"y\":%.4f,\"p\":%.4f", act,
                           ev.gtouchpad.touchpad, ev.gtouchpad.finger, ev.gtouchpad.x, ev.gtouchpad.y,
                           ev.gtouchpad.pressure);
                } else {
                    printf(" %s pad=%d finger=%d x=%.4f y=%.4f p=%.4f", act, ev.gtouchpad.touchpad,
                           ev.gtouchpad.finger, ev.gtouchpad.x, ev.gtouchpad.y, ev.gtouchpad.pressure);
                }
                ev_end();
                break;
            }
            case SDL_EVENT_GAMEPAD_SENSOR_UPDATE: {
                int s = ev.gsensor.sensor == SDL_SENSOR_GYRO;
                const float *d = ev.gsensor.data;
                Uint64 t = SDL_GetTicks();
                int changed = !seen[s] || memcmp(d, last_data[s], sizeof last_data[s]);
                if (json ? !changed : t - last_sensor[s] < 250) break;
                last_sensor[s] = t;
                seen[s] = 1;
                memcpy(last_data[s], d, sizeof last_data[s]);
                ev_begin("sensor", ev.common.timestamp);
                if (json) printf(",\"name\":\"%s\",\"data\":[%.4f,%.4f,%.4f]", sname[s], d[0], d[1], d[2]);
                else printf(" %s %.4f %.4f %.4f", sname[s], d[0], d[1], d[2]);
                ev_end();
                break;
            }
            case SDL_EVENT_GAMEPAD_REMOVED:
                ev_begin("removed", ev.common.timestamp);
                ev_end();
                end = 0;
                break;
            case SDL_EVENT_QUIT: /* SIGINT / SIGTERM */
                end = 0;
                break;
            default:
                break;
            }
        }
        fflush(stdout);
        SDL_Delay(2);
    }

done:
    ps = SDL_GetGamepadPowerInfo(pad, &pct);
    fact_s("final.power", pstate(ps));
    fact_i("final.battery", pct);
    SDL_CloseGamepad(pad);
    SDL_Quit();
    return 0;
}

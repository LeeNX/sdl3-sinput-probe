/* sdl3probe -- what SDL3 sees of a gamepad: driver path, caps, sensors, touchpads, live events.
 *
 *   sdl3probe [--wait S] [--events S] [--rumble LO HI MS] [--led R G B] [--player N] [--no-hidapi]
 *
 * Prints one key=value line per fact so a test script can grep it. A path starting with /dev/hidraw means the
 * HIDAPI driver (SInput here) owns the device; /dev/input/event* means SDL fell back to evdev.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        else {
            fprintf(stderr, "usage: %s [--wait S] [--events S] [--rumble LO HI MS] [--led R G B] [--player N] "
                            "[--no-hidapi]\n", argv[0]);
            return 2;
        }
    }

    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, hidapi ? "1" : "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SINPUT, hidapi ? "1" : "0");
    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC | SDL_INIT_SENSOR)) {
        printf("error=SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    int v = SDL_GetVersion();
    printf("sdl_version=%d.%d.%d\nsdl_revision=%s\nhidapi=%d\n", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v),
           SDL_VERSIONNUM_MICRO(v), SDL_GetRevision(), hidapi);

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
    printf("joysticks=%d\n", n);
    for (int i = 0; i < n; i++) {
        printf("joystick.%d=%s vid=%04x pid=%04x gamepad=%d type=%s path=%s\n", i, SDL_GetJoystickNameForID(ids[i]),
               SDL_GetJoystickVendorForID(ids[i]), SDL_GetJoystickProductForID(ids[i]), SDL_IsGamepad(ids[i]),
               SDL_GetGamepadStringForType(SDL_GetGamepadTypeForID(ids[i])), SDL_GetJoystickPathForID(ids[i]));
    }
    SDL_free(ids);
    if (!pad) {
        printf("error=no gamepad within %.1fs\n", wait_s);
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
    printf("name=%s\npath=%s\ndriver=%s\n", SDL_GetGamepadName(pad), path ? path : "",
           path && !strncmp(path, "/dev/hidraw", 11) ? "hidapi" : "evdev");
    printf("vid=%04x\npid=%04x\nversion=%04x\nserial=%s\n", SDL_GetGamepadVendor(pad), SDL_GetGamepadProduct(pad),
           SDL_GetGamepadProductVersion(pad), SDL_GetGamepadSerial(pad) ? SDL_GetGamepadSerial(pad) : "");
    printf("type=%s\nreal_type=%s\n", SDL_GetGamepadStringForType(SDL_GetGamepadType(pad)),
           SDL_GetGamepadStringForType(SDL_GetRealGamepadType(pad)));
    printf("connection=%s\n", ctype(SDL_GetGamepadConnectionState(pad)));
    int pct = -1;
    SDL_PowerState ps = SDL_GetGamepadPowerInfo(pad, &pct);
    printf("power=%s\nbattery=%d\n", pstate(ps), pct);

    SDL_PropertiesID props = SDL_GetGamepadProperties(pad);
    printf("cap.rumble=%d\ncap.trigger_rumble=%d\ncap.rgb_led=%d\ncap.mono_led=%d\ncap.player_led=%d\n",
           SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false),
           SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false),
           SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN, false),
           SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_MONO_LED_BOOLEAN, false),
           SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_PLAYER_LED_BOOLEAN, false));

    printf("buttons=");
    for (int bt = 0; bt < SDL_GAMEPAD_BUTTON_COUNT; bt++) {
        if (SDL_GamepadHasButton(pad, (SDL_GamepadButton)bt))
            printf("%s ", SDL_GetGamepadStringForButton((SDL_GamepadButton)bt));
    }
    printf("\naxes=");
    for (int ax = 0; ax < SDL_GAMEPAD_AXIS_COUNT; ax++) {
        if (SDL_GamepadHasAxis(pad, (SDL_GamepadAxis)ax))
            printf("%s ", SDL_GetGamepadStringForAxis((SDL_GamepadAxis)ax));
    }
    printf("\ntouchpads=%d\n", SDL_GetNumGamepadTouchpads(pad));
    for (int t = 0; t < SDL_GetNumGamepadTouchpads(pad); t++)
        printf("touchpad.%d.fingers=%d\n", t, SDL_GetNumGamepadTouchpadFingers(pad, t));
    SDL_SensorType sensors[] = {SDL_SENSOR_ACCEL, SDL_SENSOR_GYRO};
    const char *sname[] = {"accel", "gyro"};
    for (int s = 0; s < 2; s++) {
        bool has = SDL_GamepadHasSensor(pad, sensors[s]);
        printf("sensor.%s=%d rate=%.1f\n", sname[s], has, has ? SDL_GetGamepadSensorDataRate(pad, sensors[s]) : 0.0);
        if (has && events_s > 0) SDL_SetGamepadSensorEnabled(pad, sensors[s], true);
    }
    for (int ax = 0; ax < SDL_GAMEPAD_AXIS_COUNT; ax++) {
        if (SDL_GamepadHasAxis(pad, (SDL_GamepadAxis)ax))
            printf("state.%s=%d\n", SDL_GetGamepadStringForAxis((SDL_GamepadAxis)ax),
                   SDL_GetGamepadAxis(pad, (SDL_GamepadAxis)ax));
    }
    char *mapping = SDL_GetGamepadMapping(pad);
    printf("mapping=%s\n", mapping ? mapping : "");
    SDL_free(mapping);

    if (rumble) printf("rumble=%d\n", SDL_RumbleGamepad(pad, (Uint16)lo, (Uint16)hi, (Uint32)ms));
    if (led) printf("led=%d\n", SDL_SetGamepadLED(pad, (Uint8)r, (Uint8)g, (Uint8)b));
    if (player >= 0) printf("player=%d\n", SDL_SetGamepadPlayerIndex(pad, player));
    if (!rumble && !led && player < 0 && events_s <= 0) goto done;

    /* Live events: one line each, sensor events throttled to one per 250 ms per sensor. */
    Uint64 last_sensor[2] = {0, 0};
    end = SDL_GetTicks() + (Uint64)((events_s > 0 ? events_s : ms / 1000.0 + 0.2) * 1000);
    while (SDL_GetTicks() < end) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            Uint64 t = SDL_GetTicks();
            switch (ev.type) {
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                printf("ev t=%llu button %s %s\n", (unsigned long long)t,
                       SDL_GetGamepadStringForButton((SDL_GamepadButton)ev.gbutton.button),
                       ev.gbutton.down ? "down" : "up");
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                printf("ev t=%llu axis %s %d\n", (unsigned long long)t,
                       SDL_GetGamepadStringForAxis((SDL_GamepadAxis)ev.gaxis.axis), ev.gaxis.value);
                break;
            case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
            case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
            case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
                printf("ev t=%llu touch %s pad=%d finger=%d x=%.4f y=%.4f p=%.4f\n", (unsigned long long)t,
                       ev.type == SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN ? "down"
                       : ev.type == SDL_EVENT_GAMEPAD_TOUCHPAD_UP ? "up" : "motion",
                       ev.gtouchpad.touchpad, ev.gtouchpad.finger, ev.gtouchpad.x, ev.gtouchpad.y,
                       ev.gtouchpad.pressure);
                break;
            case SDL_EVENT_GAMEPAD_SENSOR_UPDATE: {
                int s = ev.gsensor.sensor == SDL_SENSOR_GYRO;
                if (t - last_sensor[s] >= 250) {
                    last_sensor[s] = t;
                    printf("ev t=%llu sensor %s %.4f %.4f %.4f\n", (unsigned long long)t, sname[s],
                           ev.gsensor.data[0], ev.gsensor.data[1], ev.gsensor.data[2]);
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_REMOVED:
                printf("ev t=%llu removed\n", (unsigned long long)t);
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
    printf("final.power=%s\nfinal.battery=%d\n", pstate(ps), pct);
    SDL_CloseGamepad(pad);
    SDL_Quit();
    return 0;
}

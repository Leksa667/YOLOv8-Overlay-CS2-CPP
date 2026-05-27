// Created by Leksa667
#include <Mouse.h>

static const int RBUF_SIZE = 64;
static char rbuf[RBUF_SIZE];
static int  rbuf_pos = 0;

static int32_t acc_x = 0;
static int32_t acc_y = 0;

static int8_t   inertia_x       = 0;
static int8_t   inertia_y       = 0;
static bool     inertia_pending = false;
static uint16_t inertia_delay_us = 0;

static uint32_t prng_state = 0xDEADBEEF;

static inline uint32_t prng()
{
    prng_state ^= prng_state << 13;
    prng_state ^= prng_state >> 17;
    prng_state ^= prng_state <<  5;
    return prng_state;
}

static void processLine(char* line, int len)
{
    if (len == 0) return;
    char type = line[0];

    if (type == 'M' && len >= 3 && line[1] == ' ') {
        char* p   = line + 2;
        char* end = p;
        long  dx  = strtol(p,   &end, 10);
        long  dy  = (end && *end == ' ') ? strtol(end + 1, nullptr, 10) : 0L;

        acc_x += dx * 256L;
        acc_y += dy * 256L;

        int32_t ix = acc_x / 256L;
        int32_t iy = acc_y / 256L;
        acc_x -= ix * 256L;
        acc_y -= iy * 256L;

        int32_t mag = (ix < 0 ? -ix : ix) + (iy < 0 ? -iy : iy);
        if (mag >= 10) {
            inertia_x = (int8_t)(ix > 0 ? 1 : ix < 0 ? -1 : 0);
            inertia_y = (int8_t)(iy > 0 ? 1 : iy < 0 ? -1 : 0);
            inertia_delay_us = (uint16_t)(800 + (prng() % 1700));
            inertia_pending  = true;
        }

        if (ix == 0 && iy == 0) return;

        uint32_t r = prng();
        if ((r & 0xFF) < 95) {
            delayMicroseconds(400 + (uint16_t)((r >> 8) & 0x4FF));
        }

        while (ix != 0 || iy != 0) {
            int mx = (int)constrain(ix, -127L, 127L);
            int my = (int)constrain(iy, -127L, 127L);
            Mouse.move((signed char)mx, (signed char)my, 0);
            ix -= mx;
            iy -= my;
        }
    }
    else if (type == 'B') {
        Mouse.click(MOUSE_LEFT);
    }
    else if (type == 'L') {
        Mouse.press(MOUSE_LEFT);
    }
    else if (type == 'U') {
        Mouse.release(MOUSE_LEFT);
    }
}

void setup()
{
    Serial.begin(115200);
    Mouse.begin();

    uint32_t seed = 0;
    for (int i = 0; i < 8; i++) {
        seed ^= (uint32_t)analogRead(A0) << (i * 2);
        seed ^= (uint32_t)analogRead(A1) << (i * 2 + 1);
    }
    if (seed != 0) prng_state ^= seed;

    pinMode(LED_BUILTIN, OUTPUT);
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_BUILTIN, HIGH); delay(80);
        digitalWrite(LED_BUILTIN, LOW);  delay(80);
    }
}

void loop()
{
    while (Serial.available() > 0) {
        char c = (char)Serial.read();

        if (c == '\n' || c == '\r') {
            rbuf[rbuf_pos] = '\0';
            processLine(rbuf, rbuf_pos);
            rbuf_pos = 0;
        }
        else if (rbuf_pos < RBUF_SIZE - 1) {
            rbuf[rbuf_pos++] = c;
        }
        else {
            rbuf_pos = 0;
        }
    }

    if (inertia_pending && Serial.available() == 0) {
        inertia_pending = false;
        if (inertia_x != 0 || inertia_y != 0) {
            delayMicroseconds(inertia_delay_us);
            Mouse.move(inertia_x, inertia_y, 0);
        }
    }
}

#include <cstdio>
#include <cstring>
#include <string>

#include "pico/stdlib.h"

#include "TtsEngine.h"
#include "LibraryData.h"

// Transport: USB CDC by default (read the board over its USB cable, no extra
// wiring), or hardware UART0 when SHVX_TRANSPORT_USB=0 (the rp2040js simulator
// taps uart0 directly). The SHVX byte protocol is identical either way.
#if SHVX_TRANSPORT_USB
#include "pico/stdio_usb.h"
#else
#include "hardware/uart.h"
#include "hardware/gpio.h"
#define UART_PORT   uart0
#define UART_BAUD   230400
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#endif

#if SHVX_OUTPUT_I2S
#include "pico/audio_i2s.h"
#endif

using namespace SharpVox;

// 22050 Hz: half CD quality.
static constexpr int32_t SAMPLE_RATE = 22050;

static constexpr int MAX_LINE = 255;

static void io_init() {
#if SHVX_TRANSPORT_USB
    stdio_usb_init();
    // Raw binary PCM must not get \n -> \r\n translation.
    stdio_set_translate_crlf(&stdio_usb, false);
    setvbuf(stdout, NULL, _IONBF, 0);
    // Hold until the host opens the CDC port so the banner is not lost.
    while (!stdio_usb_connected()) {
        sleep_ms(50);
    }
#else
    uart_init(UART_PORT, UART_BAUD);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
#endif
}

static void io_write_all(const uint8_t* data, size_t len) {
#if SHVX_TRANSPORT_USB
    fwrite(data, 1, len, stdout);
    fflush(stdout);
#else
    for (size_t i = 0; i < len; i++) {
        uart_putc_raw(UART_PORT, data[i]);
    }
#endif
}

static void io_puts(const char* s) {
    io_write_all(reinterpret_cast<const uint8_t*>(s), strlen(s));
}

// Returns the next input byte, or -1 if none is currently available.
static int io_try_getc() {
#if SHVX_TRANSPORT_USB
    int c = getchar_timeout_us(1000);
    return c == PICO_ERROR_TIMEOUT ? -1 : c;
#else
    if (!uart_is_readable(UART_PORT)) return -1;
    return uart_getc(UART_PORT);
#endif
}

#if SHVX_OUTPUT_I2S
// Mono int16 PCM out the I2S DAC. The producer pool buffers ahead of the DMA
// to absorb synthesis jitter; take blocks at the real-time playback rate.
static audio_buffer_pool_t* g_i2s_pool = nullptr;

static void i2s_init() {
    static audio_format_t fmt = { (uint32_t)SAMPLE_RATE, AUDIO_BUFFER_FORMAT_PCM_S16, 1 };
    static audio_buffer_format_t pfmt = { &fmt, 2 };  // mono int16: 2-byte stride
    g_i2s_pool = audio_new_producer_pool(&pfmt, 4, 1024);

    // data_pin, clock_pin_base, dma_channel, pio_sm (positional: C++11 has no
    // designated initializers).
    audio_i2s_config_t cfg = { PICO_AUDIO_I2S_DATA_PIN, PICO_AUDIO_I2S_CLOCK_PIN_BASE, 0, 0 };
    audio_i2s_setup(&fmt, &cfg);
    audio_i2s_connect(g_i2s_pool);
    audio_i2s_set_enabled(true);
}

static void i2s_play(const int16_t* buf, int32_t len) {
    int32_t off = 0;
    while (off < len) {
        audio_buffer_t* ab = take_audio_buffer(g_i2s_pool, true);
        uint32_t n = ab->max_sample_count;
        if (n > (uint32_t)(len - off)) n = (uint32_t)(len - off);
        memcpy(ab->buffer->bytes, buf + off, n * sizeof(int16_t));
        ab->sample_count = n;
        give_audio_buffer(g_i2s_pool, ab);
        off += (int32_t)n;
    }
}
#endif

// Streams each synthesis chunk directly to the host; no audio buffer in RAM.
// Protocol:
//   "SHVX BEGIN <rate>\r\n"
//   "SHVX CHUNK <n>\r\n" + n*2 bytes raw int16_t PCM  (repeated per chunk)
//   "SHVX END\r\n"
static void on_chunk(const int16_t* buf, int32_t len, void*) {
#ifndef BENCH
    char hdr[32];
    int hlen = snprintf(hdr, sizeof(hdr), "SHVX CHUNK %d\r\n", (int)len);
    io_write_all(reinterpret_cast<const uint8_t*>(hdr), (size_t)hlen);
    io_write_all(reinterpret_cast<const uint8_t*>(buf), (size_t)len * sizeof(int16_t));
#if SHVX_OUTPUT_I2S
    i2s_play(buf, len);
#endif
#else
    (void)buf; (void)len;
#endif
}

int main() {
    io_init();
#if SHVX_OUTPUT_I2S
    i2s_init();
#endif
    io_puts("SHVX UART OK\r\n");

    TtsEngine engine(
        LibraryData::dictionary,
        static_cast<size_t>(LibraryData::dictionarySize),
        [](const std::string& key, size_t& sz) -> const uint8_t* {
            return LibraryData::FindSymbol(key.c_str(), sz);
        },
        SAMPLE_RATE);

    io_puts("SHVX READY\r\n");

    char line[MAX_LINE + 1];
    int linePos = 0;

    while (true) {
        int ci = io_try_getc();
        if (ci < 0) {
            tight_loop_contents();
            continue;
        }

        char c = (char)ci;

        if (c == '\r') continue;

        if (c == '\n') {
            if (linePos == 0) continue;
            line[linePos] = '\0';
            linePos = 0;

            char hdr[32];
            int hlen = snprintf(hdr, sizeof(hdr), "SHVX BEGIN %d\r\n", (int)SAMPLE_RATE);
            io_write_all(reinterpret_cast<const uint8_t*>(hdr), (size_t)hlen);

            engine.Speak(std::string(line), on_chunk, nullptr);

            io_puts("SHVX END\r\n");

        } else if (linePos < MAX_LINE) {
            line[linePos++] = c;
        }
    }
}

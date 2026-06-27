#include "jaszczurhal/doom_boot_log.h"

#include <Arduino.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#ifndef DOOM_BOOT_LOG_BAUD
#define DOOM_BOOT_LOG_BAUD 115200u
#endif

#ifndef DOOM_BOOT_LOG_BUFFER_SIZE
#define DOOM_BOOT_LOG_BUFFER_SIZE 8192u
#endif

static bool s_serial_started = false;
static bool s_log_truncated = false;
static bool s_in_write = false;
static char s_log_buffer[DOOM_BOOT_LOG_BUFFER_SIZE];
static unsigned int s_log_length = 0;
static unsigned int s_log_sent = 0;

static void ensure_serial_started(void) {
  if (s_serial_started) {
    return;
  }

  Serial.begin(DOOM_BOOT_LOG_BAUD);
  s_serial_started = true;
  yield();
}

static bool serial_is_connected(void) {
  ensure_serial_started();
  return (bool)Serial;
}

static void append_to_buffer(const char *data, unsigned int length) {
  if (data == nullptr || length == 0u) {
    return;
  }

  if (s_log_length + length <= (unsigned int)sizeof(s_log_buffer)) {
    memcpy(s_log_buffer + s_log_length, data, length);
    s_log_length += length;
    return;
  }

  if (!s_log_truncated) {
    static const char truncated[] = "\n[boot] log buffer truncated\n";
    const unsigned int truncated_len = sizeof(truncated) - 1u;

    if (s_log_length + truncated_len <= (unsigned int)sizeof(s_log_buffer)) {
      memcpy(s_log_buffer + s_log_length, truncated, truncated_len);
      s_log_length += truncated_len;
    }
    s_log_truncated = true;
  }
}

void DoomBootLog_Init(void) { ensure_serial_started(); }

bool DoomBootLog_SerialConnected(void) { return serial_is_connected(); }

void DoomBootLog_Flush(void) {
  ensure_serial_started();

  if (!Serial) {
    yield();
    return;
  }

  while (s_log_sent < s_log_length) {
    const unsigned int remaining = s_log_length - s_log_sent;
    const size_t written =
        Serial.write((const uint8_t *)(s_log_buffer + s_log_sent), remaining);
    if (written == 0u) {
      break;
    }
    s_log_sent += (unsigned int)written;
    yield();
  }

  Serial.flush();

  if (s_log_sent == s_log_length) {
    s_log_sent = 0;
    s_log_length = 0;
    s_log_truncated = false;
  }
}

void DoomBootLog_WaitForSerial(uint32_t timeout_ms) {
  const uint32_t start_ms = millis();

  while (!DoomBootLog_SerialConnected() &&
         (uint32_t)(millis() - start_ms) < timeout_ms) {
    DoomBootLog_Flush();
    delay(10);
  }

  DoomBootLog_Flush();
}

void DoomBootLog_WriteRaw(const char *data, unsigned int length) {
  if (data == nullptr || length == 0u) {
    return;
  }

  ensure_serial_started();
  append_to_buffer(data, length);
  DoomBootLog_Flush();
}

void DoomBootLog_Printf(const char *format, ...) {
  char line[384];
  va_list args;

  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);

  if (length <= 0) {
    return;
  }

  const unsigned int bounded_length =
      (length < (int)sizeof(line)) ? (unsigned int)length
                                   : (unsigned int)sizeof(line) - 1u;
  DoomBootLog_WriteRaw(line, bounded_length);
}

void DoomFatalError(const char *format, ...) {
  char message[384];
  va_list args;

  va_start(args, format);
  const int length = vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  if (length > 0) {
    DoomBootLog_Printf("\n[fatal] I_Error: %s\n", message);
  } else {
    DoomBootLog_Printf("\n[fatal] I_Error\n");
  }

  for (;;) {
    DoomBootLog_Flush();
    delay(100);
  }
}

extern "C" ssize_t _write(int fd, const void *buf, size_t count) {
  (void)fd;

  if (buf == nullptr || count == 0u || s_in_write) {
    return (ssize_t)count;
  }

  s_in_write = true;
  DoomBootLog_WriteRaw((const char *)buf, (unsigned int)count);
  s_in_write = false;

  return (ssize_t)count;
}

// Minimal Arduino stub for running Adafruit_GFX and scene.h on the PC.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <string>
#include "Print.h"
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t *)(a))
#define pgm_read_word(a) (*(const uint16_t *)(a))
#define pgm_read_dword(a) (*(const unsigned long *)(a))
typedef bool boolean;
class __FlashStringHelper;
class String {
 public:
  std::string s;
  String(const char *c = "") : s(c) {}
  const char *c_str() const { return s.c_str(); }
  unsigned length() const { return s.size(); }
};

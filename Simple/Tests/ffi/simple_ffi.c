#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

int32_t simple_add_i32(int32_t a, int32_t b) {
  return a + b;
}

int8_t simple_add_i8(int8_t a, int8_t b) {
  return (int8_t)(a + b);
}

int16_t simple_add_i16(int16_t a, int16_t b) {
  return (int16_t)(a + b);
}

int64_t simple_mul_i64(int64_t a, int64_t b) {
  return a * b;
}

uint8_t simple_add_u8(uint8_t a, uint8_t b) {
  return (uint8_t)(a + b);
}

uint16_t simple_add_u16(uint16_t a, uint16_t b) {
  return (uint16_t)(a + b);
}

uint32_t simple_add_u32(uint32_t a, uint32_t b) {
  return a + b;
}

uint64_t simple_add_u64(uint64_t a, uint64_t b) {
  return a + b;
}

float simple_add_f32(float a, float b) {
  return a + b;
}

double simple_add_f64(double a, double b) {
  return a + b;
}

uint8_t simple_char_max(uint8_t a, uint8_t b) {
  return a > b ? a : b;
}

bool simple_bool_and(bool a, bool b) {
  return a && b;
}

const char* simple_hello(void) {
  return "hello this is a string from C function";
}

int32_t simple_inc_i32(int32_t a) {
  return a + 1;
}

double simple_mix_i32_f64(int32_t a, double b) {
  return (double)a + b;
}

int32_t simple_strlen_cstr(const char* text) {
  if (!text) return -1;
  return (int32_t)strlen(text);
}

const char* simple_echo(const char* text) {
  return text;
}

void simple_sink_i32(int32_t value) {
  (void)value;
}

typedef struct Array {
  int32_t* data;
  size_t length;
} Array;

Array simple_create_array(size_t length) {
  Array arr;
  arr.data = (int32_t*)malloc(length * sizeof(int32_t));
  arr.length = length;
  for (size_t i = 0; i < length; i++) {
    arr.data[i] = (int32_t)i;
  }
  return arr;
}

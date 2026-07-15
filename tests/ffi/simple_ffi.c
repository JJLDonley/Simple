#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

int32_t simple_add_i32(int32_t a, int32_t b) {
  return a + b;
}

typedef int32_t (*SimpleBinaryI32)(int32_t, int32_t);

int32_t simple_apply_i32(SimpleBinaryI32 callback, int32_t a, int32_t b) {
  return callback ? callback(a, b) : -1;
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

int32_t simple_inc_i32(int32_t a) {
  return a + 1;
}

double simple_mix_i32_f64(int32_t a, double b) {
  return (double)a + b;
}

void simple_sink_i32(int32_t value) {
  (void)value;
}

size_t simple_add_usize(size_t a, size_t b) {
  return a + b;
}

ptrdiff_t simple_add_isize(ptrdiff_t a, ptrdiff_t b) {
  return a + b;
}

int32_t* simple_maybe_i32_pointer(int32_t present) {
  static int32_t value = 37;
  return present ? &value : NULL;
}

int32_t simple_pointer_is_null(const int32_t* pointer) {
  return pointer == NULL ? 1 : 0;
}

int32_t simple_read_i32_pointer(const int32_t* pointer) {
  return pointer ? *pointer : -1;
}

typedef struct SimpleColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
} SimpleColor;

int32_t simple_color_sum(SimpleColor color) {
  return (int32_t)color.r + (int32_t)color.g + (int32_t)color.b + (int32_t)color.a;
}

SimpleColor simple_color_make(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  SimpleColor color;
  color.r = r;
  color.g = g;
  color.b = b;
  color.a = a;
  return color;
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

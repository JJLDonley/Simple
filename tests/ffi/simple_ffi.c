#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

int32_t simple_add_i32(int32_t a, int32_t b) {
  return a + b;
}

int32_t simple_cstr_length(const uint8_t* text) {
  int32_t length = 0;
  if (!text) return -1;
  while (text[length] != 0) ++length;
  return length;
}

bool simple_bool_not(bool value) {
  return !value;
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

static int32_t simple_pointer_value = 37;
static int32_t* simple_pointer_slot = &simple_pointer_value;

int32_t* simple_i32_pointer(void) {
  return &simple_pointer_value;
}

int32_t* simple_maybe_i32_pointer(int32_t present) {
  return present ? &simple_pointer_value : NULL;
}

void simple_write_i32_pointer(int32_t* pointer, int32_t value) {
  if (pointer) *pointer = value;
}

int32_t** simple_i32_pointer_slot(void) {
  return &simple_pointer_slot;
}

void simple_write_i32_pointer_slot(int32_t** destination, int32_t* value) {
  if (destination) *destination = value;
}

int32_t simple_read_i32_pointer_slot(int32_t** slot) {
  return slot && *slot ? **slot : -1;
}

typedef struct SimpleI32View {
  const int32_t* data;
  size_t length;
} SimpleI32View;

SimpleI32View simple_i32_view(void) {
  SimpleI32View view = {&simple_pointer_value, 1};
  return view;
}

int32_t simple_read_i32_view(SimpleI32View view) {
  return view.data && view.length > 0 ? view.data[0] : -1;
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

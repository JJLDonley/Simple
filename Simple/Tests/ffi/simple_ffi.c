#include <stdint.h>
#include <stddef.h>

int32_t simple_add_i32(int32_t a, int32_t b) {
  return a + b;
}

int64_t simple_mul_i64(int64_t a, int64_t b) {
  return a * b;
}

float simple_add_f32(float a, float b) {
  return a + b;
}

double simple_add_f64(double a, double b) {
  return a + b;
}

const char* simple_hello(void) {
  return "hello";
}

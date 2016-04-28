#include "fixed_point.h"
#include <inttypes.h>

fixed int_to_fixed (int n) {
	return n << FIXED_SHIFT;
}

int fixed_to_int (fixed f) {
	return f >> FIXED_SHIFT;
}

fixed add_fixed_fixed (fixed x, fixed y) {
	return x + y;
}

fixed add_fixed_int (fixed f, int n) {
	return f + (n << FIXED_SHIFT);
}

fixed add_int_fixed (int n, fixed f) {
	return (n << FIXED_SHIFT) + f;
}

fixed add_int_int (int n, int m) {
	return add_fixed_fixed(int_to_fixed(n), int_to_fixed(m));
}

fixed sub_fixed_fixed (fixed x, fixed y) {
	return x - y;
}

fixed sub_fixed_int (fixed f, int n) {
	return f - (n << FIXED_SHIFT);
}

fixed sub_int_fixed (int n, fixed f) {
	return (n << FIXED_SHIFT) - f;
}

fixed sub_int_int (int n, int m) {
	return sub_fixed_fixed(int_to_fixed(n), int_to_fixed(m));
}

fixed mul_fixed_fixed (fixed x, fixed y) {
	return  (int64_t)x * y >> FIXED_SHIFT;
}

fixed mul_fixed_int (fixed f, int n) {
	return f * n;
}

fixed mul_int_fixed (int n, fixed f) {
	return n * f;
}

fixed mul_int_int (int n, int m) {
	return mul_fixed_fixed(int_to_fixed(n), int_to_fixed(m));
}

fixed div_fixed_fixed (fixed x, fixed y) {
	return ((int64_t)x << FIXED_SHIFT )/ y;
}

fixed div_fixed_int (fixed f, int n) {
	return f / n;
}

fixed div_int_fixed (int n, fixed f) {
	return div_fixed_fixed(int_to_fixed(n), f);
}

fixed div_int_int (int n, int m) {
	return div_fixed_fixed(int_to_fixed(n), int_to_fixed(m));
}
int round_fixed_to_int (fixed f) {
	if (f >= 0) return (f + (1 << (FIXED_SHIFT-1))) >> FIXED_SHIFT;
	else return (f - (1 << (FIXED_SHIFT-1))) >> FIXED_SHIFT;
}

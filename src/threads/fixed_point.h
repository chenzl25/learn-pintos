#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#define FIXED_SHIFT 16 

typedef int fixed;

fixed int_to_fixed (int n);
int fixed_to_int (fixed f);
fixed add_fixed_fixed (fixed x, fixed y);
fixed add_fixed_int (fixed f, int n);
fixed add_int_fixed (int n, fixed f);
fixed add_int_int (int n, int m);
fixed sub_fixed_fixed (fixed x, fixed y);
fixed sub_fixed_int (fixed f, int n);
fixed sub_int_fixed (int n, fixed f);
fixed sub_int_int (int n, int m);
fixed mul_fixed_fixed (fixed x, fixed y);
fixed mul_fixed_int (fixed f, int n);
fixed mul_int_fixed (int n, fixed f);
fixed mul_int_int (int n, int m);
fixed div_fixed_fixed (fixed x, fixed y);
fixed div_fixed_int (fixed f, int n);
fixed div_int_fixed (int n, fixed f);
fixed div_int_int (int n, int m);
int round_fixed_to_int (fixed f);




#endif



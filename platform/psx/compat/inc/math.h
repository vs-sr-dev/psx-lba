/* math.h — libpsn00b has no libm, and the R3000A has no FPU.
 *
 * The engine's entire use of it is one live call: GIF.C computes a GIF colour
 * table size as pow(2, n + 1.0). psx_libc.c implements pow for the integer
 * exponents that reaches it and nothing else, which keeps GIF.C unmodified.
 * See the note there before assuming this is a general pow(). */
#ifndef PSX_COMPAT_MATH_H
#define PSX_COMPAT_MATH_H

double pow(double base, double exponent);

#endif

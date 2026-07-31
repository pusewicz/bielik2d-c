#pragma once
#include <stddef.h>
#include <stdint.h>

// Bare, unprefixed names (Rust/Zig/Odin-style) traded deliberately against
// bielik2d's usual bk_/BK_ namespacing -- see the design spec's §3 for the
// rationale and the collision risk this accepts.

/// 8/16/32/64-bit signed integers.
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

/// 8/16/32/64-bit unsigned integers.
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/// IEEE-754 single/double precision floats.
typedef float f32;
typedef double f64;

/// Pointer-width unsigned/signed sizes (aliases of size_t / ptrdiff_t).
typedef size_t usize;
typedef ptrdiff_t isize;

/// 32-bit boolean for GPU/shader-layout structs (std140/std430 alignment rules
/// don't allow C's 1-byte bool). NOT a replacement for bool in ordinary control
/// flow -- use bool/true/false for that.
typedef int32_t b32;

static_assert(sizeof(i8) == 1, "i8 must be 1 byte");
static_assert(sizeof(i16) == 2, "i16 must be 2 bytes");
static_assert(sizeof(i32) == 4, "i32 must be 4 bytes");
static_assert(sizeof(i64) == 8, "i64 must be 8 bytes");
static_assert(sizeof(u8) == 1, "u8 must be 1 byte");
static_assert(sizeof(u16) == 2, "u16 must be 2 bytes");
static_assert(sizeof(u32) == 4, "u32 must be 4 bytes");
static_assert(sizeof(u64) == 8, "u64 must be 8 bytes");
static_assert(sizeof(f32) == 4, "f32 must be 4 bytes");
static_assert(sizeof(f64) == 8, "f64 must be 8 bytes");
static_assert(sizeof(b32) == 4, "b32 must be 4 bytes");

#pragma once
#include <bielik/bk_alloc.h>

/// True if a is nullptr, all-zero, or has all three functions set -- the two
/// valid shapes. A partially-set struct is invalid everywhere.
bool bk__allocator_valid(const BK_Allocator *a);

/// Installs the app-wide base allocator (what a == nullptr resolves to).
/// Validates all-or-nothing; returns false and logs on a partial struct.
/// nullptr or all-zero resets to the SDL-heap default. Called once from
/// bk__boot before SDL_Init; tests call it directly.
bool bk__alloc_install(const BK_Allocator *base);

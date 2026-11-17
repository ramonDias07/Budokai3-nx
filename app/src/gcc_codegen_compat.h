#pragma once

// GCC needs the linkage specification before the function attributes.
#ifndef DEFINE_REX_FUNC
#define DEFINE_REX_FUNC(name) \
  extern "C" __attribute__((alias("__imp__" #name), weak, noinline)) REX_FUNC(name); \
  REX_EXTERN(__imp__##name)
#endif

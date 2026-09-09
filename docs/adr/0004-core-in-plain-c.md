# ADR 0004: Core engine is plain C, not C++

## Status

Accepted (revisit if RAII/container needs grow painful)

## Context

Spec section 4 allows either plain C or "C with a thin C++ wrapper for
RAII/containers." Section 4 also requires a stable C ABI for compiled
feature modules and the plugin host. Starting in C++ and exposing a C ABI
on top is more moving parts than starting in C and staying there.

## Decision

The core engine (`core/`) is written in plain C (C11), compiled as a
library with a plain C header (`core/include/ase/*.h`, wrapped in
`extern "C"` for C++ consumers like the Qt GUI). A C++ wrapper layer may be
added later, inside `core/` or as a separate adapter, if RAII/container
ergonomics become a real maintenance burden — not preemptively.

## Consequences

No name-mangling or ABI-stability surprises between core and consumers.
Slightly more manual memory/lifetime management inside the engine itself,
which is an acceptable cost given the "robust, no data loss" pillar demands
careful manual attention to buffer/undo lifetimes either way.

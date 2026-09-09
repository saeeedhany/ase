#ifndef ASE_CORE_H
#define ASE_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Stable C ABI surface for the core engine (see docs/adr/0002 and 0004).
 * This header is scaffold-only: the buffer engine, undo/redo history, and
 * config/theme parser land in Phase 1 (docs/ROADMAP.md) and will extend
 * this surface.
 */

const char *ase_core_version(void);

#ifdef __cplusplus
}
#endif

#endif /* ASE_CORE_H */

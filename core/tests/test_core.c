/* Smoke test proving the build/test wiring works. Real coverage of the
 * buffer engine starts in Phase 1 (docs/ROADMAP.md). */

#include <assert.h>
#include <string.h>

#include "ase/core.h"

int main(void) {
    const char *version = ase_core_version();
    assert(version != NULL);
    assert(strlen(version) > 0);
    return 0;
}

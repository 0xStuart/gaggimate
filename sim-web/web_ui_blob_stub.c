/* Browser build: the page cannot listen on a port, so the embedded web UI is
 * never served. Stands in for sim/web/web_ui_blob_sim.S, whose .incbin the
 * WebAssembly assembler cannot handle. */
#include <stdint.h>

const uint8_t gWebUiBlobStart[4] = {0};
const uint8_t gWebUiBlobEnd[1] = {0};

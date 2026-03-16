#ifndef E2SIM_KPM_DEBUG_HPP
#define E2SIM_KPM_DEBUG_HPP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static inline int e2sim_debug_enabled_local(void) {
  const char *value = getenv("E2SIM_DEBUG");
  if (value == NULL || value[0] == '\0') {
    return 0;
  }

  if (strcmp(value, "0") == 0 || strcasecmp(value, "false") == 0 ||
      strcasecmp(value, "off") == 0 || strcasecmp(value, "no") == 0) {
    return 0;
  }

  return 1;
}

#ifndef E2SIM_DBG_PRINTF
#define E2SIM_DBG_PRINTF(...) do { if (e2sim_debug_enabled_local()) printf(__VA_ARGS__); } while (0)
#endif

#ifndef E2SIM_DBG_FPRINTF
#define E2SIM_DBG_FPRINTF(stream, ...) do { if (e2sim_debug_enabled_local()) fprintf((stream), __VA_ARGS__); } while (0)
#endif

#ifndef E2SIM_DBG_XER
#define E2SIM_DBG_XER(stream, td, sptr) do { if (e2sim_debug_enabled_local()) xer_fprint((stream), (td), (sptr)); } while (0)
#endif

#endif

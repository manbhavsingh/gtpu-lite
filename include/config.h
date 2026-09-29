#ifndef GTPU_CONFIG_H
#define GTPU_CONFIG_H

#include "session.h"

/* Load "session <teid_ul> <teid_dl> <ue_ip>" lines into the table.
 * Returns number of sessions loaded, or -1 on error. */
int config_load(const char *path, struct session_table *t);

#endif
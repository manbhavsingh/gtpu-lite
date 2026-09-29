#ifndef GTPU_TUN_H
#define GTPU_TUN_H

/* Attach to an existing TUN device (IFF_TUN, no packet info header).
 * Returns fd on success, -1 on failure with errno set. */
int tun_open(const char *name);

#endif
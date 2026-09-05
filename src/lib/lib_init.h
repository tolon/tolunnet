/*
 * tolunnet — bsdsocket.library initialization and lifecycle management.
 */
#ifndef TOLUNNET_LIB_INIT_H
#define TOLUNNET_LIB_INIT_H

#include <exec/types.h>
#include <exec/libraries.h>

/* Create and add bsdsocket.library to Exec's library list */
struct Library *tn_lib_create(void);

/* Remove and free bsdsocket.library from Exec's library list */
void tn_lib_destroy(struct Library *lib);

#endif /* TOLUNNET_LIB_INIT_H */

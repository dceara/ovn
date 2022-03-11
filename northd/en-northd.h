#ifndef EN_NORTHD_H
#define EN_NORTHD_H 1

#include <config.h>

#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>

#include "lib/inc-proc-eng.h"

void en_northd_run(struct engine_node *node OVS_UNUSED, void *data OVS_UNUSED);
void *en_northd_init(struct engine_node *node OVS_UNUSED,
                     struct engine_arg *arg);
void en_northd_cleanup(void *data);

/* I-P change handlers. */
bool en_northd_lswitch_handler(struct engine_node *node, void *data);
bool en_northd_lrouter_handler(struct engine_node *node, void *data);

#endif /* EN_NORTHD_H */

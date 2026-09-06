#ifndef VANGOPIX_CORE_H
#define VANGOPIX_CORE_H

/* The frame. Everything that happens between two presents happens in here. */

extern void vangopix_input (void);   /* drain the event queue */
extern void vangopix_core  (void);   /* draw and present      */

#endif

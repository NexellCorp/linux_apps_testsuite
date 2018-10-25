#ifndef _OPTION_H
#define _OPTION_H

#ifdef __cplusplus
extern "C" {
#endif

int handle_option(int argc, char **argv, uint32_t *m, uint32_t *w, uint32_t *h,
	uint32_t *W, uint32_t *H, uint32_t *f, uint32_t *bus_f,
	uint32_t *count, struct rect *S, uint32_t *t);

#ifdef __cplusplus
}
#endif

#endif

#ifndef _OPTION_H
#define _OPTION_H

#ifdef __cplusplus
extern "C" {
#endif

int handle_option(int argc, char **argv, uint32_t *w,
		  uint32_t *h, uint32_t *count, char *dev_path, uint32_t *t);

#ifdef __cplusplus
}
#endif

#endif

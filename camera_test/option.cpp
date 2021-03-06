#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>

#include "option.h"

int handle_option(int argc, char **argv, uint32_t *m, uint32_t *w,
		  uint32_t *h, uint32_t *f, uint32_t *bus_f, uint32_t *c)
{
	int opt;

	while ((opt = getopt(argc, argv, "m:w:h:f:F:c:")) != -1) {
		switch (opt) {
		case 'm':
			*m = atoi(optarg);
			break;
		case 'w':
			*w = atoi(optarg);
			break;
		case 'h':
			*h = atoi(optarg);
			break;
		case 'f':
			*f = atoi(optarg);
			break;
		case 'F':
			*bus_f = atoi(optarg);
			break;
		case 'c':
			*c = atoi(optarg);
			break;
		}
	}

	printf("m: %d, w: %d, h: %d, f: %d, bus_f: %d, c: %d\n",
	       *m, *w, *h, *f, *bus_f, *c);

	return 0;
}

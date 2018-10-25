/*
 * Copyright (C) 2016  Nexell Co., Ltd.
 * Author: Jongkeun, Choi <jkchoi@nexell.co.kr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>

#include <nx-scaler.h>

#include "option.h"


int handle_option(int argc, char **argv, uint32_t *m, uint32_t *w, uint32_t *h,
	uint32_t *W, uint32_t *H, uint32_t *f, uint32_t *bus_f, uint32_t *c,
	struct rect *S, uint32_t *t)
{
	int opt;

	while ((opt = getopt(argc, argv, "m:w:h:W:H:f:F:c:S:t:")) != -1) {
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
		case 'W':
			*W = atoi(optarg);
			break;
		case 'H':
			*H = atoi(optarg);
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
		case 'S':
			sscanf(optarg, "%d, %d, %d, %d",
			&S->x, &S->y, &S->width, &S->height);
			break;
		case 't':
			*t = atoi(optarg);
			break;

		}
	}

	printf("m: %d, w: %d, h: %d, W: %d, H: %d, f: %d, bus_f: %d, c: %d, t: %d\n",
	       *m, *w, *h, *W, *H, *f, *bus_f, *c, *t);
	printf("c_x : %d, c_y : %d, c_w : %d, c_h : %d\n", S->x, S->y,
		S->width, S->height);

	return 0;
}

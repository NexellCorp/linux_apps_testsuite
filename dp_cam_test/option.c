#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>

#include "option.h"


int handle_option(int argc, char **argv, DP_CAM_INFO *pDPCamInfo)
{
	int opt;

	while ((opt = getopt(argc, argv, "m:w:h:f:F:c:W:H:t:p:o:d:x:y:s:M:r:")) != -1) {
		switch (opt) {
		case 'm':
			pDPCamInfo->m = atoi(optarg);
			break;
		case 'w':
			pDPCamInfo->w = atoi(optarg);
			break;
		case 'h':
			pDPCamInfo->h = atoi(optarg);
			break;
		case 'f':
			pDPCamInfo->f = atoi(optarg);
			break;
		case 'F':
			pDPCamInfo->bus_f = atoi(optarg);
			break;
		case 'c':
			pDPCamInfo->count = atoi(optarg);
			break;
		case 'W':
			pDPCamInfo->sw = atoi(optarg);
			break;
		case 'H':
			pDPCamInfo->sh = atoi(optarg);
			break;
		case 't':
			pDPCamInfo->t = atoi(optarg);
			break;
		case 'p':
			pDPCamInfo->port = atoi(optarg);
			break;
		case 'o':
			pDPCamInfo->overlay_draw_format = atoi(optarg);
			break;
		case 'd':
			pDPCamInfo->full_screen = atoi(optarg);
			break;
		case 'x':
			pDPCamInfo->crop_x = atoi(optarg);
			break;
		case 'y':
			pDPCamInfo->crop_y = atoi(optarg);
			break;
		case 's':
			sscanf(optarg, "%dx%d", &(pDPCamInfo->crop_width), &(pDPCamInfo->crop_height) );
			break;
		case 'r':
			sscanf(optarg, "%dx%d", &(pDPCamInfo->dp_width), &(pDPCamInfo->dp_height) );
			break;
		case 'M':
			pDPCamInfo->use_max9286 = atoi(optarg);
			break;
		}
	}

	if(pDPCamInfo->crop_width == 0)
	{
		pDPCamInfo->crop_width = pDPCamInfo->w;
	}
	if(pDPCamInfo->crop_height == 0)
	{
		pDPCamInfo->crop_height = pDPCamInfo->h;
	}

	return 0;
}

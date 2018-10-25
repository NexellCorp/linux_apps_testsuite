#ifndef _OPTION_H
#define _OPTION_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct{
	uint32_t m;
	uint32_t w;
	uint32_t h;
	uint32_t sw;
	uint32_t sh;
	uint32_t f;
	uint32_t bus_f;
	uint32_t count;
	uint32_t t;
	uint32_t port;
	uint32_t overlay_draw_format;
	uint32_t full_screen;
	uint32_t crop_x;
	uint32_t crop_y;
	uint32_t crop_width;
	uint32_t crop_height;
	uint32_t dp_width;
	uint32_t dp_height;
	uint32_t use_max9286;
}DP_CAM_INFO;

int handle_option(int argc, char **argv, DP_CAM_INFO *pDPCamInfo);
#ifdef __cplusplus
}
#endif

#endif

#ifndef _OPTION_H_
#define _OPTION_H_

#ifdef __cplusplus
extern "C" {
#endif

enum {
	none = 0,
	enb_clip,
	enb_deci,
};

struct scale_data {
	uint32_t srcWidth;
	uint32_t srcHeight;
	uint32_t dstWidth;
	uint32_t dstHeight;
	uint32_t crop[4];
};

struct camera_data {
	uint32_t module;
	uint32_t type;
	uint32_t width;
	uint32_t height;
	uint32_t format;
	bool	vip_crop;
	uint32_t crop[4];
	uint32_t display;
	bool	scaling;
	struct scale_data scale;
	uint32_t file_save;
	bool	fps_enable;
	uint32_t count;
};

int handle_option(int32_t argc, char **argv, struct camera_data *data);

#ifdef __cplusplus
}
#endif

#endif

/*
 * Copyright (c) 2016 Nexell Co., Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>

#include <linux/videodev2.h>

#include <nx-v4l2.h>
#include <dp.h>
#include <dp_common.h>

#include "globaldef.h"
#include "debug.h"
#include "option.h"

bool cam_dbg_on = false;

enum {
	fmt_yuv420 = 0,
	fmt_yvu420,
	fmt_yuv422,
	fmt_yuv444,
	fmt_yuyv,
};

static void print_usage(const char *appName)
{
	printf(
		"Usage : %s [options] [M] = mandatory, [O] = option\n"
		"  common options :\n"
		"     -e						: enumerate all video devices\n"
		"     -p						: print all connected v4l2 devices\n"
		"     -A						: test all connected video devices\n"
		"     -m [module num]				[M]  : device index of the list, default:0\n"
		"     -v [clipper enable],[decimator enable]	[M]  : vip enable, default:1,0\n"
		"     -f [format]				[M]  : input image format\n"
		"						     : 0 - YUV420, 1 - YVU420\n"
		"						     : 1 - YVU420\n"
		"						     : 2 - YUV422, 3 - YUV444\n"
		"						     : 4 - YUYV only for clipper\n"
		"     -i [width],[height]			[M]  : input image's size\n"
		"						     : width should be aligned by 32 pixel\n"
		"     -c [left],[top],[width],[height]		[O]  : vip crop\n"
		"     -d [display]				[O]  : display enable, default:0-disable\n"
		"						     : 1 - clipper display, 2 - decimator display\n"
		"     -s [width],[height]			[O]  : scaling enable, output image's size\n"
		"						     : source width should be aligned by 32 pixel\n"
		"     -C [left],[top],[width],[height]		[O]  : scaling crop\n"
		"						     : only works scaling is enabled\n"
		"     -r [count]					[O]  : repeat count, default:1\n"
		"     -S [save frmae to file]			[O]  : file saving enable, default:0\n"
		"						     : clipper.yuv, decimator.yuv\n"
		"						     : -S 10 means 10th frame will be saved\n"
		"     -F [framerate display]			[O]  : framerate display enable, default:0\n"
		"     -D [debug enable]				[O]  : debug enable, default:0\n"
		"     -h : help\n"
		" =========================================================================================================\n\n",
		appName);
	printf(
		" Example :\n"
		"     #> %s -m 0 -v 1,0 -f 0 -i 1920,1080 -d 1 -r 10\n"
		, appName);

}

static bool get_crop_info(uint32_t width, uint32_t height, uint32_t *crop)
{
	bool ret = NO_ERROR;

	if (((crop[2] + crop[0]) > width) || ((crop[3] + crop[1]) > height)) {
		CAM_ERR("Invalid crop information width:%d height:%d crop[%d:%d:%d:%d]\n",
				width, height, crop[0], crop[1], crop[2], crop[3]);
		ret = true;
	}
	return ret;
}

static void cam_debug_on(bool on, int display)
{
	cam_dbg_on = on;
	if (display)
		dp_debug_on(on);
}

int handle_option(int32_t argc, char **argv, struct camera_data *data)
{
	int32_t opt;
	uint32_t inWidth = 0, inHeight = 0, outWidth = 0, outHeight = 0, format = 0;
	uint32_t count = 1, module = 0, display = 0, file_save = 0, fps_enable = 0;
	uint32_t clipper = 0, decimator = 0, debug = 0;
	uint32_t vip_crop[4] = {0, };
	uint32_t scaling_crop[4] = {0, };
	uint32_t dWidth = 0, dHeight = 0;

	while ((opt = getopt(argc, argv, "m:v:i:f:c:d:s:C:r:S:F:D:Aeph")) != -1)
	{
		switch (opt)
		{
			case 'm':
				sscanf(optarg, "%d", &module);
				break;
			case 'v':
				sscanf(optarg, "%d,%d", &clipper, &decimator);
				break;
			case 'f':
				sscanf(optarg, "%d", &format);
				break;
			case 'i':
				sscanf(optarg, "%d,%d", &inWidth, &inHeight);
				break;
			case 'c':
				sscanf(optarg, "%d,%d,%d,%d", &vip_crop[0], &vip_crop[1],
						&vip_crop[2], &vip_crop[3]);
				break;
			case 'd':
				sscanf(optarg, "%d", &display);
				break;
			case 's':
				sscanf(optarg, "%d,%d", &outWidth, &outHeight);
				break;
			case 'C':
				sscanf(optarg, "%d,%d,%d,%d", &scaling_crop[0], &scaling_crop[1],
						&scaling_crop[2], &scaling_crop[3]);
				break;
			case 'r':
				sscanf(optarg, "%d", &count);
				break;
			case 'S':
				sscanf(optarg, "%d", &file_save);
				break;
			case 'F':
				sscanf(optarg, "%d", &fps_enable);
				break;
			case 'D':
				sscanf(optarg, "%d", &debug);
				break;
			case 'A':
				/* test all video device */
				CAM_DBG("Not implemented yet\n");
				return NO_ERROR;
			case 'e':
				nx_v4l2_enumerate();
				return ERROR;
			case 'p':
				nx_v4l2_print_all_video_entry();
				return ERROR;
			case 'h':
				print_usage(argv[0]);
				return ERROR;
			default:
				break;
		}
	}

	if (debug)
		cam_debug_on(true, display);

	if (!clipper && !decimator)
		clipper = true;
	if(!inWidth || !inHeight) {
		CAM_ERR("Invalid source width or height\n");
		goto retry;
	}

	if (display > enb_deci) {
		CAM_ERR("Invalid display\n");
		goto retry;
	}

	CAM_DBG("module:%d clipper:%s decimator:%s format:%d width:%d height:%d display:%s repeat:%d file save:%s fps %s\n",
			module, (clipper) ? "enable":"disable", (decimator) ? "enable" : "disable",
			format, inWidth, inHeight, (display) ? "enable" : "disable", count,
			(file_save) ? "enable" : "disable",
			(fps_enable) ? "enable" : "disable");

	if (vip_crop[2] && vip_crop[3]) {
		if(get_crop_info(inWidth, inHeight, vip_crop))
			goto retry;
		data->vip_crop = true;
		data->crop[0] = vip_crop[0];
		data->crop[1] = vip_crop[1];
		data->crop[2] = vip_crop[2];
		data->crop[3] = vip_crop[3];
		CAM_DBG("vip crop - %d:%d:%d:%d\n", vip_crop[0], vip_crop[1], vip_crop[2], vip_crop[3]);
	}

	if (outWidth && outHeight) {
		int width = inWidth, height = inHeight;

		if (data->vip_crop) {
			width = vip_crop[2];
			height = vip_crop[3];
		}
		if (scaling_crop[2] && scaling_crop[3]) {
			if(get_crop_info(width, height, scaling_crop))
				goto retry;
			CAM_DBG("scaling enable - dest width:%d height:%d\n", outWidth, outHeight);
			data->scale.crop[0] = scaling_crop[0];
			data->scale.crop[1] = scaling_crop[1];
			data->scale.crop[2] = scaling_crop[2];
			data->scale.crop[3] = scaling_crop[3];
			CAM_DBG("scaling crop - %d:%d:%d:%d\n", scaling_crop[0], scaling_crop[1],
					scaling_crop[2], scaling_crop[3]);
			data->scaling = true;
			data->scale.srcWidth = width;
			data->scale.srcHeight = height;
			data->scale.dstWidth = outWidth;
			data->scale.dstHeight = outHeight;
			CAM_DBG("scaling enable - dest width:%d height:%d\n", outWidth, outHeight);
		} else {
			if (decimator) {
				CAM_DBG("decimation - src %d:%d to dst %d:%d\n", width, height,
					outWidth, outHeight);
				if ((outWidth > width) || (outHeight > height)) {
					CAM_DBG("can't use decimator for up scaling\n");
					goto retry;
				}
				data->scale.dstWidth = outWidth;
				data->scale.dstHeight = outHeight;
			}
		}
	}

	if (file_save > count) {
		CAM_ERR("save frame: %d can't be over repeat count:%d\n",
				file_save, count);
		data->file_save = count;
	}
	data->module = module;
	data->type = (clipper) ? clipper : 0;
	data->type |= (decimator) ? decimator << 1 : 0;
	data->width = inWidth;
	data->height = inHeight;
	data->display = display;
	data->count = count;
	data->file_save = file_save;
	data->fps_enable = fps_enable;

	if (!format)
		data->format = V4L2_PIX_FMT_YUV420;
	else if (format == fmt_yvu420) {
		if (data->display) {
			CAM_ERR("Spec Over: Display can't support YVU420 format\n");
			goto retry;
		}
		if (data->scaling) {
			CAM_ERR("Spec Over: Scaler can't support YVU420 format\n");
			goto retry;
		}
		data->format = V4L2_PIX_FMT_YVU420;
	} else if (format == fmt_yuv422) {
		data->format = V4L2_PIX_FMT_YUV422P;
		if (data->scaling) {
			CAM_ERR("Currently Scaler can't support YUV422 format\n");
			goto retry;
		}
	} else if (format == fmt_yuv444) {
		data->format = V4L2_PIX_FMT_YUV444;
		if (data->scaling) {
			CAM_ERR("Currently Scaler can't support YUV444 format\n");
			goto retry;
		}
	} else if (format == fmt_yuyv) {
		if (data->type & enb_deci) {
			CAM_ERR("Spec Over: Decimator can't support YUYV format\n");
			goto retry;
		}
		if (data->scaling) {
			CAM_ERR("Spec Over: Scaler can't support YUYV format\n");
			goto retry;
		}
		data->format = V4L2_PIX_FMT_YUYV;
	} else
		return -EINVAL;

	return NO_ERROR;
retry:
	CAM_ERR("Error : Invalid arguments!!!\n");
	print_usage(argv[0]);
	return -EINVAL;
}

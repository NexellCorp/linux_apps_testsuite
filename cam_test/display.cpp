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
#include <errno.h>
#include <string.h>

#include <linux/videodev2.h>

#include <drm_fourcc.h>
#include "dp.h"
#include "dp_common.h"

#include "xf86drm.h"
#include <nx-drm-allocator.h>

#include "globaldef.h"
#include "debug.h"
#include "display.h"

#define NX_PLANE_TYPE_RGB       (0<<4)
#define NX_PLANE_TYPE_VIDEO     (1<<4)
#define NX_PLANE_TYPE_UNKNOWN   (0xFFFFFFF)

static int convert_formats(uint32_t format)
{
	int ret = -EINVAL;

	switch (format)
	{
	case V4L2_PIX_FMT_YUV420:
		ret = DRM_FORMAT_YUV420;
		break;
	case V4L2_PIX_FMT_YUV422P:
		ret = DRM_FORMAT_YUV422;
		break;
	case V4L2_PIX_FMT_YUV444:
		ret = DRM_FORMAT_YUV444;
		break;
	case V4L2_PIX_FMT_YUYV:
		ret = DRM_FORMAT_YUYV;
		break;
	default:
		CAM_ERR("format:0x%x is not supported in display\n", format);
		break;
	}
	return ret;
}

static int get_plane_prop_id_by_property_name(int drm_fd, uint32_t plane_id,
		char *prop_name)
{
	int res = -1;
	drmModeObjectPropertiesPtr props;
	props = drmModeObjectGetProperties(drm_fd, plane_id,
			DRM_MODE_OBJECT_PLANE);

	if (props) {
		uint32_t i;

		for (i = 0; i < props->count_props; i++) {
			drmModePropertyPtr this_prop;
			this_prop = drmModeGetProperty(drm_fd, props->props[i]);

			if (this_prop) {
				CAM_DBG("prop name : %s, prop id: %d, param prop id: %s\n",
						this_prop->name,
						this_prop->prop_id,
						prop_name
						);

				if (!strncmp(this_prop->name, prop_name,
							DRM_PROP_NAME_LEN)) {

					res = this_prop->prop_id;

					drmModeFreeProperty(this_prop);
					break;
				}
				drmModeFreeProperty(this_prop);
			}
		}
		drmModeFreeObjectProperties(props);
	}

	return res;
}

static int set_priority_video_plane(struct dp_device *device, uint32_t plane_idx,
		uint32_t set_idx)
{
	uint32_t plane_id = device->planes[plane_idx]->id;
	uint32_t prop_id = get_plane_prop_id_by_property_name(device->fd,
							plane_id,
							(char*)"video-priority");
	int res = -1;

	res = drmModeObjectSetProperty(device->fd,
			plane_id,
			DRM_MODE_OBJECT_PLANE,
			prop_id,
			set_idx);

	return res;
}

static int get_plane_index_by_type(struct dp_device *device,
		uint32_t port, uint32_t type)
{
	uint32_t i = 0, j = 0;
	int ret = -1;
	drmModeObjectPropertiesPtr props;
	int plane_type = -1;
	int find_idx = 0;
	int prop_type = -1;

	/* type overlay or primary or cursor */
	int layer_type = type & 0x3;
	/*	display_type video : 1 << 4, rgb : 0 << 4	*/
	int display_type = type & 0xf0;

	for (i = 0; i < device->num_planes; i++) {
		props = drmModeObjectGetProperties(device->fd,
					device->planes[i]->id,
					DRM_MODE_OBJECT_PLANE);
		if (!props)
			return -ENODEV;

		prop_type = -1;
		plane_type = NX_PLANE_TYPE_VIDEO;

		for (j = 0; j < props->count_props; j++) {
			drmModePropertyPtr prop;

			prop = drmModeGetProperty(device->fd, props->props[j]);
			if (prop) {
				CAM_DBG("plane.%2d %d.%d [%s]\n",
					device->planes[i]->id,
					props->count_props,
					j,
					prop->name);

				if (strcmp(prop->name, "type") == 0)
					prop_type = (int)props->prop_values[j];

				if (strcmp(prop->name, "alphablend") == 0)
					plane_type = NX_PLANE_TYPE_RGB;

				drmModeFreeProperty(prop);
			}
		}
		drmModeFreeObjectProperties(props);

		CAM_DBG("prop type : %d, layer type : %d\n",
				prop_type, layer_type);
		CAM_DBG("disp type : %d, plane type : %d\n",
				display_type, plane_type);
		CAM_DBG("find idx : %d, port : %d\n\n",
				find_idx, port);

		if (prop_type == layer_type && display_type == plane_type
				&& (uint32_t)find_idx == port) {
			ret = i;
			break;
		}

		if (prop_type == layer_type && display_type == plane_type)
			find_idx++;

		if ((uint32_t)find_idx > port)
			break;
	}

	return ret;
}

struct dp_device *dp_device_init(int drmFd)
{
	struct dp_device *device = NULL;
	int err;

	err = drmSetClientCap(drmFd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	if (err < 0) {
		DP_ERR("drmSetClientCap() failed: %d %m\n", err);
	}

	device = dp_device_open(drmFd);
	if (!device)
		CAM_ERR("failed to open display device\n");
	return device;
}

struct dp_plane* dp_plane_init(struct dp_device *device, uint32_t format)
{
	struct dp_plane *plane = NULL;
	int32_t video_pidx;

	video_pidx = get_plane_index_by_type(device, 0,
			DRM_PLANE_TYPE_OVERLAY | NX_PLANE_TYPE_VIDEO);
	if (video_pidx < 0) {
		CAM_ERR("failed to video plane index:%d\n", video_pidx);
		return plane;
	}
	plane = device->planes[video_pidx];
	if (!plane) {
		CAM_ERR("failed to get plane:%d\n", video_pidx);
		return plane;
	}
	if (!dp_plane_supports_format(plane, convert_formats(format))) {
		CAM_ERR("the format:%s is not supported on video plane\n",
				dp_forcc_name(format));
		return NULL;
	}

	/* set priority */
	if (set_priority_video_plane(device, video_pidx, 1))
		CAM_ERR("failed to set the priority of the video plane\n");
	return plane;
}

struct dp_framebuffer *dp_buffer_add(struct dp_device *device,
		uint32_t format, uint32_t width, uint32_t height,
		bool seperated,
		int gem_fd, uint32_t size, bool interlaced)
{
	struct dp_framebuffer *fb = NULL;

	if (interlaced)
		fb = dp_framebuffer_interlace_config(device,
				convert_formats(format),
				width, height, seperated,
				gem_fd, size);
	else
		fb = dp_framebuffer_config(device,
				convert_formats(format),
				width, height, seperated,
				gem_fd, size);
	if (!fb) {
		CAM_ERR("failed to create framebuffer\n");
		return fb;
	}
	if (dp_framebuffer_addfb2(fb)) {
		CAM_ERR("failed to add framebuffer\n");
		dp_framebuffer_free(fb);
		return NULL;
	}
	return fb;
}

int dp_plane_update(struct dp_device *device, struct dp_framebuffer *fb, struct dp_plane *plane,
		uint32_t crop_x, uint32_t crop_y, uint32_t crop_w, uint32_t crop_h,
		uint32_t dw, uint32_t dh)
{
	int err;

	err = dp_plane_set(plane, fb, 0, 0, (int)dw, (int)dh, crop_x, crop_y, crop_w, crop_h);
	if(err < 0) {
		CAM_ERR("set plane failed \n");
		return -EINVAL;
	}

	return NO_ERROR;
}

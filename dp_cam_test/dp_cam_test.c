#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#include <drm_fourcc.h>
#include "xf86drm.h"
#include "dp.h"
#include "dp_common.h"
#include <nx-drm-allocator.h>
#include <inttypes.h>
#include <assert.h>

#include <linux/videodev2.h>
#include "media-bus-format.h"
#include "nexell_drmif.h"
#include "nx-v4l2.h"

#include "option.h"

#ifndef ALIGN
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#endif

#define PAGE_SIZE 4096

#define MAX_BUFFER_COUNT	4

#define YUV_STRIDE_ALIGN_FACTOR		64
#define YUV_STRIDE(w)	ALIGN(w, YUV_STRIDE_ALIGN_FACTOR)

#define NX_PLANE_TYPE_RGB       (0<<4)
#define NX_PLANE_TYPE_VIDEO     (1<<4)
#define NX_PLANE_TYPE_UNKNOWN   (0xFFFFFFF)

#define VIDEO_FORMAT_FIRST_INDEX	-1
#define RGB_FORMAT_FIRST_INDEX		13

static const uint32_t dp_formats[] = {

	/* 1 buffer */
    DRM_FORMAT_YUYV,
    DRM_FORMAT_YVYU,
    DRM_FORMAT_UYVY,
    DRM_FORMAT_VYUY,

    /* 2 buffer */
    DRM_FORMAT_NV12,	/* NV12 : 2x2 subsampled Cr:Cb plane */
    DRM_FORMAT_NV21,    /* NV21 : 2x2 subsampled Cb:Cr plane */
    DRM_FORMAT_NV16,    /* NV16 : 2x1 subsampled Cr:Cb plane */
    DRM_FORMAT_NV61,    /* NV61 : 2x1 subsampled Cb:Cr plane */
//	DRM_FORMAT_NV24,    /* NV24 : non-subsampled Cr:Cb plane */
//	DRM_FORMAT_NV42,    /* NV42 : non-subsampled Cb:Cr plane */

	/* 3 buffer */
	#if 0
    DRM_FORMAT_YUV410, 	/* YUV9 : 4x4 subsampled Cb (1) and Cr (2) planes */
    DRM_FORMAT_YVU410,	/* YVU9 : 4x4 subsampled Cr (1) and Cb (2) planes */
    DRM_FORMAT_YUV411,	/* YU11 : 4x1 subsampled Cb (1) and Cr (2) planes */
    DRM_FORMAT_YVU411,	/* YV11 : 4x1 subsampled Cr (1) and Cb (2) planes */
    #endif
    DRM_FORMAT_YUV420,	/* YU12 : 2x2 subsampled Cb (1) and Cr (2) planes */
    DRM_FORMAT_YVU420,	/* YV12 : 2x2 subsampled Cr (1) and Cb (2) planes */
    DRM_FORMAT_YUV422,	/* YU16 : 2x1 subsampled Cb (1) and Cr (2) planes */
    DRM_FORMAT_YVU422,	/* YV16 : 2x1 subsampled Cr (1) and Cb (2) planes */
    DRM_FORMAT_YUV444,	/* YU24 : non-subsampled Cb (1) and Cr (2) planes */
    DRM_FORMAT_YVU444,	/* YV24 : non-subsampled Cr (1) and Cb (2) planes */

    /* RGB 1 buffer */
    DRM_FORMAT_RGB565,
    DRM_FORMAT_BGR565,
    DRM_FORMAT_RGB888,
    DRM_FORMAT_BGR888,
    DRM_FORMAT_ARGB8888,
    DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
};

struct dp_framebuffer *dp_framebuffer_interlace_config(struct dp_device *device,
	uint32_t format, int width, int height, bool seperate,
	int gem_fd, int size);

int get_plane_index_by_type(struct dp_device *device, uint32_t port, uint32_t type);

static size_t calc_alloc_size(uint32_t w, uint32_t h, uint32_t f)
{
	uint32_t y_stride = ALIGN(w, 32);
	uint32_t y_size = y_stride * ALIGN(h, 16);
	size_t size = 0;

	switch (f) {
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		size = y_size << 1;
		break;

	case V4L2_PIX_FMT_YUV420:
		size = y_size +
			2 * (ALIGN(y_stride >> 1, 16) * ALIGN(h >> 1, 16));
		break;

	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV12:
		size = y_size + y_stride * ALIGN(h >> 1, 16);
		break;
	}
	DP_DBG("[%s] format = %u, size = %d \n ",__func__,f,size);

	return size;
}

static size_t calc_alloc_size_interlace(uint32_t w, uint32_t h, uint32_t f, uint32_t t)
{
	uint32_t y_stride = ALIGN(w, 128);
	uint32_t y_size = y_stride * ALIGN(h, 16);
	size_t size = 0;
	uint32_t y = 0, cb = 0, cr = 0;

	switch (f) {
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		size = y_size << 1;
		break;

	case V4L2_PIX_FMT_YUV420:
		size = y_size +
			2 * (ALIGN(w >> 1, 64) * ALIGN(h >> 1, 16));
		y = y_size;
		cb = (ALIGN(w >> 1, 64) * ALIGN(h >> 1, 16));
		cr = (ALIGN(w >> 1, 64) * ALIGN(h >> 1, 16));
		break;

	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV12:
		size = y_size + y_stride * ALIGN(h >> 1, 16);
		break;
	}
	DP_DBG("[%s] format = %u, size = %d \n ",__func__,f,size);

	return size;
}

static uint32_t get_pixel_byte(uint32_t rgb_format)
{
	switch (rgb_format) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		return 2;

	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
		return 3;

	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
		return 4;

	default:
		DP_ERR("%s: not support drm_rgb_format(0x%x)\n",
				__func__, rgb_format);
		return 0;
	}
}

static size_t calc_alloc_size_rgb(uint32_t w, uint32_t h, uint32_t f_idx)
{
	uint32_t format = dp_formats[f_idx];
	uint32_t pixelbyte = get_pixel_byte(format);
	size_t size = 0;

	size = w * h * pixelbyte;
	size = ALIGN(size, PAGE_SIZE);

	return size;
}

static uint32_t choose_format(struct dp_plane *plane, int select)
{
	uint32_t format;
	int size = ARRAY_SIZE(dp_formats);

	if (select > (size-1)) {
		DP_ERR("fail : not support format index (%d) over size %d\n",
			select, size);
		return -EINVAL;
	}
	format = dp_formats[select];

	if (!dp_plane_supports_format(plane, format)) {
		DP_ERR("fail : not support %s\n", dp_forcc_name(format));
		return -EINVAL;
	}

	DP_DBG("format: %s\n", dp_forcc_name(format));

	return format;
}

struct dp_device * dp_device_init(int fd)
{
	struct dp_device *device;
	int err;

	err = drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	if (err < 0) {
		DP_ERR("drmSetClientCap() failed: %d %m\n", err);
	}

	device = dp_device_open(fd);
	if (!device) {
		DP_ERR("fail : device open() %m\n");
		return NULL;
	}

	return device;
}

int dp_plane_update(struct dp_device *device, struct dp_framebuffer *fb, uint32_t crop_x, uint32_t crop_y,
		uint32_t crop_width, uint32_t crop_height, uint32_t dw, uint32_t dh, uint32_t p)
{
	int err;
	struct dp_plane *plane;
	uint32_t video_type, video_index;

	video_type = DRM_PLANE_TYPE_OVERLAY | NX_PLANE_TYPE_VIDEO;
	video_index = get_plane_index_by_type(device, p, video_type);
	if (video_index < 0) {
		DP_ERR("fail : not found matching layer\n");
		return -EINVAL;
	}

	plane = device->planes[video_index];
	if (!plane) {
		DP_ERR("no overlay plane found\n");
		return -EINVAL;
	}

	//err = dp_plane_set(plane, fb, 0, 0, (int)dw, (int)dh, 0, 0, w, h);
	err = dp_plane_set(plane, fb, 0, 0, (int)dw, (int)dh, crop_x, crop_y, crop_width, crop_height);
	
	if(err<0) {
		DP_ERR("set plane failed \n");
		return -EINVAL;
	}

	return 1;
}

struct dp_framebuffer * dp_buffer_init(
	struct dp_device *device, int  x, int y, int gem_fd, uint32_t p, uint32_t use_max9286)
{
	struct dp_framebuffer *fb = NULL;
	int op_format = 8/*YUV420*/;
	struct dp_plane *plane;

	uint32_t format;
	int err;
	uint32_t video_type, video_index;

	video_type = DRM_PLANE_TYPE_OVERLAY | NX_PLANE_TYPE_VIDEO;
	video_index = get_plane_index_by_type(device, p, video_type);
	if (video_index < 0) {
		DP_ERR("fail : not found matching layer\n");
		return NULL;
	}

	plane = device->planes[video_index];
	if (!plane) {
		DP_ERR("no overlay plane found\n");
		return NULL;
	}

	/*
	 * set plane format
	 */
	format = choose_format(plane, op_format);
	if (!format) {
		DP_ERR("fail : no matching format found\n");
		return NULL;
	}

	DP_DBG("format is %d\n",format);
	fb = dp_framebuffer_config(device, format, x, y, 0, gem_fd,
		calc_alloc_size(x,y,format));

	if (!fb) {
		DP_ERR("fail : framebuffer create Fail \n");
		return NULL;
	}

	if(use_max9286 == 1)
		fb->height += 1;

	err = dp_framebuffer_addfb2(fb);
	if (err<0) {
		DP_ERR("fail : framebuffer add Fail \n");
		if (fb) {
			dp_framebuffer_free(fb);
		}

		return NULL;
	}

	if(use_max9286 == 1)
		fb->height -= 1;

	return fb;
}

void get_display_resolution(struct dp_device *device,
		uint32_t *width, uint32_t *height)
{
	int i = 0;

	DP_DBG("Screens: %u\n", device->num_screens);

	for (i = 0; i < device->num_screens; i++) {
		struct dp_screen *screen = device->screens[i];
		const char *status = "disconnected";

		if (screen->connected) {
			status = "connected";

			*width = screen->width;
			*height = screen->height;
		}

		DP_DBG("  %u: %x\n", i, screen->id);
		DP_DBG("    Status: %s\n", status);
		DP_DBG("    Name: %s\n", screen->name);
		DP_DBG("    Resolution: %ux%u\n", screen->width,
				screen->height);
	}

}

/* type : DRM_PLANE_TYPE_OVERLAY | NX_PLANE_TYPE_VIDEO */
int get_plane_index_by_type(struct dp_device *device, uint32_t port, uint32_t type)
{
	int i = 0, j = 0;
	int ret = -1;
	drmModeObjectPropertiesPtr props;
	uint32_t plane_type = -1;
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
				DP_DBG("plane.%2d %d.%d [%s]\n",
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

		DP_DBG("prop type : %d, layer type : %d\n",
				prop_type, layer_type);
		DP_DBG("disp type : %d, plane type : %d\n",
				display_type, plane_type);
		DP_DBG("find idx : %d, port : %d\n\n",
				find_idx, port);

		if (prop_type == layer_type && display_type == plane_type
				&& find_idx == port) {
			ret = i;
			break;
		}

		if (prop_type == layer_type && display_type == plane_type)
			find_idx++;

		if (find_idx > port)
			break;
	}

	return ret;
}

int get_plane_prop_id_by_property_name(int drm_fd, uint32_t plane_id,
		char *prop_name)
{
	int res = -1;
	drmModeObjectPropertiesPtr props;
	props = drmModeObjectGetProperties(drm_fd, plane_id,
			DRM_MODE_OBJECT_PLANE);

	if (props) {
		int i;

		for (i = 0; i < props->count_props; i++) {
			drmModePropertyPtr this_prop;
			this_prop = drmModeGetProperty(drm_fd, props->props[i]);

			if (this_prop) {
				DP_DBG("prop name : %s, prop id: %d, param prop id: %s\n",
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

int set_priority_video_plane(struct dp_device *device, uint32_t plane_idx,
		uint32_t set_idx)
{
	uint32_t plane_id = device->planes[plane_idx]->id;
	uint32_t prop_id = get_plane_prop_id_by_property_name(device->fd,
							plane_id,
							"video-priority");
	int res = -1;

	res = drmModeObjectSetProperty(device->fd,
			plane_id,
			DRM_MODE_OBJECT_PLANE,
			prop_id,
			set_idx);

	return res;
}

struct dp_framebuffer * dp_buffer_init_interlace(
	struct dp_device *device, int  x, int y, int gem_fd, uint32_t t,
	uint32_t p)
{
	struct dp_framebuffer *fb = NULL;
	int op_format = 8/*YUV420*/;
	struct dp_plane *plane;

	uint32_t format;
	int err;

	uint32_t video_type, video_index;

	video_type = DRM_PLANE_TYPE_OVERLAY | NX_PLANE_TYPE_VIDEO;
	video_index = get_plane_index_by_type(device, p, video_type);
	if (video_index < 0) {
		DP_ERR("fail : not found matching layer\n");
		return NULL;
	}
	plane = device->planes[video_index];

	/*
	 * set plane format
	 */
	format = choose_format(plane, op_format);
	if (!format) {
		DP_ERR("fail : no matching format found\n");
		return NULL;
	}

	DP_DBG("format is %d\n",format);

	if (t == V4L2_FIELD_INTERLACED)
		fb = dp_framebuffer_interlace_config(device, format, x, y, 0, gem_fd,
			calc_alloc_size_interlace(x, y, format, t));
	else
		fb = dp_framebuffer_config(device, format, x, y, 0, gem_fd,
			calc_alloc_size(x,y,format));
	if (!fb)
	{
		DP_ERR("fail : framebuffer create Fail \n");
		return NULL;
	}

	err = dp_framebuffer_addfb2(fb);
	if (err < 0)
	{
		DP_ERR("fail : framebuffer add Fail \n");
		if (fb)
		{
			dp_framebuffer_free(fb);
		}
		return NULL;
	}

	return fb;
}

void dealloc_rgb_framebuffer(int drm_fd, struct dp_framebuffer **fb,
		int *dma_fd, int *gem_fd)
{
	if (*fb) {
		dp_framebuffer_delfb2(*fb);
		*fb = NULL;
	}

	if (*dma_fd >= 0)
		close(*dma_fd);

	if (*gem_fd >= 0)
		nx_free_gem(drm_fd, *gem_fd);
}

int alloc_rgb_framebuffer(struct dp_device *device, uint32_t w, uint32_t h,
			uint32_t f_idx, int drm_fd, int *gem_fd, int *dma_fd,
			struct dp_framebuffer **fb, void **vaddr, uint32_t p)
{
	size_t alloc_size = 0;
	uint32_t rgb_type, rgb_index;
	struct dp_plane *plane;
	int err;
	uint32_t format;

	alloc_size = calc_alloc_size_rgb(w, h, f_idx);
	if (alloc_size <= 0) {
		DP_ERR("invalid alloc size %lu\n", alloc_size);
		return -1;
	}

	*gem_fd = nx_alloc_gem(drm_fd, alloc_size, 0);
	if (gem_fd < 0) {
		DP_ERR("failed to alloc_gem\n");
		return -2;
	}

	*dma_fd = nx_gem_to_dmafd(drm_fd, *gem_fd);
	if (dma_fd < 0) {
		DP_ERR("failed to gem_to_dmafd\n");
		return -3;
	}

	rgb_type = DRM_PLANE_TYPE_OVERLAY | NX_PLANE_TYPE_RGB;
	rgb_index = get_plane_index_by_type(device, p, rgb_type);
	if (rgb_index < 0) {
		DP_ERR("fail : not found matching layer\n");
		return -4;
	}
	plane = device->planes[rgb_index];

	format = choose_format(plane, f_idx);
	if (!format) {
		DP_ERR("fail : no matching format found\n");
		return -5;
	}

	*fb = dp_framebuffer_config(device, format, w, h, 0, *gem_fd,
			alloc_size);
	if (!(*fb)) {
		DP_ERR("fail : framebuffer create Fail \n");
		return -6;
	}

	err = dp_framebuffer_addfb2(*fb);
	if (err < 0) {
		DP_ERR("fail : framebuffer add Fail \n");
		if (*fb)
			dp_framebuffer_free(*fb);

		return -7;
	}

	if (get_vaddr(drm_fd, *gem_fd, alloc_size, vaddr)) {
		printf( "failed to get_vaddr gem_fd : %d", *gem_fd);
		return -8;
	}

	return 0;
}

void draw_rgb_overlay(int width, int height, uint32_t pixelbyte, void *vaddr)
{
	DP_DBG("width : %d, height : %d, pixelbyte : %d, vaddr : %p\n",
			width, height, pixelbyte, vaddr);

	if (vaddr != NULL) {
		memset(vaddr, 0, width * height * pixelbyte);
		/* draw red box at (0, 0) -- (150, 150) */
		{
			uint32_t color = 0xffff0000;
			int i, j;
			uint32_t *pbuf = (uint32_t *)vaddr;

			for (i = 0; i < 150; i++) {
				for (j = 0; j < 150; j++) {
					pbuf[i * width + j] = color;
				}
			}
		}
	}
}

int camera_test(struct dp_device *device, int drm_fd, DP_CAM_INFO mDPCamInfo)
{
	int ret;
	int gem_fds[MAX_BUFFER_COUNT] = { -1, };
	int dma_fds[MAX_BUFFER_COUNT] = { -1, };
	struct dp_framebuffer *fbs[MAX_BUFFER_COUNT] = {NULL,};
	size_t alloc_size = 0;

	uint32_t video_type, video_index;
	uint32_t rgb_type, rgb_index;
	int res = -1;

	int rgb_gem_fd = -1;
	int rgb_dma_fd = -1;
	struct dp_framebuffer *rgb_fb = NULL;
	struct dp_plane *plane;

	uint32_t dp_width = 0;
	uint32_t dp_height = 0;
	uint32_t dw = 0;
	uint32_t dh = 0;

	void *vaddr;
	uint32_t pixelbyte;
	int format;
	int overlay = 0;

	DP_DBG("m: %d, w: %d, h: %d, sw : %d, sh : %d, f: %d, bus_f: %d, c: %d, type : %d\n",
	       mDPCamInfo.m, mDPCamInfo.w, mDPCamInfo.h, mDPCamInfo.sw, mDPCamInfo.sh, mDPCamInfo.f, mDPCamInfo.bus_f, mDPCamInfo.count, mDPCamInfo.t);

	// workaround code
	if (mDPCamInfo.f == 0)
		mDPCamInfo.f = V4L2_PIX_FMT_YUV420;

	switch (mDPCamInfo.bus_f) {
	case 0:
		mDPCamInfo.bus_f = MEDIA_BUS_FMT_YUYV8_2X8;
		break;
	case 1:
		mDPCamInfo.bus_f = MEDIA_BUS_FMT_UYVY8_2X8;
		break;
	case 2:
		mDPCamInfo.bus_f = MEDIA_BUS_FMT_VYUY8_2X8;
		break;
	case 3:
		mDPCamInfo.bus_f = MEDIA_BUS_FMT_YVYU8_2X8;
		break;
	};

	switch (mDPCamInfo.t) {
	case 0:
		mDPCamInfo.t = V4L2_FIELD_ANY;
		break;
	case 1:
		mDPCamInfo.t = V4L2_FIELD_NONE;
		break;
	case 2:
		mDPCamInfo.t = V4L2_FIELD_INTERLACED;
		break;
	default:
		mDPCamInfo.t = V4L2_FIELD_ANY;
		break;
	};

	int sensor_fd = nx_v4l2_open_device(nx_sensor_subdev, mDPCamInfo.m);
	if (sensor_fd < 0) {
		DP_ERR("failed to open camera %d sensor\n", mDPCamInfo.m);
		return -1;
	}

	bool is_mipi = nx_v4l2_is_mipi_camera(mDPCamInfo.m);

	int csi_subdev_fd;
	if (is_mipi) {
		csi_subdev_fd = nx_v4l2_open_device(nx_csi_subdev, mDPCamInfo.m);
		if (csi_subdev_fd < 0) {
			DP_ERR("failed open mipi csi\n");
			return -1;
		}
	}

	int clipper_subdev_fd = nx_v4l2_open_device(nx_clipper_subdev, mDPCamInfo.m);
	if (clipper_subdev_fd < 0) {
		DP_ERR("failed to open clipper_subdev %d\n", mDPCamInfo.m);
		return -1;
	}

	int clipper_video_fd = nx_v4l2_open_device(nx_clipper_video, mDPCamInfo.m);
	if (clipper_video_fd < 0) {
		DP_ERR("failed to open clipper_video %d\n", mDPCamInfo.m);
		return -1;
	}

	nx_v4l2_streamoff(clipper_video_fd, nx_clipper_video);

	ret = nx_v4l2_link(true, mDPCamInfo.m, nx_clipper_subdev, 1,
			   nx_clipper_video, 0);

	if (is_mipi) {
		// link sensor to mipi csi
		ret = nx_v4l2_link(true, mDPCamInfo.m, nx_sensor_subdev, 0, nx_csi_subdev,
				   0);
		if (ret) {
			DP_ERR("failed to link sensor to csi\n");
			return ret;
		}

		// link mipi csi to clipper subdev
		ret = nx_v4l2_link(true, mDPCamInfo.m, nx_csi_subdev, 1, nx_clipper_subdev,
				   0);
		if (ret) {
			DP_ERR("failed to link csi to clipper\n");
			return ret;
		}
	} else {
		ret = nx_v4l2_link(true, mDPCamInfo.m, nx_sensor_subdev, 0,
				   nx_clipper_subdev, 0);
		if (ret) {
			DP_ERR("failed to link sensor to clipper\n");
			return ret;
		}
	}

	ret = nx_v4l2_set_format(sensor_fd, nx_sensor_subdev, mDPCamInfo.w, mDPCamInfo.h,
				mDPCamInfo.bus_f);
	if (ret) {
		DP_ERR("failed to set_format for sensor\n");
		return ret;
	}

	if (is_mipi) {
		ret = nx_v4l2_set_format(csi_subdev_fd, nx_csi_subdev, mDPCamInfo.w, mDPCamInfo.h, mDPCamInfo.f);
		if (ret) {
			DP_ERR("failed to set_format for csi\n");
			return ret;
		}
	}

	ret = nx_v4l2_set_format(clipper_subdev_fd, nx_clipper_subdev, mDPCamInfo.w, mDPCamInfo.h,
				mDPCamInfo.bus_f);
	if (ret) {
		DP_ERR("failed to set_format for clipper subdev\n");
		return ret;
	}

	ret = nx_v4l2_set_format_with_field(clipper_video_fd, nx_clipper_video,
		mDPCamInfo.w, mDPCamInfo.h, mDPCamInfo.f, mDPCamInfo.t);
	if (ret) {
		DP_ERR("failed to set_format for clipper video\n");
		return ret;
	}

	// set crop
	ret = nx_v4l2_set_crop(clipper_subdev_fd, nx_clipper_subdev, 0, 0, mDPCamInfo.w,
			       mDPCamInfo.h);
	if (ret) {
		DP_ERR("failed to set_crop for clipper subdev\n");
		return ret;
	}

	if (mDPCamInfo.sw > 0 && mDPCamInfo.sh > 0) {
		ret = nx_v4l2_set_crop(clipper_video_fd, nx_clipper_video,
				0, 0, mDPCamInfo.sw, mDPCamInfo.sh);
		if (ret) {
			DP_ERR("failed to set_crop for clipper subdev\n");
			return ret;
		}
	}

	ret = nx_v4l2_reqbuf(clipper_video_fd, nx_clipper_video,
			     MAX_BUFFER_COUNT);
	if (ret) {
		DP_ERR("failed to reqbuf\n");
		return ret;
	}

	if (mDPCamInfo.t == V4L2_FIELD_INTERLACED)
		alloc_size = calc_alloc_size_interlace(mDPCamInfo.w, mDPCamInfo.h, mDPCamInfo.f, mDPCamInfo.t);
	else
		alloc_size = calc_alloc_size(mDPCamInfo.w, mDPCamInfo.h, mDPCamInfo.f);

	if (alloc_size <= 0) {
		DP_ERR("invalid alloc size %lu\n", alloc_size);
		return -1;
	}

	int i;

	for (i = 0; i < MAX_BUFFER_COUNT; i++) {
		int gem_fd = nx_alloc_gem(drm_fd, alloc_size, 0);
		if (gem_fd < 0) {
			DP_ERR("failed to alloc_gem\n");
			return -1;
		}

		int dma_fd = nx_gem_to_dmafd(drm_fd, gem_fd);
		if (dma_fd < 0) {
			DP_ERR("failed to gem_to_dmafd\n");
			return -1;
		}

		struct dp_framebuffer *fb;

		if (mDPCamInfo.t == V4L2_FIELD_INTERLACED)
			fb = dp_buffer_init_interlace(device, mDPCamInfo.w, mDPCamInfo.h, gem_fd, mDPCamInfo.t,
					mDPCamInfo.port);
		else
			fb = dp_buffer_init(device, mDPCamInfo.w, mDPCamInfo.h, gem_fd, mDPCamInfo.port, mDPCamInfo.use_max9286);
		if (!fb) {
			DP_ERR("fail : framebuffer Init %m\n");
			ret = -1;
			return ret;
		}

		fbs[i] = fb;
		gem_fds[i] = gem_fd;
		dma_fds[i] = dma_fd;
	}

	/*	qbuf	*/
	for (i = 0; i < MAX_BUFFER_COUNT; i++) {
		ret = nx_v4l2_qbuf(clipper_video_fd, nx_clipper_video, 1, i,
				   &dma_fds[i], (int *)&alloc_size);
		if (ret) {
			DP_ERR("failed qbuf index %d\n", i);
			return ret;
		}
	}

	/*	set video priority	*/
	video_type = DRM_PLANE_TYPE_OVERLAY | NX_PLANE_TYPE_VIDEO;
	video_index = get_plane_index_by_type(device, mDPCamInfo.port, video_type);
	if (video_index < 0) {
		DP_ERR("fail : not found matching layer\n");
		return -1;
	}

	rgb_type = DRM_PLANE_TYPE_OVERLAY | NX_PLANE_TYPE_RGB;
	rgb_index = get_plane_index_by_type(device, mDPCamInfo.port, rgb_type);
	if (rgb_index < 0) {
		DP_ERR("fail : not found matching layer\n");
		return -1;
	}

	res = set_priority_video_plane(device, video_index, 1);
	if (res) {
		DP_ERR("failed setting priority : %d\n", res);
		return res;
	}

	get_display_resolution(device, &dp_width, &dp_height);
	if (dp_width <= 0 || dp_height <= 0) {
		DP_ERR("fail : not found supporting resolution. %dx%d\n",
				dp_width, dp_height);
		return -1;
	}

	// camsys camera
	if(mDPCamInfo.dp_width != 0)
		dp_width = mDPCamInfo.dp_width;
	
	if(mDPCamInfo.dp_height != 0)
		dp_height = mDPCamInfo.dp_height;

	if ( mDPCamInfo.overlay_draw_format < 1 || mDPCamInfo.overlay_draw_format > 8) {
		DP_ERR("not support drawing overlay format.\n");
		overlay = 0;
	} else
		overlay = 1;

	if (overlay) {
		/*	alloc framebuffer rgb layer	*/
		res = alloc_rgb_framebuffer(device, dp_width, dp_height,
				RGB_FORMAT_FIRST_INDEX + mDPCamInfo.overlay_draw_format,
				drm_fd, &rgb_gem_fd, &rgb_dma_fd, &rgb_fb,
				&vaddr, mDPCamInfo.port);
		if (res < 0)
			DP_ERR("failed allocation rgb framebuffer : %d\n",
					res);

		plane = device->planes[rgb_index];
		dp_plane_set(plane, rgb_fb, 0, 0, dp_width, dp_height, 0, 0,
				dp_width, dp_height);

		format = choose_format(plane, RGB_FORMAT_FIRST_INDEX + mDPCamInfo.overlay_draw_format);
		pixelbyte = get_pixel_byte(format);

		/* draw overlay */
		draw_rgb_overlay(dp_width, dp_height, pixelbyte, vaddr);
	}

	ret = nx_v4l2_streamon(clipper_video_fd, nx_clipper_video);
	if (ret) {
		DP_ERR("failed to streamon\n");
		return ret;
	}

	int loop_count = mDPCamInfo.count;
	while (loop_count--) {
		int dq_index;

		ret = nx_v4l2_dqbuf(clipper_video_fd, nx_clipper_video, 1,
				    &dq_index);
		if (ret) {
			DP_ERR("failed to dqbuf\n");
			return ret;
		}

		ret = nx_v4l2_qbuf(clipper_video_fd, nx_clipper_video, 1,
				   dq_index, &dma_fds[dq_index],
				   (int *)&alloc_size);
		if (ret) {
			DP_ERR("failed qbuf index %d\n", dq_index);
			return ret;
		}

		if (mDPCamInfo.full_screen) {
			dw = dp_width;
			dh = dp_height;
		} else {
			dw = mDPCamInfo.w;
			dh = mDPCamInfo.h;
		}

		ret = dp_plane_update(device, fbs[dq_index], 
		                      mDPCamInfo.crop_x, mDPCamInfo.crop_y, 
							  mDPCamInfo.crop_width, mDPCamInfo.crop_height, 
							  dw, dh, mDPCamInfo.port);
	/*	if (ret) {
			DP_ERR("failed plane update \n");
			return ret;
		}*/
	}

	nx_v4l2_streamoff(clipper_video_fd, nx_clipper_video);


	// free buffers
	for (i = 0; i < MAX_BUFFER_COUNT; i++) {

		if (fbs[i])
		{
			dp_framebuffer_delfb2(fbs[i]);
		}

		if (dma_fds[i] >= 0)
			close(dma_fds[i]);
		if (gem_fds[i] >= 0)
			nx_free_gem(drm_fd, gem_fds[i]);
	}
	if (overlay)
		dealloc_rgb_framebuffer(drm_fd, &rgb_fb, &rgb_dma_fd,
				&rgb_gem_fd);

	return ret;
}

int main(int argc, char *argv[])
{
	int ret, drm_fd, err;
	struct dp_device *device = NULL;
	int dbg_on = 0;
	
	DP_CAM_INFO mDPCamInfo;

	memset(&mDPCamInfo, 0x0, sizeof(DP_CAM_INFO));
	mDPCamInfo.overlay_draw_format = -1;

	dp_debug_on(dbg_on);
	
	ret = handle_option(argc, argv, &mDPCamInfo);
	
	if (ret) {
		DP_ERR("failed to handle_option\n");
		return ret;
	}

	drm_fd = util_open(device, "nexell");
	if (drm_fd < 0) {
		DP_ERR("failed to open_drm_device\n");
		return -1;
	}

	device = dp_device_init(drm_fd);
	if (device==(struct dp_device *)NULL) {
		DP_ERR("fail : device open() %m\n");
		return -1;
	}

	err = camera_test(device, drm_fd, mDPCamInfo);
	if (err < 0) {
		DP_ERR("failed to do camera_test \n");
		return -1;
	}

	dp_device_close(device);
	drmClose(drm_fd);

	return ret;
}

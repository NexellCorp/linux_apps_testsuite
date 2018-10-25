#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#include <drm_fourcc.h>
#include "xf86drm.h"
#include "dp.h"
#include "dp_common.h"

#include <linux/videodev2.h>
#include "media-bus-format.h"
#include "nexell_drmif.h"
#include "nx-v4l2.h"

#include "option.h"

#ifndef ALIGN
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#endif

#define MAX_BUFFER_COUNT	4

#define CLIPPER		1
#define DECIMATOR	1

#define NX_PLANE_TYPE_RGB       (0<<4)
#define NX_PLANE_TYPE_VIDEO     (1<<4)
#define NX_PLANE_TYPE_UNKNOWN   (0xFFFFFFF)

struct thread_data {
	int drm_fd;
	struct dp_device *device;
	int video_dev;
	int module;
	int width;
	int height;
	int scale_width;
	int scale_height;
	int format;
	int bus_format;
	int count;
	int display_idx;
};

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
	/*	DRM_FORMAT_NV24,*/ /* NV24 : non-subsampled Cr:Cb plane */
	/*	DRM_FORMAT_NV42,*/ /* NV42 : non-subsampled Cb:Cr plane */

	/* 3 buffer */
#if 0
	DRM_FORMAT_YUV410,/* YUV9 : 4x4 subsampled Cb (1) and Cr (2) planes */
	DRM_FORMAT_YVU410,/* YVU9 : 4x4 subsampled Cr (1) and Cb (2) planes */
	DRM_FORMAT_YUV411,/* YU11 : 4x1 subsampled Cb (1) and Cr (2) planes */
	DRM_FORMAT_YVU411,/* YV11 : 4x1 subsampled Cr (1) and Cb (2) planes */
#endif
	DRM_FORMAT_YUV420,/* YU12 : 2x2 subsampled Cb (1) and Cr (2) planes */
	DRM_FORMAT_YVU420,/* YV12 : 2x2 subsampled Cr (1) and Cb (2) planes */
	DRM_FORMAT_YUV422,/* YU16 : 2x1 subsampled Cb (1) and Cr (2) planes */
	DRM_FORMAT_YVU422,/* YV16 : 2x1 subsampled Cr (1) and Cb (2) planes */
	DRM_FORMAT_YUV444,/* YU24 : non-subsampled Cb (1) and Cr (2) planes */
	DRM_FORMAT_YVU444,/* YV24 : non-subsampled Cr (1) and Cb (2) planes */

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
	DP_DBG("[%s] format = %u, size = %d\n", __func__, f, size);

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

	DP_LOG("format: %s\n", dp_forcc_name(format));
	return format;
}

struct dp_device *dp_device_init(int fd)
{
	struct dp_device *device;
	int err;

	err = drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	if (err < 0)
		DP_ERR("drmSetClientCap() failed: %d %m\n", err);

	device = dp_device_open(fd);
	if (!device) {
		DP_ERR("fail : device open() %m\n");
		return NULL;
	}

	return device;
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

int dp_plane_update(struct dp_device *device, struct dp_framebuffer *fb,
	uint32_t w, uint32_t h, int dp_idx)
{
	int d_idx = 1/*overlay*/, p_idx = 0, err;
	struct dp_plane *plane;
	uint32_t video_type, video_index;

	video_type = DRM_PLANE_TYPE_OVERLAY | NX_PLANE_TYPE_VIDEO;
	video_index = get_plane_index_by_type(device, 0, video_type);
	if (video_index < 0) {
		DP_ERR("fail : not found matching layer\n");
		return -EINVAL;
	}
	plane = device->planes[video_index];

	err = dp_plane_set(plane, fb, 0, 0, w, h, 0, 0, w, h);
	if (err < 0) {
		DP_ERR("set plane failed\n");
		return -EINVAL;
	}

	return 1;
}

struct dp_framebuffer *dp_buffer_init(struct dp_device *device, int  x, int y,
int gem_fd, int dp_idx)
{
	struct dp_framebuffer *fb = NULL;
	int d_idx = 1, p_idx = 0, op_format = 8/*YUV420*/;
	struct dp_plane *plane;

	uint32_t format;
	int err;
	uint32_t video_type, video_index;

	video_type = DRM_PLANE_TYPE_OVERLAY | NX_PLANE_TYPE_VIDEO;
	video_index = get_plane_index_by_type(device, 0, video_type);
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

	DP_DBG("format is %d\n", format);

	fb = dp_framebuffer_config(device, format, x, y, 0, gem_fd,
				calc_alloc_size(x, y, format));
	if (!fb) {
		DP_ERR("fail : framebuffer create Fail\n");
		return NULL;
	}

	err = dp_framebuffer_addfb2(fb);
	if (err < 0) {
		DP_ERR("fail : framebuffer add Fail\n");
		if (fb)
			dp_framebuffer_free(fb);

		return NULL;
	}

	err = set_priority_video_plane(device, video_index, 1);
	if (err) {
		DP_ERR("failed setting priority : %d\n", err);
		return NULL;
	}

	return fb;
}

int test_run(struct thread_data *p)
{
	int ret;
	int gem_fds[MAX_BUFFER_COUNT] = { -1, };
	int dma_fds[MAX_BUFFER_COUNT] = { -1, };
	struct dp_framebuffer *fbs[MAX_BUFFER_COUNT] = {NULL,};

	int nx_video = p->video_dev;
	int m = p->module;
	int w = p->width;
	int h = p->height;
	int f = p->format;
	int drm_fd = p->drm_fd;
	struct dp_device *device = p->device;
	int count = p->count;
	int d_idx = p->display_idx;


#if 0
	DP_DBG("m: %d, w: %d, h: sw : %d, sh : %d, f: %d, bus_f: %d, c: %d\n",
	       m, w, h, sw, sh, f, bus_f, count);
#endif

	int video_fd = nx_v4l2_open_device(nx_video, m);

	if (video_fd < 0) {
		DP_ERR("failed to open video %d\n", m);
		return -1;
	}

	nx_v4l2_streamoff(video_fd, nx_video);

	ret = nx_v4l2_set_format(video_fd, nx_video, w, h, f);
	if (ret) {
		DP_ERR("failed to set_format for video\n");
		return ret;
	}

	ret = nx_v4l2_reqbuf(video_fd, nx_video,
			MAX_BUFFER_COUNT);
	if (ret) {
		DP_ERR("failed to clipper reqbuf\n");
		return ret;
	}

	size_t alloc_size = calc_alloc_size(w, h, f);

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

		struct dp_framebuffer *fb = dp_buffer_init(device, w, h,
							gem_fd, d_idx);
		if (!fb) {
			DP_ERR("fail : framebuffer Init %m\n");
			ret = -1;
			return ret;
		}
		fbs[i] = fb;
		gem_fds[i] = gem_fd;
		dma_fds[i] = dma_fd;
	}

	for (i = 0; i < MAX_BUFFER_COUNT; i++) {
		ret = nx_v4l2_qbuf(video_fd, nx_video, 1, i,
				   &dma_fds[i], (int *)&alloc_size);
		if (ret) {
			DP_ERR("failed qbuf index %d\n", i);
			return ret;
		}
	}

	ret = nx_v4l2_streamon(video_fd, nx_video);
	if (ret) {
		DP_ERR("failed to streamon\n");
		return ret;
	}

	int loop_count = count;

	while (loop_count--) {
		int dq_index;

		ret = nx_v4l2_dqbuf(video_fd, nx_video, 1,
				    &dq_index);
		if (ret) {
			DP_ERR("failed to dqbuf\n");
			return ret;
		}

		ret = nx_v4l2_qbuf(video_fd, nx_video, 1,
				   dq_index, &dma_fds[dq_index],
				   (int *)&alloc_size);
		if (ret) {
			DP_ERR("failed qbuf index %d\n", dq_index);
			return ret;
		}
		ret = dp_plane_update(device, fbs[dq_index], w, h, d_idx);
		/*
		if (ret) {
			DP_ERR("failed plane update\n");
			return ret;
		}
		*/
	}

	nx_v4l2_streamoff(video_fd, nx_video);

	for (i = 0; i < MAX_BUFFER_COUNT; i++) {
		if (fbs[i])
			dp_framebuffer_delfb2(fbs[i]);

		if (dma_fds[i] >= 0)
			close(dma_fds[i]);
		if (gem_fds[i] >= 0)
			nx_free_gem(drm_fd, gem_fds[i]);
	}

	close(video_fd);

	return ret;
}

struct dp_device *drm_card_init(int *drm_fd, int card_num)
{
	char path[20];
	struct dp_device *device;

	sprintf(path, "/dev/dri/card%d", card_num);
	*drm_fd = open(path, O_RDWR);
	if (*drm_fd < 0) {
		DP_ERR("failed to handle_option\n");
		return NULL;
	}

	device = dp_device_init(*drm_fd);
	if (device == (struct dp_device *)NULL) {
		DP_ERR("fail : device open() %m");
		return NULL;
	}

	return device;
}

void drm_card_deinit(int drm_fd, struct dp_device *device)
{
	dp_device_close(device);
	close(drm_fd);
}

static void *test_thread(void *data)
{
	struct thread_data *p = (struct thread_data *)data;

	test_run(p);

	return NULL;
}

static struct thread_data s_thread_data0;
static struct thread_data s_thread_data1;
int main(int argc, char *argv[])
{
	int ret, drm_fd;
	uint32_t m, w, h, f, bus_f, count;
	struct dp_device *device;
	int dbg_on = 0;
	uint32_t sw = 0, sh = 0;
	pthread_t clipper_thread, decimator_thread;
	int result_clipper, result_decimator;
	int result[2];

	dp_debug_on(dbg_on);

	ret = handle_option(argc, argv, &m, &w, &h, &sw, &sh, &f, &bus_f,
			&count);
	if (ret) {
		DP_ERR("failed to handle_option\n");
		return ret;
	}

	if (f == 0)
		f = V4L2_PIX_FMT_YUV420;

	switch (bus_f) {
	case 0:
		bus_f = MEDIA_BUS_FMT_YUYV8_2X8;
		break;
	case 1:
		bus_f = MEDIA_BUS_FMT_UYVY8_2X8;
		break;
	case 2:
		bus_f = MEDIA_BUS_FMT_VYUY8_2X8;
		break;
	case 3:
		bus_f = MEDIA_BUS_FMT_YVYU8_2X8;
		break;
	};

#if CLIPPER
	device = drm_card_init(&drm_fd, 0);
	if (device == NULL) {
		DP_ERR("failed to card init\n");
		return -1;
	}
#endif
#if CLIPPER
	s_thread_data0.module = m;
	s_thread_data0.width = w;
	s_thread_data0.height = h;
	s_thread_data0.scale_width = sw;
	s_thread_data0.scale_height = sh;
	s_thread_data0.format = f;
	s_thread_data0.bus_format = bus_f;
	s_thread_data0.count = count;
	s_thread_data0.drm_fd = drm_fd;
	s_thread_data0.device = device;
	s_thread_data0.video_dev = nx_clipper_video;
	s_thread_data0.display_idx = 0;

	ret = pthread_create(&clipper_thread, NULL, test_thread,
			&s_thread_data0);
	if (ret) {
		DP_ERR("failed to start clipper thread: %s", strerror(ret));
		return ret;
	}
#endif
#if DECIMATOR
	s_thread_data1.module = m;
	s_thread_data1.width = w;
	s_thread_data1.height = h;
	s_thread_data1.scale_width = sw;
	s_thread_data1.scale_height = sh;
	s_thread_data1.format = f;
	s_thread_data1.bus_format = bus_f;
	s_thread_data1.count = count;
	s_thread_data1.drm_fd = drm_fd;
	s_thread_data1.device = device;
	s_thread_data1.video_dev = nx_decimator_video;
	s_thread_data1.display_idx = 1;

	ret = pthread_create(&decimator_thread, NULL, test_thread,
			&s_thread_data1);
	if (ret) {
		DP_ERR("failed to start clipper thread: %s", strerror(ret));
		return ret;
	}
#endif
#if CLIPPER
	result_clipper = pthread_join(clipper_thread, (void **)&result[0]);
	if (result_clipper != 0) {
		DP_ERR("fail to clipper pthread join\n");
		return -1;
	}
#endif
#if DECIMATOR
	result_decimator = pthread_join(decimator_thread, (void **)&result[1]);
	if (result_decimator != 0) {
		DP_ERR("fail to decimator pthread join\n");
		return -1;
	}
#endif
	drm_card_deinit(drm_fd, device);

	pthread_exit(0);

	return ret;
}

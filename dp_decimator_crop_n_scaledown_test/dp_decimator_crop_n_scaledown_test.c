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

static int disp_width;
static int disp_height;

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
	struct rect crop;
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
	if (!plane) {
		DP_ERR("no overlay plane found\n");
		return -EINVAL;
	}
	d_idx = dp_idx;

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
		return -EINVAL;
	}

	plane = device->planes[video_index];
	if (!plane) {
		DP_ERR("no overlay plane found\n");
		return NULL;
	}
	d_idx = dp_idx;

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

	return fb;
}

#if CLIPPER
int clipper_test_run(struct thread_data *p)
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
	int video_fd = 0;

	video_fd = nx_v4l2_open_device(nx_video, m);
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

		ret = dp_plane_update(device, fbs[dq_index],
			disp_width, disp_height, d_idx);
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

	if (video_fd > 0)
		close(video_fd);

	return ret;
}
#endif

#if DECIMATOR
int decimator_test_run(struct thread_data *p)
{
	int ret;
	int i;
	int gem_fds[MAX_BUFFER_COUNT] = { -1, };
	int dma_fds[MAX_BUFFER_COUNT] = { -1, };
	struct dp_framebuffer *fbs[MAX_BUFFER_COUNT] = {NULL,};

	int nx_video = p->video_dev;
	int m = p->module;
	int w = p->width;
	int h = p->height;
	int f = p->format;
	int sw = p->scale_width;
	int sh = p->scale_height;
	int drm_fd = p->drm_fd;
	int c_x = p->crop.x;
	int c_y = p->crop.y;
	int c_w = p->crop.width;
	int c_h = p->crop.height;
	struct dp_device *device = p->device;
	int count = p->count;
	int d_idx = p->display_idx;
	int video_fd = 0;
	int subdev_fd = 0;
	size_t alloc_size;
	int gem_fd;
	int dma_fd;

	subdev_fd = nx_v4l2_open_device(nx_decimator_subdev, m);
	if (subdev_fd < 0) {
		DP_ERR("failed to open subdev %d\n", m);
		return -1;
	}

	video_fd = nx_v4l2_open_device(nx_video, m);
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

	if (c_x >= 0 && c_y >= 0 && c_x < w && c_y < h &&
	    c_w > 0 && c_h > 0 && c_w <= w && c_h <= h) {

		ret = nx_v4l2_set_crop(subdev_fd, nx_decimator_subdev,
			c_x, c_y, c_w, c_h);
		if (ret) {
			DP_ERR("failed to set_crop for decimator subdev\n");
			return ret;
		}

		disp_width = c_w;
		disp_height = c_h;
	} else {
		disp_width = w;
		disp_height = h;
	}

	if (sw > 0 && sh > 0) {
		ret = nx_v4l2_set_crop(video_fd, nx_video, 0, 0, sw, sh);
		if (ret) {
			DP_ERR("failed to set_crop for decimator videodev\n");
			return ret;
		}

		disp_width = sw;
		disp_height = sh;
	}

	ret = nx_v4l2_reqbuf(video_fd, nx_video,
			MAX_BUFFER_COUNT);
	if (ret) {
		DP_ERR("failed to clipper reqbuf\n");
		return ret;
	}

	alloc_size = calc_alloc_size(w, h, f);
	if (alloc_size <= 0) {
		DP_ERR("invalid alloc size %lu\n", alloc_size);
		return -1;
	}

	for (i = 0; i < MAX_BUFFER_COUNT; i++) {
		gem_fd = nx_alloc_gem(drm_fd, alloc_size, 0);
		if (gem_fd < 0) {
			DP_ERR("failed to alloc_gem\n");
			return -1;
		}

		dma_fd = nx_gem_to_dmafd(drm_fd, gem_fd);
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

		ret = dp_plane_update(device, fbs[dq_index],
			disp_width, disp_height, d_idx);
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

	if (subdev_fd > 0)
		close(subdev_fd);

	if (video_fd > 0)
		close(video_fd);

	return ret;
}
#endif

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

#if CLIPPER
static void *clipper_test_thread(void *data)
{
	struct thread_data *p = (struct thread_data *)data;

	clipper_test_run(p);

	return NULL;
}
#endif

#if DECIMATOR
static void *decimator_test_thread(void *data)
{
	struct thread_data *p = (struct thread_data *)data;

	decimator_test_run(p);

	return NULL;
}
#endif

#if CLIPPER
static struct thread_data s_thread_data0;
#endif
#if DECIMATOR
static struct thread_data s_thread_data1;
#endif
int main(int argc, char *argv[])
{
	int ret, drm_fd;
	uint32_t m, w, h, f, bus_f, count;
	struct dp_device *device;
	int dbg_on = 0;
	uint32_t sw = 0, sh = 0;
	struct rect crop;

	crop.x = 0;
	crop.y = 0;
	crop.width = 0;
	crop.height = 0;

#if CLIPPER
	pthread_t clipper_thread;
	int result_clipper;
#endif
#if DECIMATOR
	pthread_t decimator_thread;
	int result_decimator;
#endif
	int result[2];

	dp_debug_on(dbg_on);

	ret = handle_option(argc, argv, &m, &w, &h, &sw, &sh, &f, &bus_f,
			&count, &crop);
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

	device = drm_card_init(&drm_fd, 0);
	if (device == NULL) {
		DP_ERR("failed to card init\n");
		return -1;
	}

	disp_width = w;
	disp_height = h;

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

	ret = pthread_create(&clipper_thread, NULL, clipper_test_thread,
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
	s_thread_data1.crop = crop;

	ret = pthread_create(&decimator_thread, NULL, decimator_test_thread,
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

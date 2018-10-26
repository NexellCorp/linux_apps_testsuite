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

#include <fcntl.h>
#include <string.h>

#include <sys/ioctl.h>

#include <drm_fourcc.h>
#include "xf86drm.h"
#include "dp.h"
#include "dp_common.h"

#include <linux/videodev2.h>
#include "media-bus-format.h"
#include "nexell_drmif.h"
#include "nx-drm-allocator.h"
#include "option.h"

#ifndef ALIGN
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#endif

#define MAX_BUFFER_COUNT	4

#define NX_PLANE_TYPE_RGB       (0<<4)
#define NX_PLANE_TYPE_VIDEO     (1<<4)
#define NX_PLANE_TYPE_UNKNOWN   (0xFFFFFFF)

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

static size_t calc_alloc_size_interlace(uint32_t w, uint32_t h, uint32_t f, uint32_t t)
{
	uint32_t y_stride = ALIGN(w, 128);
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
			2 * (ALIGN(w >> 1, 64) * ALIGN(h >> 1, 16));
		break;

	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV12:
		size = y_size + y_stride * ALIGN(h >> 1, 16);
		break;
	}
	DP_DBG("[%s] format = %u, size = %d \n ",__func__, f, size);

	return size;
}

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

static uint32_t choose_format(struct dp_plane *plane, int select)
{
	uint32_t format;
	int size = ARRAY_SIZE(dp_formats);

	if (select > (size - 1)) {
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

int dp_plane_update(struct dp_device *device, struct dp_framebuffer *fb,
		    uint32_t w, uint32_t h)
{
	int err;
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

	err = dp_plane_set(plane, fb, 0,0, w, h,0,0,w,h);
	if (err < 0) {
		DP_ERR("set plane failed \n");
		return -EINVAL;
	}

	return 1;
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

struct dp_framebuffer * dp_buffer_init(struct dp_device *device, int  x, int y,
		int gem_fd, int t)
{
	struct dp_framebuffer *fb = NULL;
	int op_format = 8/*YUV420*/;
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

	if (t == V4L2_FIELD_INTERLACED)
		fb = dp_framebuffer_interlace_config(device, format, x, y, 0, gem_fd,
			calc_alloc_size_interlace(x, y, format, t));
	else
		fb = dp_framebuffer_config(device, format, x, y, 0, gem_fd,
			calc_alloc_size(x, y, format));
	if (!fb) {
		DP_ERR("fail : framebuffer create Fail \n");
		return NULL;
	}

	err = dp_framebuffer_addfb2(fb);
	if (err < 0) {
		DP_ERR("fail : framebuffer add Fail \n");
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

static int v4l2_qbuf(int fd, int index, uint32_t buf_type, uint32_t mem_type,
		     int dma_fd, int length)
{
	struct v4l2_buffer v4l2_buf;

	bzero(&v4l2_buf, sizeof(v4l2_buf));
	v4l2_buf.m.fd = dma_fd;
	v4l2_buf.type = buf_type;
	v4l2_buf.memory = mem_type;
	v4l2_buf.index = index;
	v4l2_buf.length = length;

	return ioctl(fd, VIDIOC_QBUF, &v4l2_buf);
}

static int v4l2_dqbuf(int fd, uint32_t buf_type, uint32_t mem_type, int *index)
{
	int ret;
	struct v4l2_buffer v4l2_buf;

	bzero(&v4l2_buf, sizeof(v4l2_buf));
	v4l2_buf.type = buf_type;
	v4l2_buf.memory = mem_type;

	ret = ioctl(fd, VIDIOC_DQBUF, &v4l2_buf);

	if (ret)
		return ret;

	*index = v4l2_buf.index;
	return 0;
}

static void enum_format(int fd, uint32_t buf_type)
{
	int i;
	struct v4l2_fmtdesc format;

	for (i = 0;; i++) {
		memset(&format, 0, sizeof(format));

		format.index = i;
		format.type = buf_type;

		if (ioctl(fd, VIDIOC_ENUM_FMT, &format) < 0)
			break;

		printf("index: %u\n", format.index);
		printf("type: %d\n", format.type);
		printf("flags: 0x%08x\n", format.flags);
		printf("description: '%s'\n", format.description);
		printf("pixelformat: 0x%08x\n", format.pixelformat);
	}
}

static int camera_test(struct dp_device *device, int drm_fd, uint32_t w,
		       uint32_t h, uint32_t count, char *path, uint32_t t)
{
	int ret;
	uint32_t buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	struct v4l2_format v4l2_fmt;
	struct v4l2_requestbuffers req;
	size_t alloc_size;
	int gem_fds[MAX_BUFFER_COUNT] = { -1, };
	int dma_fds[MAX_BUFFER_COUNT] = { -1, };
	struct dp_framebuffer *fbs[MAX_BUFFER_COUNT] = {NULL,};
	int i, f;

	int video_fd = open(path, O_RDWR);
	if (video_fd < 0) {
		fprintf(stderr, "failed to open video device %s\n", path);
		return -ENODEV;
	}

	enum_format(video_fd, buf_type);

	f = V4L2_PIX_FMT_YUV420;

	switch (t) {
	case 0:
		t = V4L2_FIELD_ANY;
		break;
	case 1:
		t = V4L2_FIELD_NONE;
		break;
	case 2:
		t = V4L2_FIELD_INTERLACED;
		break;
	default:
		t = V4L2_FIELD_ANY;
		break;
	};

	/* set format */
	bzero(&v4l2_fmt, sizeof(v4l2_fmt));
	v4l2_fmt.type = buf_type;
	v4l2_fmt.fmt.pix.width = w;
	v4l2_fmt.fmt.pix.height = h;
	v4l2_fmt.fmt.pix.pixelformat = f;
	v4l2_fmt.fmt.pix.field = t;
	ret = ioctl(video_fd, VIDIOC_S_FMT, &v4l2_fmt);
	if (ret) {
		fprintf(stderr, "failed to set format\n");
		return ret;
	}

	bzero(&req, sizeof(req));
	req.count = MAX_BUFFER_COUNT;
	req.memory = V4L2_MEMORY_DMABUF;
	req.type = buf_type;
	ret = ioctl(video_fd, VIDIOC_REQBUFS, &req);
	if (ret) {
		fprintf(stderr, "failed to reqbuf\n");
		return ret;
	}

	if (t == V4L2_FIELD_INTERLACED)
		alloc_size = calc_alloc_size_interlace(w, h, f, t);
	else
		alloc_size = calc_alloc_size(w, h, f);
	if (alloc_size <= 0) {
		fprintf(stderr, "invalid alloc size %lu\n", alloc_size);
		return -EINVAL;
	}


	for (i = 0; i < MAX_BUFFER_COUNT; i++) {
		struct dp_framebuffer *fb;
		int gem_fd = alloc_gem(drm_fd, alloc_size, 0);
		if (gem_fd < 0) {
			fprintf(stderr, "failed to alloc_gem\n");
			return -ENOMEM;
		}

		int dma_fd = gem_to_dmafd(drm_fd, gem_fd);
		if (dma_fd < 0) {
			fprintf(stderr, "failed to gem_to_dmafd\n");
			return -ENOMEM;
		}

		fb = dp_buffer_init(device, w, h, gem_fd, t);
		if (!fb) {
			DP_ERR("fail : framebuffer Init %m\n");
			ret = -1;
			return ret;
		}

		fbs[i] = fb;
		gem_fds[i] = gem_fd;
		dma_fds[i] = dma_fd;
	}

	/* qbuf */
	for (i = 0; i < MAX_BUFFER_COUNT; i++) {
		ret = v4l2_qbuf(video_fd, i, buf_type, V4L2_MEMORY_DMABUF,
				dma_fds[i], alloc_size);
		if (ret) {
			fprintf(stderr, "failed qbuf index %d\n", i);
			return ret;
		}
	}

	/* stream on */
	ret = ioctl(video_fd, VIDIOC_STREAMON, &buf_type);
	if (ret) {
		fprintf(stderr, "failed to streamon\n");
		return ret;
	}

	int loop_count = count;
	while (loop_count--) {
		int dq_index;
		ret = v4l2_dqbuf(video_fd, buf_type, V4L2_MEMORY_DMABUF,
				 &dq_index);
		if (ret) {
			fprintf(stderr, "failed to dqbuf\n");
			return ret;
		}

		ret = v4l2_qbuf(video_fd, dq_index, buf_type,
				V4L2_MEMORY_DMABUF, dma_fds[dq_index],
				alloc_size);
		if (ret) {
			fprintf(stderr, "failed qbuf index %d\n", dq_index);
			return ret;
		}

		dp_plane_update(device, fbs[dq_index], w, h);
	}

	/* stream off */
	ret = ioctl(video_fd, VIDIOC_STREAMOFF, &buf_type);

	close(video_fd);

	/* free buffers */
	for (i = 0; i < MAX_BUFFER_COUNT; i++) {
		if (fbs[i])
			dp_framebuffer_delfb2(fbs[i]);
		/* if (dma_fds[i] >= 0) */
		/* 	close(dma_fds[i]); */
		if (gem_fds[i] >= 0)
			close(gem_fds[i]);
	}

	return ret;
}

/* dp_cam_test_onedevice -w 1280 -h 720 -c 1000 -d /dev/video6 */
int main(int argc, char *argv[])
{
	int ret, drm_fd, err;
	uint32_t w, h, count, t;
	char dev_path[64] = {0, };
	struct dp_device *device;
	int dbg_on = 0;

	dp_debug_on(dbg_on);

	ret = handle_option(argc, argv, &w, &h, &count, dev_path, &t);
	if (ret) {
		DP_ERR("failed to handle_option\n");
		return ret;
	}

	printf("camera device path ==> %s\n", dev_path);

	drm_fd = open("/dev/dri/card0",O_RDWR);
	if (drm_fd < 0) {
		DP_ERR("failed to open_drm_device\n");
		return -1;
	}

	device = dp_device_init(drm_fd);
	if (!device) {
		DP_ERR("fail : device open() %m\n");
		return -1;
	}

	err = camera_test(device, drm_fd, w, h, count, dev_path, t);
	if (err < 0) {
		DP_ERR("failed to do camera_test \n");
		return -1;
	}

	dp_device_close(device);
	close(drm_fd);

	return ret;
}


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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <time.h>

#include <linux/videodev2.h>
#include <linux/media-bus-format.h>

#include <dp.h>
#include <dp_common.h>
#include <nx-v4l2.h>
#include <nx-drm-allocator.h>
#include <nx-scaler.h>

#include "xf86drm.h"
#include "globaldef.h"
#include "debug.h"
#include "option.h"
#include "memory.h"
#include "display.h"

//------------------------------------------------------------------------------

static int32_t save_output_image(memory_handle hImg,
		uint32_t width, uint32_t height,
		const char *fileName)
{
	FILE *fd = fopen(fileName, "wb");
	uint32_t w_half = 2, h_half = 2, num_planes = MAX_PLANES;
	uint8_t *pDst;

	if (NULL == fd)
		return -1;

	switch (hImg->format)
	{
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_YVU420M:
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_NV21M:
		break;

	case V4L2_PIX_FMT_YUYV:
		num_planes = 1;
		width <<= 1;
		break;

	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_YUV422M:
	case V4L2_PIX_FMT_NV16M:
	case V4L2_PIX_FMT_NV61M:
		h_half = 1;
		break;

	case V4L2_PIX_FMT_YUV444:
	case V4L2_PIX_FMT_YUV444M:
		w_half = 1;
		h_half = 1;
		break;

	case V4L2_PIX_FMT_GREY:
		num_planes = 1;
		break;

	default:
		CAM_ERR("Unknown format type\n");
		return -EINVAL;
	}

	//	Decide Memory Size
	for (uint32_t i = 0; i < num_planes; i++)
	{
		pDst = (uint8_t *)hImg->vaddr[i];
		if (pDst == NULL) {
			CAM_ERR("vaddr for is NULL\n");
			break;
		}
		for (uint32_t j = 0; j < height; j++)
		{
			fwrite(pDst + hImg->stride[i] * j, 1, width, fd);
		}
		if (!i) {
			if (!w_half)
				break;
			width /= w_half;
			height /= h_half;
		}
	}

	fclose(fd);
	return 0;
}

static int convert_formats(uint32_t format)
{
	int ret = -EINVAL;

	switch (format)
	{
	case V4L2_PIX_FMT_YUV420:
		ret = MEDIA_BUS_FMT_YUYV8_2X8;
		break;
	case V4L2_PIX_FMT_YUV422P:
		ret = MEDIA_BUS_FMT_YUYV8_1X16;
		break;
	case V4L2_PIX_FMT_YUV444:
		ret = MEDIA_BUS_FMT_AYUV8_1X32;
		break;
	default:
		CAM_ERR("format:0x%x is not supported in scaler\n", format);
		break;
	}
	return ret;
}

static int32_t do_scaling(int hScaler, memory_handle hIn, memory_handle hOut, uint32_t *crop)
{
	struct nx_scaler_context scalerCtx;

	memset(&scalerCtx, 0, sizeof(struct nx_scaler_context));

	// scaler crop
	scalerCtx.crop.x = crop[0];
	scalerCtx.crop.y = crop[1];
	scalerCtx.crop.width = crop[2];
	scalerCtx.crop.height = crop[3];

	// scaler src
	scalerCtx.src_plane_num = MAX_PLANES;
	scalerCtx.src_width     = hIn->width;
	scalerCtx.src_height    = hIn->height;
	scalerCtx.src_code      = convert_formats(hIn->format);
	scalerCtx.src_fds[0]    = hIn->dmaFd[0];
	scalerCtx.src_fds[1]    = hIn->dmaFd[1];
	scalerCtx.src_fds[2]    = hIn->dmaFd[2];
	scalerCtx.src_stride[0] = hIn->stride[0];
	scalerCtx.src_stride[1] = hIn->stride[1];
	scalerCtx.src_stride[2] = hIn->stride[2];

	// scaler dst
	scalerCtx.dst_plane_num = MAX_PLANES;
	scalerCtx.dst_width     = hOut->width;
	scalerCtx.dst_height    = hOut->height;
	scalerCtx.dst_code      = convert_formats(hOut->format);
	scalerCtx.dst_fds[0]    = hOut->dmaFd[0];
	scalerCtx.dst_fds[1]    = hOut->dmaFd[1];
	scalerCtx.dst_fds[2]    = hOut->dmaFd[2];
	scalerCtx.dst_stride[0] = hOut->stride[0];
	scalerCtx.dst_stride[1] = hOut->stride[1];
	scalerCtx.dst_stride[2] = hOut->stride[2];

	return nx_scaler_run(hScaler, &scalerCtx);
}

int camera_run(struct camera_data *p)
{
	int ret = NO_ERROR, videoFd = -1, scaleFd = -1, drmFd = -1;
	uint32_t i, p_num, plane_num = MAX_PLANES;
	int loop_count = p->count, dq_index = 0;
	int save = SAVE_FRAME_NUM;
	uint32_t s_w = p->width, s_h = p->height;
	uint32_t d_w = p->width, d_h = p->height;
	int size[MAX_PLANES] = {0, };
	memory_handle srcMem[MAX_BUFFER_COUNT] = {0, };
	memory_handle dstMem[MAX_BUFFER_COUNT] = {0, };
	struct dp_device *device = NULL;
	struct dp_plane *plane = NULL;
	struct dp_framebuffer *fb[MAX_BUFFER_COUNT] = {0, };
	struct timeval	start, end, gap;
	uint32_t frame_counts = 0;

	CAM_DBG("camera test run\n");
	videoFd = nx_v4l2_open_device(p->type, p->module);
	if (videoFd < 0) {
		CAM_ERR("[%d] failed to open video device %s\n",
				p->type, nx_v4l2_get_video_path(p->type, p->module));
		return -ENODEV;
	}

	/* when use open_drm_device() - open("/dev/dri/card0"),
	 * no display at the first without error
	 * but using drmOpen doesn't have the problem*/
	drmFd = drmOpen("nexell", NULL);
	if (drmFd < 0) {
		CAM_ERR("failed to open drm device:%d\n", drmFd);
		return -ENODEV;
	}

	if (p->display) {
		device = dp_device_init(drmFd);
		if (!device) {
			CAM_ERR("failed to display device init\n");
			ret = -ENODEV;
			goto close;
		}
	}

	if (p->scaling) {
		scaleFd = scaler_open();
		if (scaleFd < 0) {
			CAM_ERR("failed to open scaler device:%d\n", scaleFd);
			ret = -ENODEV;
			goto close;
		}
	}

	if (p->vip_crop) {
		s_w = p->crop[2];
		s_h = p->crop[3];
		d_w = p->crop[2];
		d_h = p->crop[3];
	}

	if (p->scaling) {
		d_w = p->scale.dstWidth;
		d_h = p->scale.dstHeight;
	} else if ((p->type == nx_decimator_video) && (p->scale.dstWidth) && (p->scale.dstHeight)) {
		d_w = p->scale.dstWidth;
		d_h = p->scale.dstHeight;
	}

	/* Allocate Image */
	if (p->format == V4L2_PIX_FMT_YUYV)
		plane_num = 1;
	for (i = 0; i < MAX_BUFFER_COUNT; i++) {
		int w = s_w, h = s_h;

		if (p->scaling) {
			dstMem[i] = alloc_memory(drmFd, d_w, d_h, plane_num, p->format,
					nx_v4l2_is_interlaced_camera(p->module));
			if (dstMem[i] == NULL) {
				CAM_ERR("[%d] scaling buf is NULL\n", p->type);
				goto free;
			}
		} else if ((p->type == nx_decimator_video) && (p->scale.dstWidth) && (p->scale.dstHeight)) {
			w = d_w;
			h = d_h;
		}
		srcMem[i] = alloc_memory(drmFd, w, h, plane_num, p->format,
				nx_v4l2_is_interlaced_camera(p->module));
		if (srcMem[i] == NULL) {
			CAM_ERR("[%d] srcMem is NULL for %d\n", p->type, i);
			goto free;
		}
	}

	/* set format */
	ret = nx_v4l2_set_fmt(videoFd, p->format, p->width, p->height, plane_num,
			srcMem[0]->stride, srcMem[0]->size);
	if (ret) {
		CAM_ERR("[%d] failed to set format:%d\n", p->type, ret);
		goto free;
	}

	if (p->display) {
		uint32_t size, gemFd;

		for (i = 0; i < MAX_BUFFER_COUNT; i++) {
			if (p->scaling) {
				size = dstMem[i]->size[0] + dstMem[i]->size[1] + dstMem[i]->size[2];
				gemFd = dstMem[i]->gemFd[0];
			} else {
				size = srcMem[i]->size[0] + srcMem[i]->size[1] + srcMem[i]->size[2];
				gemFd = srcMem[i]->gemFd[0];
			}
			fb[i] = dp_buffer_add(device, p->format, d_w, d_h,
					!IsContinuousPlane(p->format),
					gemFd, size,
					nx_v4l2_is_interlaced_camera(p->module));
			if (!fb[i])
				goto free;
		}
	}

	if (p->vip_crop) {
		ret = nx_v4l2_set_crop(videoFd, p->type, p->crop[0], p->crop[1],
				p->crop[2], p->crop[3]);
		if (ret) {
			CAM_ERR("[%d] failed to set crop: %d\n", p->type, ret);
			goto free;
		}
	}

	if (!p->scaling && (p->type == nx_decimator_video) && (p->scale.dstWidth) && (p->scale.dstHeight)) {
		ret = nx_v4l2_set_selection(videoFd, p->type, d_w, d_h);
		if (ret) {
			CAM_ERR("[%d] failed to set selection: %d\n", p->type, ret);
			goto free;
		}
	}

	ret = nx_v4l2_reqbuf(videoFd, p->type, MAX_BUFFER_COUNT);
	if (ret) {
		CAM_ERR("[%d] failed to reqbuf: %d\n", p->type, ret);
		goto free;
	}

	/* qbuf */
	p_num = (IsContinuousPlane(p->format)) ? 1 : MAX_PLANES;
	if (p_num != MAX_PLANES) {
		size[0] = srcMem[0]->size[0] + srcMem[0]->size[1] + srcMem[0]->size[2];
		size[1] = size[2] = 0;
	} else {
		size[0] = srcMem[0]->size[0];
		size[1] = srcMem[0]->size[1];
		size[2] = srcMem[0]->size[2];
	}

	for (i = 0; i < MAX_BUFFER_COUNT; i++) {
		ret = nx_v4l2_qbuf(videoFd, p->type, p_num, i, &srcMem[i]->dmaFd[0],
				size);
		if (ret) {
			CAM_ERR("[%d] failed to qbuf index %d\n", p->type, i);
			goto finish;
		}
		CAM_DBG("[%d] qbuf:%d done, size:%d\n", p->type, i,
				(srcMem[0]->size[0] + srcMem[0]->size[1] + srcMem[0]->size[2]));
	}

	if (p->display) {
		plane = dp_plane_init(device, p->format);
		if (!plane) {
			CAM_ERR("[%d] failed to init plane\n", p->type);
			ret = -EINVAL;
			goto finish;
		}
	}
	/* stream on */
	CAM_DBG("[%d] start stream\n", p->type);
	ret = nx_v4l2_streamon(videoFd, p->type);
	if (ret) {
		CAM_ERR("[%d] failed to streamon:%d\n", p->type, ret);
		goto finish;
	}
	CAM_DBG("[%d] start stream done\n", p->type);

	if (p->fps_enable)
		gettimeofday(&start, NULL);

	if (p->file_save)
		save = loop_count - p->file_save;

	while (loop_count--) {
		ret = nx_v4l2_dqbuf(videoFd, p->type, 1, &dq_index);
		if (ret) {
			CAM_ERR("failed to dqbuf: %d\n", ret);
			goto stop;
		}
		CAM_DBG("[%d] Dqbuf:%d done\n", p->type, dq_index);

		if (p->fps_enable) {
			frame_counts++;
			gettimeofday(&end, NULL);
		}

		if (p->scaling) {
			ret = do_scaling(scaleFd, srcMem[dq_index], dstMem[dq_index], p->scale.crop);
			if (ret) {
				CAM_ERR("[%d] failed to scale for %d buf, ret:%d\n",
						p->type, dq_index, ret);
				goto stop;
			}
		}

		if (p->display) {
			ret = dp_plane_update(device, fb[dq_index], plane,
					0, 0, d_w, d_h, d_w, d_h);
			if (ret)
				CAM_ERR("[%d] failed to update plane for %d buffer, ret:%d\n",
						p->type, dq_index, ret);
		}
		if (p->file_save && (loop_count == save)) {
			char filename[20];
			memory_handle mem = NULL;

			if (p->type == nx_clipper_video)
				strcpy(filename, "clipper.yuv\0");
			else
				strcpy(filename, "decimator.yuv\0");
			if (p->scaling)
				mem = dstMem[dq_index];
			else
				mem = srcMem[dq_index];
			ret = save_output_image(mem, d_w, d_h, filename);
			if (ret) {
				CAM_ERR("[%d] Error : save_output_image !!\n", p->type);
				goto stop;
			}

		}
		if ((p->count == 1) || (!loop_count))
			break;
		ret = nx_v4l2_qbuf(videoFd, p->type, p_num, dq_index,
				&srcMem[dq_index]->dmaFd[0], size);
		if (ret) {
			CAM_ERR("[%d] failed qbuf index %d\n", p->type, ret);
			goto stop;
		}
	}

stop:
	/* stream off */
	CAM_DBG("[%d] stop stream\n", p->type);
	ret = nx_v4l2_streamoff(videoFd, p->type);
	if (ret) {
		CAM_ERR("[%d] failed to streamoff:%d\n", p->type, ret);
		return ret;
	}

	if (p->fps_enable) {
		long msec;
		float sec;

		gap.tv_sec = end.tv_sec - start.tv_sec;
		gap.tv_usec = end.tv_usec - start.tv_usec;

		if (gap.tv_usec < 0) {
			gap.tv_sec = gap.tv_sec - 1;
			gap.tv_usec = gap.tv_usec + 1000000;
		}
		msec = gap.tv_usec/1000 + gap.tv_sec*1000;
		sec = gap.tv_sec + (float)gap.tv_usec/1000000;
		printf("[%d] frame counts:%d, time:%02ld.%02ld sec, rate:%0.2lf fps, duration:%ld ms\n",
				p->type, frame_counts, gap.tv_sec, gap.tv_usec,
				frame_counts/sec,
				msec/frame_counts);
	}

finish:
	CAM_DBG("[%d] req buf 0\n", p->type);
	ret = nx_v4l2_reqbuf(videoFd, p->type, 0);
	if (ret) {
		CAM_ERR("[%d] failed to reqbuf:%d\n", p->type, ret);
		return ret;
	}
free:
	for (i = 0; i < MAX_BUFFER_COUNT; i++) {
		if (p->display && (fb[i])) {
			dp_framebuffer_delfb2(fb[i]);
			fb[i] = NULL;
		}
		if (dstMem[i]) {
			free_memory(drmFd, dstMem[i]);
			dstMem[i] = NULL;
		}
		if (srcMem[i]) {
			free_memory(drmFd, srcMem[i]);
			srcMem[i] = NULL;
		}
	}
close:
	if (scaleFd >= 0)
		nx_scaler_close(scaleFd);
	if (p->display && (device != NULL))
		dp_device_close(device);
	else {
		if (drmFd >= 0)
			close(drmFd);
	}
	if (videoFd >= 0)
		close(videoFd);
	return ret;
}

static void *camera_test(void *data)
{
	struct camera_data *p = (struct camera_data *)data;

	camera_run(p);

	return NULL;
}

int main(int32_t argc, char *argv[])
{
	int ret;
	struct camera_data data;
	struct camera_data test_data[2];
	pthread_t clipper_thread, decimator_thread;
	int result_clipper, result_decimator;
	int result[2];

	memset(&data, 0x0, sizeof(struct camera_data));
	ret = handle_option(argc, argv, &data);
	if (ret)
		return ret;

	CAM_DBG("======camera test application=====\n");
	if (data.type & enb_clip) {
		memcpy(&test_data[0], &data, sizeof(struct camera_data));
		test_data[0].type = nx_clipper_video;
		test_data[0].display = (data.display & enb_clip) ? true : false;
		ret = pthread_create(&clipper_thread, NULL, camera_test,
				&test_data[0]);
		if (ret) {
			CAM_ERR("failed to start clipper thread: %d", ret);
			return ret;
		}
	}
	if (data.type & enb_deci) {
		memcpy(&test_data[1], &data, sizeof(struct camera_data));
		test_data[1].type = nx_decimator_video;
		test_data[1].display = (data.display & enb_deci) ? true : false;
		ret = pthread_create(&decimator_thread, NULL, camera_test,
				&test_data[1]);
		if (ret) {
			CAM_ERR("failed to start clipper thread: %d", ret);
			return ret;
		}
	}

	if (data.type & enb_clip) {
		result_clipper = pthread_join(clipper_thread, (void **)&result[0]);
		if (result_clipper != 0) {
			CAM_ERR("fail to clipper pthread join\n");
			return -EINVAL;
		}
	}
	if (data.type & enb_deci) {
		result_decimator = pthread_join(decimator_thread, (void **)&result[1]);
		if (result_decimator != 0) {
			CAM_ERR("fail to decimator pthread join\n");
			return -EINVAL;
		}
	}

	/*pthread_exit(0);*/
	CAM_DBG("Camera Test Done!!\n");
	return NO_ERROR;
}

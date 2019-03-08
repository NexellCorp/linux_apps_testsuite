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
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <linux/videodev2.h>

#include <nx-drm-allocator.h>

#include "globaldef.h"
#include "debug.h"
#include "memory.h"

#define ALIGN_CAMERA		32
#define ALIGN_INTERLACED	128

#ifndef ALIGN
#define	ALIGN(X,N)		((X+N-1) & (~(N-1)))
#endif

uint32_t IsContinuousPlane(uint32_t fourcc)
{
	switch (fourcc)
	{
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_YVU420M:
	case V4L2_PIX_FMT_YUV422M:
	case V4L2_PIX_FMT_NV16M:
	case V4L2_PIX_FMT_NV61M:
	case V4L2_PIX_FMT_YUV444M:
		return 0;
	}

	return 1;
}

memory_info *alloc_memory(int drmFd, uint32_t width, uint32_t height, uint32_t planes,
		uint32_t format, bool interlaced)
{
	int gemFd[MAX_PLANES] = {0, };
	int dmaFd[MAX_PLANES] = {0, };
	uint32_t flink[MAX_PLANES] = {0, };
	void* vaddr[MAX_PLANES] = {0, };
	uint32_t flags = 0, i=0;
	uint32_t luStride, cStride;
	uint32_t luVStride, cVStride;
	uint32_t stride[MAX_PLANES];
	uint32_t size[MAX_PLANES];
	uint32_t y_align;
	memory_info *pMem = NULL;

	y_align = (interlaced) ? ALIGN_INTERLACED : ALIGN_CAMERA;
	luStride = ALIGN(width, y_align);
	luVStride = ALIGN(height, 16);

	//	Chroma
	switch (format)
	{
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_YVU420M:
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_NV21M:
		cStride = luStride/2;
		cVStride = ALIGN(height/2, 16);
		break;

	case V4L2_PIX_FMT_YUYV:
		luStride = luStride << 1;
		cStride = 0;
		cVStride = 0;
		break;

	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_YUV422M:
	case V4L2_PIX_FMT_NV16M:
	case V4L2_PIX_FMT_NV61M:
		cStride = luStride/2;
		cVStride = luVStride;
		break;

	case V4L2_PIX_FMT_YUV444:
	case V4L2_PIX_FMT_YUV444M:
		cStride = luStride;
		cVStride = luVStride;
		break;

	case V4L2_PIX_FMT_GREY:
		cStride = 0;
		cVStride = 0;
		break;

	default:
		CAM_ERR("Unknown format type\n");
		goto ErrorExit;
	}

	//	Decide Memory Size
	switch (planes)
	{
	case 1:
		size[0] = luStride*luVStride + cStride*cVStride*2;
		stride[0] = luStride;

		gemFd[0] = alloc_gem(drmFd, size[0], flags);
		if (gemFd[0] < 0) goto ErrorExit;

		dmaFd[0] = gem_to_dmafd(drmFd, gemFd[0]);
		if (dmaFd[0] < 0) goto ErrorExit;

		flink[0] = get_flink_name(drmFd, gemFd[0]);

		if (get_vaddr(drmFd, gemFd[0], size[0], &vaddr[0])) {
			CAM_ERR("failed to get vaddr gemFd:%d\n", gemFd[0]);
			goto ErrorExit;
		}
		break;

	case 2:
		size[0] = luStride*luVStride;
		stride[0] = luStride;

		size[1] = cStride*cVStride*2;
		stride[1] = cStride * 2;

		if (IsContinuousPlane(format))
		{
			gemFd[0] =
			gemFd[1] = alloc_gem(drmFd, size[0] + size[1], flags);
			if (gemFd[0] < 0) goto ErrorExit;

			dmaFd[0] =
			dmaFd[1] = gem_to_dmafd(drmFd, gemFd[0]);
			if (dmaFd[0] < 0) goto ErrorExit;

			flink[0] = flink[1] = get_flink_name(drmFd, gemFd[0]);

			if (get_vaddr(drmFd, gemFd[0], size[0] + size[1], &vaddr[0])) {
				CAM_ERR("failed to get vaddr gemFd:%d\n", gemFd[0]);
				goto ErrorExit;
			}
			vaddr[1] = vaddr[0] + size[0];
		}
		else
		{
			gemFd[0] = alloc_gem(drmFd, size[0], flags);
			if (gemFd[0] < 0) goto ErrorExit;
			dmaFd[0] = gem_to_dmafd(drmFd, gemFd[0]);
			if (dmaFd[0] < 0) goto ErrorExit;

			gemFd[1] = alloc_gem(drmFd, size[1], flags);
			if (gemFd[1] < 0) goto ErrorExit;
			dmaFd[1] = gem_to_dmafd(drmFd, gemFd[1]);
			if (dmaFd[1] < 0) goto ErrorExit;

			flink[0] = get_flink_name(drmFd, gemFd[0]);
			flink[1] = get_flink_name(drmFd, gemFd[1]);

			if (get_vaddr(drmFd, gemFd[0], size[0], &vaddr[0])) {
				CAM_ERR("failed to get vaddr gemFd:%d\n", gemFd[0]);
				goto ErrorExit;
			}
			if (get_vaddr(drmFd, gemFd[1], size[1], &vaddr[1])) {
				CAM_ERR("failed to get vaddr gemFd:%d\n", gemFd[1]);
				goto ErrorExit;
			}
		}
		break;

	case 3:
		size[0] = luStride*luVStride;
		stride[0] = luStride;

		size[1] = cStride*cVStride;
		stride[1] = cStride;

		size[2] = cStride*cVStride;
		stride[2] = cStride;

		if (IsContinuousPlane(format))
		{
			gemFd[0] =
			gemFd[1] =
			gemFd[2] = alloc_gem(drmFd, size[0] + size[1] + size[2], flags);
			if (gemFd[0] < 0) goto ErrorExit;

			dmaFd[0] =
			dmaFd[1] =
			dmaFd[2] = gem_to_dmafd(drmFd, gemFd[0]);
			if (dmaFd[0] < 0) goto ErrorExit;

			flink[0] = flink[1] = flink[2] = get_flink_name(drmFd, gemFd[0]);

			if (get_vaddr(drmFd, gemFd[0], size[0] + size[1] + size[2], &vaddr[0])) {
				CAM_ERR("failed to get vaddr gemFd:%d\n", gemFd[0]);
				goto ErrorExit;
			}
			if (format != V4L2_PIX_FMT_YVU420) {
				vaddr[1] = vaddr[0] + size[0];
				vaddr[2] = vaddr[1] + size[1];
			} else {
				vaddr[2] = vaddr[0] + size[0];
				vaddr[1] = vaddr[2] + size[2];
			}
		}
		else
		{
			gemFd[0] = alloc_gem(drmFd, size[0], flags);
			if (gemFd[0] < 0) goto ErrorExit;
			dmaFd[0] = gem_to_dmafd(drmFd, gemFd[0]);
			if (dmaFd[0] < 0) goto ErrorExit;
			if (get_vaddr(drmFd, gemFd[0], size[0], &vaddr[0])) {
				CAM_ERR("failed to get vaddr gemFd:%d\n", gemFd[0]);
				goto ErrorExit;
			}

			gemFd[1] = alloc_gem(drmFd, size[1], flags);
			if (gemFd[1] < 0) goto ErrorExit;
			dmaFd[1] = gem_to_dmafd(drmFd, gemFd[1]);
			if (dmaFd[1] < 0) goto ErrorExit;
			if (get_vaddr(drmFd, gemFd[1], size[1], &vaddr[1])) {
				CAM_ERR("failed to get vaddr gemFd:%d\n", gemFd[1]);
				goto ErrorExit;
			}

			gemFd[2] = alloc_gem(drmFd, size[2], flags);
			if (gemFd[2] < 0) goto ErrorExit;
			dmaFd[2] = gem_to_dmafd(drmFd, gemFd[2]);
			if (dmaFd[2] < 0) goto ErrorExit;
			if (get_vaddr(drmFd, gemFd[2], size[2], &vaddr[2])) {
				CAM_ERR("failed to get vaddr gemFd:%d\n", gemFd[2]);
				goto ErrorExit;
			}

			flink[0] = get_flink_name(drmFd, gemFd[0]);
			flink[1] = get_flink_name(drmFd, gemFd[1]);
			flink[2] = get_flink_name(drmFd, gemFd[2]);
		}
		break;
	}

	pMem = (memory_info *)calloc(1, sizeof(memory_info));
	pMem->width = width;
	pMem->height = height;
	pMem->planes = planes;
	pMem->format = format;
	CAM_DBG("Memory\n");
	for (i = 0; i < planes; i++)
	{
		pMem->dmaFd[i] = dmaFd[i];
		pMem->gemFd[i] = gemFd[i];
		pMem->flink[i] = flink[i];
		pMem->size[i] = size[i];
		pMem->stride[i] = stride[i];
		pMem->vaddr[i] = vaddr[i];
		CAM_DBG("[%d]: dma:%d gem:%d size:%d stride:%d viaddr:%p\n",
				i, pMem->dmaFd[i], pMem->gemFd[i], pMem->size[i],
				pMem->stride[i], pMem->vaddr[i]);
	}
	return pMem;

ErrorExit:
	for (i = 0; i < planes; i++)
	{
		if (gemFd[i] > 0)
			free_gem(drmFd, gemFd[i]);
		if (dmaFd[i] > 0)
			close(dmaFd[i]);
	}

	if (drmFd > 0)
		close(drmFd);

	return NULL;
}

void free_memory(int drmFd, memory_info * pMem)
{
	uint32_t i;
	if (pMem)
	{
		for (i = 0;i < pMem->planes; i++)
		{
			free_gem(drmFd, pMem->gemFd[i]);
			close(pMem->dmaFd[i]);
		}

		free(pMem);
	}
}

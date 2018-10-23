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
#include <getopt.h>
#include <linux/videodev2.h>
#include <linux/media-bus-format.h>

#include <nx-alloc.h>
#include <nx-scaler.h>


//------------------------------------------------------------------------------
#ifndef ALIGN
#define ALIGN(x, a) (((x) + (a)-1) & ~((a)-1))
#endif
static int32_t LoadInputImage(NX_MEMORY_HANDLE hImg, const char *fileName)
{
	FILE *fd;
	int32_t width = hImg->width;
	int32_t height = hImg->height;
	uint8_t *pDst;

	printf("[%s]\n", __func__);
	fd = fopen(fileName, "rb");
	if( NULL == fd ) {
		printf("[%s] fd is invalid\n", __func__);
		return -1;
	}
	for( int32_t i=0 ; i<3 ;i++ )
	{
		pDst = (uint8_t *)hImg->pBuffer[i];
		for (int32_t j = 0; j < height; j++)
		{
			if (width != (int32_t)fread(pDst, 1, width, fd))
			{
				printf("Read failed!! (j=%d, width=%d) \n", j, width);
				fclose(fd);
				return -1;
			}
			pDst += hImg->stride[i];
		}
		if( i == 0 )
		{
			width /= 2;
			height /= 2;
		}
	}
	fclose(fd);

	printf("[%s] finished\n", __func__);
	return 0;
}

static int32_t DoScaling(NX_MEMORY_HANDLE hIn, NX_MEMORY_HANDLE hOut,
		int32_t cropX, int32_t cropY, int32_t cropWidth, int32_t cropHeight)
{
	struct nx_scaler_context scalerCtx;
	int32_t ret;
	int32_t hScaler = scaler_open();

	if (hScaler < 0) {
		pr_err("[%s] failed to open scaler:%d\n", __func__, hScaler);
		return hScaler;
	}

	memset(&scalerCtx, 0, sizeof(struct nx_scaler_context));

	// scaler crop
	scalerCtx.crop.x = cropX;
	scalerCtx.crop.y = cropY;
	scalerCtx.crop.width = cropWidth;
	scalerCtx.crop.height = cropHeight;

	// scaler src
	scalerCtx.src_plane_num = 3;
	scalerCtx.src_width     = hIn->width;
	scalerCtx.src_height    = hIn->height;
	scalerCtx.src_code      = MEDIA_BUS_FMT_YUYV8_2X8;
	scalerCtx.src_fds[0]    = hIn->sharedFd[0];
	scalerCtx.src_fds[1]    = hIn->sharedFd[1];
	scalerCtx.src_fds[2]    = hIn->sharedFd[2];
	scalerCtx.src_stride[0] = hIn->stride[0];
	scalerCtx.src_stride[1] = hIn->stride[1];
	scalerCtx.src_stride[2] = hIn->stride[2];

	// scaler dst
	scalerCtx.dst_plane_num = 3;
	scalerCtx.dst_width     = hOut->width;
	scalerCtx.dst_height    = hOut->height;
	scalerCtx.dst_code      = MEDIA_BUS_FMT_YUYV8_2X8;
	scalerCtx.dst_fds[0]    = hOut->sharedFd[0];
	scalerCtx.dst_fds[1]    = hOut->sharedFd[1];
	scalerCtx.dst_fds[2]    = hOut->sharedFd[2];
	scalerCtx.dst_stride[0] = hOut->stride[0];
	scalerCtx.dst_stride[1] = hOut->stride[1];
	scalerCtx.dst_stride[2] = hOut->stride[2];

	ret = nx_scaler_run(hScaler, &scalerCtx);
	nx_scaler_close(hScaler);
	return ret;
}

static int32_t SaveOutputImage(NX_MEMORY_HANDLE hImg, const char *fileName)
{
	FILE *fd = fopen(fileName, "wb");
	int32_t width = hImg->width;
	int32_t height = hImg->height;
	uint8_t *pDst;

	if (NULL == fd)
		return -1;

	for( int32_t i=0 ; i<3 ; i++ )
	{
		pDst = (uint8_t *)hImg->pBuffer[i];
		for (int32_t j = 0; j < height; j++)
		{
			fwrite(pDst + hImg->stride[i] * j, 1, width, fd);
		}
		if( i==0 )
		{
			width /= 2;
			height /= 2;
		}
	}

	fclose(fd);

	return 0;
}

static int32_t VerifyOutputImage(NX_MEMORY_HANDLE srcImg, const char *srcName,
		NX_MEMORY_HANDLE dstImg, const char *dstName)
{
	FILE *src_fd, *dst_fd;
	int32_t src_width = srcImg->width;
	int32_t src_height = srcImg->height;
	int32_t dst_width = dstImg->width;
	int32_t dst_height = dstImg->height;
	uint8_t *pSrc, *pDst;

	printf("[%s]\n", __func__);

	if ((src_height != dst_height) || (src_width != dst_width))
		return -1;
	src_fd = fopen(srcName, "rb");
	if( NULL == src_fd ) {
		printf("[%s] fd is invalid\n", __func__);
		return -1;
	}
	dst_fd = fopen(dstName, "rb");
	if( NULL == dst_fd ) {
		printf("[%s] fd is invalid\n", __func__);
		return -1;
	}
	for( int32_t i=0 ; i<3 ;i++ )
	{
		pSrc = (uint8_t *)srcImg->pBuffer[i];
		pDst = (uint8_t *)dstImg->pBuffer[i];
		for (int32_t j = 0; j < src_height; j++)
		{
			if (src_width != (int32_t)fread(pSrc, 1, src_width, src_fd))
			{
				printf("Read failed!! (j=%d, width=%d) \n", j, src_width);
				fclose(src_fd);
				return -1;
			}
			if (dst_width != (int32_t)fread(pDst, 1, dst_width, dst_fd))
			{
				printf("Read failed!! (j=%d, width=%d) \n", j, dst_width);
				fclose(dst_fd);
				return -1;
			}
			for (int32_t h = 0; h < src_width; h++)
			{
				if (*pSrc != *pDst) {
					printf("src[%x] dst[%x] is not matched\n", *pSrc, *pDst);
					fclose(src_fd);
					fclose(dst_fd);
					return -1;
				}
				pSrc++;
				pDst++;
			}
			pSrc += srcImg->stride[i] - src_width;
			pDst += dstImg->stride[i] - dst_width;
		}
		if( i == 0 )
		{
			src_width /= 2;
			src_height /= 2;
			dst_width /= 2;
			dst_height /= 2;
		}
	}
	fclose(src_fd);
	fclose(dst_fd);

	return 0;
}

//------------------------------------------------------------------------------
static void print_usage(const char *appName)
{
	printf(
		"Usage : %s [options] -i [input file] , [M] = mandatory, [O] = Optional \n"
		"  common options :\n"
		"     -i [input file name]        [M]  : input media file name (When is camera encoder, the value set NULL\n"
		"     -o [output file name]       [M]  : output file name\n"
		"     -s [width],[height]         [M]  : input image's size\n"
		"     -d [width],[height]         [M]  : destination image's size\n"
		"     -c [x],[y],[width],[height] [O]  : input crop information\n"
		"     -h : help\n"
		" =========================================================================================================\n\n",
		appName);
	printf(
		" Example :\n"
		"     #> %s -i [input filename] -o [output filename] -s 1920,1080 -d 1280,720\n"
		"     #> %s -i [input filename] -o [output filename] -s 1920,1080 -d 1280,720 -c 0,0,640,480\n",
		appName, appName);
}

int32_t main(int32_t argc, char *argv[])
{
	int32_t opt;
	char *strInFile = NULL;
	char *strOutFile = NULL;
	int32_t inWidth, inHeight;
	int32_t outWidth, outHeight;
	int32_t cropX=0, cropY=0, cropWidth=0, cropHeight=0;

	printf("======scaler test application=====\n");
	while (-1 != (opt = getopt(argc, argv, "i:o:hc:d:s:")))
	{
		switch (opt)
		{
			case 'i':
				strInFile = strdup(optarg);
				break;
			case 'o':
				strOutFile = strdup(optarg);
				break;
			case 's':
				sscanf(optarg, "%d,%d", &inWidth, &inHeight);
				break;
			case 'd':
				sscanf(optarg, "%d,%d", &outWidth, &outHeight);
				break;
			case 'c':
				sscanf(optarg, "%d,%d,%d,%d", &cropX, &cropY, &cropWidth, &cropHeight);
				break;
			case 'h':
				print_usage(argv[0]);
				return 0;
			default:
				break;
		}
	}

	if( NULL==strInFile || NULL==strOutFile || 0>=inWidth || 0>=inHeight || 0>=outWidth || 0>=outHeight )
	{
		printf("Error : Invalid arguments!!!");
		print_usage(argv[0]);
		return -1;
	}

	printf("input:%s width:%d height:%d\n", strInFile, inWidth, inHeight);
	printf("output:%s width:%d height:%d\n", strOutFile, outWidth, outHeight);

	if( cropWidth==0 && cropHeight==0 )
	{
		cropX = 0;
		cropY = 0;
		cropWidth = inWidth;
		cropHeight = inHeight;
	}

	//	Allocate Input Image
	NX_MEMORY_HANDLE hInMem;
	hInMem = NX_AllocateMemory(inWidth, inHeight, 3, V4L2_PIX_FMT_YUV420, 4096); //	page table align
	if (hInMem == NULL) {
		printf("hInMem is NULL\n");
		return -1;
	}
	if (0 != NX_MapMemory(hInMem)) {
		printf("failed to map hInMem\n");
		return -1;
	}
	//	Allocate Output Image
	NX_MEMORY_HANDLE hOutMem;
	hOutMem = NX_AllocateMemory(outWidth, outHeight, 3, V4L2_PIX_FMT_YUV420, 4096); //	page table align
	if (hOutMem == NULL) {
		printf("hOutMem is NULL\n");
		return -1;
	}
	if (0 != NX_MapMemory(hOutMem)) {
		printf("failed to map hOutMem\n");
		return -1;
	}

	printf("Allocation Done!!\n");

	if (0 != LoadInputImage(hInMem, (const char *)strInFile))
	{
		printf( "Error : LoadInputImage !!!\n" );
		goto ErrorExit;
	}
	printf("LoadInputImage Done!!\n");
	if (0 != DoScaling(hInMem, hOutMem, cropX, cropY, cropWidth, cropHeight))
	{
		printf("Error : DoScaling !!!\n");
		goto ErrorExit;
	}
	printf("DoScaling Done!!\n");
	if (0 != SaveOutputImage(hOutMem, (const char *)strOutFile))
	{
		printf("Error : SaveOutputImage !!\n");
		goto ErrorExit;
	}
	if (0 != VerifyOutputImage(hInMem, (const char *)strInFile,
				hOutMem, (const char *)strOutFile))
	{
		printf("Error : VerifyOutputImage !!\n");
		goto ErrorExit;
	}
	printf("Done!!!\n");

ErrorExit:
    return 0;
}

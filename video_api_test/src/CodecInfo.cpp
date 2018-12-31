#include <linux/videodev2.h>
#include <videodev2_nxp_media.h>

//  FFMPEG Headers
#ifdef __cplusplus
#define __STDC_CONSTANT_MACROS
#ifdef _STDINT_H
#undef _STDINT_H
#endif
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C"{
#endif
#include "libavcodec/avcodec.h"
#ifdef __cplusplus
}
#endif

#ifndef MKTAG
#define MKTAG(a,b,c,d) (a | (b << 8) | (c << 16) | (d << 24))
#endif

unsigned int CodecIdToV4l2Type(int codecId, unsigned int fourcc)
{
	unsigned int v4l2CodecType = -1;

	if (codecId == AV_CODEC_ID_H264)
	{
		v4l2CodecType = V4L2_PIX_FMT_H264;
	}
	else if (codecId == AV_CODEC_ID_MPEG2VIDEO)
	{
		v4l2CodecType = V4L2_PIX_FMT_MPEG2;
	}
	else if (codecId == AV_CODEC_ID_MPEG4 || codecId == AV_CODEC_ID_MSMPEG4V3)
	{
		switch (fourcc)
		{
		case MKTAG('D','I','V','3'):
		case MKTAG('M','P','4','3'):
		case MKTAG('M','P','G','3'):
		case MKTAG('D','V','X','3'):
		case MKTAG('A','P','4','1'):
			v4l2CodecType = V4L2_PIX_FMT_DIV3;
			break;
		case MKTAG('X','V','I','D'):
			v4l2CodecType = V4L2_PIX_FMT_XVID;
			break;
		case MKTAG('D','I','V','X'):
			v4l2CodecType = V4L2_PIX_FMT_DIVX;
			break;
		case MKTAG('D','I','V','4'):
			v4l2CodecType = V4L2_PIX_FMT_DIV4;
			break;
		case MKTAG('D','X','5','0'):
		case MKTAG('D','I','V','5'):
			v4l2CodecType = V4L2_PIX_FMT_DIV5;
			break;
		case MKTAG('D','I','V','6'):
			v4l2CodecType = V4L2_PIX_FMT_DIV6;
			break;
		default:
			v4l2CodecType = V4L2_PIX_FMT_MPEG4;
			break;
		}
	}
	else if (codecId == AV_CODEC_ID_H263 || codecId == AV_CODEC_ID_H263P || codecId == AV_CODEC_ID_H263I)
	{
		v4l2CodecType = V4L2_PIX_FMT_H263;
	}
	else if (codecId == AV_CODEC_ID_WMV3)
	{
		v4l2CodecType = V4L2_PIX_FMT_WMV9;
	}
	else if (codecId == AV_CODEC_ID_VC1)
	{
		v4l2CodecType = V4L2_PIX_FMT_WVC1;
	}
	else if (codecId == AV_CODEC_ID_RV30)
	{
		v4l2CodecType = V4L2_PIX_FMT_RV8;
	}
	else if (codecId == AV_CODEC_ID_RV40)
	{
		v4l2CodecType = V4L2_PIX_FMT_RV9;
	}
	else if (codecId == AV_CODEC_ID_VP8)
	{
		v4l2CodecType = V4L2_PIX_FMT_VP8;
	}
	else if (codecId == AV_CODEC_ID_FLV1)
	{
		v4l2CodecType = V4L2_PIX_FMT_FLV1;
	}
	else if (codecId == AV_CODEC_ID_THEORA)
	{
		v4l2CodecType = V4L2_PIX_FMT_THEORA;
	}
	else if (codecId == AV_CODEC_ID_MJPEG)
	{
		v4l2CodecType = V4L2_PIX_FMT_MJPEG;
	}
	/*else if(codecId == AV_CODEC_ID_HEVC)
	{
		v4l2CodecType = NX_HEVC_DEC;
	}*/
	else
	{
		printf("Cannot support codecid(%d) (0x %x) \n", codecId, codecId);
		return -1;
	}

	return v4l2CodecType;
}

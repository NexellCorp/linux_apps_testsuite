//------------------------------------------------------------------------------
//
//	Copyright (C) 2016 Nexell Co. All Rights Reserved
//	Nexell Co. Proprietary & Confidential
//
//	NEXELL INFORMS THAT THIS CODE AND INFORMATION IS PROVIDED "AS IS" BASE
//  AND	WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING
//  BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS
//  FOR A PARTICULAR PURPOSE.
//
//	Module		:
//	File		:
//	Description	:
//	Author		:
//	Export		:
//	History		:
//
//------------------------------------------------------------------------------

#include <stdio.h>

#include <sys/types.h>	//	open
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include <unistd.h>

#include <linux/videodev2.h>

#include <nx_video_alloc.h>
#include <nx_video_api.h>

#include "MediaExtractor.h"
#include "CodecInfo.h"
#include "Util.h"
#include "NX_V4l2Utils.h"

#include <videodev2_nxp_media.h>

#ifdef ANDROID
#include "NX_AndroidRenderer.h"
#define	WINDOW_WIDTH				800 // 1024
#define	WINDOW_HEIGHT				480 // 600
#define	NUMBER_OF_BUFFER			12
#define	MAX_NUMBER_OF_BUFFER		12
#endif

#define ENABLE_ASPECT_RATIO
#define SCREEN_WIDTH				(1080)
#define SCREEN_HEIGHT				(1920)

#if ENABLE_DRM_DISPLAY
#include <drm_fourcc.h>
#include "DrmRender.h"
#endif

#define NX_IMAGE_FORMAT					V4L2_PIX_FMT_YUV420	// V4L2_PIX_FMT_YVU420
#define NX_ADDITIONAL_BUFFER			3

#define NX_ARRAY_SIZE(X)				(sizeof(X) / sizeof(X[0]))
#define NX_ENABLE_FRAME_INFO			true
#define NX_TEST_KEYFRAME_INTERVAL		false
#define NX_TEST_PERFORMANCE_TYPE		DECODED_FRAME	// DECODED_FRAME, DISPLAY_FRAME
#define NX_TEST_PERFORMANCE_SAMPLING	false
#define NX_FRAME_SAMPLING				500

#define NX_TEST_FLUSH_API				true	// use flush api
#define NX_TEST_REMAIN_SIZE				true

#define NX_DUMP_PATH					"/mnt/dump"
#define NX_DUMP_INSTREAM				false

#define PLANE_ID						26
#define CRTC_ID							31

extern bool bExitLoop;

static const char *pstPicType[] = {
	/* 0 */	"I Frame",
	/* 1 */	"P Frame",
	/* 2 */	"B Frame",
	/* 3 */	"VC1BI Frame",
	/* 4 */	"Skip Frame",
	/* 5 */	"IDR Frame",
	/* 6 */	"Unknown Frame",
	/* 7 */	"Total Frame",
};

static uint64_t stFrameCount[NX_ARRAY_SIZE(pstPicType)] = { 0, };
static uint64_t stFrameTime[NX_ARRAY_SIZE(pstPicType)] = { 0, };

#define NX_INDEX_TOTAL_FRAME			(NX_ARRAY_SIZE(pstPicType)-1)

//----------------------------------------------------------------------------------------------------
//
//	Signal Handler
//
static void signal_handler( int32_t sig )
{
	printf("Aborted by signal %s (%d)..\n", (char*)strsignal(sig), sig);

	switch( sig )
	{
		case SIGINT :
			printf("SIGINT..\n"); 	break;
		case SIGTERM :
			printf("SIGTERM..\n");	break;
		case SIGABRT :
			printf("SIGABRT..\n");	break;
		default :
			break;
	}

	if( !bExitLoop )
		bExitLoop = true;
	else{
		//	wait 1 seconds for double Ctrl+C operation
		usleep(1000000);
		exit(EXIT_FAILURE);
	}
}

//------------------------------------------------------------------------------
static void register_signal( void )
{
	signal( SIGINT,  signal_handler );
	signal( SIGTERM, signal_handler );
	signal( SIGABRT, signal_handler );
}

//------------------------------------------------------------------------------
static void CalStatistics( int32_t iIndex, int32_t iPicType, uint64_t iInterval )
{
	if( 0 > iIndex )
		return;

	stFrameTime[(0 <= iPicType && 6 >= iPicType) ? iPicType : 6] += iInterval;
	stFrameCount[(0 <= iPicType && 6 >= iPicType) ? iPicType : 6]++;

	stFrameTime[7] += iInterval;
	stFrameCount[7]++;
}

//------------------------------------------------------------------------------
static void DisplayStatistics( void )
{
	printf("-------------------+-------------------+-------------------+-------------------+--------------------\n");
	printf("         Frame Type|        Total Frame|         Total Time|       Average Time|        Average Fps \n");
	printf("-------------------+-------------------+-------------------+-------------------+--------------------\n");
	for( int32_t i = 0; i < NX_INDEX_TOTAL_FRAME; i++ )
	{
		printf("%19s|%13lld frame|%14lld mSec|%14.2lf mSec|%15.2lf fps\n",
			pstPicType[i],
			stFrameCount[i],
			stFrameTime[i],
			stFrameTime[i] ? (double)stFrameTime[i]/(double)stFrameCount[i] : 0,
			stFrameCount[i] ? (double)(stFrameCount[i]*1000)/(double)stFrameTime[i] : 0
		);
	}
	printf("-------------------+-------------------+-------------------+-------------------+--------------------\n");
	{
		char szTemp[128];
		snprintf( szTemp, sizeof(szTemp), "Total %s Frame", NX_TEST_PERFORMANCE_TYPE ? "Display" : "Decoded" );

		uint64_t iFrameCount = stFrameCount[NX_INDEX_TOTAL_FRAME];
		uint64_t iFrameTime  = stFrameTime[NX_INDEX_TOTAL_FRAME];

		printf("%19s|%13lld frame|%14lld mSec|%14.2lf mSec|%15.2lf fps\n",
			szTemp,
			iFrameCount,
			iFrameTime,
			iFrameTime ? (double)iFrameTime/(double)iFrameCount : 0,
			iFrameCount ? (double)(iFrameCount*1000)/(double)iFrameTime : 0
		);
	}
	printf("-------------------+-------------------+-------------------+-------------------+--------------------\n");
}

//------------------------------------------------------------------------------
int32_t VpuDecStrmMain( CODEC_APP_DATA *pAppData )
{
	NX_V4L2DEC_HANDLE hDec = NULL;
#if ENABLE_DRM_DISPLAY
	DRM_DSP_HANDLE hDsp = NULL;
#endif
	uint8_t streamBuffer[4*1024*1024];
	int32_t ret;
	int32_t imgWidth = -1, imgHeight = -1;
	uint32_t v4l2CodecType;
	int32_t iStrmCount = 0;

	if( bExitLoop )
		return -1;

	STREAM_FILE *pInStreamFile = pAppData->pInStreamFile;

	imgWidth      = pInStreamFile->iWidth;
	imgHeight     = pInStreamFile->iHeight;
	v4l2CodecType = pInStreamFile->iCodecType;

	register_signal();

	for( int32_t i = 0; i < NX_ARRAY_SIZE(pstPicType); i++ )
		stFrameCount[i] = stFrameTime[i] = 0;

	//==============================================================================
	// DISPLAY INITIALIZATION
	//==============================================================================
	{
#if ENABLE_DRM_DISPLAY
		int drmFd = open("/dev/dri/card0", O_RDWR);

		hDsp = CreateDrmDisplay(drmFd);
		DRM_RECT srcRect, dstRect;

		srcRect.x = 0;
		srcRect.y = 0;
		srcRect.width = imgWidth;
		srcRect.height = imgHeight;
		dstRect.x = 0;
		dstRect.y = 0;
		dstRect.width = imgWidth;
		dstRect.height = imgHeight;

#ifdef ENABLE_ASPECT_RATIO
		double xRatio = (double)SCREEN_WIDTH/(double)imgWidth;
		double yRatio = (double)SCREEN_HEIGHT/(double)imgHeight;

		if (xRatio > yRatio)
		{
			dstRect.width = imgWidth * yRatio;
			dstRect.height = SCREEN_HEIGHT;
			dstRect.x = abs(SCREEN_WIDTH - dstRect.width)/2;
		}
		else
		{
			dstRect.width = SCREEN_WIDTH;
			dstRect.height = imgHeight * xRatio;
			dstRect.y = abs(SCREEN_HEIGHT - dstRect.height)/2;
		}
#endif

		InitDrmDisplay(hDsp, PLANE_ID, CRTC_ID, DRM_FORMAT_YUV420, srcRect, dstRect);
#endif	//	ENABLE_DRM_DISPLAY
	}

#ifdef ANDROID
	CNX_AndroidRenderer *pAndRender = new CNX_AndroidRenderer(WINDOW_WIDTH, WINDOW_HEIGHT);
	NX_VID_MEMORY_HANDLE *pMemHandle = NULL;
#endif

	hDec = NX_V4l2DecOpen(v4l2CodecType);
	if (hDec == NULL)
	{
		printf("Fail, NX_V4l2DecOpen().\n");
		return -1;
	}

	printf(">>> StreamFile ( format: 0x%08X, %s )\n",
		v4l2CodecType, NX_V4l2GetFormatString(v4l2CodecType));

	//==============================================================================
	// PROCESS UNIT
	//==============================================================================
	{
		int32_t bInit = false;

		int frmCnt = 0, size = 0;
		uint32_t outFrmCnt = 0;
		uint64_t startTime, endTime, totalTime = 0;
		uint64_t prevTime = 0;
		int64_t timeStamp = -1;

		FILE *fpOut = NULL;
		int32_t prvIndex = -1, curIndex = -1;
		NX_VID_MEMORY_HANDLE hCurImg = NULL;

		int32_t additionSize = 0;

		NX_V4L2DEC_SEQ_IN seqIn;
		NX_V4L2DEC_SEQ_OUT seqOut;

		NX_V4L2DEC_IN decIn;
		NX_V4L2DEC_OUT decOut;

		if (pAppData->outFileName)
		{
			fpOut = fopen(pAppData->outFileName, "wb");
			if (fpOut == NULL) {
				printf("output file open error!!\n");
				ret = -1;
				goto DEC_TERMINATE;
			}
		}

		while(!bExitLoop)
		{
			if( !bInit )
			{
				memcpy( streamBuffer, pInStreamFile->ppStrmInfo[iStrmCount]->pBuf, pInStreamFile->ppStrmInfo[iStrmCount]->iSize );
				additionSize = pInStreamFile->ppStrmInfo[iStrmCount]->iSize;
				iStrmCount++;
			}

			{
				if( pInStreamFile->iStrmNum > iStrmCount )
				{
					memcpy( streamBuffer + additionSize, pInStreamFile->ppStrmInfo[iStrmCount]->pBuf, pInStreamFile->ppStrmInfo[iStrmCount]->iSize );
					size = pInStreamFile->ppStrmInfo[iStrmCount]->iSize;
				}
				else
				{
					size = 0;
				}

				timeStamp = 0;
				iStrmCount++;
			}

			if( !bInit )
			{
				memset(&seqIn, 0, sizeof(seqIn));
				seqIn.width     = imgWidth;
				seqIn.height    = imgHeight;
				seqIn.strmBuf   = streamBuffer+additionSize;
				seqIn.strmSize  = size;
				seqIn.seqBuf	= streamBuffer;
				seqIn.seqSize   = additionSize;
				seqIn.timeStamp = timeStamp;

				if (v4l2CodecType == V4L2_PIX_FMT_MJPEG)
					seqIn.thumbnailMode = 0;

				ret = NX_V4l2DecParseVideoCfg(hDec, &seqIn, &seqOut);
				if (ret < 0)
				{
					printf("Fail, NX_V4l2DecParseVideoCfg()\n");
					goto DEC_TERMINATE;
				}

				seqIn.width       = seqOut.width;
				seqIn.height      = seqOut.height;
				seqIn.imgPlaneNum = NX_V4l2GetPlaneNum(NX_IMAGE_FORMAT);
				seqIn.imgFormat   = NX_IMAGE_FORMAT;
				seqIn.numBuffers  = seqOut.minBuffers + NX_ADDITIONAL_BUFFER;

#ifdef ANDROID
				pAndRender->GetBuffers(seqIn.numBuffers, imgWidth, imgHeight, &pMemHandle );
				NX_VID_MEMORY_HANDLE hVideoMemory[MAX_NUMBER_OF_BUFFER];
				for( int32_t i=0 ; i<seqIn.numBuffers ; i++ )
				{
					hVideoMemory[i] = pMemHandle[i];
				}
				seqIn.pMemHandle = &hVideoMemory[0];
				seqIn.imgFormat = hVideoMemory[0]->format;
#endif

				printf("[Sequence Data] width( %d ), height( %d ), plane( %d ), format( 0x%08x ), reqiured buffer( %d ), current buffer( %d )\n",
					seqIn.width, seqIn.height, seqIn.imgPlaneNum, seqIn.imgFormat, seqIn.numBuffers - NX_ADDITIONAL_BUFFER, seqIn.numBuffers );

				ret = NX_V4l2DecInit(hDec, &seqIn);
				if (ret < 0)
				{
					printf("Fail, NX_V4l2DecInit().\n");
					goto DEC_TERMINATE;
				}

				bInit = true;
				additionSize = 0;
				continue;
			}

			/*
			Skip Bitstream
			*/
			if(	(V4L2_PIX_FMT_XVID == v4l2CodecType && 0 < size &&   7 >= size) ||
				(V4L2_PIX_FMT_DIV5 == v4l2CodecType && 0 < size &&   8 >= size) ) {
				// NX_DumpData( streamBuffer, (size < 16) ? size : 16, "[     Skip] " );
				continue;
			}

			do {
				memset(&decIn, 0, sizeof(NX_V4L2DEC_IN));
				decIn.strmBuf   = (size > 0) ? streamBuffer : NULL;
				decIn.strmSize  = (size > 0) ? size + additionSize : 0;
				decIn.timeStamp = (size > 0) ? timeStamp : 0;
				decIn.eos       = (size > 0) ? 0 : 1;

				startTime       = NX_GetTickCount();
				ret             = NX_V4l2DecDecodeFrame(hDec, &decIn, &decOut);
				endTime         = NX_GetTickCount();
				totalTime       += (endTime - startTime);

				CalStatistics(
					NX_TEST_PERFORMANCE_TYPE ? decOut.dispIdx : decOut.decIdx,
					decOut.picType[NX_TEST_PERFORMANCE_TYPE],
					endTime - startTime
				);

				if (ret > 0) {
					printf("Need more frame. ( StrmCnt=%3d, Size=%6d, InTimeStamp=%7lld, RemainByte=%6d )\n",
						iStrmCount, decIn.strmSize, timeStamp, decOut.remainByte);
					continue;
				}

				if (ret < 0)
				{
					printf("Fail, NX_V4l2DecDecodeFrame().\n");
					DisplayStatistics();
					break;
				}

				printf("[%5d Frm] StrmCnt=%3d, Size=%6d, DecIdx=%2d, DispIdx=%2d, InTimeStamp=%7lld, outTimeStamp=%7lld, Time=%6llu, interlace=%1d %1d, Reliable=%3d, %3d, type =%3d, %3d, UsedByte=%6d, RemainByte=%6d\n",
					frmCnt, iStrmCount, decIn.strmSize, decOut.decIdx, decOut.dispIdx, timeStamp, decOut.timeStamp[DISPLAY_FRAME], (endTime - startTime), decOut.interlace[DECODED_FRAME], decOut.interlace[DISPLAY_FRAME],
					decOut.outFrmReliable_0_100[DECODED_FRAME], decOut.outFrmReliable_0_100[DISPLAY_FRAME], decOut.picType[DECODED_FRAME], decOut.picType[DISPLAY_FRAME], decOut.usedByte, decOut.remainByte);
				/*printf("%2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x\n",
					streamBuffer[0], streamBuffer[1], streamBuffer[2], streamBuffer[3], streamBuffer[4], streamBuffer[5], streamBuffer[6], streamBuffer[7],
					streamBuffer[8], streamBuffer[9], streamBuffer[10], streamBuffer[11], streamBuffer[12], streamBuffer[13], streamBuffer[14], streamBuffer[15]);*/

				curIndex = decOut.dispIdx;
				hCurImg  = &decOut.hImg;

				if (curIndex >= 0)
				{
					if (fpOut)
					{
						NX_V4l2DumpMemory(hCurImg, fpOut);
					}

#if ENABLE_DRM_DISPLAY
					UpdateBuffer(hDsp, hCurImg, NULL);
#endif

#ifdef ANDROID
					pAndRender->DspQueueBuffer( NULL, curIndex );
					if( prvIndex != -1 )
					{
						pAndRender->DspDequeueBuffer(NULL, NULL);
					}
#endif

					if( pAppData->dumpFileName && outFrmCnt==pAppData->dumpFrameNumber )
					{
						printf("Dump Frame. ( frm: %d, name: %s )\n", outFrmCnt, pAppData->dumpFileName);
						NX_V4l2DumpMemory(hCurImg, (const char*)pAppData->dumpFileName );
					}

					if( prvIndex >= 0 )
					{
						ret = NX_V4l2DecClrDspFlag(hDec, NULL, prvIndex);
						if (0 > ret)
						{
							printf("Fail, NX_V4l2DecClrDspFlag().\n");
							break;
						}
					}

					prvIndex = curIndex;
					outFrmCnt++;
				}

				frmCnt++;
				additionSize = 0;
			} while( size == 0 && decOut.remainByte != 0 );

			if( (0 > ret) || (size == 0 && decOut.remainByte == 0) ) break;
		}

		if( prvIndex >= 0 )
		{
			NX_V4l2DecClrDspFlag(hDec, NULL, prvIndex);
			prvIndex = -1;
		}

		if (fpOut)
			fclose(fpOut);
	}

	//==============================================================================
	// TERMINATION
	//==============================================================================
DEC_TERMINATE:
	if (hDec)
		ret = NX_V4l2DecClose(hDec);

#ifdef ANDROID
	if( pAndRender )
		delete pAndRender;
#endif

	printf("Decode End!!(ret = %d)\n", ret);
	return ret;
}

//------------------------------------------------------------------------------
int32_t VpuDecMain( CODEC_APP_DATA *pAppData )
{
	NX_V4L2DEC_HANDLE hDec = NULL;
#if ENABLE_DRM_DISPLAY
	DRM_DSP_HANDLE hDsp = NULL;
#endif
	uint8_t streamBuffer[4*1024*1024];
	int32_t ret, seqflg = 0;
	int32_t imgWidth = -1, imgHeight = -1;

	if( bExitLoop )
		return -1;

	CMediaReader *pMediaReader = new CMediaReader();
	if (!pMediaReader->OpenFile( pAppData->inFileName))
	{
		printf("Cannot open media file(%s)\n", pAppData->inFileName);
		exit(-1);
	}
	pMediaReader->GetVideoResolution(&imgWidth, &imgHeight);

	register_signal();

	for( int32_t i = 0; i < NX_ARRAY_SIZE(pstPicType); i++ )
		stFrameCount[i] = stFrameTime[i] = 0;

	//==============================================================================
	// DISPLAY INITIALIZATION
	//==============================================================================
	{
#if ENABLE_DRM_DISPLAY
		int drmFd = open("/dev/dri/card0", O_RDWR);

		hDsp = CreateDrmDisplay(drmFd);
		DRM_RECT srcRect, dstRect;

		srcRect.x = 0;
		srcRect.y = 0;
		srcRect.width = imgWidth;
		srcRect.height = imgHeight;
		dstRect.x = 0;
		dstRect.y = 0;
		dstRect.width = imgWidth;
		dstRect.height = imgHeight;

#ifdef ENABLE_ASPECT_RATIO
		double xRatio = (double)SCREEN_WIDTH/(double)imgWidth;
		double yRatio = (double)SCREEN_HEIGHT/(double)imgHeight;

		if (xRatio > yRatio)
		{
			dstRect.width = imgWidth * yRatio;
			dstRect.height = SCREEN_HEIGHT;
			dstRect.x = abs(SCREEN_WIDTH - dstRect.width)/2;
		}
		else
		{
			dstRect.width = SCREEN_WIDTH;
			dstRect.height = imgHeight * xRatio;
			dstRect.y = abs(SCREEN_HEIGHT - dstRect.height)/2;
		}
#endif

		InitDrmDisplay(hDsp, PLANE_ID, CRTC_ID, DRM_FORMAT_YUV420, srcRect, dstRect);
#endif	//	ENABLE_DRM_DISPLAY
	}

#ifdef ANDROID
	CNX_AndroidRenderer *pAndRender = new CNX_AndroidRenderer(WINDOW_WIDTH, WINDOW_HEIGHT);
	NX_VID_MEMORY_HANDLE *pMemHandle = NULL;
#endif

	uint32_t v4l2CodecType;
	int32_t fourcc = -1, codecId = -1;

	pMediaReader->GetCodecTagId(AVMEDIA_TYPE_VIDEO, &fourcc, &codecId);
	v4l2CodecType = CodecIdToV4l2Type(codecId, fourcc);

	hDec = NX_V4l2DecOpen(v4l2CodecType);
	if (hDec == NULL)
	{
		printf("Fail, NX_V4l2DecOpen().\n");
		return -1;
	}

	printf(">>> File Info: %s ( format: 0x%08X, %s )\n",
		pAppData->inFileName, v4l2CodecType, NX_V4l2GetFormatString(v4l2CodecType));

	//==============================================================================
	// PROCESS UNIT
	//==============================================================================
	{
		int32_t bInit = false, bSeek = false;

		int frmCnt = 0, size = 0;
		uint32_t outFrmCnt = 0;
		uint64_t startTime, endTime, totalTime = 0;
		uint64_t prevTime = 0;
		int64_t timeStamp = -1;

		FILE *fpOut = NULL;
		int32_t prvIndex = -1, curIndex = -1;
		NX_VID_MEMORY_HANDLE hCurImg = NULL;
		int32_t bIsSeek = 0;

		int32_t additionSize = 0;

#if NX_TEST_KEYFRAME_INTERVAL
		int32_t prvKeyFrame = -1;
#endif

		NX_V4L2DEC_SEQ_IN seqIn;
		NX_V4L2DEC_SEQ_OUT seqOut;

		NX_V4L2DEC_IN decIn;
		NX_V4L2DEC_OUT decOut;

		if (pAppData->outFileName)
		{
			fpOut = fopen(pAppData->outFileName, "wb");
			if (fpOut == NULL) {
				printf("output file open error!!\n");
				ret = -1;
				goto DEC_TERMINATE;
			}
		}


#if NX_DUMP_INSTREAM
		int32_t iDumpCount = 0;
#endif

		while(!bExitLoop)
		{
			int32_t key = 0;

			if( !bInit )
			{
				additionSize = pMediaReader->GetVideoSeqInfo(streamBuffer);
#if NX_DUMP_INSTREAM
				NX_DumpStream(streamBuffer, additionSize, "%s/dump_seq.h264", NX_DUMP_PATH );
#endif
			}

			if (pMediaReader->ReadStream(CMediaReader::MEDIA_TYPE_VIDEO, streamBuffer + additionSize, &size, &key, &timeStamp ) != 0)
			{
				size = 0;
			}

#if NX_DUMP_INSTREAM
			if( size != 0 )	NX_DumpData(streamBuffer+additionSize, size > 16 ? 16 : size, "DumpData[%04d]: ", iDumpCount );
			NX_DumpStream(streamBuffer+additionSize, size, "%s/dump_%04d.h264", NX_DUMP_PATH, iDumpCount++);
#endif

			if( !bInit && !key )
				continue;

			if( bSeek && !key )
				continue;

			if( !bInit )
			{
				memset(&seqIn, 0, sizeof(seqIn));
				seqIn.width     = imgWidth;
				seqIn.height    = imgHeight;
				seqIn.strmBuf   = streamBuffer+additionSize;
				seqIn.strmSize  = size;
				seqIn.seqBuf	= streamBuffer;
				seqIn.seqSize   = additionSize;
				seqIn.timeStamp = timeStamp;

				if (v4l2CodecType == V4L2_PIX_FMT_MJPEG)
					seqIn.thumbnailMode = 0;

				ret = NX_V4l2DecParseVideoCfg(hDec, &seqIn, &seqOut);
				if (ret < 0)
				{
					printf("Fail, NX_V4l2DecParseVideoCfg()\n");
					goto DEC_TERMINATE;
				}

				seqIn.width       = seqOut.width;
				seqIn.height      = seqOut.height;
				seqIn.imgPlaneNum = NX_V4l2GetPlaneNum(NX_IMAGE_FORMAT);
				seqIn.imgFormat   = NX_IMAGE_FORMAT;
				seqIn.numBuffers  = seqOut.minBuffers + NX_ADDITIONAL_BUFFER;

#ifdef ANDROID
				pAndRender->GetBuffers(seqIn.numBuffers, imgWidth, imgHeight, &pMemHandle );
				NX_VID_MEMORY_HANDLE hVideoMemory[MAX_NUMBER_OF_BUFFER];
				for( int32_t i=0 ; i<seqIn.numBuffers ; i++ )
				{
					hVideoMemory[i] = pMemHandle[i];
				}
				seqIn.pMemHandle = &hVideoMemory[0];
				seqIn.imgFormat = hVideoMemory[0]->format;
#endif

				printf("[Sequence Data] width( %d ), height( %d ), plane( %d ), format( 0x%08x ), reqiured buffer( %d ), current buffer( %d )\n",
					seqIn.width, seqIn.height, seqIn.imgPlaneNum, seqIn.imgFormat, seqIn.numBuffers - NX_ADDITIONAL_BUFFER, seqIn.numBuffers );

				ret = NX_V4l2DecInit(hDec, &seqIn);
				if (ret < 0)
				{
					printf("Fail, NX_V4l2DecInit().\n");
					goto DEC_TERMINATE;
				}

				bInit = true;
				continue;
			}

 			if (frmCnt == pAppData->iSeekStartFrame && pAppData->iSeekStartFrame)
			{
				int32_t seekPos;
				int64_t duration;
				pMediaReader->GetDuration(&duration);

				seekPos = (0 > pAppData->iSeekPos) ? duration/1000000 + pAppData->iSeekPos : pAppData->iSeekPos;

				printf("Seek. ( frmCnt(%d frm), seekPos(%d sec) )\n", frmCnt, seekPos );
				usleep(1000000);

				pMediaReader->SeekStream( seekPos * 1000 );

#if NX_TEST_FLUSH_API
				NX_V4l2DecFlush( hDec );
#else
				do {
					decIn.strmBuf   = 0;
					decIn.strmSize  = 0;
					decIn.timeStamp = 0;
					decIn.eos       = 1;

					ret = NX_V4l2DecDecodeFrame( hDec, &decIn, &decOut );
					if( !ret && decOut.dispIdx >= 0 )
						NX_V4l2DecClrDspFlag( hDec, NULL, decOut.dispIdx );
				} while( !ret && decOut.dispIdx >= 0 );

				if( prvIndex >= 0 )
					NX_V4l2DecClrDspFlag( hDec, NULL, prvIndex );
#endif
				prvIndex = -1;
				additionSize = 0;
				bSeek = true;
				frmCnt++;
				continue;
			}

			/*
			Skip Bitstream
			*/
			if(	(V4L2_PIX_FMT_XVID == v4l2CodecType && 0 < size &&   7 >= size) ||
				(V4L2_PIX_FMT_DIV5 == v4l2CodecType && 0 < size &&   8 >= size) ) {
				// NX_DumpData( streamBuffer, (size < 16) ? size : 16, "[     Skip] " );
				continue;
			}

			do {
				memset(&decIn, 0, sizeof(NX_V4L2DEC_IN));
				decIn.strmBuf   = (size > 0) ? streamBuffer : NULL;
				decIn.strmSize  = (size > 0) ? size + additionSize : 0;
				decIn.timeStamp = (size > 0) ? timeStamp : 0;
				decIn.eos       = (size > 0) ? 0 : 1;

				startTime       = NX_GetTickCount();
				ret             = NX_V4l2DecDecodeFrame(hDec, &decIn, &decOut);
				endTime         = NX_GetTickCount();
				totalTime       += (endTime - startTime);

				bSeek = false;

				CalStatistics(
					NX_TEST_PERFORMANCE_TYPE ? decOut.dispIdx : decOut.decIdx,
					decOut.picType[NX_TEST_PERFORMANCE_TYPE],
					endTime - startTime
				);

				if (ret > 0) {
#if NX_ENABLE_FRAME_INFO
					// printf("Need more frame. ( Key=%d, Size=%6d, InTimeStamp=%7lld, RemainByte=%6d )\n",
					// 	key, decIn.strmSize, timeStamp, decOut.remainByte);
#endif
					continue;
				}

				if (ret < 0)
				{
					printf("Fail, NX_V4l2DecDecodeFrame().\n");
					DisplayStatistics();
					break;
				}

#if NX_TEST_KEYFRAME_INTERVAL
				if (key) {
					if (0 > prvKeyFrame)
						printf("[%5d Frm] KeyFrame\n", frmCnt);
					else
						printf("[%5d Frm] KeyFrame, Interval=%3d\n", frmCnt, frmCnt-prvKeyFrame);

					prvKeyFrame = frmCnt;
				}

				frmCnt++;
				continue;
#else
#if NX_ENABLE_FRAME_INFO
				printf("[%5d Frm] Key=%d, Size=%6d, DecIdx=%2d, DispIdx=%2d, InTimeStamp=%7lld, outTimeStamp=%7lld, Time=%6llu, interlace=%1d %1d, Reliable=%3d, %3d, type =%3d, %3d, UsedByte=%6d, RemainByte=%6d\n",
					frmCnt, key, decIn.strmSize, decOut.decIdx, decOut.dispIdx, timeStamp, decOut.timeStamp[DISPLAY_FRAME], (endTime - startTime), decOut.interlace[DECODED_FRAME], decOut.interlace[DISPLAY_FRAME],
					decOut.outFrmReliable_0_100[DECODED_FRAME], decOut.outFrmReliable_0_100[DISPLAY_FRAME], decOut.picType[DECODED_FRAME], decOut.picType[DISPLAY_FRAME], decOut.usedByte, decOut.remainByte);
				/*printf("%2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x\n",
					streamBuffer[0], streamBuffer[1], streamBuffer[2], streamBuffer[3], streamBuffer[4], streamBuffer[5], streamBuffer[6], streamBuffer[7],
					streamBuffer[8], streamBuffer[9], streamBuffer[10], streamBuffer[11], streamBuffer[12], streamBuffer[13], streamBuffer[14], streamBuffer[15]);*/

				// NX_DumpData( streamBuffer, (size < 16) ? size : 16, "[%5d Frm] ", frmCnt );
#endif
#endif
				curIndex = decOut.dispIdx;
				hCurImg  = &decOut.hImg;

				if (curIndex >= 0)
				{
					if (fpOut)
					{
						NX_V4l2DumpMemory(hCurImg, fpOut);
					}

#if ENABLE_DRM_DISPLAY
					UpdateBuffer(hDsp, hCurImg, NULL);
#endif

#ifdef ANDROID
					pAndRender->DspQueueBuffer( NULL, curIndex );
					if( prvIndex != -1 )
					{
						pAndRender->DspDequeueBuffer(NULL, NULL);
					}
#endif

					if( pAppData->dumpFileName && outFrmCnt==pAppData->dumpFrameNumber )
					{
						printf("Dump Frame. ( frm: %d, name: %s )\n", outFrmCnt, pAppData->dumpFileName);
						NX_V4l2DumpMemory(hCurImg, (const char*)pAppData->dumpFileName );
					}

					if( prvIndex >= 0 )
					{
						ret = NX_V4l2DecClrDspFlag(hDec, NULL, prvIndex);
						if (0 > ret)
						{
							printf("Fail, NX_V4l2DecClrDspFlag().\n");
							break;
						}
					}

					prvIndex = curIndex;
					outFrmCnt++;
				}

#if NX_TEST_PERFORMANCE_SAMPLING
				if (NX_FRAME_SAMPLING <= stFrameCount[NX_INDEX_TOTAL_FRAME] )
				{
					printf("Force Break For Sampling.\n");
					DisplayStatistics();
					ret = -1;
					break;
				}
#endif

				frmCnt++;
				additionSize = 0;

				if (pAppData->iMaxLimitFrame != 0 && pAppData->iMaxLimitFrame <= frmCnt)
				{
					printf("Force Break by User.\n");
					DisplayStatistics();
					ret = -1;
					break;
				}
#if NX_TEST_REMAIN_SIZE
			} while( size == 0 && decOut.remainByte != 0 );

			if( (0 > ret) || (size == 0 && decOut.remainByte == 0) ) break;
#else
			} while( size == 0 && decOut.dispIdx >= 0 && !ret );

			if( 0 > ret ) break;
#endif
		}

		if( prvIndex >= 0 )
		{
			NX_V4l2DecClrDspFlag(hDec, NULL, prvIndex);
			prvIndex = -1;
		}

		if (fpOut)
			fclose(fpOut);
	}

	//==============================================================================
	// TERMINATION
	//==============================================================================
DEC_TERMINATE:
	if (hDec)
		ret = NX_V4l2DecClose(hDec);

#ifdef ANDROID
	if( pAndRender )
		delete pAndRender;
#endif

	if (pMediaReader)
		delete pMediaReader;

	printf("Decode End!!(ret = %d)\n", ret);
	return ret;
}

//------------------------------------------------------------------------------
//
//	Copyright (C) 2018 Nexell Co. All Rights Reserved
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
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <Util.h>
#include <NX_V4l2Utils.h>

#include <linux/videodev2.h>
#include <videodev2_nxp_media.h>

#include <nx_video_api.h>

#define NX_MEMORY_TYPE		1			// 0: memory allocation, 1: video memory allocation
#define NX_MEMORY_NUM		16
#define NX_MEMORY_ALIGN		4096

// Nexell Video Memory Allcation Paramreter
#define NX_IMAGE_FORMAT		V4L2_PIX_FMT_NV12 // V4L2_PIX_FMT_YUV420
#define NX_MEMORY_WIDTH		1920
#define NX_MEMORY_HEIGHT	1080

// Nexell Memory Allocation Parameter
#define NX_MEMORY_SIZE		1024

#if NX_MEMORY_TYPE
static NX_VID_MEMORY_INFO* pstInfo[NX_MEMORY_NUM];
#else
static NX_MEMORY_INFO*	pstInfo[NX_MEMORY_NUM];
#endif

static uint8_t* pstData[NX_MEMORY_NUM];

static int32_t	AllocateMemory( void );
static void		FreeMemory( void );
static int32_t	VerifyMemory( void );

//----------------------------------------------------------------------------------------------------
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

	FreeMemory();
	exit(EXIT_FAILURE);
}

//------------------------------------------------------------------------------
static void register_signal( void )
{
	signal( SIGINT,  signal_handler );
	signal( SIGTERM, signal_handler );
	signal( SIGABRT, signal_handler );
}

//------------------------------------------------------------------------------
static int32_t GetRandomValue( int32_t iStartNum, int32_t iEndNum )
{
	if( iStartNum >= iEndNum )
		return -1;

	static int bSeed = false;
	if( !bSeed ) {
		srand(time(NULL));
		bSeed = true;
	}

	return rand() % (iEndNum - iStartNum + 1) + iStartNum;
}

//------------------------------------------------------------------------------
static void DumpHex( const void *pData, int32_t iSize, const char *format, ... )
{
	int32_t i;
	const uint8_t *_data = (const uint8_t *)pData;

	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);

	for( i = 0; i < iSize; i++ )
	{
		if( (i % 16) ==  0 ) printf("%04x\t", i);
		printf(" %02x", _data[i]);
		if( (i % 16) == 15 ) printf("\n");
	}

	if( (i % 16) != 0 ) printf("\n");
}

//------------------------------------------------------------------------------
static int32_t AllocateMemory()
{
	for( int32_t i = 0; i < NX_MEMORY_NUM; i++ )
	{
		pstInfo[i] = NULL;
		pstData[i] = NULL;
	}

	for( int32_t i = 0; i < NX_MEMORY_NUM; i++ )
	{
#if NX_MEMORY_TYPE
		pstInfo[i] = NX_AllocateVideoMemory( NX_MEMORY_WIDTH, NX_MEMORY_HEIGHT, NX_V4l2GetPlaneNum(NX_IMAGE_FORMAT), NX_IMAGE_FORMAT, NX_MEMORY_ALIGN );
		if( NULL == pstInfo[i] )
		{
			printf("Fail, NX_AllocateMemory(). ( size: %d, aling: %d )\n", NX_MEMORY_SIZE, NX_MEMORY_ALIGN	);
			return -1;
		}

		if( 0 > NX_MapVideoMemory( pstInfo[i] ) )
		{
			printf("Fail, NX_MapMemory().\n");
			return -1;
		}

		int32_t iSize = 0;
		for( int32_t j = 0; j < pstInfo[i]->planes; j++ )
			iSize += pstInfo[i]->size[j];

		pstData[i] = (uint8_t*)malloc( iSize );
		if( NULL == pstData[i] )
		{
			printf("Fail, malloc(). ( size: %d )\n", iSize);
			return -1;
		}
#else
		pstInfo[i] = NX_AllocateMemory( NX_MEMORY_SIZE, NX_MEMORY_ALIGN );
		if( NULL == pstInfo[i] )
		{
			printf("Fail, NX_AllocateMemory(). ( size: %d, aling: %d )\n", NX_MEMORY_SIZE, NX_MEMORY_ALIGN	);
			return -1;
		}

		if( 0 > NX_MapMemory( pstInfo[i] ) )
		{
			printf("Fail, NX_MapMemory().\n");
			return -1;
		}

		int32_t iSize = 0;
		iSize += pstInfo[i]->size;

		pstData[i] = (uint8_t*)malloc( iSize );
		if( NULL == pstData[i] )
		{
			printf("Fail, malloc(). ( size: %d )\n", iSize);
			return -1;
		}
#endif
	}

	for( int32_t i = 0; i < NX_MEMORY_NUM; i++ )
	{
#if NX_MEMORY_TYPE
		int32_t iSize = 0;
		for( int32_t j = 0; j < pstInfo[i]->planes; j++ )
			iSize += pstInfo[i]->size[j];

		for( int32_t j = 0; j < iSize; j++ )
		{
			pstData[i][j] = GetRandomValue( 0x00, 0xFF );
		}

		int32_t iOffset  = 0;
		for( int32_t j = 0; j < pstInfo[i]->planes; j++ )
		{
			uint8_t *pBuffer = (uint8_t*)pstInfo[i]->pBuffer[j];
			memcpy( pBuffer, pstData[i] + iOffset, pstInfo[i]->size[j] );
			iOffset += pstInfo[i]->size[j];
		}
#else
		int32_t iSize = 0;
		iSize += pstInfo[i]->size;

		for( int32_t j = 0; j < iSize; j++ )
		{
			pstData[i][j] = GetRandomValue( 0x00, 0xFF );
		}

		uint8_t *pBuffer = (uint8_t*)pstInfo[i]->pBuffer;
		memcpy( pBuffer, pstData[i], iSize );
#endif
	}

	return 0;
}

//------------------------------------------------------------------------------
static void FreeMemory()
{
	for( int32_t i = 0; i < NX_MEMORY_NUM; i++ )
	{
		if( pstInfo[i] )
		{
#if NX_MEMORY_TYPE
			NX_FreeVideoMemory( pstInfo[i] );
#else
			NX_FreeMemory( pstInfo[i] );
#endif
			pstInfo[i] = NULL;
		}
	}

	for( int32_t i = 0; i < NX_MEMORY_NUM; i++ )
	{
		if( pstData[i] )
		{
			free( pstData[i] );
			pstData[i] = NULL;
		}
	}
}

//------------------------------------------------------------------------------
static int32_t VerifyMemory()
{
	for( int32_t i = 0; i < NX_MEMORY_NUM; i++ )
	{
#if NX_MEMORY_TYPE
		int32_t iOffset  = 0;
		for( int32_t j = 0; j < pstInfo[i]->planes; j++ )
		{
			uint8_t *pBuffer = (uint8_t*)pstInfo[i]->pBuffer[j];
			DumpHex( pBuffer, 16, ">> Video Memory( mem: %d, plane: %d ) \n", i, j );

			for( int32_t k = 0; k < pstInfo[i]->size[j]; k++ )
			{
				if( pBuffer[j] != (pstData[i][j+iOffset]))
				{
					printf("Fail, Verify data.\n");
					return -1;
				}
			}

			iOffset += pstInfo[i]->size[j];
		}
#else
		uint8_t *pBuffer = (uint8_t*)pstInfo[i]->pBuffer;
		DumpHex( pBuffer, 16, ">> Memory( mem: %d ) \n", i );

		for( int32_t j = 0; j < pstInfo[i]->size; j++ )
		{
			if( pBuffer[j] != pstData[i][j] )
			{
				printf("Fail, Verify data.\n");
				return -1;
			}
		}
#endif
	}
	return 0;
}

//------------------------------------------------------------------------------
int32_t VpuAllocMain( CODEC_APP_DATA *pAppData )
{
	int32_t iLoopCount = (0 >= pAppData->iMaxLimitFrame) ? 1 : pAppData->iMaxLimitFrame;
	register_signal();

	for( int32_t loop = 0; loop < iLoopCount; loop++ )
	{
		printf(">>> Loop: %d\n", loop + 1);
		if( 0 > AllocateMemory() )
		{
			printf("Fail, AllocateMemory().\n");
			goto ERROR;
		}

		if( 0 > VerifyMemory() )
		{
			printf("Fail, VerifyMemory().\n");
			goto ERROR;
		}

ERROR:
		FreeMemory();
	}

	return 0;
}

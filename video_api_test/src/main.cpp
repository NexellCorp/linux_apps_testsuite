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

#include <stdio.h>		// printf
#include <unistd.h>		// getopt & optarg
#include <stdlib.h>		// atoi
#include <string.h>		// strdup
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>

#include <Util.h>

//------------------------------------------------------------------------------
extern int32_t VpuDecStrmMain(CODEC_APP_DATA *pAppData);
extern int32_t VpuDecMain(CODEC_APP_DATA *pAppData);
extern int32_t VpuEncMain(CODEC_APP_DATA *pAppData);
extern int32_t VpuAllocMain(CODEC_APP_DATA *pAppData);

#define MAX_FILE_LIST		256
#define MAX_PATH_SIZE		1024

#define NX_ENABLE_STRM_FILE	false

bool bExitLoop = false;

enum {
	MODE_NONE,
	DECODER_MODE,
	ENCODER_MODE,
	ALLOC_MODE,
	DEC_MULTI_INST_MODE,
	ENC_MULTI_INST_MODE,
	MODE_MAX
};

//------------------------------------------------------------------------------
void print_usage(const char *appName)
{
	printf(
		"Usage : %s [options] -i [input file], [M] = mandatory, [O] = Optional \n"
		"  common options :\n"
		"     -m [mode]                  [O]  : 1:decoder mode, 2:encoder mode (def:decoder mode)\n"
		"     -i [input file name]       [M]  : input media file name (When is camera encoder, the value set NULL\n"
		"     -o [output file name]      [O]  : output file name\n"
		"     -r [repeat count]          [O]  : application repeat num. ( 0: unlimited repeat )\n"
		"     -t [instance number]       [O]  : decoder/encoder instance num.\n"
		"     -h : help\n"
		" -------------------------------------------------------------------------------------------------------------------\n"
		"  only decoder options :\n"
		"     -j [seek frame],[position] [O]  : seek start frame, seek potition(sec)\n"
		"     -d [frame num],[file name] [O]  : dump frame number, dump file name\n"
		"     -l [frame num]             [O]  : max limitation frame\n"
		" -------------------------------------------------------------------------------------------------------------------\n"
		"  only encoder options :\n"
		"     -c [codec]                 [O]  : 0:H.264, 1:Mp4v, 2:H.263, 3:JPEG (def:H.264)\n"
		"     -s [width],[height]        [M]  : input image's size\n"
		"     -f [fps Num],[fps Den]     [O]  : input image's framerate(def:30/1) \n"
		"     -b [Kbitrate]              [M]  : target Kilo bitrate (0:VBR mode, other:CBR mode)\n"
		"     -g [gop size]              [O]  : gop size (def:framerate) \n"
		"     -q [quality or QP]         [O]  : Jpeg Quality or Other codec Quantization Parameter(When is VBR, it is valid) \n"
		"     -v [VBV]                   [O]  : VBV Size (def:2Sec)\n"
		"     -x [Max Qp]                [O]  : Maximum Qp \n"
		" -------------------------------------------------------------------------------------------------------------------\n"
		"  only allocator options :\n"
		"     -l [loop]                  [O]  : loop test\n"
		" ===================================================================================================================\n\n"
		,appName);
	printf(
		"Examples\n");
	printf(
		" Decoder Mode :\n"
		"     #> %s -i [input filename]\n", appName);
	printf(
		" Decoder Mode & Capture :\n"
		"     #> %s -i [input filename] -d [num],[dump filename] \n", appName);
	printf(
		" Encoder Camera Mode :\n"
		"     #> %s -m 2 -o [output filename]\n", appName);
	printf(
		" Encoder File Mode :(H.264, 1920x1080, 10Mbps, 30fps, 30 gop)\n"
		"     #> %s -m 2 -i [input filename] -o [output filename] -s 1920,1080 -f 30,1 -b 10000 -g 30 \n", appName);
}

//------------------------------------------------------------------------------
static int32_t IsRegularFile( char *pFile )
{
	struct stat statinfo;
	if( 0 > stat( pFile, &statinfo) )
		return 0;

	return S_ISREG( statinfo.st_mode );
}

//------------------------------------------------------------------------------
static int32_t IsDirectory( char *pFile )
{
	struct stat statinfo;
	if( 0 > stat( pFile, &statinfo) )
		return 0;

	return S_ISDIR( statinfo.st_mode );
}

//------------------------------------------------------------------------------
static int32_t IsVideo( char *pFile )
{
	const char *pVidExtension[] = {
		"avi",	"wmv",	"asf",	"mpg",	"mpeg",	"mpv2",	"mp2v",	"ts",	"tp",	"vob",
		"ogg",	"ogm",	"ogv",	"mp4",	"m4v",	"m4p",	"3gp",	"3gpp",	"mkv",	"rm",
		"rmvb",	"flv",	"m2ts",	"m2t",	"divx",	"webm",
	};

	char *pExtension = pFile + strlen(pFile) - 1;
	while( pFile != pExtension )
	{
		if( *pExtension == '.' ) {
			pExtension++;
			break;
		}
		pExtension--;
	}

	if( pFile != pExtension )
	{
		for( int32_t i = 0; i < (int32_t)(sizeof(pVidExtension) / sizeof(pVidExtension[0])); i++ )
		{
			if( !strcasecmp( pExtension, pVidExtension[i] ) )
				return true;
		}
	}

	return false;
}

//------------------------------------------------------------------------------
static int32_t MakeFileList( char *pDirectory, char *pFileList[MAX_FILE_LIST], int32_t *iFileNum )
{
	DIR *pDir = NULL;
	struct dirent *pDirent;

	if( NULL == (pDir = opendir( pDirectory )) )
	{
		return -1;
	}

	while( NULL != (pDirent = readdir(pDir)) )
	{
		if( !strcmp( pDirent->d_name, "." ) ||
			!strcmp( pDirent->d_name, ".." ) )
			continue;

		char szFile[MAX_PATH_SIZE];
		sprintf( szFile, "%s/%s", pDirectory, pDirent->d_name );

		if( IsDirectory(szFile) )
		{
			MakeFileList( szFile, pFileList, iFileNum );
		}
		else if( IsRegularFile(szFile) )
		{
			if (MAX_FILE_LIST <= *iFileNum)
			{
				printf("Warn, List Array Limitation. ( %s )\n", szFile);
				continue;
			}
			else
			{
				if( IsVideo(szFile) )
				{
					pFileList[*iFileNum] = strdup(szFile);
					*iFileNum = *iFileNum + 1;
				}
			}
		}
	}

	closedir( pDir );
	return 0;
}

//------------------------------------------------------------------------------
static void FreeFileList( char *pFileList[MAX_FILE_LIST], int32_t iFileNum )
{
	for( int32_t i = 0; i < iFileNum; i++ )
	{
		if (pFileList[i]) free( pFileList[i] );
	}
}

//------------------------------------------------------------------------------
static void* ThreadDecProc(void *pObj)
{
	CODEC_APP_DATA *pData = (CODEC_APP_DATA*)pObj;
	VpuDecMain(pData);
	return (void*)0xDEADDEAD;
}

//------------------------------------------------------------------------------
static void* ThreadEncProc(void *pObj)
{
	CODEC_APP_DATA *pData = (CODEC_APP_DATA*)pObj;
	VpuEncMain(pData);
	return (void*)0xDEADDEAD;
}

//------------------------------------------------------------------------------
static STREAM_INFO* GetStreamInfo( const char *pFile )
{
	struct stat statinfo;
	if( 0 > stat( pFile, &statinfo) )
		return NULL;

	STREAM_INFO* pStrmInfo = NULL;
	FILE *hFile = fopen( pFile, "r" );

	if( hFile )
	{
		pStrmInfo = (STREAM_INFO*)malloc( sizeof(STREAM_INFO) );
		pStrmInfo->iSize = statinfo.st_size;
		pStrmInfo->pBuf  = (char*)malloc( pStrmInfo->iSize );

		fread( pStrmInfo->pBuf, 1, pStrmInfo->iSize, hFile );
		fclose( hFile );
	}

	return pStrmInfo;
}

//------------------------------------------------------------------------------
static void FreeStreamInfo( STREAM_INFO *pStrmInfo )
{
	if( pStrmInfo )
	{
		if( pStrmInfo->pBuf )
		{
			free( pStrmInfo->pBuf );
			pStrmInfo->pBuf = NULL;
		}
		free( pStrmInfo );
		pStrmInfo = NULL;
	}
}

//------------------------------------------------------------------------------
#include <linux/videodev2.h>

static STREAM_INFO* pStrmInfo[] = {
	/* Sequence Data */
	GetStreamInfo("/mnt/dump/dump_0000.h264"),	/* 0 */
	/* Stream Data */
	GetStreamInfo("/mnt/dump/dump_0021.h264"),	/* 1 */
	GetStreamInfo("/mnt/dump/dump_0022.h264"),	/* 2 */
	GetStreamInfo("/mnt/dump/dump_0023.h264"),	/* 3 */
	GetStreamInfo("/mnt/dump/dump_0024.h264"),	/* 4 */
	GetStreamInfo("/mnt/dump/dump_0025.h264"),	/* 5 */
	GetStreamInfo("/mnt/dump/dump_0026.h264"),	/* 6 */
	GetStreamInfo("/mnt/dump/dump_0027.h264"),	/* 7 */
	GetStreamInfo("/mnt/dump/dump_0028.h264"),	/* 8 */
	GetStreamInfo("/mnt/dump/dump_0029.h264"),	/* 9 */
	GetStreamInfo("/mnt/dump/dump_0030.h264"),	/* 10 */
	GetStreamInfo("/mnt/dump/dump_0031.h264"),	/* 11 */
	GetStreamInfo("/mnt/dump/dump_0032.h264"),	/* 12 */
	GetStreamInfo("/mnt/dump/dump_0033.h264"),	/* 13 */
	GetStreamInfo("/mnt/dump/dump_0034.h264"),	/* 14 */
	GetStreamInfo("/mnt/dump/dump_0035.h264"),	/* 15 */
	GetStreamInfo("/mnt/dump/dump_0036.h264"),	/* 16 */
	GetStreamInfo("/mnt/dump/dump_0037.h264"),	/* 17 */
	GetStreamInfo("/mnt/dump/dump_0038.h264"),	/* 18 */
	GetStreamInfo("/mnt/dump/dump_0039.h264"),	/* 19 */
	GetStreamInfo("/mnt/dump/dump_0040.h264"),	/* 20 */
	GetStreamInfo("/mnt/dump/dump_0041.h264"),	/* 21 */
	GetStreamInfo("/mnt/dump/dump_0042.h264"),	/* 22 */
	GetStreamInfo("/mnt/dump/dump_0043.h264"),	/* 23 */
	GetStreamInfo("/mnt/dump/dump_0044.h264"),	/* 24 */
	GetStreamInfo("/mnt/dump/dump_0045.h264"),	/* 25 */
	GetStreamInfo("/mnt/dump/dump_0046.h264"),	/* 26 */
	GetStreamInfo("/mnt/dump/dump_0047.h264"),	/* 27 */
	GetStreamInfo("/mnt/dump/dump_0048.h264"),	/* 28 */
	GetStreamInfo("/mnt/dump/dump_0049.h264"),	/* 29 */
	GetStreamInfo("/mnt/dump/dump_0050.h264"),	/* 30 */
	GetStreamInfo("/mnt/dump/dump_0051.h264"),	/* 31 */
	GetStreamInfo("/mnt/dump/dump_0052.h264"),	/* 32 */
	GetStreamInfo("/mnt/dump/dump_0053.h264"),	/* 33 */
	GetStreamInfo("/mnt/dump/dump_0054.h264"),	/* 34 */
	GetStreamInfo("/mnt/dump/dump_0055.h264"),	/* 35 */
	GetStreamInfo("/mnt/dump/dump_0056.h264"),	/* 36 */
	GetStreamInfo("/mnt/dump/dump_0056.h264"),	/* 37 */
	GetStreamInfo("/mnt/dump/dump_0057.h264"),	/* 38 */
	GetStreamInfo("/mnt/dump/dump_0058.h264"),	/* 39 */
	GetStreamInfo("/mnt/dump/dump_0059.h264"),	/* 40 */
	GetStreamInfo("/mnt/dump/dump_0060.h264"),	/* 41 */
	GetStreamInfo("/mnt/dump/dump_0061.h264"),	/* 42 */
	GetStreamInfo("/mnt/dump/dump_0062.h264"),	/* 43 */
	GetStreamInfo("/mnt/dump/dump_0063.h264"),	/* 44 */
	GetStreamInfo("/mnt/dump/dump_0064.h264"),	/* 45 */
	GetStreamInfo("/mnt/dump/dump_0065.h264"),	/* 46 */
	GetStreamInfo("/mnt/dump/dump_0066.h264"),	/* 47 */
	GetStreamInfo("/mnt/dump/dump_0067.h264"),	/* 48 */
	GetStreamInfo("/mnt/dump/dump_0068.h264"),	/* 49 */
};

static STREAM_FILE gstInStreamFile = {
	1920,				/* width */
	1088,				/* height */
	V4L2_PIX_FMT_H264,	/* Codec Type */
	&pStrmInfo[0],		/* Stream Info */
	sizeof(pStrmInfo) / sizeof(pStrmInfo[0])
};

//------------------------------------------------------------------------------
int32_t main(int32_t argc, char *argv[])
{
	int32_t iRet = 0;
	int32_t opt;
	int32_t mode = DECODER_MODE;
	uint32_t iRepeat = 1, iCount = 0;
	uint32_t iInstance = 1;
	char szTemp[1024];

	CODEC_APP_DATA appData;
	memset(&appData, 0, sizeof(CODEC_APP_DATA));

#if NX_ENABLE_STRM_FILE
	appData.pInStreamFile = &gstInStreamFile;
#endif

	while (-1 != (opt = getopt(argc, argv, "m:i:o:hc:d:s:f:b:g:q:v:x:j:l:r:t:")))
	{
		switch (opt)
		{
		case 'm':
			mode = atoi(optarg);
			if( MODE_NONE >= mode && MODE_MAX <= mode )
			{
				printf("Error : invalid mode ( %d:decoder mode, %d:encoder mode, %d: allocator, %d: multiple instance )!!!\n",
					DECODER_MODE, ENCODER_MODE, ALLOC_MODE );
				return -1;
			}
			break;
		case 'i':
			appData.inFileName  = ( IsRegularFile(optarg) && !IsDirectory(optarg)) ? strdup(optarg)         : NULL;
			appData.inDirectory = (!IsRegularFile(optarg) &&  IsDirectory(optarg)) ? realpath(optarg, NULL) : NULL;
			break;
		case 'o':	appData.outFileName = strdup(optarg);  break;
		case 'h':	print_usage(argv[0]);  return 0;
		case 'c':	appData.codec = atoi(optarg);  break;
		case 'd':	sscanf(optarg, "%d,%s", &appData.dumpFrameNumber, szTemp); appData.dumpFileName = strdup(szTemp); break;
		case 's':	sscanf(optarg, "%d,%d", &appData.width, &appData.height); break;
		case 'f':	sscanf( optarg, "%d,%d", &appData.fpsNum, &appData.fpsDen );  break;
		case 'b':	appData.kbitrate = atoi(optarg);  break;
		case 'g':	appData.gop = atoi(optarg);  break;
		case 'q':	appData.qp = atoi(optarg);  break;		/* JPEG Quality or Quantization Parameter */
		case 'v':	appData.vbv = atoi(optarg);  break;
		case 'x':	appData.maxQp = atoi(optarg);  break;
		case 'l':	appData.iMaxLimitFrame = atoi(optarg);  break;
		case 'j':	sscanf(optarg, "%d,%d", &appData.iSeekStartFrame, &appData.iSeekPos);  break;
		case 'r':	sscanf(optarg, "%u", &iRepeat);
		case 't':	sscanf(optarg, "%u", &iInstance);
		default:	break;
		}
	}

	do {
		switch (mode)
		{
		case DECODER_MODE:
			if( appData.pInStreamFile )
			{
				iRet = VpuDecStrmMain(&appData);
			}
			else if( appData.inFileName )
			{
				iRet = VpuDecMain(&appData);
			}
			else if( appData.inDirectory )
			{
				char *pFileList[MAX_FILE_LIST];
				int32_t iFileNum = 0;
				MakeFileList( appData.inDirectory, pFileList, &iFileNum );
				for( int32_t i = 0; i < iFileNum; i++ )
				{
					appData.inFileName = pFileList[i];
					VpuDecMain(&appData);
				}
				FreeFileList( pFileList, iFileNum );
				appData.inFileName = NULL;
			}
			break;
		case ENCODER_MODE:
			iRet = VpuEncMain(&appData);
			break;
		case ALLOC_MODE:
			iRet = VpuAllocMain(&appData);
			break;
		case DEC_MULTI_INST_MODE:
		case ENC_MULTI_INST_MODE:
			for( int32_t i = 0; i < iInstance; i++ )
			{
				pthread_t hThread;
				pthread_attr_t hAttr;

				pthread_attr_init( &hAttr );
				pthread_attr_setdetachstate( &hAttr, PTHREAD_CREATE_DETACHED );
				if( 0 > pthread_create(&hThread, &hAttr,
					(mode == DEC_MULTI_INST_MODE) ? ThreadDecProc : ThreadEncProc, (void*)&appData) )
				{
					printf("Fail, pthread_create().\n");
					exit(EXIT_FAILURE);
				}
				pthread_attr_destroy( &hAttr );
				usleep(100000);
			}

			while(!bExitLoop)
			{
				usleep(100000);
			}
			break;
		default:
			break;
		}
	} while(++iCount != iRepeat);

	if( appData.pInStreamFile )
	{
		for( int32_t i = 0; i < appData.pInStreamFile->iStrmNum; i++ )
		{
			FreeStreamInfo(appData.pInStreamFile->ppStrmInfo[i]);
		}
	}

	if( appData.inFileName )	free( appData.inFileName );
	if( appData.inDirectory )	free( appData.inDirectory );
	if( appData.outFileName )	free( appData.outFileName );

	return iRet;
}

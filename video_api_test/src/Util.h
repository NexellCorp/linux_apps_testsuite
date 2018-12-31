#ifndef __UTIL_h__
#define __UTIL_h__

#include <stdint.h>

/* */
typedef struct STREAM_INFO {
	char*	pBuf;
	int32_t iSize;
} STREAM_INFO;

typedef struct STREAM_FILE {
	int32_t iWidth;
	int32_t iHeight;
	uint32_t iCodecType;

	STREAM_INFO** ppStrmInfo;
	int32_t iStrmNum;
} STREAM_FILE;

/* Application Data */
typedef struct CODEC_APP_DATA {
	/* Input Options */
	char *inFileName;			/* Input File Name */

	/* */
	STREAM_FILE* pInStreamFile;

	/* for Decoder Only Options */
	char *inDirectory;			/* Input Directory */
	int32_t iSeekStartFrame;	/* Seek Start Frame */
	int32_t iSeekPos;			/* Seek Position */
	int32_t iMaxLimitFrame;		/* Max Limitation Frame */

	/* for Encoder Only Options */
	int32_t width;				/* Input YUV Image Width */
	int32_t height;				/* Input YUV Image Height */
	int32_t fpsNum;				/* Input Image Fps Number */
	int32_t fpsDen;				/* Input Image Fps Density */
	int32_t kbitrate;			/* Kilo Bitrate */
	int32_t gop;				/* GoP */
	uint32_t codec;				/* 0:H.264, 1:Mp4v, 2:H.263, 3:JPEG (def:H.264) */
	int32_t qp;					/* Fixed Qp */
	int32_t vbv;
	int32_t maxQp;

	/* Output Options */
	char *outFileName;			/* Output File Name */

	/* Dump Options */
	uint32_t dumpFrameNumber;	/* Dump Frame Number */
	char *dumpFileName;			/* Dump File Name */
} CODEC_APP_DATA;

uint64_t NX_GetTickCount( void );
void NX_DumpData( void *data, int32_t len, const char *pFormat, ... );
void NX_DumpStream( uint8_t *pStrmBuf, int32_t iStrmSize, const char *pFormat, ... );
void NX_DumpStream( uint8_t *pStrmBuf, int32_t iStrmSize, FILE *pFile );

#endif // __UTIL_h__

#include <stdio.h>
#include <stdarg.h>

#include <sys/time.h>

#include "Util.h"

//------------------------------------------------------------------------------
uint64_t NX_GetTickCount( void )
{
	uint64_t ret;
	struct timeval	tv;
	struct timezone	zv;
	gettimeofday( &tv, &zv );
	ret = ((uint64_t)tv.tv_sec)*1000 + tv.tv_usec/1000;
	return ret;
}

//------------------------------------------------------------------------------
void NX_DumpData( void *data, int32_t len, const char *pFormat, ... )
{
	va_list args;
	va_start(args, pFormat);
	vprintf(pFormat, args);
	va_end(args);

	int32_t i=0;
	uint8_t *byte = (uint8_t *)data;

	if( data == NULL || len == 0 )
		return;

	for( i=0 ; i<len ; i ++ )
	{
		if( i!=0 && i%16 == 0 )	printf("\n\t");
		printf("%.2x", byte[i] );
		if( i%4 == 3 ) printf(" ");
	}
	printf("\n");
}

//------------------------------------------------------------------------------
void NX_DumpStream( uint8_t *pStrmBuf, int32_t iStrmSize, const char *pFormat, ... )
{
	char szFile[1024] = {0x00, };

	va_list args;
	va_start(args, pFormat);
	vsnprintf(szFile, sizeof(szFile), pFormat, args);
	va_end(args);

	FILE *pFile = fopen( szFile, "wb" );
	if( pFile )
	{
		if( pStrmBuf )	fwrite( pStrmBuf, 1, iStrmSize, pFile );
		fclose( pFile );
	}
}

//------------------------------------------------------------------------------
void NX_DumpStream( uint8_t *pStrmBuf, int32_t iStrmSize, FILE *pFile )
{
	if( pFile && pStrmBuf )
	{
		fwrite( pStrmBuf, 1, iStrmSize, pFile );
	}
}

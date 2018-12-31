FFMPEG_INC	= ${top_builddir}/src/ffmpeg/ffmpeg-2.8.6-arm32/include
FFMPEG_LIB	= ${top_builddir}/src/ffmpeg/ffmpeg-2.8.6-arm32/lib

AM_CFLAGS =

AM_CPPFLAGS = \
	$(WARN_CFLAGS)	\
	-I.				\
	-I${includedir}	\
	-I${includedir}/libdrm	\
	-I${top_builddir}/src/include	\
	-I$(FFMPEG_INC)

video_api_test_LDADD = \
	-lpthread			\
	-L$(FFMPEG_LIB)		\
	-lavcodec			\
	-lavformat			\
	-lavutil			\
	-lswresample		\
	-L${libdir}			\
	-ldrm				\
	-lnx_video_api

video_api_test_SOURCES = \
	CodecInfo.cpp		\
	DrmRender.cpp		\
	MediaExtractor.cpp	\
	NX_Queue.cpp		\
	NX_Semaphore.cpp	\
	Util.cpp		\
	VideoAllocTest.cpp	\
	VideoDecTest.cpp	\
	VideoEncTest.cpp	\
	NX_V4l2Utils.cpp	\
	main.cpp

bin_PROGRAMS = video_api_test

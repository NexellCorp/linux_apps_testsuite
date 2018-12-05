if TARGET_IS_64BIT
FFMPEG_INC	= ${top_builddir}/src/ffmpeg/ffmpeg-2.8.6-arm64/include
FFMPEG_LIB	= ${top_builddir}/src/ffmpeg/ffmpeg-2.8.6-arm64/lib
else
FFMPEG_INC	= ${top_builddir}/src/ffmpeg/ffmpeg-2.8.6-arm32/include
FFMPEG_LIB	= ${top_builddir}/src/ffmpeg/ffmpeg-2.8.6-arm32/lib
endif

AM_CFLAGS =

AM_CPPFLAGS = \
	$(WARN_CFLAGS)	\
	-I.				\
	-I${includedir}	\
	-I${includedir}/libdrm	\
	-I${top_builddir}/src/include

#AM_CPPFLAGS += -I$(FFMPEG_INC)

video_api_test_LDADD =	\
	-L${libdir}			\
	-ldrm				\
	-lnx_video_api		\
	-lnx_drm_allocator	\
	-lnx_v4l2			\
	-lavcodec			\
	-lavformat			\
	-lavutil			\
	-lswresample
#video_api_test_LDADD += -L$(FFMPEG_LIB)

video_api_test_SOURCES = \
	CodecInfo.cpp		\
	DrmRender.cpp		\
	MediaExtractor.cpp	\
	NX_CV4l2Camera.cpp	\
	NX_V4l2Utils.cpp	\
	NX_Queue.cpp		\
	NX_Semaphore.cpp	\
	Util.cpp			\
	VideoDecTest.cpp	\
	VideoEncTest.cpp	\
	main.cpp

bin_PROGRAMS = video_api_test

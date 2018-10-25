DIR :=
DIR += allocator_test
DIR += camera_test
DIR += dp_cam_test
DIR += scaler_test
DIR += scaler_test_file
DIR += cam_test
DIR += camera_test_onedevice
DIR += dp_cam_test_onedevice
DIR += mpegts_test
DIR += uac_test

CROSS_COMPILE ?= aarch64-linux-gnu-

all:
	@for dir in $(DIR); do	\
	make CROSS_COMPILE=$(CROSS_COMPILE) -C $$dir || exit $?;	\
	make -C $$dir install;	\
	done

clean:
	@for dir in $(DIR); do	\
	make -C $$dir clean || exit $?;	\
	done

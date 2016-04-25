# Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.

makefile: all
all: MatrixMul

SDKINC=../../include/

HSADIR=/opt/rocm/hsa/
HSAINC=$(HSADIR)include/
HSALIB=$(HSADIR)lib/

LIBLINE=-L$(HSALIB) -l:libhsa-runtime64.so.1

CC=g++

TESTCOMMON=../Common
CFLAGS= -g -D_DEBUG -std=c++11 -m64 -Werror -I$(HSAINC) -I$(TESTCOMMON) -I$(SDKINC)
LDFLAGS= -g -m64 -Werror -Wl,--unresolved-symbols=ignore-in-shared-libs

OBJFLAGS = -c $(CFLAGS)

SOURCES=\
	$(TESTCOMMON)/HSAResourceManager.cpp\
	$(TESTCOMMON)/HSAExtensionFinalizer.cpp\
	MatrixMul.cpp

OBJECTS=$(SOURCES:.cpp=.o)

DEPS := $(OBJECTS:.o=.d)

MatrixMul : $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS)  $(LIBLINE) -o MatrixMul

.cpp.o:
	$(CC) -c -MMD $(CFLAGS) $< -o $@

clean:
	rm -f $(TESTCOMMON)/*.o $(TESTCOMMON)/*.d
	rm -f *.o *.d
	rm -f MatrixMul

-include $(DEPS)

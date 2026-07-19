SHELL      := bash.exe
.SHELLFLAGS := -ec

CC_WIN = x86_64-w64-mingw32-gcc
CXX_WIN = x86_64-w64-mingw32-g++

CFLAGS_WIN = -O2 -Iinclude -I.
CXXFLAGS_WIN = -O2 -Iinclude -I.
LDFLAGS_WIN = -shared -static -s -ld3d11 -lshell32 tools\libGMDXhook.def -Wl,--enable-stdcall-fixup

SRC_DIR := src
EXT_DIR := external/bc7enc_rdo

RES_SOURCE := tools\resources.rc
RES_FILE   := tools\resources.res

TEXTURE_MODE ?= 1
ALLOW_CACHING ?= 1

OBJ_DIR := build_temp_mode$(TEXTURE_MODE)_cache$(ALLOW_CACHING)
OUT_NAME := gmdxh_mode$(TEXTURE_MODE)_cache$(ALLOW_CACHING).dll

rwildcard = $(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))

SRCS := $(call rwildcard,$(SRC_DIR)/,*.c)
OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS))

ifeq ($(TEXTURE_MODE),1)
EXT_SRCS := $(EXT_DIR)/bc7enc.cpp src/libs/bc7_wrapper.cpp
EXT_OBJS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(EXT_SRCS))
else
EXT_SRCS :=
EXT_OBJS :=
endif

CFLAGS_WIN += -DTEXTURE_MODE=$(TEXTURE_MODE) -DALLOW_CACHING=$(ALLOW_CACHING)
CXXFLAGS_WIN += -DTEXTURE_MODE=$(TEXTURE_MODE) -DALLOW_CACHING=$(ALLOW_CACHING)

MAKEFLAGS += -j32

.PHONY: all clean full mode1 mode2 mode1-nocache mode1-cache mode2-nocache mode2-cache

all: compile

full: mode1-cache mode1-nocache mode2

mode1: mode1-cache

mode1-cache:
	$(MAKE) TEXTURE_MODE=1 ALLOW_CACHING=1 compile

mode1-nocache:
	$(MAKE) TEXTURE_MODE=1 ALLOW_CACHING=0 compile

mode2:
	$(MAKE) TEXTURE_MODE=2 ALLOW_CACHING=0 compile

compile: $(OBJS) $(EXT_OBJS) $(RES_FILE)
	$(CXX_WIN) $(CFLAGS_WIN) -o $(OUT_NAME) $(OBJS) $(EXT_OBJS) $(RES_FILE) $(LDFLAGS_WIN)

$(RES_FILE): $(RES_SOURCE)
	windres $(RES_SOURCE) -O coff -o $(RES_FILE)

$(OBJ_DIR)/%.o: %.c
	@if not exist "$(dir $@)" mkdir "$(dir $@)"
	$(CC_WIN) $(CFLAGS_WIN) -c $< -o $@

$(OBJ_DIR)/%.o: %.cpp
	@if not exist "$(dir $@)" mkdir "$(dir $@)"
	$(CXX_WIN) $(CXXFLAGS_WIN) -c $< -o $@

clean:
	del "gmdxh_mode*.dll"
	del "$(RES_FILE)"
	rmdir "build_temp_mode1_cache1" /s /q
	rmdir "build_temp_mode1_cache0" /s /q
	rmdir "build_temp_mode2_cache0" /s /q
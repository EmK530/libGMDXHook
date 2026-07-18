SHELL      := bash.exe
.SHELLFLAGS := -ec

CC_WIN = x86_64-w64-mingw32-gcc
CXX_WIN = x86_64-w64-mingw32-g++

CFLAGS_WIN = -O2 -Iinclude -I.
CXXFLAGS_WIN = -O2 -Iinclude -I.
LDFLAGS_WIN = -shared -static -s -ld3d11 tools\libGMDXhook.def -Wl,--enable-stdcall-fixup

OBJ_DIR := build_temp
RES_SOURCE := tools\resources.rc
RES_FILE   := tools\resources.res
SRC_DIR := src
EXT_DIR := external/bc7enc_rdo

OUT_NAME := gmdxh.dll

rwildcard = $(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))
SRCS := $(call rwildcard,$(SRC_DIR)/,*.c)

EXT_SRCS := $(EXT_DIR)/bc7enc.cpp src/libs/bc7_wrapper.cpp

OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS))
EXT_OBJS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(EXT_SRCS))

MAKEFLAGS += -j32

.PHONY: all clean

all: compile

compile: $(OBJS) $(EXT_OBJS) $(RES_FILE)
	$(CXX_WIN) $(CFLAGS_WIN) -o $(OUT_NAME) $(OBJS) $(EXT_OBJS) $(RES_FILE) $(LDFLAGS_WIN)
	copy $(OUT_NAME) "C:/Users/EmK530/Desktop/minVS/$(OUT_NAME)"

$(RES_FILE): $(RES_SOURCE)
	windres $(RES_SOURCE) -O coff -o $(RES_FILE)

$(OBJ_DIR)/%.o: %.c
	@if not exist "$(dir $@)" mkdir "$(dir $@)"
	$(CC_WIN) $(CFLAGS_WIN) -c $< -o $@

$(OBJ_DIR)/%.o: %.cpp
	@if not exist "$(dir $@)" mkdir "$(dir $@)"
	$(CXX_WIN) $(CXXFLAGS_WIN) -c $< -o $@

clean:
	del "$(OUT_NAME)"
	del "$(RES_FILE)"
	rmdir "$(OBJ_DIR)" /s /q
SHELL      := C:\msys64\mingw64\bin\bash.exe
.SHELLFLAGS := -ec

CC_WIN = x86_64-w64-mingw32-gcc
CXX_WIN = x86_64-w64-mingw32-g++

CFLAGS_WIN = -O2 -Iinclude -I. -Wl,--gc-sections -ffunction-sections -fdata-sections
CXXFLAGS_WIN = -O2 -Iinclude -I. -Wl,--gc-sections -ffunction-sections -fdata-sections
LDFLAGS_WIN = -shared -static -s -ld3d11 -lshell32 tools\vsD3DHook.def -Wl,--enable-stdcall-fixup

SRC_DIR := src
EXT_DIR := external

RES_SOURCE := tools\resources.rc
RES_FILE   := tools\resources.res

OBJ_DIR := build_temp
OUT_NAME := vsD3DHook.dll

rwildcard = $(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))

C_SRCS := $(call rwildcard,$(SRC_DIR)/,*.c)
CPP_SRCS := $(call rwildcard,$(SRC_DIR)/,*.cpp)

OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(C_SRCS)) \
        $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(CPP_SRCS))

EXT_SRCS := $(EXT_DIR)/bc7enc_rdo/bc7enc.cpp $(EXT_DIR)/cJSON/cJSON.c #src/libs/bc7_wrapper.cpp
EXT_OBJS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(EXT_SRCS))

MAKEFLAGS += -j32

.PHONY: all clean full compile

all: compile

compile: $(OBJS) $(EXT_OBJS) $(RES_FILE)
	$(CXX_WIN) $(CFLAGS_WIN) -o $(OUT_NAME) $(OBJS) $(EXT_OBJS) $(RES_FILE) $(LDFLAGS_WIN)
	copy "$(OUT_NAME)" "C:\Users\EmK530\Desktop\vs_memopt\vsD3DHook.x"

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
	rmdir /s /q "$(OBJ_DIR)"
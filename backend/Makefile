CC = gcc
CFLAGS = -std=gnu11 -Wall -Wextra -O2 -g
CFLAGS += -Isrc -Ivendor
LDFLAGS = -lpthread -ldl -lrt -lm
CFLAGS += -Wno-implicit-function-declaration

# ---- NVML detection (header from system, library from ldconfig) ----
NVML_INC_PATH :=
NVML_LDFLAGS :=
GPU_FLAGS :=

# 1. Find nvml.h on the system (not a vendor stub)
# Search CUDA toolkit paths first, then /usr/include
_NVML_CUDA := $(wildcard /usr/local/cuda-*/targets/x86_64-linux/include/nvml.h)
_NVML_CUDA += $(wildcard /usr/local/cuda/include/nvml.h)
_NVML_CUDA += $(wildcard /usr/local/cuda/targets/x86_64-linux/include/nvml.h)
_NVML_SYS  := $(wildcard /usr/include/nvml.h)

_NVML_FOUND := $(firstword $(_NVML_CUDA) $(_NVML_SYS))

ifneq ($(_NVML_FOUND),)
  # 2. Find the NVML library.
  # Prefer libnvidia-ml.so (unversioned symlink), but fall back to the
  # versioned .so.1 file via its absolute path (handles missing -dev package).
  _NVML_LIB_SO_PATH := $(shell ldconfig -p 2>/dev/null | \
    grep -oP '/.*/libnvidia-ml\.so\b' | head -1)
  _NVML_LIB_SO1_PATH := $(shell ldconfig -p 2>/dev/null | \
    grep -oP '/.*/libnvidia-ml\.so\.\d+' | head -1)
  ifneq ($(_NVML_LIB_SO_PATH),)
    NVML_LDFLAGS := -lnvidia-ml
  else ifneq ($(_NVML_LIB_SO1_PATH),)
    NVML_LDFLAGS := $(_NVML_LIB_SO1_PATH)
  endif

  ifneq ($(NVML_LDFLAGS),)
    NVML_INC_PATH := $(dir $(_NVML_FOUND))
    GPU_FLAGS := -DHAS_NVML -I$(NVML_INC_PATH)
  endif
endif

ifneq ($(GPU_FLAGS),)
  $(info [NVML] OK: $(NVML_INC_PATH) -> $(NVML_LDFLAGS))
else
  ifneq ($(_NVML_FOUND),)
    $(warning [NVML] nvml.h found but libnvidia-ml not in ldconfig — GPU disabled)
  else
    $(info [NVML] nvml.h not found on system — GPU monitoring disabled)
    $(info [NVML] Install CUDA or nvidia-ml-dev, then rebuild)
  endif
endif

# ---- GPU flag change detection ----
# Make does not track compiler flags as file dependencies. When GPU_FLAGS
# changes, stale .o files would be silently reused. We solve this with a
# sentinel file: $(GPU_FLAG_FILE) is a dependency of every .o file, and its
# timestamp only updates when the actual flags change.
GPU_FLAG_FILE := .gpu_flags

$(GPU_FLAG_FILE):
	@if [ ! -f $@ ] || [ "$$(cat $@)" != "$(GPU_FLAGS)" ]; then \
		echo '$(GPU_FLAGS)' > $@; \
	fi

# ---- Sources ----
SRCS = src/main.c \
       src/platform.c \
       src/config.c \
       src/http_server.c \
       src/api_handlers.c \
       src/sampler.c \
       src/snapshot.c \
       src/process_probe.c \
       src/gpu_probe_nvml.c \
       src/json_writer.c \
       src/auth_stub.c \
       src/ops_stub.c \
       vendor/mongoose.c \
       vendor/yyjson.c \
       vendor/toml.c

OBJS = $(SRCS:.c=.o)
TARGET = monitor-backend

.PHONY: all clean run

all: $(TARGET)

%.o: %.c $(GPU_FLAG_FILE)
	$(CC) $(CFLAGS) $(GPU_FLAGS) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(GPU_FLAGS) -o $@ $^ $(LDFLAGS) $(NVML_LDFLAGS)

clean:
	rm -f $(OBJS) $(TARGET) $(GPU_FLAG_FILE)

run: $(TARGET)
	./$(TARGET) --config config/monitor.conf

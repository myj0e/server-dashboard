CC = gcc
CFLAGS = -std=gnu11 -Wall -Wextra -O2 -g
CFLAGS += -Isrc -Ivendor
LDFLAGS = -lpthread -ldl -lrt -lm
# Mongoose uses alloca; suppress the warning
CFLAGS += -Wno-implicit-function-declaration

# NVML: check for header before enabling GPU support
NVML_H = vendor/nvml.h
NVML_LDFLAGS =
GPU_FLAGS =

ifneq ($(wildcard $(NVML_H)),)
  NVML_LDFLAGS = -lnvidia-ml
  GPU_FLAGS = -DHAS_NVML
else
  $(info Note: nvml.h not found in vendor/ - GPU monitoring will be disabled.)
  $(info Run: wget https://raw.githubusercontent.com/NVIDIA/gpu-monitoring-tools/master/nvml.h -O $(NVML_H))
  $(info Or set enable_gpu=false in config if GPU monitoring is not needed.)
endif

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

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(GPU_FLAGS) -o $@ $^ $(LDFLAGS) $(NVML_LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(GPU_FLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET) --config config/monitor.conf

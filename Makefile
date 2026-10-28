CC := $(CROSS_COMPILE)gcc
#CXX := $(CROSS_COMPILE)g++
AR := $(CROSS_COMPILE)ar
LD := $(CROSS_COMPILE)gcc

ifeq ($(MARIO_VM),)
MARIO_VM = .
endif

include $(MARIO_VM)/lang/js/lang.mk

mario_OBJS = $(MARIO_VM)/mario/mario.o $(MARIO_VM)/mario/lex/mario_lex.o $(MARIO_VM)/mario/bcdump/bcdump.o \
	$(MARIO_VM)/mario/jit/mario_jit.o $(MARIO_VM)/mario/jit/mario_jit_mem.o $(MARIO_VM)/mario/jit/mario_jit_arm64.o
mvm_OBJS = bin/mario/mario.o bin/lib/mbc.o bin/lib/js.o bin/lib/host_task.o 

MARIO_OBJS = $(mario_OBJS) $(mvm_OBJS) $(lang_OBJS) \
		$(NATIVE_OBJS)

ifeq ($(MARIO_DEBUG),yes)
CFLAGS += -g -DMARIO_DEBUG
#CXXFLAGS += -g -DMARIO_DEBUG
mvm_OBJS += bin/lib/mem_debug.o
else
CFLAGS += -O2
#CXXFLAGS += -O2
endif


HEADS = -I$(NATIVES_PATH_BUILTIN) \
	-I$(NATIVES_PATH) \
	-I$(MARIO_VM)/mario \
	-I$(MARIO_VM)/bin/lib \
	-I$(MARIO_VM)/platform

CFLAGS += $(HEADS)
#CXXFLAGS += $(HEADS)

CFLAGS += -Wno-incompatible-function-pointer-types
#CXXFLAGS += -Wno-incompatible-function-pointer-types

MARIO = build/mario

all: $(MARIO)
	@echo "done"


$(MARIO): $(MARIO_OBJS)
	mkdir -p build
	$(LD) -o $(MARIO) $(MARIO_OBJS) $(LDFLAGS)

clean:
	rm -f $(MARIO_OBJS) $(MARIO)

# core-js bundle harness (see test/corejs/). The bundle is fetched on demand and
# gitignored; corejs-check loads prelude + bundle and reports via a sentinel.
corejs-fetch:
	@test/corejs/fetch.sh

corejs-check: $(MARIO)
	@test/corejs/fetch.sh
	@test/corejs/run.sh

.PHONY: all clean corejs-fetch corejs-check

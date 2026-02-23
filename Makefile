CC := $(CROSS_COMPILE)gcc
#CXX := $(CROSS_COMPILE)g++
AR := $(CROSS_COMPILE)ar
LD := $(CROSS_COMPILE)gcc

ifeq ($(MARIO_VM),)
MARIO_VM = .
endif

include $(MARIO_VM)/lang/js/lang.mk

mario_OBJS = $(MARIO_VM)/mario/mario.o $(MARIO_VM)/mario/lex/mario_lex.o $(MARIO_VM)/mario/bcdump/bcdump.o
mvm_OBJS = bin/mario/mario.o bin/lib/mbc.o bin/lib/js.o 

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


HEADS = -I$(NATIVE_PATH_BUILTIN) \
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

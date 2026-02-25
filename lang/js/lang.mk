MARIO_LANG = js
lang_OBJS = $(MARIO_VM)/lang/$(MARIO_LANG)/compiler.o 

NATIVE_PATH = $(MARIO_VM)/lang/$(MARIO_LANG)/native
NATIVES_PATH_BUILTIN = $(NATIVE_PATH)/builtin
NATIVES_PATH = $(NATIVE_PATH)/natives

NATIVE_OBJS= \
	$(NATIVE_PATH)/natives_all.o  \
	\
	$(NATIVES_PATH_BUILTIN)/natives_builtin.o  \
	$(NATIVES_PATH_BUILTIN)/Object/native_Object.o  \
	$(NATIVES_PATH_BUILTIN)/Error/native_Error.o  \
	$(NATIVES_PATH_BUILTIN)/Number/native_Number.o  \
	$(NATIVES_PATH_BUILTIN)/Console/native_Console.o \
	$(NATIVES_PATH_BUILTIN)/String/native_String.o \
	$(NATIVES_PATH_BUILTIN)/Array/native_Array.o \
	$(NATIVES_PATH_BUILTIN)/Promise/native_Promise.o \
	\
	$(NATIVES_PATH)/natives.o  \
	$(NATIVES_PATH)/JSON/JSON.o \
	$(NATIVES_PATH)/JSON/native_JSON.o \
	$(NATIVES_PATH)/Date/native_Date.o \
	#$(NATIVES_PATH)/Math/native_Math.o

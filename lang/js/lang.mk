MARIO_LANG = js
lang_OBJS = $(MARIO_VM)/lang/$(MARIO_LANG)/compiler.o 

NATIVE_PATH = $(MARIO_VM)/lang/$(MARIO_LANG)/native
NATIVE_PATH_BUILTIN = $(MARIO_VM)/lang/$(MARIO_LANG)/native/builtin
NATIVE_PATH = $(MARIO_VM)/lang/$(MARIO_LANG)/native/natives

NATIVE_OBJS= \
	$(NATIVE_PATH_BUILTIN)/natives_builtin.o  \
	$(NATIVE_PATH_BUILTIN)/Object/native_Object.o  \
	$(NATIVE_PATH_BUILTIN)/Error/native_Error.o  \
	$(NATIVE_PATH_BUILTIN)/Number/native_Number.o  \
	$(NATIVE_PATH_BUILTIN)/Console/native_Console.o \
	$(NATIVE_PATH_BUILTIN)/String/native_String.o \
	$(NATIVE_PATH_BUILTIN)/Array/native_Array.o \
	$(NATIVE_PATH_BUILTIN)/Promise/native_Promise.o \
	\
	$(NATIVE_PATH)/natives.o  \
	$(NATIVE_PATH)/JSON/JSON.o \
	$(NATIVE_PATH)/JSON/native_JSON.o \
	$(NATIVE_PATH)/Date/native_Date.o \
	#$(NATIVE_PATH)/Math/native_Math.o
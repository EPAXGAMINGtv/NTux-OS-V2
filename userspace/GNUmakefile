ARCH ?= x86_64
CC := cc
LD := ld
AR := ar

# Pick the first available C++ compiler. Users can still override via `make CXX=...`.
CXX_CANDIDATES := $(if $(CXX),$(CXX)) g++ clang++ c++
CXX := $(firstword $(foreach c,$(CXX_CANDIDATES),$(if $(shell command -v $(c) 2>/dev/null),$(c))))
HAS_CXX := $(if $(CXX),1,0)

CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -ffreestanding -fno-stack-protector -fno-pie -m64 -nostdlib -nostdinc
CXXFLAGS := -std=gnu++17 -O2 -g -Wall -Wextra -ffreestanding -fno-stack-protector -fno-pie -m64 -nostdlib -nostdinc -fno-exceptions -fno-rtti -fno-threadsafe-statics
CPPFLAGS := -I libc/include -I libcpp/include -I include -I src/doom/doomgeneric
CPPFLAGS_TCC := $(CPPFLAGS) -I src/tinycc -I src/tinycc/include
LDFLAGS := -m elf_x86_64 -nostdlib -static -T linker.ld

BASE_DESKTOP := 0x00160000
BASE_HELLO := 0x03000000
BASE_INSTALLER := 0x05800000
BASE_INSTALLER_TERM := 0x06800000
BASE_TERMINAL := 0x08000000
BASE_DESKDEMO := 0x0A800000
BASE_LOGIN := 0x0D000000
BASE_DOOM := 0x0F800000
BASE_HELLOKONSOLE := 0x12000000
BASE_CPPHELLO := 0x14800000
BASE_EPAXFETCH := 0x1A800000
BASE_TETRIS := 0x1D800000
BASE_LUA := 0x20000000

BASE_DESKCONSOLE := 0x26000000
BASE_EXPLORER := 0x29000000
BASE_EDITOR := 0x2C000000
BASE_BENCH := 0x2F000000
BASE_IMGVIEW := 0x32000000
BASE_OBJVIEW := 0x35000000
BASE_FLAPPY := 0x3B000000
BASE_XEYES := 0x3E000000
BASE_SETTINGS := 0x41000000
BASE_PARTUTIL := 0x44000000
BASE_TEST := 0x47000000
BASE_SNAKE := 0x4D000000
BASE_HEALTHCHECK := 0x50000000
BASE_TCC := 0x53000000
BASE_BROWSER := 0x56000000
BASE_PAINT := 0x59000000
BASE_SCREENSAVER := 0x5C000000
BASE_CALC := 0x5F000000

LDFLAGS_DESKTOP := $(LDFLAGS) --defsym=USER_BASE=$(BASE_DESKTOP)
LDFLAGS_HELLO := $(LDFLAGS) --defsym=USER_BASE=$(BASE_HELLO)
LDFLAGS_INSTALLER := $(LDFLAGS) --defsym=USER_BASE=$(BASE_INSTALLER)
LDFLAGS_INSTALLER_TERM := $(LDFLAGS) --defsym=USER_BASE=$(BASE_INSTALLER_TERM)
LDFLAGS_TERMINAL := $(LDFLAGS) --defsym=USER_BASE=$(BASE_TERMINAL)
LDFLAGS_DESKDEMO := $(LDFLAGS) --defsym=USER_BASE=$(BASE_DESKDEMO)
LDFLAGS_LOGIN := $(LDFLAGS) --defsym=USER_BASE=$(BASE_LOGIN)
LDFLAGS_DOOM := $(LDFLAGS) --defsym=USER_BASE=$(BASE_DOOM)
LDFLAGS_HELLOKONSOLE := $(LDFLAGS) --defsym=USER_BASE=$(BASE_HELLOKONSOLE)
LDFLAGS_CPPHELLO := $(LDFLAGS) --defsym=USER_BASE=$(BASE_CPPHELLO)
LDFLAGS_EPAXFETCH := $(LDFLAGS) --defsym=USER_BASE=$(BASE_EPAXFETCH)
LDFLAGS_TETRIS := $(LDFLAGS) --defsym=USER_BASE=$(BASE_TETRIS)
LDFLAGS_LUA := $(LDFLAGS) --defsym=USER_BASE=$(BASE_LUA)

LDFLAGS_DESKCONSOLE := $(LDFLAGS) --defsym=USER_BASE=$(BASE_DESKCONSOLE)
LDFLAGS_EXPLORER := $(LDFLAGS) --defsym=USER_BASE=$(BASE_EXPLORER)
LDFLAGS_EDITOR := $(LDFLAGS) --defsym=USER_BASE=$(BASE_EDITOR)
LDFLAGS_BENCH := $(LDFLAGS) --defsym=USER_BASE=$(BASE_BENCH)
LDFLAGS_IMGVIEW := $(LDFLAGS) --defsym=USER_BASE=$(BASE_IMGVIEW)
LDFLAGS_OBJVIEW := $(LDFLAGS) --defsym=USER_BASE=$(BASE_OBJVIEW)
LDFLAGS_FLAPPY := $(LDFLAGS) --defsym=USER_BASE=$(BASE_FLAPPY)
LDFLAGS_XEYES := $(LDFLAGS) --defsym=USER_BASE=$(BASE_XEYES)
LDFLAGS_SETTINGS := $(LDFLAGS) --defsym=USER_BASE=$(BASE_SETTINGS)
LDFLAGS_PARTUTIL := $(LDFLAGS) --defsym=USER_BASE=$(BASE_PARTUTIL)
LDFLAGS_TEST := $(LDFLAGS) --defsym=USER_BASE=$(BASE_TEST)
LDFLAGS_SNAKE := $(LDFLAGS) --defsym=USER_BASE=$(BASE_SNAKE)
LDFLAGS_HEALTHCHECK := $(LDFLAGS) --defsym=USER_BASE=$(BASE_HEALTHCHECK)
LDFLAGS_TCC := $(LDFLAGS) --defsym=USER_BASE=$(BASE_TCC)
LDFLAGS_BROWSER := $(LDFLAGS) --defsym=USER_BASE=$(BASE_BROWSER)
LDFLAGS_PAINT := $(LDFLAGS) --defsym=USER_BASE=$(BASE_PAINT)
LDFLAGS_SCREENSAVER := $(LDFLAGS) --defsym=USER_BASE=$(BASE_SCREENSAVER)
LDFLAGS_CALC := $(LDFLAGS) --defsym=USER_BASE=$(BASE_CALC)

OUT_DIR := bin
OBJ_DIR := obj

LIBC_SRC := $(shell find libc -name '*.c' | LC_ALL=C sort)
WIN32_SRC :=
LIBCPP_SRC := $(if $(HAS_CXX),$(shell find libcpp -name '*.cpp' | LC_ALL=C sort))
RUNTIME_SRC := $(LIBC_SRC)
CPP_RUNTIME_SRC := $(RUNTIME_SRC) $(LIBCPP_SRC)

HELLO_SRC := src/konsole/konsole.c
INSTALLER_SRC := src/installer/installer.c
INSTALLER_TERM_SRC := src/installer/installer_term.c
DESKTOP_SRC := src/desktop/main.c src/desktop/desktop.c src/desktop/window.c src/desktop/cursor.c src/desktop/api.c src/desktop/taskbar.c
DESKDEMO_SRC := src/deskdemo/deskdemo.c
SCREENSAVER_SRC := src/screensaver/screensaver.c
TERMINAL_SRC := src/terminal/terminal.c
LOGIN_SRC := src/login/login.c
HELLOKONSOLE_SRC := src/hellokonsole/hellokonsole.c
CPPHELLO_SRC := src/cpphello/cpphello.cpp
EPAXFETCH_SRC := src/epaxfetch/epaxfetch.c
TETRIS_SRC := src/tetris/tetris.c
TASKMGR_SRC := src/taskmanager/taskmgr_main.c src/taskmanager/taskmgr_ui.c src/taskmanager/taskmgr_stats.c
DESKCONSOLE_SRC := src/deskconsole/deskconsole.c
EXPLORER_SRC := src/explorer/explorer.c
EDITOR_SRC := src/editor/editor.c
BENCH_SRC := src/bench/bench.c
IMGVIEW_SRC := src/imgview/imgview.c
OBJVIEW_SRC := src/objview/objview.c
FLAPPY_SRC := src/flappy/flappy.c
XEYES_SRC := src/xeyes/xeyes.c
SETTINGS_SRC := src/settings/settings_main.c src/settings/settings_ui.c src/settings/settings_store.c src/settings/settings_data.c
PARTUTIL_SRC := src/partutil/partutil_main.c src/partutil/partutil_ui.c src/partutil/partutil_ops.c
TEST_SRC := src/test/test.c
SNAKE_SRC := src/snake/snake.c
HEALTHCHECK_SRC := src/healthcheck/healthcheck.c
LUA_APP_SRC := src/lua/lua_desk.c
TCC_SRC := src/tinycc/tcc_desk.c
BROWSER_SRC := src/browser/browser.c
PAINT_SRC := src/paint/paint.c
CALC_SRC := src/calc/calc.c



LUA_DIR := src/lua
LUA_CORE_SRC := \
	$(LUA_DIR)/lapi.c \
	$(LUA_DIR)/lcode.c \
	$(LUA_DIR)/lctype.c \
	$(LUA_DIR)/ldebug.c \
	$(LUA_DIR)/ldo.c \
	$(LUA_DIR)/ldump.c \
	$(LUA_DIR)/lfunc.c \
	$(LUA_DIR)/lgc.c \
	$(LUA_DIR)/llex.c \
	$(LUA_DIR)/lmem.c \
	$(LUA_DIR)/lobject.c \
	$(LUA_DIR)/lopcodes.c \
	$(LUA_DIR)/lparser.c \
	$(LUA_DIR)/lstate.c \
	$(LUA_DIR)/lstring.c \
	$(LUA_DIR)/ltable.c \
	$(LUA_DIR)/ltm.c \
	$(LUA_DIR)/lundump.c \
	$(LUA_DIR)/lvm.c \
	$(LUA_DIR)/lzio.c

LUA_LIB_SRC := \
	$(LUA_DIR)/lauxlib.c \
	$(LUA_DIR)/lbaselib.c \
	$(LUA_DIR)/lcorolib.c \
	$(LUA_DIR)/lstrlib.c \
	$(LUA_DIR)/ltablib.c \
	$(LUA_DIR)/lutf8lib.c \
	$(LUA_DIR)/linit.c \
	$(LUA_DIR)/ntuxlib.c

DOOM_DIR := src/doom/doomgeneric/doomgeneric
DOOM_SRC := \
	$(DOOM_DIR)/dummy.c \
	$(DOOM_DIR)/am_map.c \
	$(DOOM_DIR)/doomdef.c \
	$(DOOM_DIR)/doomstat.c \
	$(DOOM_DIR)/dstrings.c \
	$(DOOM_DIR)/d_event.c \
	$(DOOM_DIR)/d_items.c \
	$(DOOM_DIR)/d_iwad.c \
	$(DOOM_DIR)/d_loop.c \
	$(DOOM_DIR)/d_main.c \
	$(DOOM_DIR)/d_mode.c \
	$(DOOM_DIR)/d_net.c \
	$(DOOM_DIR)/f_finale.c \
	$(DOOM_DIR)/f_wipe.c \
	$(DOOM_DIR)/g_game.c \
	$(DOOM_DIR)/hu_lib.c \
	$(DOOM_DIR)/hu_stuff.c \
	$(DOOM_DIR)/info.c \
	$(DOOM_DIR)/i_cdmus.c \
	$(DOOM_DIR)/i_endoom.c \
	$(DOOM_DIR)/i_joystick.c \
	$(DOOM_DIR)/i_scale.c \
	$(DOOM_DIR)/i_sound.c \
	$(DOOM_DIR)/i_system.c \
	$(DOOM_DIR)/i_timer.c \
	$(DOOM_DIR)/memio.c \
	$(DOOM_DIR)/m_argv.c \
	$(DOOM_DIR)/m_bbox.c \
	$(DOOM_DIR)/m_cheat.c \
	$(DOOM_DIR)/m_config.c \
	$(DOOM_DIR)/m_controls.c \
	$(DOOM_DIR)/m_fixed.c \
	$(DOOM_DIR)/m_menu.c \
	$(DOOM_DIR)/m_misc.c \
	$(DOOM_DIR)/m_random.c \
	$(DOOM_DIR)/p_ceilng.c \
	$(DOOM_DIR)/p_doors.c \
	$(DOOM_DIR)/p_enemy.c \
	$(DOOM_DIR)/p_floor.c \
	$(DOOM_DIR)/p_inter.c \
	$(DOOM_DIR)/p_lights.c \
	$(DOOM_DIR)/p_map.c \
	$(DOOM_DIR)/p_maputl.c \
	$(DOOM_DIR)/p_mobj.c \
	$(DOOM_DIR)/p_plats.c \
	$(DOOM_DIR)/p_pspr.c \
	$(DOOM_DIR)/p_saveg.c \
	$(DOOM_DIR)/p_setup.c \
	$(DOOM_DIR)/p_sight.c \
	$(DOOM_DIR)/p_spec.c \
	$(DOOM_DIR)/p_switch.c \
	$(DOOM_DIR)/p_telept.c \
	$(DOOM_DIR)/p_tick.c \
	$(DOOM_DIR)/p_user.c \
	$(DOOM_DIR)/r_bsp.c \
	$(DOOM_DIR)/r_data.c \
	$(DOOM_DIR)/r_draw.c \
	$(DOOM_DIR)/r_main.c \
	$(DOOM_DIR)/r_plane.c \
	$(DOOM_DIR)/r_segs.c \
	$(DOOM_DIR)/r_sky.c \
	$(DOOM_DIR)/r_things.c \
	$(DOOM_DIR)/sha1.c \
	$(DOOM_DIR)/sounds.c \
	$(DOOM_DIR)/statdump.c \
	$(DOOM_DIR)/st_lib.c \
	$(DOOM_DIR)/st_stuff.c \
	$(DOOM_DIR)/s_sound.c \
	$(DOOM_DIR)/tables.c \
	$(DOOM_DIR)/v_video.c \
	$(DOOM_DIR)/wi_stuff.c \
	$(DOOM_DIR)/w_checksum.c \
	$(DOOM_DIR)/w_file.c \
	$(DOOM_DIR)/w_main.c \
	$(DOOM_DIR)/w_wad.c \
	$(DOOM_DIR)/z_zone.c \
	$(DOOM_DIR)/w_file_stdc.c \
	$(DOOM_DIR)/i_input.c \
	$(DOOM_DIR)/i_video.c \
	$(DOOM_DIR)/doomgeneric.c \
	$(DOOM_DIR)/doomgeneric_ntux.c

HELLO_ALL_SRC := $(RUNTIME_SRC) $(HELLO_SRC)
INSTALLER_ALL_SRC := $(RUNTIME_SRC) $(INSTALLER_SRC)
INSTALLER_TERM_ALL_SRC := $(RUNTIME_SRC) $(INSTALLER_TERM_SRC)
DESKTOP_ALL_SRC := $(RUNTIME_SRC) $(DESKTOP_SRC)
DESKDEMO_ALL_SRC := $(RUNTIME_SRC) $(DESKDEMO_SRC)
SCREENSAVER_ALL_SRC := $(RUNTIME_SRC) $(SCREENSAVER_SRC)
TERMINAL_ALL_SRC := $(RUNTIME_SRC) $(TERMINAL_SRC)
LOGIN_ALL_SRC := $(RUNTIME_SRC) $(LOGIN_SRC)
DOOM_ALL_SRC := $(RUNTIME_SRC) $(DOOM_SRC)
HELLOKONSOLE_ALL_SRC := $(RUNTIME_SRC) $(HELLOKONSOLE_SRC)
CPPHELLO_ALL_SRC := $(CPP_RUNTIME_SRC) $(CPPHELLO_SRC)
EPAXFETCH_ALL_SRC := $(RUNTIME_SRC) $(EPAXFETCH_SRC)
TETRIS_ALL_SRC := $(RUNTIME_SRC) $(TETRIS_SRC)
TASKMGR_ALL_SRC := $(RUNTIME_SRC) $(TASKMGR_SRC)
DESKCONSOLE_ALL_SRC := $(RUNTIME_SRC) $(DESKCONSOLE_SRC)
EXPLORER_ALL_SRC := $(RUNTIME_SRC) $(EXPLORER_SRC)
EDITOR_ALL_SRC := $(RUNTIME_SRC) $(EDITOR_SRC)
BENCH_ALL_SRC := $(RUNTIME_SRC) $(BENCH_SRC)
IMGVIEW_ALL_SRC := $(RUNTIME_SRC) $(IMGVIEW_SRC)
OBJVIEW_ALL_SRC := $(RUNTIME_SRC) $(OBJVIEW_SRC)
FLAPPY_ALL_SRC := $(RUNTIME_SRC) $(FLAPPY_SRC)
XEYES_ALL_SRC := $(RUNTIME_SRC) $(XEYES_SRC)
SETTINGS_ALL_SRC := $(RUNTIME_SRC) $(SETTINGS_SRC)
PARTUTIL_ALL_SRC := $(RUNTIME_SRC) $(PARTUTIL_SRC)
TEST_ALL_SRC := $(RUNTIME_SRC) $(TEST_SRC)
SNAKE_ALL_SRC := $(RUNTIME_SRC) $(SNAKE_SRC)
HEALTHCHECK_ALL_SRC := $(RUNTIME_SRC) $(HEALTHCHECK_SRC)
LUA_ALL_SRC := $(RUNTIME_SRC) $(LUA_CORE_SRC) $(LUA_LIB_SRC) $(LUA_APP_SRC)
TCC_ALL_SRC := $(RUNTIME_SRC) $(TCC_SRC)
BROWSER_ALL_SRC := $(RUNTIME_SRC) $(BROWSER_SRC)
PAINT_ALL_SRC := $(RUNTIME_SRC) $(PAINT_SRC)
CALC_ALL_SRC := $(RUNTIME_SRC) $(CALC_SRC)

HELLO_OBJ := $(patsubst %.c,$(OBJ_DIR)/hello/%.o,$(filter %.c,$(HELLO_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/hello/%.o,$(filter %.cpp,$(HELLO_ALL_SRC)))
INSTALLER_OBJ := $(patsubst %.c,$(OBJ_DIR)/installer/%.o,$(filter %.c,$(INSTALLER_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/installer/%.o,$(filter %.cpp,$(INSTALLER_ALL_SRC)))
INSTALLER_TERM_OBJ := $(patsubst %.c,$(OBJ_DIR)/installer_term/%.o,$(filter %.c,$(INSTALLER_TERM_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/installer_term/%.o,$(filter %.cpp,$(INSTALLER_TERM_ALL_SRC)))
DESKTOP_OBJ := $(patsubst %.c,$(OBJ_DIR)/desktop/%.o,$(filter %.c,$(DESKTOP_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/desktop/%.o,$(filter %.cpp,$(DESKTOP_ALL_SRC)))
DESKDEMO_OBJ := $(patsubst %.c,$(OBJ_DIR)/deskdemo/%.o,$(filter %.c,$(DESKDEMO_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/deskdemo/%.o,$(filter %.cpp,$(DESKDEMO_ALL_SRC)))
SCREENSAVER_OBJ := $(patsubst %.c,$(OBJ_DIR)/screensaver/%.o,$(filter %.c,$(SCREENSAVER_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/screensaver/%.o,$(filter %.cpp,$(SCREENSAVER_ALL_SRC)))
TERMINAL_OBJ := $(patsubst %.c,$(OBJ_DIR)/terminal/%.o,$(filter %.c,$(TERMINAL_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/terminal/%.o,$(filter %.cpp,$(TERMINAL_ALL_SRC)))
LOGIN_OBJ := $(patsubst %.c,$(OBJ_DIR)/login/%.o,$(filter %.c,$(LOGIN_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/login/%.o,$(filter %.cpp,$(LOGIN_ALL_SRC)))
DOOM_OBJ := $(patsubst %.c,$(OBJ_DIR)/doom/%.o,$(filter %.c,$(DOOM_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/doom/%.o,$(filter %.cpp,$(DOOM_ALL_SRC)))
HELLOKONSOLE_OBJ := $(patsubst %.c,$(OBJ_DIR)/hellokonsole/%.o,$(filter %.c,$(HELLOKONSOLE_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/hellokonsole/%.o,$(filter %.cpp,$(HELLOKONSOLE_ALL_SRC)))
CPPHELLO_OBJ := $(patsubst %.c,$(OBJ_DIR)/cpphello/%.o,$(filter %.c,$(CPPHELLO_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/cpphello/%.o,$(filter %.cpp,$(CPPHELLO_ALL_SRC)))
EPAXFETCH_OBJ := $(patsubst %.c,$(OBJ_DIR)/epaxfetch/%.o,$(filter %.c,$(EPAXFETCH_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/epaxfetch/%.o,$(filter %.cpp,$(EPAXFETCH_ALL_SRC)))
TETRIS_OBJ := $(patsubst %.c,$(OBJ_DIR)/tetris/%.o,$(filter %.c,$(TETRIS_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/tetris/%.o,$(filter %.cpp,$(TETRIS_ALL_SRC)))
TASKMGR_OBJ := $(patsubst %.c,$(OBJ_DIR)/taskmgr/%.o,$(filter %.c,$(TASKMGR_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/taskmgr/%.o,$(filter %.cpp,$(TASKMGR_ALL_SRC)))
DESKCONSOLE_OBJ := $(patsubst %.c,$(OBJ_DIR)/deskconsole/%.o,$(filter %.c,$(DESKCONSOLE_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/deskconsole/%.o,$(filter %.cpp,$(DESKCONSOLE_ALL_SRC)))
EXPLORER_OBJ := $(patsubst %.c,$(OBJ_DIR)/explorer/%.o,$(filter %.c,$(EXPLORER_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/explorer/%.o,$(filter %.cpp,$(EXPLORER_ALL_SRC)))
EDITOR_OBJ := $(patsubst %.c,$(OBJ_DIR)/editor/%.o,$(filter %.c,$(EDITOR_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/editor/%.o,$(filter %.cpp,$(EDITOR_ALL_SRC)))
BENCH_OBJ := $(patsubst %.c,$(OBJ_DIR)/bench/%.o,$(filter %.c,$(BENCH_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/bench/%.o,$(filter %.cpp,$(BENCH_ALL_SRC)))
IMGVIEW_OBJ := $(patsubst %.c,$(OBJ_DIR)/imgview/%.o,$(filter %.c,$(IMGVIEW_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/imgview/%.o,$(filter %.cpp,$(IMGVIEW_ALL_SRC)))
OBJVIEW_OBJ := $(patsubst %.c,$(OBJ_DIR)/objview/%.o,$(filter %.c,$(OBJVIEW_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/objview/%.o,$(filter %.cpp,$(OBJVIEW_ALL_SRC)))
FLAPPY_OBJ := $(patsubst %.c,$(OBJ_DIR)/flappy/%.o,$(filter %.c,$(FLAPPY_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/flappy/%.o,$(filter %.cpp,$(FLAPPY_ALL_SRC)))
XEYES_OBJ := $(patsubst %.c,$(OBJ_DIR)/xeyes/%.o,$(filter %.c,$(XEYES_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/xeyes/%.o,$(filter %.cpp,$(XEYES_ALL_SRC)))
SETTINGS_OBJ := $(patsubst %.c,$(OBJ_DIR)/settings/%.o,$(filter %.c,$(SETTINGS_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/settings/%.o,$(filter %.cpp,$(SETTINGS_ALL_SRC)))
PARTUTIL_OBJ := $(patsubst %.c,$(OBJ_DIR)/partutil/%.o,$(filter %.c,$(PARTUTIL_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/partutil/%.o,$(filter %.cpp,$(PARTUTIL_ALL_SRC)))
TEST_OBJ := $(patsubst %.c,$(OBJ_DIR)/test/%.o,$(filter %.c,$(TEST_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/test/%.o,$(filter %.cpp,$(TEST_ALL_SRC)))
SNAKE_OBJ := $(patsubst %.c,$(OBJ_DIR)/snake/%.o,$(filter %.c,$(SNAKE_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/snake/%.o,$(filter %.cpp,$(SNAKE_ALL_SRC)))
HEALTHCHECK_OBJ := $(patsubst %.c,$(OBJ_DIR)/healthcheck/%.o,$(filter %.c,$(HEALTHCHECK_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/healthcheck/%.o,$(filter %.cpp,$(HEALTHCHECK_ALL_SRC)))
LUA_OBJ := $(patsubst %.c,$(OBJ_DIR)/lua/%.o,$(filter %.c,$(LUA_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/lua/%.o,$(filter %.cpp,$(LUA_ALL_SRC)))
TCC_OBJ := $(patsubst %.c,$(OBJ_DIR)/tcc/%.o,$(filter %.c,$(TCC_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/tcc/%.o,$(filter %.cpp,$(TCC_ALL_SRC)))
BROWSER_OBJ := $(patsubst %.c,$(OBJ_DIR)/browser/%.o,$(filter %.c,$(BROWSER_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/browser/%.o,$(filter %.cpp,$(BROWSER_ALL_SRC)))
PAINT_OBJ := $(patsubst %.c,$(OBJ_DIR)/paint/%.o,$(filter %.c,$(PAINT_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/paint/%.o,$(filter %.cpp,$(PAINT_ALL_SRC)))
CALC_OBJ := $(patsubst %.c,$(OBJ_DIR)/calc/%.o,$(filter %.c,$(CALC_ALL_SRC))) $(patsubst %.cpp,$(OBJ_DIR)/calc/%.o,$(filter %.cpp,$(CALC_ALL_SRC)))

TCC_OUT_DIR := $(OUT_DIR)/tcc
TCC_LIB_DIR := $(TCC_OUT_DIR)/lib
TCC_INC_DIR := $(TCC_OUT_DIR)/include

TCC_LIBC_SRC := $(filter-out libc/crt0.c,$(LIBC_SRC))
TCC_LIBC_OBJ := $(patsubst %.c,$(OBJ_DIR)/tcc_libc/%.o,$(TCC_LIBC_SRC))
TCC_LIBTCC1_OBJ := $(OBJ_DIR)/tcc_support/libtcc1.o
TCC_CRT1_OBJ := $(TCC_LIB_DIR)/crt1.o
TCC_CRTI_OBJ := $(TCC_LIB_DIR)/crti.o
TCC_CRTN_OBJ := $(TCC_LIB_DIR)/crtn.o
TCC_LIBC_A := $(TCC_LIB_DIR)/libc.a
TCC_LIBTCC1_A := $(TCC_LIB_DIR)/libtcc1.a
TCC_INCLUDE_STAMP := $(TCC_INC_DIR)/.stamp

.PHONY: all clean

ALL_TARGETS := $(OUT_DIR)/hello.elf $(OUT_DIR)/konsole.elf $(OUT_DIR)/hellokonsole.elf $(OUT_DIR)/installer.elf $(OUT_DIR)/installer_term.elf $(OUT_DIR)/desktop.elf $(OUT_DIR)/terminal.elf $(OUT_DIR)/deskdemo.elf $(OUT_DIR)/screensaver.elf $(OUT_DIR)/login.elf $(OUT_DIR)/doom.elf $(OUT_DIR)/epaxfetch.elf $(OUT_DIR)/tetris.elf $(OUT_DIR)/taskmgr.elf $(OUT_DIR)/deskconsole.elf $(OUT_DIR)/explorer.elf $(OUT_DIR)/editor.elf $(OUT_DIR)/bench.elf $(OUT_DIR)/imgview.elf $(OUT_DIR)/objview.elf $(OUT_DIR)/flappy.elf $(OUT_DIR)/xeyes.elf $(OUT_DIR)/settings.elf $(OUT_DIR)/partutil.elf $(OUT_DIR)/test.elf $(OUT_DIR)/snake.elf $(OUT_DIR)/healthcheck.elf $(OUT_DIR)/lua.elf $(OUT_DIR)/tcc.elf $(OUT_DIR)/browser.elf $(OUT_DIR)/paint.elf $(OUT_DIR)/calc.elf $(TCC_LIBC_A) $(TCC_LIBTCC1_A) $(TCC_CRT1_OBJ) $(TCC_CRTI_OBJ) $(TCC_CRTN_OBJ) $(TCC_INCLUDE_STAMP)
ifeq ($(HAS_CXX),1)
ALL_TARGETS += $(OUT_DIR)/cpphello.elf
endif

all: $(ALL_TARGETS)

$(OUT_DIR)/hello.elf: $(HELLO_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_HELLO) $(HELLO_OBJ) -o $@

$(OUT_DIR)/konsole.elf: $(OUT_DIR)/hello.elf
	@mkdir -p $(OUT_DIR)
	cp -f $(OUT_DIR)/hello.elf $(OUT_DIR)/konsole.elf

$(OUT_DIR)/installer.elf: $(INSTALLER_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_INSTALLER) $(INSTALLER_OBJ) -o $@

$(OUT_DIR)/installer_term.elf: $(INSTALLER_TERM_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_INSTALLER_TERM) $(INSTALLER_TERM_OBJ) -o $@

$(OUT_DIR)/desktop.elf: $(DESKTOP_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_DESKTOP) $(DESKTOP_OBJ) -o $@

$(OUT_DIR)/deskdemo.elf: $(DESKDEMO_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_DESKDEMO) $(DESKDEMO_OBJ) -o $@

$(OUT_DIR)/screensaver.elf: $(SCREENSAVER_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_SCREENSAVER) $(SCREENSAVER_OBJ) -o $@

$(OUT_DIR)/terminal.elf: $(TERMINAL_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_TERMINAL) $(TERMINAL_OBJ) -o $@

$(OUT_DIR)/login.elf: $(LOGIN_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_LOGIN) $(LOGIN_OBJ) -o $@

$(OUT_DIR)/doom.elf: $(DOOM_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_DOOM) $(DOOM_OBJ) -o $@

$(OUT_DIR)/hellokonsole.elf: $(HELLOKONSOLE_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_HELLOKONSOLE) $(HELLOKONSOLE_OBJ) -o $@

$(OUT_DIR)/cpphello.elf: $(CPPHELLO_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_CPPHELLO) $(CPPHELLO_OBJ) -o $@

$(OUT_DIR)/epaxfetch.elf: $(EPAXFETCH_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_EPAXFETCH) $(EPAXFETCH_OBJ) -o $@

$(OUT_DIR)/tetris.elf: $(TETRIS_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_TETRIS) $(TETRIS_OBJ) -o $@

$(OUT_DIR)/taskmgr.elf: $(TASKMGR_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_TETRIS) $(TASKMGR_OBJ) -o $@

$(OUT_DIR)/deskconsole.elf: $(DESKCONSOLE_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_DESKCONSOLE) $(DESKCONSOLE_OBJ) -o $@

$(OUT_DIR)/explorer.elf: $(EXPLORER_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_EXPLORER) $(EXPLORER_OBJ) -o $@

$(OUT_DIR)/editor.elf: $(EDITOR_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_EDITOR) $(EDITOR_OBJ) -o $@

$(OUT_DIR)/bench.elf: $(BENCH_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_BENCH) $(BENCH_OBJ) -o $@

$(OUT_DIR)/imgview.elf: $(IMGVIEW_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_IMGVIEW) $(IMGVIEW_OBJ) -o $@

$(OUT_DIR)/objview.elf: $(OBJVIEW_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_OBJVIEW) $(OBJVIEW_OBJ) -o $@

$(OUT_DIR)/flappy.elf: $(FLAPPY_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_FLAPPY) $(FLAPPY_OBJ) -o $@

$(OUT_DIR)/xeyes.elf: $(XEYES_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_XEYES) $(XEYES_OBJ) -o $@

$(OUT_DIR)/settings.elf: $(SETTINGS_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_SETTINGS) $(SETTINGS_OBJ) -o $@

$(OUT_DIR)/partutil.elf: $(PARTUTIL_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_PARTUTIL) $(PARTUTIL_OBJ) -o $@

$(OUT_DIR)/test.elf: $(TEST_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_TEST) $(TEST_OBJ) -o $@

$(OUT_DIR)/snake.elf: $(SNAKE_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_SNAKE) $(SNAKE_OBJ) -o $@

$(OUT_DIR)/healthcheck.elf: $(HEALTHCHECK_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_HEALTHCHECK) $(HEALTHCHECK_OBJ) -o $@

$(OUT_DIR)/browser.elf: $(BROWSER_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_BROWSER) $(BROWSER_OBJ) -o $@

$(OUT_DIR)/paint.elf: $(PAINT_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_PAINT) $(PAINT_OBJ) -o $@

$(OUT_DIR)/calc.elf: $(CALC_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_CALC) $(CALC_OBJ) -o $@

$(OUT_DIR)/lua.elf: $(LUA_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_LUA) $(LUA_OBJ) -o $@

$(OUT_DIR)/tcc.elf: $(TCC_OBJ) linker.ld
	@mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS_TCC) $(TCC_OBJ) -o $@

$(OBJ_DIR)/hello/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/hello/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/hellokonsole/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/hellokonsole/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/epaxfetch/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/doom/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

DESKTOP_CFLAGS := $(CFLAGS) -fno-omit-frame-pointer -march=x86-64 -mno-mmx

$(OBJ_DIR)/desktop/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(DESKTOP_CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/desktop/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/deskdemo/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/deskdemo/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/screensaver/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/screensaver/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/paint/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/paint/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/calc/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/xeyes/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/settings/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/partutil/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/test/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/snake/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/healthcheck/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/browser/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/browser/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/tcc/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS_TCC) -c $< -o $@

$(OBJ_DIR)/tcc/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS_TCC) -c $< -o $@

$(OBJ_DIR)/tcc_libc/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(TCC_LIBC_A): $(TCC_LIBC_OBJ)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(TCC_LIBTCC1_OBJ): src/tinycc/lib/libtcc1.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS_TCC) -c $< -o $@

$(TCC_LIBTCC1_A): $(TCC_LIBTCC1_OBJ)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(TCC_CRT1_OBJ): libc/crt0.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(TCC_CRTI_OBJ): src/tinycc/ntux/crti.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(TCC_CRTN_OBJ): src/tinycc/ntux/crtn.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(TCC_INCLUDE_STAMP):
	@mkdir -p $(TCC_INC_DIR)
	cp -r src/tinycc/include/. $(TCC_INC_DIR)/
	cp -r libc/include/. $(TCC_INC_DIR)/
	cp -r include/. $(TCC_INC_DIR)/
	@echo "tcc include staged" > $(TCC_INCLUDE_STAMP)

CPPFLAGS_LUA := $(CPPFLAGS) -I src/lua -DLUA_USE_C89 -include stddef.h

$(OBJ_DIR)/tcc/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/lua/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS_LUA) -c $< -o $@

$(OBJ_DIR)/tetris/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/taskmgr/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/deskconsole/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/explorer/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/editor/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/bench/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/imgview/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/flappy/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/objview/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/tetris/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/deskconsole/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/explorer/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/editor/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/terminal/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/terminal/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/login/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/login/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/installer/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/installer/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/installer_term/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/installer_term/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/cpphello/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/cpphello/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/gltest/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/installer/src/installer/installer.o: src/installer/installer.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -O1 -fno-inline -fno-omit-frame-pointer -march=x86-64 -mno-mmx -mno-sse -mno-sse2 -fno-tree-vectorize $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/installer_term/src/installer/installer_term.o: src/installer/installer_term.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -O1 -fno-inline -fno-omit-frame-pointer -march=x86-64 -mno-mmx -mno-sse -mno-sse2 -fno-tree-vectorize $(CPPFLAGS) -c $< -o $@

clean:
	rm -rf $(OUT_DIR) $(OBJ_DIR)

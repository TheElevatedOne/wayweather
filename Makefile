# Wayweather — weather module for Waybar + libwayweather
#
#   make                 binary + libraries   -> dist/
#   make debug           debug + sanitizers   -> build/debug/wayweather
#   make check           offline tests (CLI smoke tests + library API)
#   make install         install to $(PREFIX) (honours DESTDIR)
#   make help            all targets and options

NAME    := wayweather
VERSION ?= $(shell cat VERSION 2>/dev/null || echo 0.0.0)
MAJOR   := $(firstword $(subst ., ,$(VERSION)))

# ---- Layout --------------------------------------------------------------
SRC_DIR   := src
BUILD_DIR := build
DIST_DIR  := dist
DATA_DIR  := data
HEADER    := $(SRC_DIR)/wayweather.h

SRCS := $(wildcard $(SRC_DIR)/*.c)
# The library leaves out the CLI, TUI and daemon (and with them ncurses)
APP_ONLY := main.c tui.c ui.c help.c daemon.c
LIB_SRCS := $(filter-out $(addprefix $(SRC_DIR)/,$(APP_ONLY)),$(SRCS))

REL_OBJ_DIR := $(BUILD_DIR)/release
DBG_OBJ_DIR := $(BUILD_DIR)/debug
REL_OBJS := $(SRCS:$(SRC_DIR)/%.c=$(REL_OBJ_DIR)/%.o)
LIB_OBJS := $(LIB_SRCS:$(SRC_DIR)/%.c=$(REL_OBJ_DIR)/%.o)
DBG_OBJS := $(SRCS:$(SRC_DIR)/%.c=$(DBG_OBJ_DIR)/%.o)

REL_BIN    := $(DIST_DIR)/$(NAME)
DBG_BIN    := $(DBG_OBJ_DIR)/$(NAME)
SO         := lib$(NAME).so
SONAME     := $(SO).$(MAJOR)
REAL_SO    := $(SO).$(VERSION)
STATIC_LIB := lib$(NAME).a
API_TEST   := $(BUILD_DIR)/api_test
PACKAGE    := $(DIST_DIR)/$(NAME)-$(VERSION).tar.gz

# ---- Toolchain -----------------------------------------------------------
CC         ?= cc
PKG_CONFIG ?= pkg-config
INSTALL    ?= install
LD         ?= ld
OBJCOPY    ?= objcopy

# GEOCLUE=auto|1|0 — GeoClue2 location support (optional)
GEOCLUE ?= auto

LIB_PKGS := libcurl libcjson
ifeq ($(GEOCLUE),auto)
  GEOCLUE := $(shell $(PKG_CONFIG) --exists libgeoclue-2.0 2>/dev/null && echo 1 || echo 0)
endif
ifeq ($(GEOCLUE),1)
  LIB_PKGS += libgeoclue-2.0
  FEATURE_CFLAGS := -DHAVE_GEOCLUE
endif
APP_PKGS := $(LIB_PKGS) ncursesw

HAVE_PKG_CONFIG := $(shell $(PKG_CONFIG) --version >/dev/null 2>&1 && echo 1)
ifeq ($(HAVE_PKG_CONFIG),1)
  DEP_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(APP_PKGS))
  LIB_LIBS   := $(shell $(PKG_CONFIG) --libs $(LIB_PKGS))
  APP_LIBS   := $(shell $(PKG_CONFIG) --libs $(APP_PKGS))
else
  LIB_LIBS := -lcurl -lcjson
  APP_LIBS := -lcurl -lcjson -lncursesw
endif

WARNINGS := -Wall -Wextra -Wshadow -Wformat=2 -Wstrict-prototypes \
            -Wmissing-prototypes -Wno-unused-parameter \
            -Wno-format-truncation
BASE_CFLAGS := -std=c11 -pthread -MMD -MP $(WARNINGS) \
               -DWW_VERSION='"$(VERSION)"' $(FEATURE_CFLAGS) $(DEP_CFLAGS)

CFLAGS  ?= -O2 -pipe
LDFLAGS ?=
# Position independent and hidden by default: only WW_API symbols are
# exported from the libraries. -fno-lto comes last on purpose: the static
# library is made with `ld -r` + `objcopy --localize-hidden`, which needs real
# machine code, not the LTO bytecode distro CFLAGS (makepkg) ask for.
REL_CFLAGS := $(BASE_CFLAGS) -fPIC -fvisibility=hidden \
              -D_FORTIFY_SOURCE=3 -fstack-protector-strong $(CFLAGS) $(CPPFLAGS) \
              -fno-lto
DBG_CFLAGS := $(BASE_CFLAGS) -O0 -g3 -DDEBUG \
              -fsanitize=address,undefined -fno-omit-frame-pointer
EXTRA_LIBS := -lm -pthread

# ---- Install paths -------------------------------------------------------
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
LIBDIR  ?= $(PREFIX)/lib
INCDIR  ?= $(PREFIX)/include
PCDIR   ?= $(LIBDIR)/pkgconfig
DATADIR ?= $(PREFIX)/share
MANDIR  ?= $(DATADIR)/man
DOCDIR  ?= $(DATADIR)/doc/$(NAME)
LICENSEDIR ?= $(DATADIR)/licenses/$(NAME)
BASHCOMPDIR ?= $(DATADIR)/bash-completion/completions
ZSHCOMPDIR  ?= $(DATADIR)/zsh/site-functions
FISHCOMPDIR ?= $(DATADIR)/fish/vendor_completions.d
DESTDIR ?=

# ---- Targets -------------------------------------------------------------
.PHONY: all release lib debug run tui check install install-user uninstall \
        package clean distclean help FORCE

all: release

release: $(REL_BIN) lib

lib: $(DIST_DIR)/$(REAL_SO) $(DIST_DIR)/$(STATIC_LIB)

debug: $(DBG_BIN)

$(REL_BIN): $(REL_OBJS) | $(DIST_DIR)
	$(CC) $(REL_CFLAGS) $(LDFLAGS) $^ -o $@ $(APP_LIBS) $(EXTRA_LIBS)
	@echo "Built $@ (GeoClue: $(if $(filter 1,$(GEOCLUE)),yes,no))"

$(DIST_DIR)/$(REAL_SO): $(LIB_OBJS) | $(DIST_DIR)
	$(CC) $(REL_CFLAGS) -shared -Wl,-soname,$(SONAME) $(LDFLAGS) $^ -o $@ \
	  $(LIB_LIBS) $(EXTRA_LIBS)
	@ln -sf $(REAL_SO) $(DIST_DIR)/$(SONAME)
	@ln -sf $(SONAME) $(DIST_DIR)/$(SO)
	@echo "Built $@"

# One relocatable object with the internal symbols made local, so programs
# linking the static library can't clash with names like log_error
$(DIST_DIR)/$(STATIC_LIB): $(LIB_OBJS) | $(DIST_DIR)
	$(LD) -r $^ -o $(REL_OBJ_DIR)/lib$(NAME).o
	$(OBJCOPY) --localize-hidden $(REL_OBJ_DIR)/lib$(NAME).o
	rm -f $@
	$(AR) rcs $@ $(REL_OBJ_DIR)/lib$(NAME).o
	@echo "Built $@"

$(DBG_BIN): $(DBG_OBJS)
	$(CC) $(DBG_CFLAGS) $(LDFLAGS) $^ -o $@ $(APP_LIBS) $(EXTRA_LIBS)
	@echo "Built $@"

$(REL_OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(BUILD_DIR)/flags.release | $(REL_OBJ_DIR)
	$(CC) $(REL_CFLAGS) -c $< -o $@

$(DBG_OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(BUILD_DIR)/flags.debug | $(DBG_OBJ_DIR)
	$(CC) $(DBG_CFLAGS) -c $< -o $@

# Rebuild everything when compiler flags change (e.g. GEOCLUE=0)
$(BUILD_DIR)/flags.%: FORCE | $(BUILD_DIR)
	@echo '$(CC) $(if $(filter release,$*),$(REL_CFLAGS),$(DBG_CFLAGS)) $(APP_LIBS)' \
	  | cmp -s - $@ || echo '$(CC) $(if $(filter release,$*),$(REL_CFLAGS),$(DBG_CFLAGS)) $(APP_LIBS)' > $@

FORCE:

$(BUILD_DIR) $(REL_OBJ_DIR) $(DBG_OBJ_DIR) $(DIST_DIR):
	@mkdir -p $@

-include $(REL_OBJS:.o=.d) $(DBG_OBJS:.o=.d)

# The API test only sees the public header and the static library
$(API_TEST): tests/api_test.c $(HEADER) $(DIST_DIR)/$(STATIC_LIB)
	$(CC) -std=c99 -Wall -Wextra -pedantic -I$(SRC_DIR) $(CFLAGS) -fno-lto $< \
	  $(DIST_DIR)/$(STATIC_LIB) -o $@ $(LIB_LIBS) $(EXTRA_LIBS)

run: release
	./$(REL_BIN) print

tui: release
	./$(REL_BIN) tui

check: release $(API_TEST)
	@sh tests/smoke.sh ./$(REL_BIN)
	@echo
	@./$(API_TEST)

install: release
	$(INSTALL) -Dm755 $(REL_BIN) $(DESTDIR)$(BINDIR)/$(NAME)
	$(INSTALL) -Dm755 $(DIST_DIR)/$(REAL_SO) $(DESTDIR)$(LIBDIR)/$(REAL_SO)
	ln -sf $(REAL_SO) $(DESTDIR)$(LIBDIR)/$(SONAME)
	ln -sf $(SONAME) $(DESTDIR)$(LIBDIR)/$(SO)
	$(INSTALL) -Dm644 $(DIST_DIR)/$(STATIC_LIB) $(DESTDIR)$(LIBDIR)/$(STATIC_LIB)
	$(INSTALL) -Dm644 $(HEADER) $(DESTDIR)$(INCDIR)/$(NAME).h
	$(INSTALL) -dm755 $(DESTDIR)$(PCDIR)
	printf '%s\n' \
	  'prefix=$(PREFIX)' \
	  'libdir=$(LIBDIR)' \
	  'includedir=$(INCDIR)' \
	  '' \
	  'Name: $(NAME)' \
	  'Description: Weather, place search and geolocation from wayweather' \
	  'Version: $(VERSION)' \
	  'Requires.private: $(LIB_PKGS)' \
	  'Libs: -L$${libdir} -l$(NAME)' \
	  'Libs.private: $(EXTRA_LIBS)' \
	  'Cflags: -I$${includedir}' > $(DESTDIR)$(PCDIR)/$(NAME).pc
	$(INSTALL) -Dm644 $(DATA_DIR)/$(NAME).1 $(DESTDIR)$(MANDIR)/man1/$(NAME).1
	$(INSTALL) -Dm644 $(DATA_DIR)/completions/$(NAME).bash $(DESTDIR)$(BASHCOMPDIR)/$(NAME)
	$(INSTALL) -Dm644 $(DATA_DIR)/completions/_$(NAME) $(DESTDIR)$(ZSHCOMPDIR)/_$(NAME)
	$(INSTALL) -Dm644 $(DATA_DIR)/completions/$(NAME).fish $(DESTDIR)$(FISHCOMPDIR)/$(NAME).fish
	$(INSTALL) -Dm644 $(DATA_DIR)/waybar/config.jsonc $(DESTDIR)$(DOCDIR)/waybar/config.jsonc
	$(INSTALL) -Dm644 $(DATA_DIR)/waybar/style.css $(DESTDIR)$(DOCDIR)/waybar/style.css
	$(INSTALL) -Dm644 README.md $(DESTDIR)$(DOCDIR)/README.md
	$(INSTALL) -Dm644 LICENSE $(DESTDIR)$(LICENSEDIR)/LICENSE

# Per-user install without root
install-user:
	@$(MAKE) --no-print-directory install PREFIX=$(HOME)/.local

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(NAME) \
	      $(DESTDIR)$(LIBDIR)/$(REAL_SO) $(DESTDIR)$(LIBDIR)/$(SONAME) \
	      $(DESTDIR)$(LIBDIR)/$(SO) $(DESTDIR)$(LIBDIR)/$(STATIC_LIB) \
	      $(DESTDIR)$(INCDIR)/$(NAME).h $(DESTDIR)$(PCDIR)/$(NAME).pc \
	      $(DESTDIR)$(MANDIR)/man1/$(NAME).1 \
	      $(DESTDIR)$(BASHCOMPDIR)/$(NAME) \
	      $(DESTDIR)$(ZSHCOMPDIR)/_$(NAME) \
	      $(DESTDIR)$(FISHCOMPDIR)/$(NAME).fish
	rm -rf $(DESTDIR)$(DOCDIR) $(DESTDIR)$(LICENSEDIR)

# Release tarball: binary, libraries, header and data files
package: release
	@rm -rf $(BUILD_DIR)/package && mkdir -p $(BUILD_DIR)/package
	cp -P $(REL_BIN) $(DIST_DIR)/$(SO)* $(DIST_DIR)/$(STATIC_LIB) $(HEADER) \
	      README.md LICENSE CHANGELOG.md $(BUILD_DIR)/package/
	cp -r $(DATA_DIR) $(BUILD_DIR)/package/
	tar -czf $(PACKAGE) -C $(BUILD_DIR)/package .
	@echo "Created $(PACKAGE)"

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -rf $(DIST_DIR)

help:
	@echo "Wayweather $(VERSION)"
	@echo ""
	@echo "Build:"
	@echo "  make [release]       binary, $(SO) and $(STATIC_LIB) in $(DIST_DIR)/"
	@echo "  make lib             only the libraries"
	@echo "  make debug           debug build with ASan/UBSan in $(DBG_OBJ_DIR)/"
	@echo "  make check           offline CLI and library tests"
	@echo "  make run | tui       build and run 'print' / the TUI"
	@echo ""
	@echo "Install:"
	@echo "  make install         to PREFIX=$(PREFIX) (sudo for system dirs)"
	@echo "  make install-user    to ~/.local"
	@echo "  make uninstall       remove (use the same PREFIX/DESTDIR)"
	@echo ""
	@echo "Package / clean:"
	@echo "  make package         $(PACKAGE)"
	@echo "  make clean           remove $(BUILD_DIR)/"
	@echo "  make distclean       also remove $(DIST_DIR)/"
	@echo ""
	@echo "Options:"
	@echo "  GEOCLUE=auto|1|0     GeoClue2 support (detected: $(GEOCLUE))"
	@echo "  VERSION, PREFIX, DESTDIR, CC, CFLAGS, LDFLAGS"
	@echo ""
	@echo "Dependencies: libcurl, cJSON, ncursesw (+ libgeoclue-2.0 optional)"

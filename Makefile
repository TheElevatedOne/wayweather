# Wayweather — weather module for Waybar
#
#   make                 optimised build      -> dist/wayweather
#   make debug           debug + sanitizers   -> build/debug/wayweather
#   make install         install to $(PREFIX) (honours DESTDIR)
#   make help            all targets and options

NAME    := wayweather
VERSION := 3.0.0

# ---- Layout --------------------------------------------------------------
SRC_DIR   := src
BUILD_DIR := build
DIST_DIR  := dist
DATA_DIR  := data

SRCS := $(wildcard $(SRC_DIR)/*.c)

REL_OBJ_DIR := $(BUILD_DIR)/release
DBG_OBJ_DIR := $(BUILD_DIR)/debug
REL_OBJS := $(SRCS:$(SRC_DIR)/%.c=$(REL_OBJ_DIR)/%.o)
DBG_OBJS := $(SRCS:$(SRC_DIR)/%.c=$(DBG_OBJ_DIR)/%.o)
REL_BIN  := $(DIST_DIR)/$(NAME)
DBG_BIN  := $(DBG_OBJ_DIR)/$(NAME)

# ---- Toolchain -----------------------------------------------------------
CC         ?= cc
PKG_CONFIG ?= pkg-config
INSTALL    ?= install

# GEOCLUE=auto|1|0 — GeoClue2 location support (optional)
GEOCLUE ?= auto

PKGS := libcurl libcjson ncursesw
ifeq ($(GEOCLUE),auto)
  GEOCLUE := $(shell $(PKG_CONFIG) --exists libgeoclue-2.0 2>/dev/null && echo 1 || echo 0)
endif
ifeq ($(GEOCLUE),1)
  PKGS += libgeoclue-2.0
  FEATURE_CFLAGS := -DHAVE_GEOCLUE
endif

HAVE_PKG_CONFIG := $(shell $(PKG_CONFIG) --version >/dev/null 2>&1 && echo 1)
ifeq ($(HAVE_PKG_CONFIG),1)
  DEP_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS))
  DEP_LIBS   := $(shell $(PKG_CONFIG) --libs $(PKGS))
else
  DEP_LIBS := -lcurl -lcjson -lncursesw
endif

WARNINGS := -Wall -Wextra -Wshadow -Wformat=2 -Wstrict-prototypes \
            -Wmissing-prototypes -Wno-unused-parameter \
            -Wno-format-truncation
BASE_CFLAGS := -std=c11 -pthread -MMD -MP $(WARNINGS) \
               -DWW_VERSION='"$(VERSION)"' $(FEATURE_CFLAGS) $(DEP_CFLAGS)

CFLAGS  ?= -O2 -pipe
LDFLAGS ?=
REL_CFLAGS := $(BASE_CFLAGS) -D_FORTIFY_SOURCE=3 -fstack-protector-strong \
              $(CFLAGS) $(CPPFLAGS)
DBG_CFLAGS := $(BASE_CFLAGS) -O0 -g3 -DDEBUG \
              -fsanitize=address,undefined -fno-omit-frame-pointer
LIBS := $(DEP_LIBS) -lm -pthread

# ---- Install paths -------------------------------------------------------
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
MANDIR  ?= $(DATADIR)/man
DOCDIR  ?= $(DATADIR)/doc/$(NAME)
LICENSEDIR ?= $(DATADIR)/licenses/$(NAME)
BASHCOMPDIR ?= $(DATADIR)/bash-completion/completions
ZSHCOMPDIR  ?= $(DATADIR)/zsh/site-functions
FISHCOMPDIR ?= $(DATADIR)/fish/vendor_completions.d
DESTDIR ?=

# ---- Targets -------------------------------------------------------------
.PHONY: all release debug run tui check install install-user uninstall package \
        tar clean distclean help

all: release

release: $(REL_BIN)

debug: $(DBG_BIN)

$(REL_BIN): $(REL_OBJS) | $(DIST_DIR)
	$(CC) $(REL_CFLAGS) $(LDFLAGS) $^ -o $@ $(LIBS)
	@echo "Built $@ (GeoClue: $(if $(filter 1,$(GEOCLUE)),yes,no))"

$(DBG_BIN): $(DBG_OBJS)
	$(CC) $(DBG_CFLAGS) $(LDFLAGS) $^ -o $@ $(LIBS)
	@echo "Built $@"

$(REL_OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(BUILD_DIR)/flags.release | $(REL_OBJ_DIR)
	$(CC) $(REL_CFLAGS) -c $< -o $@

$(DBG_OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(BUILD_DIR)/flags.debug | $(DBG_OBJ_DIR)
	$(CC) $(DBG_CFLAGS) -c $< -o $@

# Rebuild everything when compiler flags change (e.g. GEOCLUE=0)
$(BUILD_DIR)/flags.%: FORCE | $(BUILD_DIR)
	@echo '$(CC) $(if $(filter release,$*),$(REL_CFLAGS),$(DBG_CFLAGS)) $(LIBS)' \
	  | cmp -s - $@ || echo '$(CC) $(if $(filter release,$*),$(REL_CFLAGS),$(DBG_CFLAGS)) $(LIBS)' > $@

FORCE:

$(BUILD_DIR) $(REL_OBJ_DIR) $(DBG_OBJ_DIR) $(DIST_DIR):
	@mkdir -p $@

-include $(REL_OBJS:.o=.d) $(DBG_OBJS:.o=.d)

run: release
	./$(REL_BIN) print

tui: release
	./$(REL_BIN) tui

# Offline smoke tests against a temporary config
check: release
	@sh tests/smoke.sh ./$(REL_BIN)

install: release
	$(INSTALL) -Dm755 $(REL_BIN) $(DESTDIR)$(BINDIR)/$(NAME)
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
	      $(DESTDIR)$(MANDIR)/man1/$(NAME).1 \
	      $(DESTDIR)$(BASHCOMPDIR)/$(NAME) \
	      $(DESTDIR)$(ZSHCOMPDIR)/_$(NAME) \
	      $(DESTDIR)$(FISHCOMPDIR)/$(NAME).fish
	rm -rf $(DESTDIR)$(DOCDIR) $(DESTDIR)$(LICENSEDIR)

# Release tarball with the binary and data files
package tar: release
	@rm -rf $(BUILD_DIR)/pkg && mkdir -p $(BUILD_DIR)/pkg/$(NAME)-$(VERSION)
	cp -r $(REL_BIN) $(DATA_DIR) README.md LICENSE CHANGELOG.md \
	      $(BUILD_DIR)/pkg/$(NAME)-$(VERSION)/
	tar -czf $(DIST_DIR)/$(NAME)-$(VERSION)-$(shell uname -m).tar.gz \
	    -C $(BUILD_DIR)/pkg $(NAME)-$(VERSION)
	@echo "Created $(DIST_DIR)/$(NAME)-$(VERSION)-$(shell uname -m).tar.gz"

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -rf $(DIST_DIR)

help:
	@echo "Wayweather $(VERSION)"
	@echo ""
	@echo "Build:"
	@echo "  make [release]       optimised binary in $(DIST_DIR)/"
	@echo "  make debug           debug build with ASan/UBSan in $(DBG_OBJ_DIR)/"
	@echo "  make check           offline smoke tests"
	@echo "  make run | tui       build and run 'print' / the TUI"
	@echo ""
	@echo "Install:"
	@echo "  make install         to PREFIX=$(PREFIX) (sudo for system dirs)"
	@echo "  make install-user    to ~/.local"
	@echo "  make uninstall       remove (use the same PREFIX/DESTDIR)"
	@echo ""
	@echo "Package / clean:"
	@echo "  make package         tarball in $(DIST_DIR)/"
	@echo "  make clean           remove $(BUILD_DIR)/"
	@echo "  make distclean       also remove $(DIST_DIR)/"
	@echo ""
	@echo "Options:"
	@echo "  GEOCLUE=auto|1|0     GeoClue2 support (detected: $(GEOCLUE))"
	@echo "  PREFIX, DESTDIR, CC, CFLAGS, LDFLAGS"
	@echo ""
	@echo "Dependencies: libcurl, cJSON, ncursesw (+ libgeoclue-2.0 optional)"

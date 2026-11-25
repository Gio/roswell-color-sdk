# Roswell Color SDK [GNUMakefile]
#
#  Note:
#    I found that '.RECIPEPREFIX' doesn't work with '»' in UTC-8 format but
#    does in ISO 8859-1. If this crashes with "permission denied", ensure the
#    character encoding is set to 'ISO 8859-1' or 'Windows 1252'.
#
#  Targets:
#    make                   performs full build
#    make rcsdk             builds librc
#    make tools             copies tools
#    make raylib            imports and builds raylib
#    make dist              creates .tar archive
#    make clean             cleans intermediate files (keeps raylib)
#    make distclean         cleans all built and imported files
#
#  Layout:
#    src/                   SDK source
#    src/inc/               SDK public header
#    src/tools/             art & audio tools
#    inc/                   headers (raylib.h)
#    lib/raylib             raylib web library
#    vendor/raylib          raylib 6 source
#
#
#  Calling 'make' checks first for 'libraylib-web', and, if not found, checks
#  for the source under vendor/raylib. If not found, it will clone the raylib
#  git repo and proceed with building the SDK.


##### §1 Setup #####
### §1.1 Names
SDK        = rcsdk
VERSION    = 1
SUBVERSION = 0
SDK_NAME   = $(SDK)-$(VERSION).$(SUBVERSION)

# Raylib
RAYLIB_VERSION = 6.0

### §1.2 Utils
CC  = emcc
AR  = emar
TAR = tar
ZIP = xz

### §1.3 Directories
srcDir  = src
pubDir  = $(srcDir)/inc
toolDir = $(srcDir)/tools
incDir  = inc
libDir  = lib
venDir  = vendor
tmpDir  = build
raySRC  = $(venDir)/raylib
rayTMP  = $(tmpDir)/raylib

sdkDir  = $(SDK_NAME)
sdkTool = $(SDK_NAME)/tools

### §1.4 Vendor
RAYLIB_REPO = https://github.com/raysan5/raylib.git

### §1.5 Files
SDK_LIB       = $(sdkDir)/librc.a
RAYLIB_LIB    = $(libDir)/libraylib.web.a
RAYLIB_HEADER = $(incDir)/raylib.h
#ART_EDITOR   = arted.py
#AUDIO_EDITOR = auded.py

### §1.6 Make stuff
.RECIPEPREFIX = »
.DEFAULT_GOAL = rcsdk

##### §2 Macros & Patterns #####


##### §3 Targets #####
### §3.1 Cooking
# Raylib
.PHONY: raylib
$(raySRC):
» @mkdir -p $(dir $@)
» git clone --quiet --depth 1 --branch $(RAYLIB_VERSION) $(RAYLIB_REPO) $@

raylib: $(RAYLIB_LIB)
$(RAYLIB_LIB): | $(raySRC)
» rm -rf $(rayTMP)
» @mkdir -p $(tmpDir) $(dir $@) $(dir $(RAYLIB_HEADER))
» cp -r $(raySRC)/src $(rayTMP)
» $(MAKE) -C $(rayTMP) PLATFORM=PLATFORM_WEB RAYLIB_LIBTYPE=STATIC RAYLIB_RELEASE_PATH=.
» cp $(rayTMP)/$(notdir $(RAYLIB_HEADER)) $(RAYLIB_HEADER)
» cp $(rayTMP)/$(notdir $@) $@

# Art & Audio Tools
.PHONY: tools
#tools:
#» mkdir -p $(sdkTool)
#» cp $(toolDir)/$(ART_EDITOR) $(sdkTool)/$(ART_EDITOR)
#» cp $(toolDir)/$(AUDIO_EDITOR) $(sdkTool)/$(AUDIO_EDITOR)

# Roswell Color SDK library
.PHONY: rcsdk
rcsdk: 


##### §3.2 Cleaning #####

#.PHONY: dist
#» @echo "Preparing distribution files:"
#» $(TAR) -cf $(NAME).tar $(files)
#» $(ZIP) -kf $(NAME).tar
#» @echo "Finished."











$(SDK_LIB): $(SDK_OBJ)
$(AR) rcs $@ $^


$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(SRC_DIR)/internal.h $(SDK_HEADER) $(RAYLIB_HEADER) | check-tools
@mkdir -p $(dir $@)
$(CC) $(CFLAGS) -I$(INC_DIR) -c -o $@ $<

SDK_SRC    := $(wildcard $(SRC_DIR)/*.c)
SDK_OBJ    := $(SDK_SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
SDK_HEADER := $(SDK_DIR)/rc_dev.h
SDK_LIB    := $(SDK_DIR)/librc.a
SHELL_HTML := $(SDK_DIR)/shell.html

OBJ_DIR := $(BUILD)/obj
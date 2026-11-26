# Roswell Color SDK [GNUMakefile]
#
#  Note:
#    I found that '.RECIPEPREFIX' doesn't work with '»' in UTF-8 format but
#    does in ISO 8859-1. If this crashes with "permission denied", ensure the
#    character encoding is set to 'ISO 8859-1' or 'Windows 1252'.
#
#  Targets:
#    make                   performs full build
#    make rcsdk             builds librc
#    make tools             copies tools
#    make raylib            imports and builds raylib
#    make release           performs full build
#    make dist              creates .tar archive
#    make clean             cleans intermediate files (keeps raylib)
#    make distclean         cleans all built and imported files
#
#  Layout:
#    src/                   SDK source
#    tools                  art & audio editors
#    inc/                   headers (raylib/raylib.h, rc_dev.h)
#    lib/raylib             raylib web library (libraylib.web.a)
#    vendor/raylib          raylib 6 source
#    build/                 intermediate files
#    rcsdk-x.y,z/           SDK output
#
#
#  Calling 'make' checks first for 'lib/raylib/libraylib.web.a', and, if not
#  found, checks for the source under vendor/raylib. If not found, it will
#  clone the raylib git repo and proceed with building the SDK.


##### §1 Setup #####
### §1.1 Names
NAME       = rcsdk
VERSION    = 0
SUBVERSION = 1
MINVERSION = 2
DIST_NAME  = $(NAME)-v$(VERSION).$(SUBVERSION).$(MINVERSION)

# Raylib
RAYLIB_VERSION = 6.0

### §1.2 Utils
CC  = emcc
AR  = emar
TAR = tar
ZIP = lzip

# Flags
CFLAGS = -std=c99 -Wall -Wextra -O2

### §1.3 Directories
srcDir  = src
toolDir = tools
incDir  = inc
libDir  = lib
venDir  = vendor
tmpDir  = build
objDir  = $(tmpDir)/obj
rayINC  = $(incDir)/raylib
rayLIB  = $(libDir)/raylib
raySRC  = $(venDir)/raylib
rayTMP  = $(tmpDir)/raylib
sdkDir  = $(DIST_NAME)
sdkTool = $(sdkDir)/tools

### §1.4 Vendor
RAYLIB_REPO = https://github.com/raysan5/raylib.git

### §1.5 Files
# Raylib
RAYLIB_LIB    = $(rayLIB)/libraylib.web.a
RAYLIB_HEADER = $(rayINC)/raylib.h

# Objects
OBJS = $(objDir)/core.o     \
       $(objDir)/display.o  \
       $(objDir)/input.o    \
       $(objDir)/audio.o    \
       $(objDir)/platform.o

# Outputs
SDK_LIB    = $(tmpDir)/librc.a

### §1.6 Make stuff
.RECIPEPREFIX = »
.DEFAULT_GOAL = release


##### §2 Macros & Patterns #####
# Object files
$(objDir)/%.o: $(srcDir)/%.c $(srcDir)/internal.h $(incDir)/rc_dev.h $(RAYLIB_HEADER)
» @mkdir -p $(dir $@)
» $(CC) $(CFLAGS) -I$(srcDir) -I$(incDir) -I$(rayINC) -c -o $@ $<


##### §3 Targets #####
### §3.1 Cooking
# Raylib
.PHONY: raylib
$(raySRC):
» @mkdir -p $(dir $@)
» git -c advice.detachedHead=false clone --quiet --depth 1 --branch $(RAYLIB_VERSION) $(RAYLIB_REPO) $@

raylib: $(RAYLIB_LIB) $(RAYLIB_HEADER)
$(RAYLIB_LIB): | $(raySRC)
» rm -rf $(rayTMP)
» @mkdir -p $(tmpDir) $(dir $@)
» cp -r $(raySRC)/src $(rayTMP)
» $(MAKE) -C $(rayTMP) PLATFORM=PLATFORM_WEB RAYLIB_LIBTYPE=STATIC RAYLIB_RELEASE_PATH=.
» cp $(rayTMP)/$(notdir $@) $@

$(RAYLIB_HEADER): | $(raySRC)
» @mkdir -p $(dir $@)
» cp $(raySRC)/src/$(notdir $@) $@

# Art & Audio Tools
.PHONY: tools
tools:
» mkdir -p $(sdkTool)
» cp -r $(toolDir)/. $(sdkTool)


# Roswell Color SDK library
.PHONY: rcsdk
rcsdk: $(SDK_LIB)
$(SDK_LIB): $(OBJS)
» rm -f $@
» $(AR) rcs $@ $^

### §3.2 Serving
# Release
.PHONY: release
release: rcsdk raylib tools
» mkdir -p $(sdkDir)/$(rayLIB) $(sdkDir)/$(rayINC)
» cp $(SDK_LIB) $(sdkDir)/$(libDir)
» cp $(RAYLIB_LIB) $(sdkDir)/$(rayLIB)
» cp $(incDir)/rc_dev.h $(sdkDir)/$(incDir)
» cp $(RAYLIB_HEADER) $(sdkDir)/$(rayINC)
» cp LICENSE $(sdkDir)

# Distribution
.PHONY: dist
dist: release
» $(TAR) -cf $(DIST_NAME).tar $(sdkDir)
» $(ZIP) -kf $(DIST_NAME).tar

### §3.3 Cleaning
# Easy clean
.PHONY: clean
clean:
» rm -rf $(objDir) $(SDK_LIB) 

# Full clean (even raylib)
.PHONY: distclean
distclean:
» rm -rf $(tmpDir) $(libDir) $(rayINC) $(venDir) $(sdkDir) $(DIST_NAME).tar $(DIST_NAME).tar.*

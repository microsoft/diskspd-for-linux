CXX=g++
LD=g++
#CPPFLAGS=
CXXFLAGS=-g -std=c++11 -D_FILE_OFFSET_BITS=64
LDFLAGS= -lpthread -lrt -laio
#LDLIBS=

# add sources here
SRCS=$(wildcard src/*.cc)

# .cc->.o
OBJS=$(subst .cc,.o,$(SRCS))

$(shell mkdir -p bin >/dev/null)

BIN=bin/diskspd

# allow make DEBUG=1 to set the debug flag during compilation
ifeq ($(DEBUG),1)
FLAG_DEBUG=-DENABLE_DEBUG=1
endif

# allow make STATIC=1 to create a static linked executable
ifeq ($(STATIC),1)
LDFLAGS= -static -static-libgcc -static-libstdc++ -pthread -lrt -laio
endif

all:$(BIN)

# link the object files into the target
$(BIN): $(OBJS)
	$(LD) $(OUTPUT_OPTION) $(OBJS) $(LDFLAGS)


.PHONY: clean
clean:
	rm -rf bin
	rm -f src/*.o
	rm -rf $(DEPDIR)

# usr/local/bin is the correct location for binaries NOT managed by the distro package manager
.PHONY: install
install:
	cp $(BIN) /usr/local/bin

# for a fuller explanation of this method of dependency generation, see:
# http://make.mad-scientist.net/papers/advanced-auto-dependency-generation/

DEPDIR := .d
# make sure dependency directory exists
$(shell mkdir -p $(DEPDIR) >/dev/null)
# gcc flags for generating dependency files as a side effect
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$(patsubst src/%.o,%.Td,$@)

# compile command for .cc to .o for overriding default
COMPILE.cc = $(CXX) $(DEPFLAGS) $(CXXFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c $(FLAG_DEBUG)
# command to rename the .Tds to .ds - we do this in case of failures in compilation step.
# no new .Td means no new .d. We also touch the target because of an issue with some older gccs
POSTCOMPILE = @mv -f $(DEPDIR)/$(patsubst src/%.o,%.Td,$@) $(DEPDIR)/$(patsubst src/%.o,%.d,$@) && touch $@

# delete default rule for .cc files
%.o : %.cc
src/%.o : src/%.cc $(DEPDIR)/%.d
		$(COMPILE.cc) $(OUTPUT_OPTION) $<
		$(POSTCOMPILE)

# empty recipe - make won't fail if the .d file doesn't exist yet
$(DEPDIR)/%.d: ;
# don't delete the .d files if make fails
.PRECIOUS: $(DEPDIR)/%.d

# finally - include the existing .d files; these contain the dependency information for each source
include $(wildcard $(DEPDIR)/*.d)

SHELL=/bin/bash -x
# Top-level makefile to build esbmc from scratch including
# solver dependencies. 
#
# Currently this is x86_64 Linux only.
#
# This also assumes all dependencies are installed - otherwise there
# shall be errors!!!


# P should be passed into makefile with
# make P=...
# However, this default is better than nothing
ifndef P
$(error Please specify PREFIX with 'make P=...')
endif
ABSPREFIX=$(abspath $(P))
DLS=$(ABSPREFIX)/downloads
BUILD=$(ABSPREFIX)/build

# VERSIONS
MATHSAT_VERSION=5.6.3
Z3_VERSION=4.8.8
CVC4_VERSION=1.8
BOOLECTOR_VERSION=3.2.1
YICES_VERSION=Yices-2.6.2
GMP_VERSION=6.2.0
CLANG_VERSION=9.0.0

# ESBMC straight from git
ESBMC_GIT_VERSION := $(shell git describe --abbrev=4 --dirty --always --tags)

# Install Paths
MATHSAT_PATH=$(ABSPREFIX)/mathsat-$(MATHSAT_VERSION)
Z3_PATH=$(ABSPREFIX)/z3-$(Z3_VERSION)
CVC4_PATH=$(ABSPREFIX)/cvc4-$(CVC4_VERSION)
BOOLECTOR_PATH=$(ABSPREFIX)/boolector-$(BOOLECTOR_VERSION)
YICES_PATH=$(ABSPREFIX)/yices-$(YICES_VERSION)
GMP_PATH=$(ABSPREFIX)/gmp-$(GMP_VERSION)
CLANG_PATH=$(ABSPREFIX)/clang-$(CLANG_VERSION)

ESBMC_PATH=$(ABSPREFIX)/esbmc-$(ESBMC_GIT_VERSION)

solvers=mathsat-$(MATHSAT_VERSION) boolector-$(BOOLECTOR_VERSION) z3-$(Z3_VERSION) yices-$(YICES_VERSION) cvc4-$(CVC4_VERSION) 
slvdirs=$(addprefix $(ABSPREFIX)/,$(solvers))
clangdir=$(ABSPREFIX)/clang-$(CLANG_VERSION)
gmpdir=$(ABSPREFIX)/gmp-$(GMP_VERSION)
esbmcdir=$(ABSPREFIX)/esbmc-release

slvstamps=$(addsuffix /.dirstamp,$(slvdirs))
clangstamp=$(clangdir)/.dirstamp
gmpstamp=$(gmpdir)/.dirstamp
esbmcstamp=$(ESBMC_PATH)/.dirstamp

all: $(esbmcstamp)

$(DLS):
	mkdir -p $@

$(BUILD):
	mkdir -p $@

$(ABSPREFIX)/mathsat-$(MATHSAT_VERSION)/.dirstamp: $(DLS)
	wget http://mathsat.fbk.eu/download.php?file=mathsat-$(MATHSAT_VERSION)-linux-x86_64.tar.gz -O $(DLS)/mathsat-$(MATHSAT_VERSION).tar.gz
	tar -xz -C $(ABSPREFIX) -f $(DLS)/mathsat-$(MATHSAT_VERSION).tar.gz
	mv $(ABSPREFIX)/mathsat-$(MATHSAT_VERSION)-linux-x86_64 $(ABSPREFIX)/mathsat-$(MATHSAT_VERSION)/
	touch $@

$(ABSPREFIX)/boolector-$(BOOLECTOR_VERSION)/.dirstamp: $(DLS) $(BUILD)
	git clone --depth=1 --branch=$(BOOLECTOR_VERSION)  https://github.com/boolector/boolector $(BUILD)/boolector-$(BOOLECTOR_VERSION)
	cd $(BUILD)/boolector-$(BOOLECTOR_VERSION) && ./contrib/setup-lingeling.sh && ./contrib/setup-btor2tools.sh && ./configure.sh --prefix $(ABSPREFIX)/boolector-$(BOOLECTOR_VERSION) && cd build && make -j$$(($$(nproc)+1)) -l$$(nproc) && make -j$$(nproc) install
	touch $@

$(ABSPREFIX)/clang-$(CLANG_VERSION)/.dirstamp: $(DLS)
	wget http://releases.llvm.org/9.0.0/clang+llvm-$(CLANG_VERSION)-x86_64-linux-gnu-ubuntu-18.04.tar.xz -O $(DLS)/clang-$(CLANG_VERSION).tar.xz
	tar -xJ -C $(ABSPREFIX) -f $(DLS)/clang-$(CLANG_VERSION).tar.xz
	mv $(ABSPREFIX)/clang+llvm-9.0.0-x86_64-linux-gnu-ubuntu-18.04 $(ABSPREFIX)/clang-$(CLANG_VERSION)
	touch $@

$(ABSPREFIX)/z3-$(Z3_VERSION)/.dirstamp: $(DLS) $(BUILD)
	git clone --depth=1 --branch=z3-$(Z3_VERSION) https://github.com/Z3Prover/z3/ $(BUILD)/z3-$(Z3_VERSION)
	cd $(BUILD)/z3-$(Z3_VERSION) && python scripts/mk_make.py --prefix=$(ABSPREFIX)/z3-$(Z3_VERSION) --staticbin --staticlib
	cd $(BUILD)/z3-$(Z3_VERSION)/build && make -j$$(($$(nproc)+1)) -l$$(nproc)
	cd $(BUILD)/z3-$(Z3_VERSION)/build && make -j$$(nproc) install
	touch $@

$(ABSPREFIX)/gmp-$(GMP_VERSION)/.dirstamp: $(DLS) $(BUILD)
	wget https://gmplib.org/download/gmp/gmp-$(GMP_VERSION).tar.xz -O $(DLS)/gmp-$(GMP_VERSION).tar.xz
	tar -xJ -C $(BUILD) -f $(DLS)/gmp-$(GMP_VERSION).tar.xz
	cd $(BUILD)/gmp-$(GMP_VERSION) && ./configure --prefix $(ABSPREFIX)/gmp-$(GMP_VERSION) --disable-shared ABI=64 CFLAGS=-fPIC CPPFLAGS=-DPIC
	cd $(BUILD)/gmp-$(GMP_VERSION) && make -j$$(($$(nproc)+1)) -l$$(nproc)
	cd $(BUILD)/gmp-$(GMP_VERSION) && make -j$$(nproc) install
	touch $@

$(ABSPREFIX)/yices-$(YICES_VERSION)/.dirstamp: $(gmpstamp) $(DLS) $(BUILD)
	git clone --depth=1 --branch=$(YICES_VERSION) https://github.com/SRI-CSL/yices2.git $(BUILD)/yices-$(YICES_VERSION)
	cd $(BUILD)/yices-$(YICES_VERSION) && autoreconf -fi
	cd $(BUILD)/yices-$(YICES_VERSION) && ./configure --prefix=$(ABSPREFIX)/yices-$(YICES_VERSION) --with-static-gmp=$(GMP_PATH)/lib/libgmp.a
	cd $(BUILD)/yices-$(YICES_VERSION) && make -j$$(($$(nproc)+1)) -l$$(nproc)
	cd $(BUILD)/yices-$(YICES_VERSION) && make -j$$(nproc) install
	touch $@

$(ABSPREFIX)/cvc4-$(CVC4_VERSION)/.dirstamp: $(DLS) $(BUILD)
	git clone --depth=1 --branch=$(CVC4_VERSION) https://github.com/CVC4/CVC4.git $(BUILD)/cvc4-$(CVC4_VERSION)
	cd $(BUILD)/cvc4-$(CVC4_VERSION) && ./contrib/get-antlr-3.4
	cd $(BUILD)/cvc4-$(CVC4_VERSION) && ./configure.sh --optimized --prefix=$(CVC4_PATH) --static --no-static-binary
	cd $(BUILD)/cvc4-$(CVC4_VERSION)/build && make -j$$(($$(nproc)+1)) -l$$(nproc)
	cd $(BUILD)/cvc4-$(CVC4_VERSION)/build && make -j$$(nproc) install
	touch $@

$(ABSPREFIX)/esbmc-$(ESBMC_GIT_VERSION)/.dirstamp: $(slvstamps) $(gmpstamp) $(clangstamp)
	mkdir $(BUILD)/esbmc-$(ESBMC_GIT_VERSION)
	cd $(BUILD)/esbmc-$(ESBMC_GIT_VERSION) && cmake $(ABSPREFIX)/esbmc -G "Unix Makefiles" -DBUILD_TESTING=On -DENABLE_REGRESSION=On -DClang_DIR=$(CLANG_PATH) -DLLVM_DIR=$(CLANG_PATH)  -DBoolector_DIR=$(BOOLECTOR_PATH) -DZ3_DIR=$(Z3_PATH) -DENABLE_MATHSAT=ON -DMathsat_DIR=$(MATHSAT_PATH) -DENABLE_YICES=On -DYices_DIR=$(YICES_PATH) -DCVC4_DIR=$(CVC4_PATH) -DGMP_DIR=$(GMP_PATH) -DCMAKE_INSTALL_PREFIX:PATH=$(ESBMC_PATH)
	cd $(BUILD)/esbmc-$(ESBMC_GIT_VERSION) && make -j$$(nproc) install

.PHONY: clean
clean:
	-rm -Rf $(DLS) $(BUILD) $(slvdirs) $(gmpdir) $(clangdir) $(esbmcdir)

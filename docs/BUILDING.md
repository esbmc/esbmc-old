# ESBMC Static Build Guide

This is a guide on how to build ESBMC and its supported solvers.

It has been tested with Ubuntu 20.04.1 and macOS Catalina as well as [Windows 10 WSL](https://docs.microsoft.com/en-us/windows/wsl/about) v1 or v2 [Ubuntu WSL](https://ubuntu.com/wsl), but the steps are mostly the same for other Linux and macOS distributions.

It is recommended that the RAM should be 6 GB at least.

Before starting, note that ESBMC is mainly distributed under the terms of the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0), so please make sure to read it carefully.

## Dependency overview

| package   | required | minimum version |
|-----------|----------|-----------------|
| clang     | yes      | 11.0.0          |
| boost     | yes      | 1.77            |
| Boolector | no       | 3.2.2           |
| CVC4      | no       | 1.8             |
| MathSAT   | no       | 5.5.4           |
| Yices     | no       | 2.6.1           |
| Z3        | no       | 4.8.9           |
| Bitwuzla  | no       | smtcomp-2021    |

The version requirements are usually pretty stable, but can change between releases.

## Installing build tools and basic dependencies

We need to install some dependencies before moving into next steps.

All of them are listed in the following installation command:

```
Linux:
sudo apt-get update && sudo apt-get install build-essential git gperf libgmp-dev cmake bison curl flex gcc-multilib linux-libc-dev libboost-all-dev libtinfo-dev ninja-build python3-setuptools unzip wget python3-pip openjdk-8-jre

macOS:
brew install gmp cmake boost ninja python3 automake && pip3 install PySMT
```

Note that they are listed with their name in Debian/Ubuntu, but they can be found in other distributions as well.

## Cloning ESBMC Source Code

ESBMC source code is publicly available in [Github](https://github.com/esbmc/esbmc).

Before Cloning ESBMC Source Code, it is better to make a directory to contain the whole project in “ESBMC_Project”.

You can get the latest version using the following __git__ command:

```
mkdir ESBMC_Project && cd ESBMC_Project && git clone https://github.com/esbmc/esbmc
```

## Preparing Clang 11

ESBMC uses [__clang__](https://clang.llvm.org/) in its front-end. It currently supports version 11.0.0.

First, we need to download the package. It can be performed using the following __wget__ command:

```
Linux:
wget https://github.com/llvm/llvm-project/releases/download/llvmorg-11.0.0/clang+llvm-11.0.0-x86_64-linux-gnu-ubuntu-20.04.tar.xz

macOS:
wget https://github.com/llvm/llvm-project/releases/download/llvmorg-11.0.0/clang+llvm-11.0.0-x86_64-apple-darwin.tar.xz
```

Then, we need to extract the package. You can use the following __tar__ command:

```
Linux:
tar xJf clang+llvm-11.0.0-x86_64-linux-gnu-ubuntu-20.04.tar.xz && mv clang+llvm-11.0.0-x86_64-linux-gnu-ubuntu-20.04 clang11

macOS:
tar xJf clang+llvm-11.0.0-x86_64-apple-darwin.tar.xz && mv clang+llvm-11.0.0-x86_64-apple-darwin clang11
```

## Preparing the Solidity frontend

We developed a solidity frontend for the efficient SMT-based context-bounded model checker (ESBMC). ESBMC-solidity is a permissively licensed open-source context-bounded model checker to verify Solidity smart contracts. It can verify both predefined safety properties (e.g., bounds check, overflow, underflow) and user-defined program assertions automatically.

In order to verify Solidity smart contract, ESBMC should be built with the option:

```
-DENABLE_SOLIDITY_FRONTEND=On
```

## Setting Up Solvers

ESBMC relies on SMT solvers to reason about formulae in its back-end.

Currently we support the following solvers: __Bitwuzla__, __Boolector__, __CVC4__, __MathSAT__, __Yices 2__, and __Z3__.

Since this guide focuses primarily on ESBMC build, we will only cover the steps needed by it.

### Setting Up Boolector

We have wrapped the entire build and setup of Boolector in the following command:

```
git clone --depth=1 --branch=3.2.2 https://github.com/boolector/boolector && cd boolector && ./contrib/setup-lingeling.sh && ./contrib/setup-btor2tools.sh && ./configure.sh --prefix $PWD/../boolector-release && cd build && make -j9 && make install && cd .. && cd ..
```

If you need more details on Boolector, please refer to [its Github](https://github.com/Boolector/boolector).

### Setting Up CVC4 (Linux Only)

We have wrapped the entire build and setup of CVC4 in the following command:

```
pip3 install toml && git clone https://github.com/CVC4/CVC4.git && cd CVC4 && git reset --hard b826fc8ae95fc && ./contrib/get-antlr-3.4 && ./configure.sh --optimized --prefix=../cvc4 --static --no-static-binary && cd build && make -j4 && make install && cd .. && cd ..
```

If you need more details on CVC4, please refer to [its Github](https://github.com/CVC4/CVC4).

### Setting Up MathSAT

We have wrapped the entire build and setup of MathSAT in the following command:

```
Linux:
wget http://mathsat.fbk.eu/download.php?file=mathsat-5.5.4-linux-x86_64.tar.gz -O mathsat.tar.gz && tar xf mathsat.tar.gz && mv mathsat-5.5.4-linux-x86_64 mathsat

macOS:
wget http://mathsat.fbk.eu/download.php?file=mathsat-5.5.4-darwin-libcxx-x86_64.tar.gz -O mathsat.tar.gz && tar xf mathsat.tar.gz && mv mathsat-5.5.4-darwin-libcxx-x86_64 mathsat
```

In macOS, the following command is required:

```
ln -s /usr/local/include/gmp.h mathsat/include/gmp.h
```

If you need more details on MathSAT, please refer to [its webpage](https://mathsat.fbk.eu).

### Setting Up Yices

First, we need to setup and build [GMP library](https://gmplib.org), by entering the following command (Linux only):

```
wget https://gmplib.org/download/gmp/gmp-6.1.2.tar.xz && tar xf gmp-6.1.2.tar.xz && rm gmp-6.1.2.tar.xz && cd gmp-6.1.2 && ./configure --prefix $PWD/../gmp --disable-shared ABI=64 CFLAGS=-fPIC CPPFLAGS=-DPIC && make -j4 && make install && cd ..
```

Then, we are able build and setup Yices 2 using the following command:

```
Linux:
git clone https://github.com/SRI-CSL/yices2.git && cd yices2 && git checkout Yices-2.6.1 && autoreconf -fi && ./configure --prefix $PWD/../yices --with-static-gmp=$PWD/../gmp/lib/libgmp.a && make -j9 && make static-lib && make install && cp ./build/x86_64-pc-linux-gnu-release/static_lib/libyices.a ../yices/lib && cd ..

macOS:
git clone https://github.com/SRI-CSL/yices2.git && cd yices2 && git checkout Yices-2.6.1 && autoreconf -fi && ./configure --prefix $PWD/../yices && make -j9 && make static-lib && make install && cp ./build/x86_64-apple-darwin*release/static_lib/libyices.a ../yices/lib
```

If you need more details on Yices 2, please refer to [its Github](https://github.com/SRI-CSL/yices2).

### Setting Up Z3

We have wrapped the entire build and setup of Z3 in the following command:

```
Linux:
wget https://github.com/Z3Prover/z3/releases/download/z3-4.8.9/z3-4.8.9-x64-ubuntu-16.04.zip && unzip z3-4.8.9-x64-ubuntu-16.04.zip && mv z3-4.8.9-x64-ubuntu-16.04 z3

macOS:
brew install z3
```

If you need more details on Z3, please refer to [its Github](https://github.com/Z3Prover/z3).

If you are using macOS and have installed z3 using brew, you'll need to copy the entire z3 directory from the place where Homebrew keeps packages to your workspace:

```
cp -rp $(brew info z3 | egrep "/usr[/a-zA-Z\.0-9]+ " -o) z3
```

### Setting Up Bitwuzla

We have wrapped the entire build and setup of Bitwuzla in the following command:

```
Linux:
git clone --depth=1 --branch=smtcomp-2021 https://github.com/bitwuzla/bitwuzla.git && cd bitwuzla && ./contrib/setup-lingeling.sh && ./contrib/setup-btor2tools.sh && ./contrib/setup-symfpu.sh && ./configure.sh --prefix $PWD/../bitwuzla-release && cd build && cmake -DGMP_INCLUDE_DIR=$PWD/../../gmp/include -DGMP_LIBRARIES=$PWD/../../gmp/lib/libgmp.a -DONLY_LINGELING=ON ../ && make -j8 && make install && cd .. && cd ..

macOS:
git clone --depth=1 --branch=smtcomp-2021 https://github.com/bitwuzla/bitwuzla.git && cd bitwuzla && ./contrib/setup-lingeling.sh && ./contrib/setup-btor2tools.sh && ./contrib/setup-symfpu.sh && ./configure.sh --prefix $PWD/../bitwuzla-release && cd build && cmake -DONLY_LINGELING=ON ../ && make -j8 && make install && cd .. && cd ..
```

If you need more details on Bitwuzla, please refer to [its Github](https://github.com/bitwuzla/bitwuzla).

Before proceeding to the next section, make sure you have clang, LLVM and all the solvers ready in your workspace:

```
clang and LLVM:
<path_to_your_project>/ESBMC_Project/clang11

Boolector:
<path_to_your_project>/ESBMC_Project/boolector

CVC4: (Linux only)
<path_to_your_project>/ESBMC_Project/CVC4

MathSAT:
<path_to_your_project>/ESBMC_Project/mathsat

Yices:
<path_to_your_project>/ESBMC_Project/yices

Z3:
<path_to_your_project>/ESBMC_Project/z3

Bitwuzla:
<path_to_your_project>/ESBMC_Project/bitwuzla
```

The above paths will be used in ESBMC's build command in the next section.

If you are using macOS, make sure you have Xcode.app in your Applications directory by checking:

```
ls /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/
```

If no such directory, please go to App Store and install Xcode. If you do not have the full version of Xcode, you will need to replace the flag `C2GOTO` with:

```
-DC2GOTO_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
```

## Building ESBMC

Now we are ready to build ESBMC. Please note that we describe the same build option used in our CI/CD. If you want to all available _cmake_ options, refer to our [Options.cmake file](https://github.com/esbmc/esbmc/blob/master/scripts/cmake/Options.cmake).

First, we need to setup __cmake__, by using the following command in ESBMC_Project directory you just created:

```
Linux:
cd esbmc && mkdir build && cd build && cmake .. -GNinja -DBUILD_TESTING=On -DENABLE_REGRESSION=On -DClang_DIR=$PWD/../../clang11 -DLLVM_DIR=$PWD/../../clang11 -DBUILD_STATIC=On -DBoolector_DIR=$PWD/../../boolector-release -DZ3_DIR=$PWD/../../z3 -DENABLE_MATHSAT=ON -DMathsat_DIR=$PWD/../../mathsat -DENABLE_YICES=On -DYices_DIR=$PWD/../../yices -DCVC4_DIR=$PWD/../../cvc4 -DGMP_DIR=$PWD/../../gmp -DBitwuzla_DIR=$PWD/../../bitwuzla-release -DCMAKE_INSTALL_PREFIX:PATH=$PWD/../../release

macOS:
cd esbmc && mkdir build && cd build && cmake .. -GNinja -DBUILD_TESTING=On -DENABLE_REGRESSION=On -DBUILD_STATIC=On -DClang_DIR=$PWD/../../clang11 -DLLVM_DIR=$PWD/../../clang11 -DBoolector_DIR=$PWD/../../boolector-release -DZ3_DIR=$PWD/../../z3 -DENABLE_MATHSAT=On -DMathsat_DIR=$PWD/../../mathsat -DENABLE_YICES=ON -DYices_DIR=$PWD/../../yices -DC2GOTO_SYSROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk -DBitwuzla_DIR=$PWD/../../bitwuzla-release -DCMAKE_INSTALL_PREFIX:PATH=$PWD/../../release
```

Finally, we can trigger the build process, by using the following command:

```
cmake --build . && ninja install
```

Once it is finished, ESBMC should be available in the _release_ folder.

## Notes on old frontends

In addition to the clang frontends, ESBMC also has two old frontends, `src/ansi-c` for C and `src/cpp` for C++. These old frontends can be built with __cmake__ flag `-DENABLE_OLD_FRONTEND=On`. To use the old frontend for verification, use the ESBMC command line option `--old-frontend`.

# ESBMC Shared Builds

It is also possible to build a non-static version of ESBMC that links to
system libraries and/or solvers installed system-wide.

Shared linking is the default when __cmake__ is invoked with
`-DBUILD_STATIC=Off` or the variable is not set.

## Notes on shared Clang

When __clang__ has been built with the __cmake__ setting
`CLANG_LINK_CLANG_DYLIB=On`, the default is to link against the dynamic library
`libclang-cpp` and also to not bundle __clang__'s header files.
This bundling can be controlled by setting the __cmake__ variable
`CLANG_HEADERS_BUNDLED` to `On` or `Off` instead of its default `detect`.

`On` means that the headers are always bundled in ESBMC's executables, and
`Off` never bundles the headers, but hard-codes their path. Not bundling headers
when linking shared against a system installation of __clang__ speeds up ESBMC's
translation of source files by avoiding the creation of a temporary directory
and extracting the bundled headers to it. As is common with runtime dependencies
on shared libraries, when __clang__ is updated to a new version, ESBMC will need
to be recompiled.

It is advised to keep the default setting `detect` for `CLANG_HEADERS_BUNDLED`,
which decides to bundle headers only when linking statically against __clang__.

# ESBMC 
## the Efficient SMT-based Bounded Model Checker

![Build All Solvers](https://github.com/esbmc/esbmc/workflows/Build%20All%20Solvers/badge.svg)
[![codecov](https://codecov.io/gh/esbmc/esbmc/branch/master/graph/badge.svg)](https://codecov.io/gh/esbmc/esbmc)

ESBMC (the Efficient SMT-based Bounded Model Checker) is a mature, permissively licensed open-source context-bounded model checker for verifying single- and multithreaded C/C++ programs. It can verify both predefined safety properties (e.g., bounds check, pointer safety, overflow) and user-defined program assertions automatically. ESBMC supports the Clang compiler as its C/C++ frontend, IEEE floating-point arithmetic for various SMT solvers, implements the Solidity grammar production rules as its Solidity frontend and state-of-the-art incremental BMC and k-induction algorithms.

To build ESBMC, please see the [BUILDING](https://github.com/esbmc/esbmc/blob/master/BUILDING.md) file. For getting started, we recommend first reading some of the background material / publications, to understand exactly what this technique can provide, for example our SV-COMP papers, which are available [online](https://ssvlab.github.io/esbmc/publications.html).

We also provide a short video that explains ESBMC: 

https://www.youtube.com/watch?v=uJ5Jn0sxm08&t=2182s

This video was delivered as part of a technical talk on exploiting the SAT revolution for automated software verification in a workshop between Arm Research and the University of Manchester.

We offer a post-graduate course in software security that explains the internals of ESBMC. 

https://ssvlab.github.io/lucasccordeiro/courses/2020/01/software-security/index.html

This course unit introduces students to basic and advanced approaches to formally build verified trustworthy software systems, where trustworthy comprise five attributes: *reliability*, *availability*, *safety*, *resilience* and *security*.

The cannonical public location of ESBMCs source is on github:

    https://github.com/esbmc/esbmc

While our main website is [esbmc.org](http://esbmc.org)

### Publications

* Rafael Menezes, Daniel Moura, Helena Cavalcante, Rosiane de Freitas, Lucas C. Cordeiro . [ESBMC-Jimple: verifying Kotlin programs via jimple intermediate representation](https://dl.acm.org/doi/abs/10.1145/3533767.3543294) In ISSTA'22, pp. 777-780, 2022. [DOI](https://doi.org/10.1145/3533767.3543294)

* Franz Brauße, Fedor Shmarov, Rafael Menezes, Mikhail R. Gadelha, Konstantin Korovin, Giles Reger, Lucas C. Cordeiro. [ESBMC-CHERI: towards verification of C programs for CHERI platforms with ESBMC](https://dl.acm.org/doi/abs/10.1145/3533767.3543289) In ISSTA'22, pp. 773-776, 2022. [DOI](https://doi.org/10.1145/3533767.3543289) 

* Felipe R. Monteiro, Mikhail R. Gadelha, Lucas C. Cordeiro. [Model checking C++ programs.](https://onlinelibrary.wiley.com/doi/epdf/10.1002/stvr.1793) In Softw. Test. Verification Reliab. 32(1), 2022. [DOI](https://doi.org/10.1002/stvr.1793), [Video](https://www.youtube.com/watch?v=cX31c976tjM), **Open access**.

* Mikhail R. Gadelha, Lucas C. Cordeiro, Denis A. Nicole. [An Efficient Floating-Point Bit-Blasting API for Verifying C Programs.](https://ssvlab.github.io/lucasccordeiro/papers/nsv2020.pdf) In VSTTE, pp. 178-195, 2020. [DOI](https://doi.org/10.1007/978-3-030-63618-0_11)

* Mikhail Y. R. Gadelha, Felipe R. Monteiro, Jeremy Morse, Lucas C. Cordeiro, Bernd Fischer, Denis A. Nicole. [ESBMC 5.0: an industrial-strength C model checker.](https://ssvlab.github.io/lucasccordeiro/papers/ase2018.pdf) In ASE, pp. 888-891, 2018. [DOI](https://doi.org/10.1145/3238147.3240481) 

* Jeremy Morse, Lucas C. Cordeiro, Denis A. Nicole, Bernd Fischer. [Model checking LTL properties over ANSI-C programs with bounded traces.](https://ssvlab.github.io/lucasccordeiro/papers/sosym2013.pdf) In Softw. Syst. Model. 14(1), pp. 65-81, 2015. [DOI](https://doi.org/10.1007/s10270-013-0366-0)

* Mikhail Y. R. Gadelha, Hussama Ibrahim Ismail, Lucas C. Cordeiro. [Handling loops in bounded model checking of C programs via k-induction.](https://ssvlab.github.io/lucasccordeiro/papers/sttt2017.pdf) In Int. J. Softw. Tools Technol. Transf. 19(1), pp. 97-114, 2017. [DOI](https://doi.org/10.1007/s10009-015-0407-9)

* Phillipe A. Pereira, Higo F. Albuquerque, Isabela da Silva, Hendrio Marques, Felipe R. Monteiro, Ricardo Ferreira, Lucas C. Cordeiro. [SMT-based context-bounded model checking for CUDA programs.](https://ssvlab.github.io/lucasccordeiro/papers/cppe2017.pdf) In Concurr. Comput. Pract. Exp. 29(22), 2017. [DOI](https://doi.org/10.1002/cpe.3934)

* Lucas C. Cordeiro, Bernd Fischer, João Marques-Silva. [SMT-Based Bounded Model Checking for Embedded ANSI-C Software.](https://ssvlab.github.io/lucasccordeiro/papers/tse2012.pdf) In IEEE Trans. Software Eng. 38(4), pp. 957-974, 2012. [DOI](https://doi.org/10.1109/TSE.2011.59)

* Lucas C. Cordeiro, Bernd Fischer. [Verifying multi-threaded software using smt-based context-bounded model checking.](https://ssvlab.github.io/lucasccordeiro/papers/icse2011.pdf) In ICSE, pp. 331-340, 2011. [DOI](https://doi.org/10.1145/1985793.1985839)

### Getting Started

- [Building from Source](docs/BUILDING.md)
- [How to Contribute](docs/CONTRIBUTING.md)

### Features

ESBMC aims to support all of C99/C11, and detects errors in software by simulating a finite prefix of the program execution with all possible inputs. Classes of problems that can be detected include:
 * User specified assertion failures
 * Out of bounds array access
 * Illegal pointer dereferences, such as:
   * Dereferencing null
   * Performing an out-of bounds dereference
   * Double-free of malloc'd memory
   * Misaligned memory access
 * Integer overflows
 * Divide by zero
 * Memory leaks

Concurrent software (using the pthread api) is verified by explicit exploration of interleavings, producing one symbolic execution per interleaving. By default, pointer-safety, array-out-of-bounds, division-by-zero, and user-specified assertions will be checked for; one can also specify options to check concurrent programs for:
 * Deadlock (only on pthread mutexes and convars)
 * Data races (i.e. competing writes)
 * Atomicity violations at visible assignments
 * Lock acquisition ordering

By default ESBMC performs a "lazy" depth first search of interleavings -- it can also encode (explicitly) all interleavings into a single SMT formula.

A number of SMT solvers are currently supported:
 * Z3 4.8+
 * Bitwuzla
 * Boolector 3.0+
 * MathSAT
 * CVC4
 * Yices 2.2+

In addition, ESBMC can be configured to use the SMTLIB interactive text format with a pipe, to communicate with an arbitary solver process, although not-insignificant overheads are involved.

A limited subset of C++98 is supported too -- a library modelling the STL is also available.

### Differences from CBMC

ESBMC is a fork of CBMC v2.9 (2008), the C Bounded Model Checker. The primary differences between the two are:

* CBMC focuses on SAT-based encodings of unrolled programs while ESBMC targets SMT-based encodings.
* CBMC's concurrency support is an entirely symbolic encoding of a concurrent program in one SAT formulae, while ESBMC explores each interleaving individually.
* CBMC uses a modified C parser written by James Roskind and a C++ parser based on OpenC++, while ESBMC relies on the Clang front-end.
* ESBMC implements the Solidity grammar production rules as its Solidity frontend, while CBMC does not implement a Solidity frontend.
* CBMC implements k-induction, requiring three different calls: to generate the CFG, to annotate the program, and to verify it, whereas ESBMC handles the whole process in a single call. Additionally, CBMC does not have a forward condition to check if all states were reached and relies on a limited loop unwinding.
* ESBMC adds some additional types to the program's internal representation.

# Open source

ESBMC has now been released as open source software -- mainly distributed under the terms of the Apache License 2.0. ESBMC contains a signficant amount of other peoples software, however, please see the COPYING file for an explanation of who-owns-what, and under what terms they are distributed.

We'd be extremely happy to receive contributions to make ESBMC better (under the terms of the Apache License 2.0). If you'd like to submit anything, please file a pull request against the public github repo. General discussion and release announcements will be made via GitHub. To contact us about research or collaboration, please post an issue in GitHub.

### Documentation

A limited number of classes have been marked up with doxygen documentation headers. Comments are put in the header files declaring classes and methods. HTML documation can be generated by running:

    doxygen .doxygen

Output will be in docs/html, open index.html to get started.

### Acknowledgments

The ESBMC development was supported by various research funding agencies, including CNPq (Brazil), CAPES (Brazil), FAPEAM (Brazil), EPSRC (UK), Royal Society (UK), British Council (UK), European Commission (Horizon 2020), and companies including Intel, Motorola, Nokia Institute of Technology and Samsung. ESBMC is currently funded by Intel, EPSRC grants [EP/T026995/1](https://enncore.github.io), [EP/V000497/1](https://scorch-project.github.io), [EU H2020 ELEGANT 957286](https://www.elegant-h2020.eu) and [Soteria project](https://soteriaresearch.org) awarded by the UK Research and Innovation for the Digital Security by Design (DSbD) Programme.

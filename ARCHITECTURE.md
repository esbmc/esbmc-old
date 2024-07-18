### Architecture

The figure below illustrates the current ESBMC architecture. The tool inputs a C/C++/CUDA, Java/Kotlin, Solidity, or CHERI-C program, then converts an abstract syntax tree (AST) into a state transition system called a GOTO program. Its symbolic execution engine unrolls the GOTO program and generates a sequence of static single assignments (SSAs). The SSAs are then converted to an SMT formula, which is satisfiable if and only if the program contains errors.

![esbmc-architecture-v3](https://github.com/esbmc/esbmc/assets/3694109/fe609179-14bc-4f3d-b507-a3b003f732ae)

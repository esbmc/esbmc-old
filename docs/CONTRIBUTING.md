# Contributing


## First time contributors 

Welcome to the project! If you wish to help the project but don't know where to start. Here are some tips:

- Look for "good-first-issue" [tasks](https://github.com/esbmc/esbmc/contribute). Those tasks usually don't require much knowledge about esbmc internals.

- If you are new to programming, you can try documenting a class. That task is not hard (but it is heavy work), and you will learn a lot in the process.

- Another taks for new programmers is refactoring. The old code does not follow some of the rules written in this document.

- There are a couple of linter issues on all modules: cmake, python, even Readme.

- Functionalities to help devs: git-hooks, CI/CD scripts, testing, etc. Just open an issue asking before start coding

- Improve our guides and noncode documentation. This is mainly to help users to learn how to use the tool.

Be reasonable when arguing with the reviewer. Sometimes they may require something that goes beyond some of the rules written in this document.

### Contributing to the code base

Here are some steps to contributing to the code base:

  1. Compile and execute esbmc. [Building](https://github.com/esbmc/esbmc/blob/master/BUILDING.md)
  1. Fork the repository
  1. Clone the repository git clone git@github.com:YOURNAME/esbmc.git
  1. Create a branch from the master branch (default branch)
  1. Make your changes
  1. Check the formatting with clang-format (use Clang 11)
  1. Push your changes to your branch
  1. Create a Pull Request targeting the master branch

Here is an example to prepare a pull request (PR)


A) Make sure that you are in the `master` branch and your fork is updated.

```
git checkout master
git fetch upstream
git pull --rebase upstream master
git push origin HEAD:master
```

Note that if you have not yet setup the `upstream`, you need to type the following command:

```
git remote add upstream https://github.com/esbmc/esbmc
```

B) Create a local branch (e.g., `model-pthread-create`) from the `master` branch:

```
git checkout -b model-pthread-equal --track master
```

C) Add your changes via commits to the local branch:

```
git add path-to-file/file.cpp
git commit -sm "added opertational model for pthread_equal"
```

Note that you can check your changes via `git status`.
Note also that every PR should contain at least two test cases
to check your implementation: one successful and one failed test case.

D) Push your changes in the local branch to the ESBMC repository:

```
git push origin model-pthread-equal
```

New contributors can check issues marked with `good first issue` by clicking [here].


## Pull Requests

### Submission summary

To help the Reviewing, PRs **must**:

- Link an issue.
- Add a summary of the changes.
- Explain how to test/evaluate the changes.
- Have acceptance tests for it (if applicable).
- Compile the changes on top of the master.
- Never do a merge commit (always rebase).

Also, PRs should:
- Have unit/fuzz tests
- Add notes (TODO, note, etc.) pointing where new features or bugs could be fixed.
- Commit messages should contains tags based on the modules that it changes, e.g. [symex] improved __ESBMC_assert intrinsic
- Commits pointing to the issue number

### Merging Pull Requests

To merge a PR, it must:
1. Pass the CI
1. Have the approval of at least two reviewers. Note that there is one main exception
   here: If nobody replies to the PR in a 1 week frame then the PR can be self-reviewed
   and merged
1. If another reviewer has put a comment, then it must be addressed before merged.
   Exceptions of this rule are in the Reviewing section

### Reviewing

This should be done in two steps:
1. First review: check the Code Style, Policies and overall functionality.
1. Code review: evaluate the maintainability and code quality. 

Discussions are encouraged; however, if the author and the reviewer disagree, we have two options: (1) the reviewer will implement his/her comment and/or (2) another reviewer will make the final decision. 

Always be reasonable when asking for changes. If you ask for something beyond this document’s scope, you may also need to open a discussion to update it. Also, try to provide explanations when you are requesting something too abstract e.g. show an example, an item from this guideline or link to a documentation.

Also, always check what is the **scope** of the PR. Avoid asking changes that go beyond it.


#### First Review

1. Does the code Follow the Standards defined in this document?
1. Is the CI complaining? If so, is it because of the PR? If not, 
   then this shouldn't block the merge (although it should be fixed asap). 
   When this happens, one reviewer must manually compile and confirm 
   that it is working.
1. Check if the functionality is tested
1. On WiP PRs, if the contributor asked for a review, ask for the author to put a to-do list

### Code Review

1. Review each code line, paying extra attention to corner cases
1. If any changes are workarounds, see that it has an issue attached to it.
1. The established way of doing things should be maintained. To change the established way, an issue must be created and the current PR shouldn't be put on hold.
1. Check if the commit history is clean.

### Dealing with regressions

The master branch should always be in a better state than the previous patch. If, for some reason, a regression happened, then the priority should be to fix it. Either by reverting the change or finding a better solution.

## Policies and Code Style

Here are all the do's and don'ts of the Code. Dead simply rules that can be pointed out e.g., "the Code must compile with werror".

### Don’t do functions with hundreds of LoC.

Avoid creating functions that do more than one thing. This makes reviewing and testing harder than necessary.

### Keep modules short

It is not reasonable to have a file with 2k lines of code. This breaks compilers and analysis.

### Don’t make use of if/elseif/.../elseif/else pattern
The pattern is slow (and hard to maintain) and can be replaced with OOP or HashMaps

### Always use clang-format based on the latest LLVM version that the project supports

At the time of this writing, we are using LLVM 11. So clang-format-11. This should be reasonable as is the same version that you used to build ESBMC. If you are running bash, you can use:

```bash
find src -iname *.h -o -iname *.c -o -iname *.cpp -o -iname *.hpp -iname *.hh | grep -v src/ansi-c/cpp | grep -v src/clang-c-frontend/headers/ | xargs clang-format-11 -style=file -i -fallback-style=none

find unit -iname *.h -o -iname *.c -o -iname *.cpp -o -iname *.hpp -iname *.hh | grep -v src/ansi-c/cpp | grep -v src/clang-c-frontend/headers/ | xargs clang-format-11 -style=file -i -fallback-style=none
```

### Don’t create C-like APIs
Avoid creating an isolated function that does not belong to a class (or namespace). If it is a single-use function, encapsulate it in an anonymous namespace.

```cpp
namespace {
  void single_use_function() { ... }
}

void some_class::some_method() {
  ...
  single_use_function();
  ...
}
```

### Don’t use one time only variables
Avoid using patterns such as `int a = 42; return a;` when you could have done `return 42;`

### Don’t add curly brackets to 1-instruction block
This means that if/while/do-while statements that have only one instruction inside, shouldn’t be encapsulated by { }

For instance, don't use:

```c
if(...) { foo(); }
```

When you could:
```c
if(...) foo();
```

### Function names should tell what the function does
Name the function based on what you expect it to do: `make_sandwich(vector<string> &igredients)`

### New functionalities should work as “passes” on the parsing tree, GOTO and SSA
This means that if you want to use a new analysis on the frontend (tree), cfg (GOTO), or symex (SSA) you should wrap it in an LLVM-like pass. See loop_unroll.h for an example. 

### Ensure that new functionalities works with `--no-simplify`
The simplification algorithm cuts down expressions, this means that your code could construct an SMT formula that would create an invalid SMT formula if not for it. This is needed because if there is a bug in the simplifier module, it should be easy to fix

### Do not use else when the if ends with a return statement.
There is no need for an else when the if block exits the function.

Don't

```c
… // some code
if(condition) return 0;
else { 
  … //else code
 }
… // more code
```
Instead, remove the else block:

```c
.../ some code
if(condition) return 0;

...// else code
...// more code
```





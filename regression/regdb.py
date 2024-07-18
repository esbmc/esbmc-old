#!/usr/bin/env python3

from testing_tool import (TestCase, apply_transform_over_tests, FAIL_MODES)

import sys
import os.path
import getopt
import shlex
import fnmatch

SOLVERS = [
    'smtlib',
    'z3',
    'boolector',
    'cvc',
    'cvc4',
    'cvc5',
    'mathsat',
    'yices',
    'bitwuzla'
]

EXTENSIONS = {
    'lang=c'     : {'c', 'i'},
    'lang=cxx'   : {'cc', 'cpp'},
    'lang=cuda'  : {'cu'},
    'lang=py'    : {'py'},
    'lang=sol'   : {'solast'},
    'lang=jimple': {'jimple'},
}

OPT2FLAGS = {
    '--' + solver: {'solver=' + solver} for solver in SOLVERS
} | { # Frontend related
    '--16'           : {'bitw=16'},
    '--32'           : {'bitw=32'},
    '--64'           : {'bitw=64'},
    '--binary'       : {'goto'},
    '--little-endian': {'endian=little'},
    '--big-endian'   : {'endian=big'},
    '--no-arch'      : {'endian=none', 'arch=none'},
    '--ppc-macos'    : {'arch=ppc', 'os=macos', 'bitw=32'},
    '--i386-macos'   : {'arch=x86', 'os=macos', 'bitw=32'},
    '--i386-linux'   : {'arch=x86', 'os=linux', 'bitw=32'},
    '--i386-win32'   : {'arch=x86', 'os=win', 'bitw=32'},
    '--cheri'        : {'cheri'},
    '--old-frontend' : {'old'},
} | { # Strategy related
    '--k-induction'    : {'strat=kind'},
    '--incremental-bmc': {'strat=incr'},
    '--falsification'  : {'strat=falsify'},
    '--termination'    : {'strat=term'},
    '--ltl'            : {'strat=ltl'},
} | { # Optimiziation related
    '--interval-analysis': {'ia'},
    '--gcse'             : {'gcse'},
}

ARGOPTS2FLAGS = {
    '--default-solver': {'default-solver=*'},
    '--std'           : {'std=*'},
}

BUG = 'bug'

FLAG_DESC = {}

class Flag(str):
    def __new__(cls, s):
        assert(len(s) > 0)
        return super().__new__(cls, s)

    @property
    def value(self):
        s = super().__str__()
        return s[s.find('=') + 1:]

    @property
    def category(self):
        s = super().__str__()
        eq = s.find('=')
        return None if eq < 0 else s[:eq]

    def matches(self, gpat_flag):
        smatch = fnmatch.fnmatchcase
        scat = self.category
        pcat = gpat_flag.category
        return ((pcat is None or scat is not None and smatch(scat, pcat)) and
                smatch(self.value, gpat_flag.value))

    def __repr__(self):
        return "Flag(%s)" % super().__repr__()

def list_flags(verbosity : int):
    # collect
    flgs = {BUG} | EXTENSIONS.keys()
    for v in OPT2FLAGS.values():
        flgs |= v
    for v in ARGOPTS2FLAGS.values():
        flgs |= v
    # output
    if verbosity > 0:
        max_w = max(len(f) for f in flgs)
        for f in sorted(flgs):
            print('%*s: %s' % (-max_w, f, FLAG_DESC.get(f, '<undocumented>')))
    else:
        for f in sorted(flgs):
            print(f)
    # finish
    sys.exit(0)

def flags(tc : TestCase):
    r = set()
    if tc.test_mode in FAIL_MODES:
        r.add(Flag(BUG))
    opts = []
    for arg in tc.generate_run_argument_list('true')[1:]:
        if os.path.exists(arg) and os.path.isfile(os.path.realpath(arg)):
            # probably an input file
            ext = arg[arg.rfind('.') + 1:]
            for lang, exts in EXTENSIONS.items():
                if ext in exts:
                    r |= {Flag(lang)}
                    break
        else:
            # not a file, so it's an option; split options containing '='
            eq = arg.find('=')
            if eq != -1:
                opts.append(arg[:eq])
            opts.append(arg[eq + 1:])
    i = 0
    while i < len(opts):
        f = set()
        opt = opts[i]
        if opt in ARGOPTS2FLAGS: # option requires an argument
            f = {Flag(g.replace('*', opts[i+1])) for g in ARGOPTS2FLAGS[opt]}
            i += 2
        else:
            f = {Flag(g) for g in OPT2FLAGS.get(opt, f)}
            i += 1
        # add all the new flags to the ones we already accumulated
        r |= f
    return r

def query(qstr, tcs):
    # parse query
    pos = set()
    neg = set()
    for spec in qstr.split(' '):
        if spec[0] == '-':
            neg.add(Flag(spec[1:]))
        else:
            pos.add(Flag(spec[1:] if spec[0] == '+' else spec))

    # process TCs
    def qualify(flgs):
        def matches_any(gpat_flag):
            return any(f.matches(gpat_flag) for f in flgs)

        return (all(matches_any(p) for p in pos) and
                all(not matches_any(n) for n in neg))

    return [tc for tc in tcs if qualify(flags(tc))]

def usage():
    print('usage: %s [-OPTS] [--] [REG_PATH [REG_PATH [...]]]' %
          (sys.argv[0] if len(sys.argv) > 0 else 'regdb.py'))
    print('''
Options:
  -h          display this help message
  -l          list known flags
  -q QUERY    output only tests matching QUERY, multiple queries can be
              performed and each will further restrict the list of results
  -v          increase verbosity (display flags for each test case or
              description for each flag), use twice to show shell-escaped
              cmdline options instead of flags

Multiple paths can optionally be specified as REG_PATH parameters, which will
restrict the test cases to process to only those given. If none are specified,
the default is to search the built-in list of all non-disabled regression tests.

QUERY is a space-separated list of flag patterns of the form:
  [+]GPAT : match only TCs that have a flag matching the glob pattern GPAT
    -GPAT : match only TCs that do not have any flag matching GPAT
All flag patterns are conjunctively connected.
'''[:-1])
    sys.exit(0)

def main():
    try:
        opts, args = getopt.gnu_getopt(sys.argv[1:], 'hlq:v')
    except getopt.GetoptError as err:
        print(err, file=sys.stderr)
        sys.exit(1)

    verbosity = len(tuple(opt for opt, val in opts if opt == '-v'))

    if ('-h', '') in opts:
        usage()

    if ('-l', '') in opts:
        list_flags(verbosity)

    tcs = []
    if args:
        for arg in args:
            tcs.append(TestCase(arg, os.path.basename(arg)))
    else:
        apply_transform_over_tests(tcs.append)

    combined_query = ' '.join(val for opt, val in opts if opt == '-q').strip()
    if combined_query:
        tcs = query(combined_query, tcs)

    if len(tcs) == 0 or verbosity == 0:
        # just directory
        for tc in tcs:
            print(tc.test_dir)
    else:
        max_w = max(len(tc.test_dir) for tc in tcs)
        for tc in tcs:
            # directory + flags or directory + cmdline options
            value = (' '.join(sorted(flags(tc))) if verbosity == 1 else
                     shlex.join(tc.generate_run_argument_list('true')[1:]))
            print('%*s: %s' % (-max_w, tc.test_dir, value))

if __name__ == '__main__':
    main()

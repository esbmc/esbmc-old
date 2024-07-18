#!/usr/bin/env python3

import sys, os
import shutil  # For git
import subprocess  # For git
from datetime import datetime  # For date
import getpass  # For username
import socket  # For hostname
from typing import Union


class BuildObj:
    """
        Build Obj will generate an string representing the current release

        The output file should be in the format:

        "ESBMC built from <git-hash> <date> by <username>@<hostname> (dirty-tree)?" 
    """
    STR_ESBMC_BUILT_FROM = "ESBMC built from "
    STR_BY = "by"
    STR_AT = "@"
    STR_DIRTY = "(dirty tree)"
    STR_NOT_GIT = "no-hash"

    @staticmethod
    def try_git_command(git_command_args: list[str]) -> Union[str, bool]:
        git = shutil.which("git")
        if git is None:
            return False

        try:
            cmd = [git]
            cmd.extend(git_command_args)
            return subprocess.check_output(cmd, cwd=os.path.dirname(__file__)).decode()
        except subprocess.CalledProcessError as e:
            # git rev-parse returns 128 if not in a git repository
            if e.returncode == 128:
                return False
            else:
                raise e

    @staticmethod
    def get_last_hash() -> str:
        """Return the hash of the latest commit"""
        result = BuildObj.try_git_command(["rev-parse", "HEAD"])
        if isinstance(result, str):
            return result.strip()
        return BuildObj.STR_NOT_GIT

    @staticmethod
    def get_datetime() -> str:
        """Try to simulate the `date` command"""
        output = datetime.now()
        return str(output)

    @staticmethod
    def get_username() -> str:
        return str(getpass.getuser())

    @staticmethod
    def get_hostname() -> str:
        return str(socket.gethostname())

    @staticmethod
    def is_dirty_tree() -> bool:
        result = BuildObj.try_git_command(["status", "-s"])
        if isinstance(result, str):
            for x in result.splitlines():
                if "??" not in x:
                    return True

            return False
        return True

    @staticmethod
    def run(output):
        with open(output, 'w') as f:
            f.write(f'{BuildObj.STR_ESBMC_BUILT_FROM} ')
            f.write(f'{BuildObj.get_last_hash()} ')
            f.write(f'{BuildObj.get_datetime()} ')
            f.write(f'{BuildObj.STR_BY} ')
            f.write(f'{BuildObj.get_username()}')
            f.write(f'{BuildObj.STR_AT}')
            f.write(f'{BuildObj.get_hostname()}')
            if BuildObj.is_dirty_tree():
                f.write(f' {BuildObj.STR_DIRTY}')


def main():
    if len(sys.argv) != 2:
        raise ValueError("Program expects <output>")

    output = sys.argv[1]
    BuildObj.run(output)


if __name__ == "__main__":
    main()

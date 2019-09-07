#!/bin/bash

> ~/.git-bash.log

onExec() {
  echo -e "$(date +%s)\tGITBASH\t$PWD\t$BASH_COMMAND" \
    >> ~/git-bash.log
}

trap onExec DEBUG





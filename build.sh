#!/bin/sh
# Build FlowTask.exe (Windows x64) using MinGW-w64.
set -e

CC=${CC:-x86_64-w64-mingw32-gcc}
RC=${RC:-x86_64-w64-mingw32-windres}

"$RC" -I res res/app.rc -O coff -o app.res
"$CC" -O2 -mwindows -o FlowTask.exe src/main.c app.res -static \
      -lcomctl32 -lcomdlg32 -lmsimg32 -lwinmm

echo "built: FlowTask.exe"

alloc=$1
bench=$2
procs=$3
TCMALLOC_TEMERAIRE=0 TCMALLOC_TEMERAIRE_WITH_SUBRELEASE_V3=0 MALLOC_CONF="dss:disabled,retain:false" MIMALLOC_ARENA_EAGER_COMMIT=0 MIMALLOC_PURGE_DELAY=0 strace -o "trace.$alloc.$bench.raw.$procs" -f -qqq -tt -e mmap,mprotect ../../bench.sh $alloc $bench --procs=$procs
python filter.py "trace.$alloc.$bench.raw.$procs" > ./trace_select/"trace.$alloc.$bench.$procs"
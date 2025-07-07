allocs=("mi" "je" "tcg" "sys")
# allocs=("mi")
benchs=("redis" "lean" "larson" "lua" "sh8bench" "rptest" "mstress" "xmalloc-test")

# benchs=("redis" "cfrac" "lean" "espresso" "gs" "larson" 
#         "lua" "alloc-test" "sh8bench" "glibc-thread" "rptest" "mstress" "xmalloc-test")

for alloc in "${allocs[@]}"; do
    for bench in "${benchs[@]}"; do
       ./collect_bench.sh $alloc $bench 16
    done
done
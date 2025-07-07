include_directories(${PROJECT_SOURCE_DIR}/include)

add_executable(microbench_common microbench_common.cc)

target_link_libraries(microbench_common
    mralloc
    ibverbs
    rdmacm
    rt
    profiler
    memkind
    hiredis
)

add_executable(v_microbench v_microbench.cc)

target_link_libraries(v_microbench
    mralloc
    ibverbs
    rdmacm
    rt
    profiler
    memkind
    hiredis
)

add_executable(input_microbench input_microbench.cc)

target_link_libraries(input_microbench
    mralloc
    ibverbs
    rdmacm
    rt
    profiler
    memkind
    hiredis
)

add_executable(multiple_microbench multiple_microbench.cc)

target_link_libraries(multiple_microbench
    mralloc
    ibverbs
    rdmacm
    rt
    profiler
    memkind
    hiredis
)
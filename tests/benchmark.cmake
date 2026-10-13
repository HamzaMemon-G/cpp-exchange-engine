foreach(profile mixed deep cancel-heavy sweep id-churn)
  execute_process(COMMAND "${BENCH}" --profile "${profile}" --levels 4 --depth 2
    --orders 100 --samples 2 --seed 0 --latency RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0 OR NOT out MATCHES "profile=${profile} commands=100" OR
     NOT out MATCHES "median_commands_per_second=" OR NOT out MATCHES "instrumented_latency_ns")
    message(FATAL_ERROR "benchmark profile ${profile} failed: ${rc} ${out} ${err}")
  endif()
  execute_process(COMMAND "${BENCH}" --profile "${profile}" --levels 4 --depth 2 --orders 100
    --memory-only RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0 OR NOT out MATCHES "memory phase=after_commands" OR
     out MATCHES "median_commands_per_second=")
    message(FATAL_ERROR "memory profile ${profile} failed: ${rc} ${out} ${err}")
  endif()
endforeach()
foreach(arguments "--orders;0" "--levels;0" "--depth;0" "--samples;0" "--profile;bad"
                  "--orders;10000001" "--memory-only;--latency" "--seed;-1" "--unknown" "--orders")
  execute_process(COMMAND "${BENCH}" ${arguments} RESULT_VARIABLE rc ERROR_VARIABLE err)
  if(NOT rc EQUAL 1 OR err STREQUAL "")
    message(FATAL_ERROR "benchmark invalid options accepted: ${arguments}")
  endif()
endforeach()
execute_process(COMMAND "${GENERATOR}" --profile id-churn --orders 4 RESULT_VARIABLE rc OUTPUT_VARIABLE out)
if(NOT rc EQUAL 0 OR NOT out STREQUAL "ADD 1 BUY 100 1\nCANCEL 1\nADD 2 BUY 100 1\nCANCEL 2\nBOOK\n")
  message(FATAL_ERROR "profile generator output differs: ${out}")
endif()

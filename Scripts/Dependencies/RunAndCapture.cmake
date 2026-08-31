if(NOT DEFINED KEIRE_PROGRAM OR NOT DEFINED KEIRE_OUTPUT)
    message(FATAL_ERROR "KEIRE_PROGRAM and KEIRE_OUTPUT are required")
endif()

set(arguments)
foreach(index RANGE 0 7)
    if(DEFINED KEIRE_ARG${index})
        list(APPEND arguments "${KEIRE_ARG${index}}")
    endif()
endforeach()

execute_process(
    COMMAND "${KEIRE_PROGRAM}" ${arguments}
    OUTPUT_FILE "${KEIRE_OUTPUT}"
    ERROR_VARIABLE failure_detail
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    file(REMOVE "${KEIRE_OUTPUT}")
    message(FATAL_ERROR "${KEIRE_PROGRAM} failed (${result}): ${failure_detail}")
endif()

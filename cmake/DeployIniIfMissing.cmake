# Deploys the packaged INI only when the target does not exist yet: the
# deployed INI holds user-tuned settings and must survive rebuilds. The
# packaged copy is always deployed alongside as *.default.ini for
# reference/diffing.
if(NOT EXISTS "${DST}")
	execute_process(COMMAND "${CMAKE_COMMAND}" -E copy "${SRC}" "${DST}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SRC}" "${DEFAULT_DST}")

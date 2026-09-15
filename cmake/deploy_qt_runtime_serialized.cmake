# windeployqt writes shared QML and runtime paths beside both GUI executables.
# Full Ninja builds may link them concurrently, so serialize only deployment;
# the Mapper and HidHide Doctor remain independent application targets.
foreach(required WINDEPLOYQT DEPLOY_LOCK QMLDIR TARGET_FILE)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Serialized Qt deployment requires ${required}.")
    endif()
endforeach()

file(LOCK "${DEPLOY_LOCK}" GUARD PROCESS TIMEOUT 300 RESULT_VARIABLE lockResult)
if (lockResult)
    message(FATAL_ERROR "Could not acquire Qt deployment lock: ${lockResult}")
endif()

execute_process(
    COMMAND "${WINDEPLOYQT}" --no-translations --no-compiler-runtime
        --qmldir "${QMLDIR}" "${TARGET_FILE}"
    RESULT_VARIABLE deployResult)
if (NOT deployResult EQUAL 0)
    message(FATAL_ERROR "windeployqt failed for ${TARGET_FILE} with exit code ${deployResult}.")
endif()

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${RC3_EXECUTABLE}"
    DIRECTORIES "${RC3_OUTPUT_DIRECTORY}" "${RC3_MINGW_RUNTIME_DIRECTORY}"
    PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*" "Azure.*" "HvsiFileTrust.*" "PdmUtilities.*" "wpaxholder.*" "WTDSENSOR\\.dll" "wtdccm\\.dll"
    POST_EXCLUDE_REGEXES ".*[Ww]indows[/\\]System32[/\\].*"
    RESOLVED_DEPENDENCIES_VAR runtimeDependencies
    UNRESOLVED_DEPENDENCIES_VAR unresolvedDependencies)

foreach(unresolvedDependency IN LISTS unresolvedDependencies)
    if(NOT unresolvedDependency MATCHES "^(AzureAttestManager|AzureAttestNormal|HvsiFileTrust|PdmUtilities|wpaxholder|[Ww][Tt][Dd][Ss][Ee][Nn][Ss][Oo][Rr]|[Ww][Tt][Dd][Cc][Cc][Mm])\\.dll$")
        list(APPEND requiredUnresolvedDependencies "${unresolvedDependency}")
    endif()
endforeach()

if(requiredUnresolvedDependencies)
    message(FATAL_ERROR "Unresolved RC3 runtime dependencies: ${requiredUnresolvedDependencies}")
endif()

foreach(runtimeDependency IN LISTS runtimeDependencies)
    file(TO_CMAKE_PATH "${RC3_MINGW_RUNTIME_DIRECTORY}" normalizedRuntimeDirectory)
    file(TO_CMAKE_PATH "${runtimeDependency}" normalizedRuntimeDependency)
    string(FIND "${normalizedRuntimeDependency}" "${normalizedRuntimeDirectory}/" runtimeDependencyPrefix)
    if(runtimeDependencyPrefix EQUAL 0)
        file(COPY "${runtimeDependency}" DESTINATION "${RC3_OUTPUT_DIRECTORY}")
    endif()
endforeach()

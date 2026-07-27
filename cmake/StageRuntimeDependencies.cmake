file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${REMOTE_CONTROL_EXECUTABLE}"
    DIRECTORIES "${REMOTE_CONTROL_MINGW_RUNTIME_DIRECTORY}"
    PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*" "Azure.*" "HvsiFileTrust.*" "PdmUtilities.*" "wpaxholder.*" "WTDSENSOR\\.dll" "wtdccm\\.dll"
    POST_EXCLUDE_REGEXES ".*[Ww]indows[/\\]System32[/\\].*"
    RESOLVED_DEPENDENCIES_VAR runtimeDependencies
    UNRESOLVED_DEPENDENCIES_VAR unresolvedDependencies
    CONFLICTING_DEPENDENCIES_PREFIX runtimeConflicts)

foreach(unresolvedDependency IN LISTS unresolvedDependencies)
    if(NOT unresolvedDependency MATCHES "^(AzureAttestManager|AzureAttestNormal|HvsiFileTrust|PdmUtilities|wpaxholder|[Ww][Tt][Dd][Ss][Ee][Nn][Ss][Oo][Rr]|[Ww][Tt][Dd][Cc][Cc][Mm])\\.dll$")
        list(APPEND requiredUnresolvedDependencies "${unresolvedDependency}")
    endif()
endforeach()

if(requiredUnresolvedDependencies)
    message(FATAL_ERROR "Unresolved Remote Control runtime dependencies: ${requiredUnresolvedDependencies}")
endif()

foreach(runtimeDependency IN LISTS runtimeDependencies)
    get_filename_component(runtimeDependencyName "${runtimeDependency}" NAME)
    set(runtimeDependencySource "${REMOTE_CONTROL_MINGW_RUNTIME_DIRECTORY}/${runtimeDependencyName}")
    if(EXISTS "${runtimeDependencySource}")
        file(COPY_FILE "${runtimeDependencySource}" "${REMOTE_CONTROL_OUTPUT_DIRECTORY}/${runtimeDependencyName}" ONLY_IF_DIFFERENT)
    endif()
endforeach()

foreach(runtimeConflict IN LISTS runtimeConflicts_FILENAMES)
    set(runtimeConflictSource "${REMOTE_CONTROL_MINGW_RUNTIME_DIRECTORY}/${runtimeConflict}")
    if(EXISTS "${runtimeConflictSource}")
        file(COPY_FILE "${runtimeConflictSource}" "${REMOTE_CONTROL_OUTPUT_DIRECTORY}/${runtimeConflict}" ONLY_IF_DIFFERENT)
    endif()
endforeach()

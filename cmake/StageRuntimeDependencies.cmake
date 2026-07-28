set(runtimeDependencyDirectories "${REMOTE_CONTROL_MINGW_RUNTIME_DIRECTORY}")
if(REMOTE_CONTROL_EXTRA_RUNTIME_DIRECTORY)
    list(APPEND runtimeDependencyDirectories "${REMOTE_CONTROL_EXTRA_RUNTIME_DIRECTORY}")
endif()
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${REMOTE_CONTROL_EXECUTABLE}"
    DIRECTORIES ${runtimeDependencyDirectories}
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
    foreach(runtimeDependencyDirectory IN LISTS runtimeDependencyDirectories)
        if(EXISTS "${runtimeDependencyDirectory}/${runtimeDependencyName}")
            file(COPY_FILE "${runtimeDependencyDirectory}/${runtimeDependencyName}" "${REMOTE_CONTROL_OUTPUT_DIRECTORY}/${runtimeDependencyName}" ONLY_IF_DIFFERENT)
            break()
        endif()
    endforeach()
endforeach()

foreach(runtimeConflict IN LISTS runtimeConflicts_FILENAMES)
    foreach(runtimeDependencyDirectory IN LISTS runtimeDependencyDirectories)
        if(EXISTS "${runtimeDependencyDirectory}/${runtimeConflict}")
            file(COPY_FILE "${runtimeDependencyDirectory}/${runtimeConflict}" "${REMOTE_CONTROL_OUTPUT_DIRECTORY}/${runtimeConflict}" ONLY_IF_DIFFERENT)
            break()
        endif()
    endforeach()
endforeach()

foreach(runtimeHelper IN ITEMS gspawn-win64-helper.exe gspawn-win64-helper-console.exe)
    foreach(runtimeDependencyDirectory IN LISTS runtimeDependencyDirectories)
        if(EXISTS "${runtimeDependencyDirectory}/${runtimeHelper}")
            file(COPY_FILE "${runtimeDependencyDirectory}/${runtimeHelper}" "${REMOTE_CONTROL_OUTPUT_DIRECTORY}/${runtimeHelper}" ONLY_IF_DIFFERENT)
            break()
        endif()
    endforeach()
endforeach()

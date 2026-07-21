get_filename_component(runtimePrefix "${REMOTE_CONTROL_MINGW_RUNTIME_DIRECTORY}" DIRECTORY)
set(sourceLoaderDirectory "${runtimePrefix}/lib/gdk-pixbuf-2.0/2.10.0/loaders")
set(loaderDirectory "${REMOTE_CONTROL_OUTPUT_DIRECTORY}/lib/gdk-pixbuf-2.0/2.10.0/loaders")
file(GLOB loaderModules "${sourceLoaderDirectory}/*.dll")
file(MAKE_DIRECTORY "${loaderDirectory}")
foreach(loaderModule IN LISTS loaderModules)
    file(COPY "${loaderModule}" DESTINATION "${loaderDirectory}")
endforeach()
file(GLOB deployedLoaderModules "${loaderDirectory}/*.dll")

file(GET_RUNTIME_DEPENDENCIES
    MODULES ${loaderModules}
    DIRECTORIES "${REMOTE_CONTROL_MINGW_RUNTIME_DIRECTORY}"
    PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*" "Azure.*" "HvsiFileTrust.*" "PdmUtilities.*" "wpaxholder.*" "WTDSENSOR\\.dll" "wtdccm\\.dll"
    RESOLVED_DEPENDENCIES_VAR loaderDependencies
    UNRESOLVED_DEPENDENCIES_VAR unresolvedDependencies)

foreach(unresolvedDependency IN LISTS unresolvedDependencies)
    if(NOT unresolvedDependency MATCHES "^(AzureAttestManager|AzureAttestNormal|HvsiFileTrust|PdmUtilities|wpaxholder|[Ww][Tt][Dd][Ss][Ee][Nn][Ss][Oo][Rr]|[Ww][Tt][Dd][Cc][Cc][Mm])\\.dll$")
        list(APPEND requiredUnresolvedDependencies "${unresolvedDependency}")
    endif()
endforeach()

if(requiredUnresolvedDependencies)
    message(FATAL_ERROR "Unresolved GTK pixbuf dependencies: ${requiredUnresolvedDependencies}")
endif()

foreach(loaderDependency IN LISTS loaderDependencies)
    file(TO_CMAKE_PATH "${REMOTE_CONTROL_MINGW_RUNTIME_DIRECTORY}" normalizedRuntimeDirectory)
    file(TO_CMAKE_PATH "${loaderDependency}" normalizedLoaderDependency)
    string(FIND "${normalizedLoaderDependency}" "${normalizedRuntimeDirectory}/" loaderDependencyPrefix)
    if(loaderDependencyPrefix EQUAL 0)
        file(COPY "${loaderDependency}" DESTINATION "${REMOTE_CONTROL_OUTPUT_DIRECTORY}")
    endif()
endforeach()

set(loaderCache "${REMOTE_CONTROL_OUTPUT_DIRECTORY}/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache")
execute_process(COMMAND "${REMOTE_CONTROL_MINGW_RUNTIME_DIRECTORY}/gdk-pixbuf-query-loaders.exe" ${deployedLoaderModules} OUTPUT_FILE "${loaderCache}" COMMAND_ERROR_IS_FATAL ANY)
file(READ "${loaderCache}" loaderCacheContents)
file(TO_CMAKE_PATH "${REMOTE_CONTROL_OUTPUT_DIRECTORY}" normalizedOutputDirectory)
string(REPLACE "${normalizedOutputDirectory}" "." loaderCacheContents "${loaderCacheContents}")
file(WRITE "${loaderCache}" "${loaderCacheContents}")

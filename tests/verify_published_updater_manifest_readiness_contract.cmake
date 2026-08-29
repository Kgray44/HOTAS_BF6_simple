if (NOT DEFINED UPDATER_ACCEPTANCE_SCRIPT OR NOT EXISTS "${UPDATER_ACCEPTANCE_SCRIPT}")
    message(FATAL_ERROR "UPDATER_ACCEPTANCE_SCRIPT must name scripts/verify-published-updater.ps1")
endif()

file(READ "${UPDATER_ACCEPTANCE_SCRIPT}" updater_source)

foreach (required_fragment IN ITEMS
    "https://github.com/Kgray44/HOTAS_BF6_simple/releases/latest/download/update-manifest.json"
    "function Wait-PublishedLatestManifest"
    "Invoke-WebRequest -Uri \$manifestUrl -UseBasicParsing"
    "\$version -eq \$expectedVersion -and \$tag -eq \"v\$expectedVersion\""
    "Wait-PublishedLatestManifest -expectedVersion \$ExpectedVersion -manifestUrl \$latestManifestUrl")
    string(FIND "${updater_source}" "${required_fragment}" fragment_offset)
    if (fragment_offset EQUAL -1)
        message(FATAL_ERROR "Published updater acceptance must retain ${required_fragment}")
    endif()
endforeach()

string(FIND "${updater_source}" "Wait-PublishedLatestManifest -expectedVersion \$ExpectedVersion -manifestUrl \$latestManifestUrl" readiness_offset)
string(FIND "${updater_source}" "[void](Start-Process -FilePath \$launcher -PassThru)" launcher_offset)
if (readiness_offset GREATER launcher_offset)
    message(FATAL_ERROR "Published updater acceptance must wait for the expected public latest manifest before launching v1.9.3")
endif()

# Runs: synth -> measure -> overlay -> run, and checks that every step
# succeeds and leaves its output behind.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

function(run_step)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "command failed (${rc}): ${ARGN}\n${out}\n${err}")
  endif()
endfunction()

run_step("${FIXMYPLANET}" synth --planet jupiter --out-dir "${WORK}/in" --count 5 --size 320 --radius 100 --truth)
file(GLOB frames "${WORK}/in/jupiter_*.tif")
list(LENGTH frames n)
if(NOT n EQUAL 5)
  message(FATAL_ERROR "expected 5 synthetic images, found ${n}")
endif()
run_step("${FIXMYPLANET}" measure --planet jupiter -o "${WORK}/in/session.json" ${frames})
run_step("${FIXMYPLANET}" overlay "${WORK}/in/session.json" --out-dir "${WORK}/overlay")
run_step("${FIXMYPLANET}" run "${WORK}/in/session.json" -o "${WORK}/out.tif" --save-frames "${WORK}/frames")
run_step("${FIXMYPLANET}" run "${WORK}/in/session.json" -o "${WORK}/out.png" --north-up --depth 8)
run_step("${FIXMYPLANET}" run "${WORK}/in/session.json" -o "${WORK}/out.fits" --system III --no-align)
run_step("${FIXMYPLANET}" ephem --planet saturn --time 2026-10-04T23:00:00)
foreach(f out.tif out.png out.fits frames/fixmyplanet_005.tif)
  if(NOT EXISTS "${WORK}/${f}")
    message(FATAL_ERROR "missing output ${f}")
  endif()
endforeach()

# Wildcards (expanded by the program itself on Windows).
execute_process(COMMAND "${FIXMYPLANET}" expand "${WORK}/in/JUPITER_*-0?-00.tif" "${WORK}/in/nothing*.tif" plain.tif
                RESULT_VARIABLE rc OUTPUT_VARIABLE out)
string(REGEX MATCHALL "jupiter_[^\n]*\\.tif" hits "${out}")
list(LENGTH hits nhits)
if(NOT rc EQUAL 0 OR NOT nhits EQUAL 5 OR NOT out MATCHES "nothing\\*\\.tif" OR NOT out MATCHES "plain\\.tif")
  message(FATAL_ERROR "wildcard expansion gave:\n${out}")
endif()

# A folder given as an image is an error, not a crash.
execute_process(COMMAND "${FIXMYPLANET}" measure --planet jupiter -o "${WORK}/x.json" "${WORK}/in"
                RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
if(rc EQUAL 0 OR NOT err MATCHES "folder")
  message(FATAL_ERROR "a folder as input was not refused cleanly: ${err}")
endif()

# A wrong option must be reported as an error, not ignored.
execute_process(COMMAND "${FIXMYPLANET}" run "${WORK}/in/session.json" -o "${WORK}/x.tif" --no-such-option 1
                RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(rc EQUAL 0)
  message(FATAL_ERROR "an unknown option was accepted")
endif()

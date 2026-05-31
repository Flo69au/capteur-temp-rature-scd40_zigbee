# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "esp-idf-scd4x")

# --- Altitude automatique via Open-Meteo ---
# Modifier ces coordonnées si le capteur est déplacé.
set(SENSOR_LAT "45.864" CACHE STRING "Latitude du capteur (degrés décimaux)")
set(SENSOR_LON "4.273" CACHE STRING "Longitude du capteur (degrés décimaux)")

find_program(PYTHON_EXE NAMES python3 python)
if(PYTHON_EXE)
    execute_process(
        COMMAND ${PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/fetch_altitude.py ${SENSOR_LAT} ${SENSOR_LON}
        OUTPUT_VARIABLE _FETCHED_ALT
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _ALT_RESULT
        TIMEOUT 10
    )
else()
    set(_ALT_RESULT 1)
endif()

if(_ALT_RESULT EQUAL 0 AND _FETCHED_ALT MATCHES "^[0-9]+$")
    message(STATUS "Altitude récupérée: ${_FETCHED_ALT} m (${SENSOR_LAT}, ${SENSOR_LON})")
else()
    set(_FETCHED_ALT 530)
    message(WARNING "Altitude non disponible depuis l'API, fallback: ${_FETCHED_ALT} m")
endif()
set(SENSOR_ALTITUDE_METERS ${_FETCHED_ALT} CACHE INTERNAL "Altitude en mètres (récupérée à la configuration)")
# -------------------------------------------

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(zigbee_co2_sensor)
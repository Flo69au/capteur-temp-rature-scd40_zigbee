@echo off
set IDF_TOOLS_PATH=C:\Espressif
set IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.2.6
set PATH=C:\Espressif\python_env\idf5.2_py3.11_env\Scripts;C:\Espressif\tools\cmake\3.27.7\bin;C:\Espressif\tools\ninja\1.11.1;%PATH%

cd /d C:\Users\Augie\Downloads\capteur\scd40_zigbee

echo === Full Clean ===
python %IDF_PATH%\tools\idf.py fullclean
if %errorlevel% neq 0 ( echo ERREUR fullclean & exit /b 1 )

echo === Build ===
python %IDF_PATH%\tools\idf.py build
if %errorlevel% neq 0 ( echo ERREUR build & exit /b 1 )

echo === Flash (JTAG COM17) ===
python %IDF_PATH%\tools\idf.py -p COM17 flash
if %errorlevel% neq 0 ( echo ERREUR flash & exit /b 1 )

echo === DONE ===

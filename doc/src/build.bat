@echo off

if exist build.bat goto OK
echo Error: must be run in the src directory
goto done

:OK

mmp index.html ../index.html

:done

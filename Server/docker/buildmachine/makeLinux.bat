@echo off
rem  Copyright (c) 2019 Christof Ruch. All rights reserved.
rem
rem Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase

if [%1]==[] goto usage

rem docker run --volume=%1:/src --rm jammernetz_buildmachine
docker run --volume=%1:/src -it --entrypoint=/bin/bash jammernetz_buildmachine
goto :eof

:usage
@echo Usage: %0 ^<JammerNetzDirectory^>
@echo You need to specify the JammerNetz top level directory in the funky Docker volume syntax with forward slashes and a double slash for the drive letter.
@echo E.g. the Windows directory D:\Development\JammerNetz needs to be specfied as //d/Development/JammerNetz
exit /B 1

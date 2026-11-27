:; # Runs the VHAL tests. Works from cmd, PowerShell, bash and zsh; the arguments are read by .test/Run.cmake.
:; exec cmake -P "$(dirname "$0")/.test/Run.cmake" -- "$@"
@echo off
cmake -P "%~dp0.test\Run.cmake" -- %*
exit /b %ERRORLEVEL%

@echo off
REM Run dd_native_repl with a series of commands to trigger a crash

(
echo set-config client-token fake-client-token
echo set-config rum-application-id 1df685d5-43e0-47e5-b6c7-c13aaf2d6fbe
echo create-core
echo register-logging
echo register-rum
echo register-crash-reporting
echo start-core
echo sleep 1
echo crash
) | build\examples\Debug\dd_native_repl.exe

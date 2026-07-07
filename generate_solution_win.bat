:: This script can be used to generate a solution for an SDK or your own projects.
::
:: The script also generates a symbolic link to the project solution at the root of the project. The 
:: script expects the project to have a ./tools directory containing the project tool at its root, 
:: as well as the /frameworks and /plugins directories containing the Cinema 4D frameworks and your 
:: code. Place and run this script at the root to update or generate your projects.
@echo off
setlocal

:: Check if the script is running as administrator by writing a dummy file so that we set a symbolic 
:: link below.
echo. > "%SYSTEMROOT%\system32\mxnsdkisadmin.tst" 2> NUL
if exist "%SYSTEMROOT%\system32\mxnsdkisadmin.tst" (
    del "%SYSTEMROOT%\system32\mxnsdkisadmin.tst"
) else (
    echo "Please run this script by selecting 'Run as administrator' from the file menu."
    exit /b 1
)

:: Check if the project tool exists.
set "ptool=%~dp0tools\projecttool\kernel_app_64bit.exe"
if not exist "%ptool%" (
    echo "Project tool not found at '%ptool%'."
    exit /b 1
)

:: Run the project tool on the project and create a symbolic link to the solution.
echo Running project tool on project at '%~dp0'.
%ptool% g_updateproject=%~dp0

set "linkTarget=%~dp0plugins\project\plugins.sln"
set "link=%~dp0solution_win"
if not exist "%link%" (
    echo "Creating symbolic link to solution at '%link%'."
    mklink %link% %linkTarget%
)

endlocal
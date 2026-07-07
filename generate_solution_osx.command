#!/bin/sh

# This script can be used to generate a solution for an SDK or your own projects.  
#
# The script expects the project to have a ./tools directory containing the project tool at its 
# root, as well as the /frameworks and /plugins directories containing the Cinema 4D frameworks
# and your code. Place and run this script at the root to update or generate your projects.

PROJECT_PATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
echo "Running project tool on project at '$PROJECT_PATH'."
$PROJECT_PATH/tools/projecttool/kernel_app.app/Contents/MacOS/kernel_app g_updateproject=$PROJECT_PATH

if [ ! -f "$PROJECT_PATH/solution_osx.command" ]; then
    cp "$PROJECT_PATH/tools/solution_osx_template.command" "$PROJECT_PATH/solution_osx.command"
fi
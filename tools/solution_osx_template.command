# Opens the plugins.xcodeproj file with /Applications/Xcode.app.
#
# This script will force an Xcode installation at /Applications/Xcode.app to open the project 
# solution. Reaching into the Xcode.app bundle ensures that macOS Ventura or higher will still run
# Xcode 13.
PROJECT_PATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
open -a /Applications/Xcode.app/Contents/MacOS/Xcode "${PROJECT_PATH}/plugins/project/plugins.xcodeproj"
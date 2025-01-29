cmake_minimum_required(VERSION 3.16)

# added for CLion to load sub-projects
# https://stackoverflow.com/questions/36443085/is-there-a-way-to-work-on-multiple-projects-in-clion
# https://youtrack.jetbrains.com/issue/CPP-1537

project(root)
add_subdirectory(framework EXCLUDE_FROM_ALL)
add_subdirectory(fbsources/capture_x11 EXCLUDE_FROM_ALL)
add_subdirectory(fbsources/crop EXCLUDE_FROM_ALL)
add_subdirectory(fbsources/dummy EXCLUDE_FROM_ALL)
add_subdirectory(fbsources/libvnc EXCLUDE_FROM_ALL)
add_subdirectory(fbsources/merge EXCLUDE_FROM_ALL)
add_subdirectory(tools/fbsourceviewer EXCLUDE_FROM_ALL)

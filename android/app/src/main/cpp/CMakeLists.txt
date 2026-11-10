# Appli (build 375) : emulateur GBA de l'appli (GBA du telephone : Tingle Tuner, Four Swords Adventures...).
# mGBA (https://mgba.io, licence MPL 2.0) n'est pas dans le depot : la compilation de l'appli le telecharge en version
# figee dans src/main/cpp/mgba (.github/workflows/android.yml). Ses fichiers gardent leur licence (mgba/LICENSE) ; seul
# gba_jni.c (le pont avec l'appli) est propre a GC Station.
cmake_minimum_required(VERSION 3.18.1)
project(gbapont C)

set(MGBA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mgba)
if(NOT EXISTS ${MGBA_DIR}/CMakeLists.txt)
	message(FATAL_ERROR "mGBA absent : ${MGBA_DIR} (telecharge par .github/workflows/android.yml)")
endif()

# mGBA en bibliotheque seule (EXCLUDE_FROM_ALL : son mode "LIBMGBA_ONLY", sans interface ni dependances), GBA seulement
set(M_CORE_GBA ON)
set(M_CORE_GB OFF)
set(USE_DEBUGGERS OFF)
set(USE_GDB_STUB OFF)
set(USE_ELF OFF)
set(USE_ZLIB OFF)
set(USE_PNG OFF)
set(USE_LZMA OFF)
set(USE_SQLITE3 OFF)
set(USE_LIBZIP OFF)
set(USE_MINIZIP OFF)
set(USE_FFMPEG OFF)
set(USE_LUA OFF)
set(ENABLE_SCRIPTING OFF)
set(USE_DISCORD_RPC OFF)
set(BUILD_GL OFF)
set(BUILD_GLES2 OFF)
set(BUILD_GLES3 OFF)
set(USE_EPOXY OFF)
set(SKIP_GIT ON)
add_subdirectory(${MGBA_DIR} ${CMAKE_CURRENT_BINARY_DIR}/mgba EXCLUDE_FROM_ALL)

add_library(gbapont SHARED gba_jni.c)
target_include_directories(gbapont PRIVATE ${MGBA_DIR}/include ${MGBA_DIR}/src ${CMAKE_CURRENT_BINARY_DIR}/mgba/include)
target_link_libraries(gbapont mgba log)

# Appli build 491 : RetroAchievements : rcheevos (https://github.com/RetroAchievements/rcheevos, licence MIT) n'est pas
# dans le depot : ci/appli.sh le telecharge en version figee dans src/main/cpp/rcheevos. ra_jni.c : le pont avec l'appli.
set(RC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/rcheevos)
if(NOT EXISTS ${RC_DIR}/include/rc_client.h)
	message(FATAL_ERROR "rcheevos absent : ${RC_DIR} (telecharge par ci/appli.sh)")
endif()
file(GLOB RC_SRC ${RC_DIR}/src/api/*.c ${RC_DIR}/src/client/*.c ${RC_DIR}/src/runtime/*.c ${RC_DIR}/src/util/*.c ${RC_DIR}/src/rc_*.c)
add_library(rapont SHARED ra_jni.c ${RC_SRC})
target_include_directories(rapont PRIVATE ${RC_DIR}/include ${RC_DIR}/src)
target_compile_definitions(rapont PRIVATE RC_DISABLE_LUA)
target_link_libraries(rapont log)

# Appli privee build 498 : presence Discord. Le Social SDK de Discord n'est pas dans le depot : discord_jni.c l'ouvre au
# moment de l'appel (dlopen), donc cette bibliotheque se compile sans lui.
add_library(discpont SHARED discord_jni.c)
target_link_libraries(discpont log dl)

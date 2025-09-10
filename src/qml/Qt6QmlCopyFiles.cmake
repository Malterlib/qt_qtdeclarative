# Copyright (C) 2024 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/Qt6QmlPublicCMakeHelpers.cmake")

# Includes FILES_INFO_PATH, expects it to have the working directory, timestamp file and a list
# of file source and destination paths, to copy them from src to dest.

# ---------------- Helpers ----------------
# Parse QT_TOOLS_REPLACE_PATHS once; expand $ENV{...} in values; keep the exact key set from ENV.
if(NOT COMMAND _qt_map_init)
  function(_qt_map_init)
    get_property(_inited GLOBAL PROPERTY QT_TOOLS_REPLACE_INIT)
    if(_inited)
      return()
    endif()

    set(_env "$ENV{QT_TOOLS_REPLACE_PATHS}")
    set(_keys "")
    set(_vals "")

    if(NOT _env STREQUAL "")
      # Semicolon-separated entries: "key=value"
      set(_pairs ${_env})
      foreach(_pair IN LISTS _pairs)
        if(_pair STREQUAL "")
          continue()
        endif()
        string(REGEX MATCH "^([^=]+)=(.*)$" _m "${_pair}")
        if(_m)
          set(_k "${CMAKE_MATCH_1}")
          set(_v "${CMAKE_MATCH_2}")

          # Expand $ENV{...} recursively in VALUE (not in key)
          set(_expanded "${_v}")
          while(_expanded MATCHES "\\$ENV\\{([^}]+)\\}")
            string(REGEX REPLACE ".*\\$ENV\\{([^}]+)\\}.*" "\\1" _envname "${_expanded}")
            set(_envval "$ENV{${_envname}}")
            string(REPLACE "$ENV{${_envname}}" "${_envval}" _expanded "${_expanded}")
          endwhile()

          list(APPEND _keys "${_k}")
          list(APPEND _vals "${_expanded}")
        endif()
      endforeach()
    endif()

    # Reorder so that keys ending with "/" (more specific) are applied first.
    set(_keys_slash "")
    set(_vals_slash "")
    set(_keys_plain "")
    set(_vals_plain "")
    list(LENGTH _keys _N)
    if(_N GREATER 0)
      math(EXPR _Nm1 "${_N} - 1")
      foreach(_i RANGE 0 ${_Nm1})
        list(GET _keys ${_i} _kk)
        list(GET _vals ${_i} _vv)
        string(REGEX MATCH "/$" _endslash "${_kk}")
        if(_endslash)
          list(APPEND _keys_slash "${_kk}")
          list(APPEND _vals_slash "${_vv}")
        else()
          list(APPEND _keys_plain "${_kk}")
          list(APPEND _vals_plain "${_vv}")
        endif()
      endforeach()
      set(_keys "${_keys_slash};${_keys_plain}")
      set(_vals "${_vals_slash};${_vals_plain}")
    endif()

    set_property(GLOBAL PROPERTY QT_TOOLS_REPLACE_KEYS "${_keys}")
    set_property(GLOBAL PROPERTY QT_TOOLS_REPLACE_VALS "${_vals}")
    set_property(GLOBAL PROPERTY QT_TOOLS_REPLACE_INIT TRUE)

    # Debug (temporary)
    # message(STATUS "[QML MAP] keys='${_keys}'")
    # message(STATUS "[QML MAP] vals='${_vals}'")
  endfunction()

  # Apply mapping literally: replace any occurrence of each KEY with its VALUE (slash-keys first).
  function(_qt_apply_map OUT STR)
    get_property(_keys GLOBAL PROPERTY QT_TOOLS_REPLACE_KEYS)
    get_property(_vals GLOBAL PROPERTY QT_TOOLS_REPLACE_VALS)
    set(_v "${STR}")
    if(_keys)
      list(LENGTH _keys _n)
      if(_n GREATER 0)
        math(EXPR _last "${_n} - 1")
        foreach(_i RANGE 0 ${_last})
          list(GET _keys ${_i} _k)
          list(GET _vals ${_i} _val)
          if(NOT _k STREQUAL "")
            string(FIND "${_v}" "${_k}" _pos)
            if(NOT _pos EQUAL -1)
              string(REPLACE "${_k}" "${_val}" _v "${_v}")
            endif()
          endif()
        endforeach()
      endif()
    endif()
    set(${OUT} "${_v}" PARENT_SCOPE)
  endfunction()

  # Normalize path:
  # - If INP is relative: make ABSOLUTE relative to BASE, then REALPATH (cleans .. and .)
  # - If INP is absolute: REALPATH (cleans .. and . even if it doesn't exist)
  function(_qt_normalize OUT BASE INP)
    set(_p "${INP}")
    if(NOT IS_ABSOLUTE "${_p}")
      if(DEFINED BASE AND NOT "${BASE}" STREQUAL "")
        get_filename_component(_abs "${_p}" ABSOLUTE BASE_DIR "${BASE}")
      else()
        get_filename_component(_abs "${_p}" ABSOLUTE)
      endif()
    else()
      set(_abs "${_p}")
    endif()
    get_filename_component(_norm "${_abs}" REALPATH)
    set(${OUT} "${_norm}" PARENT_SCOPE)
  endfunction()
endif()
# -------------- End Helpers --------------

function(qt_internal_qml_copy_files)
    if(NOT FILES_INFO_PATH)
        message(FATAL_ERROR "FILES_INFO_PATH is not defined")
    endif()

    include("${FILES_INFO_PATH}")

    if(NOT working_dir)
        message(FATAL_ERROR "working_dir is not defined")
    endif()

    if(NOT timestamp_file)
        message(FATAL_ERROR "timestamp_file is not defined")
    endif()

    # Init mapping once
    _qt_map_init()

    # --- Timestamp: apply map and normalize; derive bases from it ---
    _qt_apply_map(_ts_mapped "${timestamp_file}")
    # message(STATUS "[QML TS] before='${timestamp_file}'  after='${_ts_mapped}'  (raw working_dir='${working_dir}')")
    _qt_normalize(_ts_abs "" "${_ts_mapped}")
    set(timestamp_file "${_ts_abs}")

    # 1) Base directories from the timestamp (you probably already have these lines)
    get_filename_component(_ts_dir    "${timestamp_file}" DIRECTORY)  # .../.qt
    get_filename_component(_dest_base "${_ts_dir}"        DIRECTORY)  # parent of .qt

    # 2) Find the directory where FILES_INFO is located (== .qt)
    get_filename_component(_info_dir "${FILES_INFO_PATH}" DIRECTORY)

    # 3) Make working_dir ABSOLUTE relative to the .qt file (not relative to PWD)
    if(NOT IS_ABSOLUTE "${working_dir}")
      get_filename_component(_wd_abs "${working_dir}" ABSOLUTE BASE_DIR "${_info_dir}")
    else()
      set(_wd_abs "${working_dir}")
    endif()
    # Clean .. and .
    get_filename_component(_wd_abs "${_wd_abs}" REALPATH BASE_DIR "${_info_dir}")
    set(working_dir "${_wd_abs}")

    #message(STATUS "[QML DBG] info_dir='${_info_dir}'  working_dir(abs)='${working_dir}'  dest_base='${_dest_base}'")

    if(NOT src_and_dest_list)
        # Return early if there are no files to copy. We still need to touch the timestamp file to
        # ensure the script is not constantly reran.
        file(TOUCH "${timestamp_file}")
        return()
    endif()

    # Check there is an even number of paths in the input list.
    list(LENGTH src_and_dest_list src_and_dest_list_length)
    math(EXPR divide_by_two_remainder "${src_and_dest_list_length} % 2")
    if(divide_by_two_remainder STREQUAL "1")
        message(FATAL_ERROR "List of files to copy is corrupted, expected even number of paths.")
    endif()

    # Iterate every two list entries, the src and dest.
    set(step "2")
    math(EXPR final_idx "${src_and_dest_list_length} - 1" )
    foreach(idx RANGE 0 "${final_idx}" "${step}")
        # Extract the src path.
        list(GET src_and_dest_list "${idx}" src)

        # Extract the dest path.
        math(EXPR next_idx "${idx} + 1")
        list(GET src_and_dest_list "${next_idx}" dest)

        # --- Make absolute paths relative to the correct bases ---
        # SOURCE: first interpreted relative to working_dir (absolute), fallback to .qt if the list is written relative to .qt
        if(IS_ABSOLUTE "${src}")
          set(_src_abs "${src}")
        else()
          get_filename_component(_src_abs "${src}" ABSOLUTE BASE_DIR "${working_dir}")
          get_filename_component(_src_abs "${_src_abs}" REALPATH BASE_DIR "${working_dir}")
          if(NOT EXISTS "${_src_abs}")
            # back-compat: interpret relative to .qt if some list happens to be written that way
            get_filename_component(_src_abs2 "${src}" ABSOLUTE BASE_DIR "${_info_dir}")
            get_filename_component(_src_abs2 "${_src_abs2}" REALPATH BASE_DIR "${_info_dir}")
            if(EXISTS "${_src_abs2}")
              set(_src_abs "${_src_abs2}")
            endif()
          endif()
        endif()

        if(IS_ABSOLUTE "${dest}")
          set(_dest_abs "${dest}")
        else()
          get_filename_component(_dest_abs "${dest}" ABSOLUTE BASE_DIR "${_dest_base}")
          get_filename_component(_dest_abs "${_dest_abs}" REALPATH BASE_DIR "${_dest_base}")
        endif()

        get_filename_component(_dest_dir "${_dest_abs}" DIRECTORY)
        if(NOT IS_DIRECTORY "${_dest_dir}")
          file(MAKE_DIRECTORY "${_dest_dir}")
        endif()

        #message(STATUS "[QML COPY] src_abs='${_src_abs}' -> dest_abs='${_dest_abs}' (wd='${working_dir}', .qt='${_info_dir}')")

        _qt_internal_qml_copy_file("${_src_abs}" "${_dest_abs}")
    endforeach()

    # Always touch the timestamp file to prevent reruns of the script.
    file(TOUCH "${timestamp_file}")
endfunction()

qt_internal_qml_copy_files()

function(package_clap target)
  set(BUNDLE_ID "org.ccrma.${target}")
  target_compile_definitions(${target} PRIVATE BUNDLE_ID="${BUNDLE_ID}")
  if(APPLE)
      set_target_properties(${target} PROPERTIES
              BUNDLE True
              BUNDLE_EXTENSION clap
              MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_ID}
              MACOSX_BUNDLE_BUNDLE_NAME ${target}
              MACOSX_BUNDLE_BUNDLE_VERSION "1"
              MACOSX_BUNDLE_SHORT_VERSION_STRING "1"
              MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/../../helpers/plugins.plist.in
              )
      # target_link_libraries(${target} "-framework CoreFoundation" "-framework AppKit" "-framework CoreGraphics")

      target_compile_definitions(${target} PRIVATE IS_MAC=1)

      # if (${COPY_AFTER_BUILD})
      #     message(STATUS "Will copy plugin after every build" )
      #     set(products_folder ${CMAKE_BINARY_DIR})
      #     add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
      #             COMMAND ${CMAKE_COMMAND} -E echo "Installing ${products_folder}/${PROJECT_NAME}.clap to ~/Library/Audio/Plug-Ins/CLAP/"
      #             COMMAND ${CMAKE_COMMAND} -E make_directory "~/Library/Audio/Plug-Ins/CLAP"
      #             COMMAND ${CMAKE_COMMAND} -E copy_directory "${products_folder}/${PROJECT_NAME}.clap" "~/Library/Audio/Plug-Ins/CLAP/${PROJECT_NAME}.clap"
      #             )
      # endif()
  elseif(UNIX)
      target_compile_definitions(${target} PRIVATE IS_LINUX=1)
      set_target_properties(${target} PROPERTIES SUFFIX ".clap" PREFIX "")
      # if (${COPY_AFTER_BUILD})
      #     message(STATUS "Will copy plugin after every build" )
      #     set(products_folder ${CMAKE_BINARY_DIR})
      #     add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
      #             COMMAND ${CMAKE_COMMAND} -E echo "Installing ${products_folder}/${PROJECT_NAME}.clap to ~/.clap"
      #             COMMAND ${CMAKE_COMMAND} -E make_directory "~/.clap"
      #             COMMAND ${CMAKE_COMMAND} -E copy "${products_folder}/${PROJECT_NAME}.clap" "~/.clap"
      #             )
      # endif()

  else()
      target_compile_definitions(${target} PRIVATE IS_WIN=1)
      set_target_properties(${target} PROPERTIES SUFFIX ".clap" PREFIX "")
  endif()
endfunction(package_clap target)

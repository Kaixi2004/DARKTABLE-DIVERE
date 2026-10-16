find_package(LLVMClangTidy REQUIRED)

unset(plugin)

if(USE_RAWSPEED_CLANG_TIDY_MODULE)
  math(EXPR CLANG_TIDY_VERSION_MAJOR_NEXT "1 + ${CLANG_TIDY_VERSION_MAJOR}")

  find_package(RawSpeedClangTidyModule ${CLANG_TIDY_VERSION_MAJOR}.0.0...<${CLANG_TIDY_VERSION_MAJOR_NEXT}.0.0 REQUIRED CONFIG)

  SET_PACKAGE_PROPERTIES(RawSpeedClangTidyModule PROPERTIES
    URL https://github.com/darktable-org/rawspeed-clang-tidy-module
    DESCRIPTION "custom clang-tidy module for RawSpeed library"
    PURPOSE "RawSpeed-specific clang-tidy checks"
  )

  set(plugin "--load=$<TARGET_PROPERTY:RawSpeedClangTidyModule::clangTidyRawSpeedModule,LOCATION>")
endif()

set(CMAKE_CXX_CLANG_TIDY "${CLANGTIDY_PATH}")

if(DEFINED plugin)
  set(CMAKE_CXX_CLANG_TIDY ${CMAKE_CXX_CLANG_TIDY} ${plugin})
endif()

if(NOT RAWSPEED_ENABLE_CLANG_TIDY_WERROR)
  set(CMAKE_CXX_CLANG_TIDY ${CMAKE_CXX_CLANG_TIDY} -warnings-as-errors=-*)
endif()

set(CMAKE_CXX_CLANG_TIDY ${CMAKE_CXX_CLANG_TIDY} -extra-arg=-Wno-unknown-warning-option)
set(CMAKE_CXX_CLANG_TIDY ${CMAKE_CXX_CLANG_TIDY} -extra-arg=-Wno-unknown-pragmas)

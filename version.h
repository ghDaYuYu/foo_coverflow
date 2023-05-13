#pragma once
#define COMPONENT_NAME_LABEL "Chronflow Mod"
#define COMPONENT_NAME "foo_chronflow_mod"
#define COMPONENT_YEAR "2023"

#define COMPONENT_VERSION_MAJOR 0

#define COMPONENT_VERSION_MINOR 5
#define COMPONENT_VERSION_PATCH 2
#define COMPONENT_VERSION_SUB_PATCH 16

#define MAKE_STRING(text) #text
#define MAKE_COMPONENT_VERSION(major, minor, patch, subpatch) \
  MAKE_STRING(major) "." MAKE_STRING(minor) "." MAKE_STRING(patch) ".mod." MAKE_STRING(subpatch)
#define MAKE_DLL_VERSION(major,minor,patch,subpatch) MAKE_STRING(major) "." MAKE_STRING(minor) "." MAKE_STRING(patch) "." MAKE_STRING(subpatch)
#define MAKE_API_SDK_VERSION(sdk_ver, sdk_target) MAKE_STRING(sdk_ver) " " MAKE_STRING(sdk_target)

//"0.1.2"
#ifdef BETA_VER
#define FOO_CHRONFLOW_VERSION \
  MAKE_COMPONENT_VERSION(COMPONENT_VERSION_MAJOR, COMPONENT_VERSION_MINOR, \
                         COMPONENT_VERSION_PATCH, COMPONENT_VERSION_SUB_PATCH) \
  ".beta"
#else
#define FOO_CHRONFLOW_VERSION \
  MAKE_COMPONENT_VERSION(COMPONENT_VERSION_MAJOR, COMPONENT_VERSION_MINOR, \
                         COMPONENT_VERSION_PATCH, COMPONENT_VERSION_SUB_PATCH)
#endif

//0.1.2.3 & "0.1.2.3"
#define DLL_VERSION_NUMERIC COMPONENT_VERSION_MAJOR, COMPONENT_VERSION_MINOR, COMPONENT_VERSION_PATCH, COMPONENT_VERSION_SUB_PATCH
#define DLL_VERSION_STRING MAKE_DLL_VERSION(COMPONENT_VERSION_MAJOR,COMPONENT_VERSION_MINOR,COMPONENT_VERSION_PATCH,COMPONENT_VERSION_SUB_PATCH)

// fb2k sdk version
#define PLUGIN_FB2K_SDK MAKE_API_SDK_VERSION(FOOBAR2000_SDK_VERSION, FOOBAR2000_TARGET_VERSION)

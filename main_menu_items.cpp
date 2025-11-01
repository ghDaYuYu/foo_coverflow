#include "stdafx.h"

#include "EngineThread.h"
#include "Engine.h"

#include "main_menu_items.h"

using EM = engine_messages::Engine::Messages;

namespace MainMenuItems {

// {2BD780AF-B8DB-47A2-829A-2BBD49108384}
const GUID CoverflowGroup::m_guid = {0x2bd780af, 0xb8db, 0x47a2, {0x82, 0x9a, 0x2b, 0xbd, 0x49, 0x10, 0x83, 0x84}};

CoverflowGroup::CoverflowGroup()
    : mainmenu_group_impl(m_guid, mainmenu_groups::view,
          mainmenu_commands::sort_priority_dontcare) {}

// {4FBBDE2F-BF9A-4D24-81BD-8E4865D72F50}
const GUID CoverflowMainPopupMenu::m_guid = {
    0x4fbbde2f, 0xbf9a, 0x4d24, {0x81, 0xbd, 0x8e, 0x48, 0x65, 0xd7, 0x2f, 0x50}};


CoverflowMainPopupMenu::CoverflowMainPopupMenu()
    : mainmenu_group_popup_impl(m_guid, CoverflowGroup::m_guid,
                                mainmenu_commands::sort_priority_base,
                                COMPONENT_NAME_LABEL) {}

t_uint32 CoverflowMainPopupCommands::get_command_count() {
  return numMenuItems;
}

GUID CoverflowMainPopupCommands::get_command(t_uint32 p_index) {

  // {6A2E35E1-B660-47D6-8FB9-EA4E80C18EF8}
  static const GUID guidReloadCollection = {
      0x6a2e35e1, 0xb660, 0x47d6, {0x8f, 0xb9, 0xea, 0x4e, 0x80, 0xc1, 0x8e, 0xf8}};

  if (p_index == miiReloadCollection)
    return guidReloadCollection;

  return pfc::guid_null;
}

void CoverflowMainPopupCommands::get_name(t_uint32 p_index, pfc::string_base& p_out) {
  if (p_index == miiReloadCollection)
    p_out = "Reload collection";
}

bool CoverflowMainPopupCommands::get_description(t_uint32 p_index, pfc::string_base& p_out) {
  if (p_index == miiReloadCollection)
    p_out = "Reload collection.";
  else
    return false;

  return true;
}

GUID CoverflowMainPopupCommands::get_parent() {
  return CoverflowMainPopupMenu::m_guid;
}

t_uint32 CoverflowMainPopupCommands::get_sort_priority() {
  return mainmenu_commands::sort_priority_base + 1;
}

void CoverflowMainPopupCommands::execute(t_uint32 p_index, service_ptr_t<service_base> p_callback) {
  if (p_index == miiReloadCollection) {
    engine::EngineThread::forEach(
        [](engine::EngineThread& t) { t.send<EM::ReloadCollection>(NULL); });
  }
}
}  // namespace MainMenuItems

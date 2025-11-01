#pragma once
namespace MainMenuItems {
class CoverflowGroup : public mainmenu_group_impl {
 public:
  static const GUID m_guid;
  CoverflowGroup();
};

class CoverflowMainPopupMenu : public mainmenu_group_popup_impl {
 public:
  static const GUID m_guid;
  CoverflowMainPopupMenu();
};

class CoverflowMainPopupCommands : public mainmenu_commands {
 public:
  virtual t_uint32 get_command_count();
  virtual GUID get_command(t_uint32 p_index);
  virtual void get_name(t_uint32 p_index, pfc::string_base& p_out);
  virtual bool get_description(t_uint32 p_index, pfc::string_base& p_out);
  virtual GUID get_parent();
  virtual t_uint32 get_sort_priority();
  virtual void execute(t_uint32 p_index, service_ptr_t<service_base> p_callback);
  virtual bool get_display(t_uint32 p_index, pfc::string_base& p_text,
                           t_uint32& p_flags) {
    p_flags = flag_defaulthidden;
    get_name(p_index, p_text);
    return true;
  }

 private:
  enum EMenuItemIndex {
    miiReloadCollection = 0,
    numMenuItems,
  };
};
}  // namespace MainMenuItems

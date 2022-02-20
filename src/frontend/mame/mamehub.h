#ifndef __MAMEHUB_H__
#define __MAMEHUB_H__

#include "ui/ui.h"
#include "uiinput.h"

class mamehub_manager {
	DISABLE_COPYING(mamehub_manager);
private:
	// construction/destruction
	mamehub_manager();
public:
	static mamehub_manager *instance() {
    if (m_manager == NULL) {
      m_manager = new mamehub_manager();
    }
    return m_manager;
  }
	~mamehub_manager();

  void ui(mame_ui_manager& ui_manager, render_container &container);
  bool handleChat(running_machine& machine, ui_event& event);
private:
  static mamehub_manager* m_manager;
};

#endif

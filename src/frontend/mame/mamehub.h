#ifndef __MAMEHUB_H__
#define __MAMEHUB_H__

#include "ui/ui.h"
#include "uiinput.h"

#include <memory>

namespace mamehub {
class discord_directory_server;
}

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
  void set_discord_directory(std::unique_ptr<mamehub::discord_directory_server> dir);
  mamehub::discord_directory_server *discord_directory() const;
  void reset();
private:
  static mamehub_manager* m_manager;
  std::unique_ptr<mamehub::discord_directory_server> m_discord_directory;
};

#endif

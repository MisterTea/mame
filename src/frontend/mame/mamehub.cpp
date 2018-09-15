#include "ui/ui.h"

#include "emu.h"
#include "emuopts.h"
#include "mamehub.h"

#include "mame.h"
#include "ui/menu.h"

#include "NSM_Client.h"
#include "NSM_Common.h"
#include "NSM_Server.h"

mamehub_manager* mamehub_manager::m_manager = NULL;

std::list<ChatLog> chatLogs;
std::vector<char> chatString;
int chatEnabled = false;
int statsVisible = true;

extern int initialSyncPercentComplete;

extern bool waitingForClientCatchup;

mamehub_manager::mamehub_manager() {}

void mamehub_manager::ui(mame_ui_manager& ui_manager,
                         render_container& container) {
  if (waitingForClientCatchup && !netCommon->isRollback()) {
    char buf[4096];
    sprintf(buf, "A new client is joining, please wait...");
    ui_manager.draw_text_box(container, buf,
                             ui::text_layout::text_justify::CENTER, 0.5f, 0.5f,
                             rgb_t(255, 0, 0, 128));
  } else if ((!netClient && ui_manager.machine().options().client()) ||
             (netClient && netClient->isInitComplete() == false)) {
    ui_manager.draw_text_box(
        container, "Please wait for server to send entire game RAM...",
        ui::text_layout::text_justify::CENTER, 0.5f, 0.5f,
        rgb_t(255, 0, 0, 128));
    ui_manager.draw_text_box(container,
                             "This could take several minutes depending on "
                             "your connection and rom chosen...",
                             ui::text_layout::text_justify::CENTER, 0.5f, 0.6f,
                             rgb_t(255, 0, 0, 128));
    ui_manager.draw_text_box(
        container,
        "Once the initial sync is complete, you may just hear game audio for a "
        "few minutes, please be patient",
        ui::text_layout::text_justify::CENTER, 0.5f, 0.7f,
        rgb_t(255, 0, 0, 128));
    char buf[4096];
    sprintf(buf, "%0.2f%% Complete (Could be over 100 for large roms)...",
            float(initialSyncPercentComplete) / 10.0f);
    ui_manager.draw_text_box(container, buf,
                             ui::text_layout::text_justify::CENTER, 0.5f, 0.8f,
                             rgb_t(255, 0, 0, 128));
  }

  if (statsVisible) {
    if (netCommon) {
      std::string allLatencyString;
      for (int a = 0; a < netCommon->getNumPeers(); a++) {
        if (netCommon->getPeerID(a) == netCommon->getSelfPeerID() ||
            netCommon->getPeerID(a) == 0)
          continue;
        allLatencyString +=
            netCommon->getLatencyString(netCommon->getPeerID(a)) +
            std::string("\n");
      }
      ui_manager.draw_text_box(container, allLatencyString.c_str(),
                               ui::text_layout::text_justify::CENTER, 0.9f,
                               0.1f, rgb_t(255, 0, 0, 128));
      ui_manager.draw_text_box(container,
                               netCommon->getStatisticsString().c_str(),
                               ui::text_layout::text_justify::CENTER, 0.1f,
                               0.1f, rgb_t(255, 0, 0, 128));
    }
  }

  time_t curRealTime = time(NULL);
  for (std::list<ChatLog>::iterator it = chatLogs.begin();
       it != chatLogs.end();) {
    if (it->timeReceived + 8 < curRealTime) {
      std::list<ChatLog>::iterator it2 = it;
      it++;
      chatLogs.erase(it2);
    } else {
      it++;
    }
  }

  while (chatLogs.size() > 5) {
    chatLogs.erase(chatLogs.begin());
  }

  int chatIndex = 0;
  static const rgb_t chatColors[] = {
      rgb_t(192, 255, 255, 255), rgb_t(192, 255, 0, 0),
      rgb_t(192, 0, 128, 0),     rgb_t(192, 0, 0, 255),
      rgb_t(192, 128, 128, 0),   rgb_t(192, 128, 0, 128),
      rgb_t(192, 0, 128, 128),   rgb_t(192, 0, 0, 0),
      rgb_t(192, 128, 128, 128), rgb_t(192, 128, 128, 255),
      rgb_t(192, 128, 255, 128), rgb_t(192, 255, 255, 128),
      rgb_t(192, 128, 255, 128), rgb_t(192, 255, 128, 128),
  };
  for (std::list<ChatLog>::iterator it = chatLogs.begin(); it != chatLogs.end();
       it++) {
    /*
      float totalWidth,totalHeight;
      ui_draw_text_full(
      container,
      it->message.cstr(),
      0.1,
      0.6+0.1*chatIndex,
      0.8,
      JUSTIFY_LEFT,
      1,
      1,
      chatColors[it->playerID],
      rgb_t(255,0,0,0),
      &totalWidth,
      &totalHeight
      );
    */
    //
    ui_manager.draw_text_box(container, it->message.c_str(),
                             ui::text_layout::text_justify::CENTER, 0.5,
                             0.7 + 0.06 * chatIndex, chatColors[it->playerID]);
    //
    chatIndex++;
  }

  if (chatEnabled) {
    std::string promptString("Chat: _");
    if (chatString.size()) {
      promptString = std::string("Chat: ") +
                     std::string(&chatString[0], chatString.size()) +
                     std::string("_");
    }

    ui_manager.draw_text_box(container, promptString.c_str(),
                             ui::text_layout::text_justify::CENTER, 0.5f, 0.8f,
                             rgb_t(255, 0, 0, 0));
  }
}

bool mamehub_manager::handleChat(running_machine& machine, ui_event& event) {
  /* if this was a UI_EVENT_CHAR event, post it */
  std::cout << "HANDLING CHAT" << std::endl;
  if (!chatEnabled && (event.ch == 'N' || event.ch == 'n')) {
    statsVisible = !statsVisible;
    return true;
  } else if (!chatEnabled && (event.ch == 'T' || event.ch == 't') &&
             netCommon) {
    chatEnabled = true;
    chatString.clear();
    return true;
  } else if (chatEnabled) {
    if (event.ch == 13) {
      if (chatString.size()) {
        static std::vector<BlockValueLocation> locationsToIntersect;
        if (chatString[0] == '/') {
          // This is a command
          if (chatString.size() > 1 && chatString[1] >= '1' &&
              chatString[1] <= '9') {
            // TODO: Player swap
            netCommon->setPlayer(chatString[1] - '1');
          } else if (netServer &&
                     std::string(&chatString[0], chatString.size()) ==
                         std::string("/lock")) {
            netServer->setBlockNewClients(!netServer->isBlockNewClients());
            if (netServer->isBlockNewClients())
              machine.ui().popup_time(
                  3, "Game is locked and new clients cannot join.");
            else
              machine.ui().popup_time(
                  3, "Game is unlocked, new clients can join.");
          } else if (netServer &&
                     std::string(&chatString[0], 5) == std::string("/find")) {
            chatString.push_back(0);
            std::vector<BlockValueLocation> locations;
            // TODO: Fix forces
            // if(netServer) locations =
            // netServer->getLocationsWithValue(atoi(&chatString[6]),locationsToIntersect,machine.getRawMemoryRegions());
            for (int a = 0; a < locations.size() && a < 100; a++) {
              std::cout << locations[a].blockIndex << ' '
                        << locations[a].memoryStart << ' '
                        << locations[a].memorySize << std::endl;
            }
            locationsToIntersect = locations;
            machine.ui().popup_time(3, "Captured %d locations",
                                    (int)locationsToIntersect.size());
          } else if (netServer &&
                     std::string(&chatString[0], 6) == std::string("/force")) {
            for (int a = 0; a < locationsToIntersect.size(); a++) {
              // netServer->forceLocation(locationsToIntersect[a],atoi(&chatString[7]));
              std::string s = "00000000000000000";
              int value = atoi(&chatString[7]);
              s[0] = 2;  // TODO: Don't need to set this anymore
              memcpy(&(s[1]), &(locationsToIntersect[a].ramRegion),
                     sizeof(int));
              memcpy(&(s[2]), &(locationsToIntersect[a].blockIndex),
                     sizeof(int));
              memcpy(&(s[6]), &(locationsToIntersect[a].memoryStart),
                     sizeof(int));
              memcpy(&(s[10]), &(locationsToIntersect[a].memorySize),
                     sizeof(int));
              memcpy(&(s[14]), &(locationsToIntersect[a].memoryMask),
                     sizeof(unsigned char));
              memcpy(&(s[15]), &(value), sizeof(int));
              attotime curtime = machine.time();
              nsm::Attotime nsmAttotime;
              nsmAttotime.set_seconds(curtime.seconds());
              nsmAttotime.set_attoseconds(curtime.attoseconds());
              netServer->sendInputs(nsmAttotime,
                                    nsm::PeerInputData::FORCE_VALUE, s);
            }
            locationsToIntersect.clear();
          } else if (netServer &&
                     std::string(&chatString[0], 6) == std::string("/clear")) {
            locationsToIntersect.clear();
          }
        } else {
          chatString.push_back(0);
          // Send chat
          printf("SENDING CHAT %s\n", &chatString[0]);
          if (netCommon) {
            attotime curtime = machine.time();
            nsm::Attotime nsmAttotime;
            nsmAttotime.set_seconds(curtime.seconds());
            nsmAttotime.set_attoseconds(curtime.attoseconds());
            netCommon->sendInputs(
                nsmAttotime, nsm::PeerInputData::CHAT,
                std::string(&chatString[0], chatString.size()));
          }
        }
        chatString.clear();
      }
      chatEnabled = false;
    } else if (event.ch == 127 || event.ch == 8) {
      if (chatString.size()) chatString.pop_back();
    } else if (event.ch > 31) {
      chatString.push_back(event.ch);
    }
    return true;
  }

  return false;
}

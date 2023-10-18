#include "emu.h"
#include "emuopts.h"

//

#include "mamehub.h"

//

#include "NSM_Common.h"
#include "mame.h"
#include "ui/menu.h"
#include "ui/ui.h"

//

#include <algorithm>

mamehub_manager* mamehub_manager::m_manager = NULL;

class ChatLog {
 public:
  time_t timeReceived;
  std::string message;
  std::string userId;

  ChatLog(time_t _timeReceived, const std::string& _message,
          const std::string& _userId)
      : timeReceived(_timeReceived), message(_message), userId(_userId) {}
};

std::list<ChatLog> chatLogs;
std::vector<char> chatString;
int chatEnabled = false;
int statsVisible = true;
int chatCounter = 0;
std::map<std::string, std::string> lastChatFromUserId;
std::map<std::string, int> userIdColorMap;

extern int initialSyncPercentComplete;

extern bool waitingForClientCatchup;

std::set<std::string> seenForces;

mamehub_manager::mamehub_manager() {}

// for string delimiter
inline std::vector<std::string> SplitForceString(std::string s, std::string delimiter) {
    size_t pos_start = 0, pos_end, delim_len = delimiter.length();
    std::string token;
    std::vector<std::string> res;

    while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos) {
        token = s.substr (pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back (token);
    }

    res.push_back (s.substr (pos_start));
    return res;
}

void mamehub_manager::ui(mame_ui_manager& ui_manager,
                         render_container& container) {
  if (statsVisible) {
    if (netCommon) {
      ui_manager.draw_text_box(container, netCommon->getLatencyString().c_str(),
                               ui::text_layout::text_justify::CENTER, 0.9f,
                               0.1f, rgb_t(255, 0, 0, 128));
      ui_manager.draw_text_box(container,
                               netCommon->getStatisticsString().c_str(),
                               ui::text_layout::text_justify::CENTER, 0.1f,
                               0.1f, rgb_t(255, 0, 0, 128));
    }
  }

  time_t curRealTime = time(NULL);
  auto timestamp = ui_manager.machine().machine_time().to_msec();
  if (timestamp >= 1000 && netCommon) {

    {
      auto values = netCommon->getAllInputValues(timestamp, std::string("CHAT"));
      for (auto value : values) {
        auto userId = value.first;
        auto chat = value.second;
        if (lastChatFromUserId.find(userId) != lastChatFromUserId.end() &&
            lastChatFromUserId[userId] == chat) {
          // Already processed
          continue;
        }
        lastChatFromUserId[userId] = chat;
        auto slashPos = chat.find('/');
        if (slashPos == string::npos) {
          LOGFATAL << "Somehow got a bad chat: " << chat;
        }
        auto chatWithoutCounter = chat.substr(slashPos + 1);
        if (userIdColorMap.find(userId) == userIdColorMap.end()) {
          int index = min(int(userIdColorMap.size()), 14);
          userIdColorMap[userId] = index;
        }
        chatLogs.push_back(ChatLog(curRealTime, chatWithoutCounter, userId));
      }
    }

    {
	    auto valuesWithPrefix = netCommon->getAllInputValuesWithPrefix(timestamp, std::string("FORCE"));
      for (auto &outerIt : valuesWithPrefix) {
      for (auto &it : outerIt.second) {
        //auto userId = it.first;
        auto forceString = it.second;
        if (seenForces.find(forceString) != seenForces.end()) {
          // Already processed
          continue;
        }
        seenForces.insert(forceString);
		    LOG(INFO) << "GOT FORCE STRING " << forceString << endl;
        auto splitString = SplitForceString(forceString, "_");
        for (auto sit : splitString) {
          LOG(INFO) << "GOT SPLIT STRING " << sit << endl;
        }
        BlockValueLocation locationToForce(
          (unsigned char)stoi(splitString[0]),
          stoi(splitString[1]),
          stoi(splitString[2]),
          stoi(splitString[3]),
          stoi(splitString[4])
        );
        int value = stoi(splitString[5]);
        netCommon->forceLocation(locationToForce,value);
      }
      }
    }
  }

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
      rgb_t(192, 0, 128, 0),     rgb_t(192, 0, 0, 255),
      rgb_t(192, 128, 128, 0),   rgb_t(192, 128, 0, 128),
      rgb_t(192, 0, 128, 128),   rgb_t(192, 0, 0, 0),
      rgb_t(192, 255, 255, 255), rgb_t(192, 255, 0, 0),
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
    ui_manager.draw_text_box(
        container, it->message.c_str(), ui::text_layout::text_justify::CENTER,
        0.5, 0.7 + 0.06 * chatIndex, chatColors[userIdColorMap[it->userId]]);
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

std::vector<BlockValueLocation> locationsToIntersect;

bool mamehub_manager::handleChat(running_machine& machine, ui_event& event) {
#if 1
  /* if this was a UI_EVENT_CHAR event, post it */
  if (!chatEnabled && (event.ch == 'N' || event.ch == 'n')) {
    statsVisible = !statsVisible;
    return true;
  } else if (!chatEnabled && (event.ch == 'T' || event.ch == 't') &&
             netCommon) {
    chatEnabled = true;
    chatString.clear();
    return true;
  } else if (chatEnabled) {
    LOG(INFO) << "HANDLING CHAT" << std::endl;
    if (event.ch == 13) {
      if (chatString.size()) {
        if (chatString[0] == '/') {
          // This is a command
          if (chatString.size() > 1 && chatString[1] >= '1' &&
              chatString[1] <= '9') {
            // TODO: Player swap
            netCommon->setMyPlayers({chatString[1] - '1'});
            /*
          } else if (netCommon &&
                     std::string(&chatString[0], chatString.size()) ==
                         std::string("/lock")) {
            netCommon->setBlockNewClients(!netCommon->isBlockNewClients());
            if (netCommon->isBlockNewClients())
              machine.ui().popup_time(
                  3, "Game is locked and new clients cannot join.");
            else
              machine.ui().popup_time(
                  3, "Game is unlocked, new clients can join.");
                  */
          } else if (netCommon &&
                     std::string(&chatString[0], 5) == std::string("/find")) {
            if (chatString.size() == 5) {
              // Reset forces
              locationsToIntersect.clear();
              machine.ui().popup_time(3, "Reset captured locations");
            } else {
              chatString.push_back(0);
              std::vector<BlockValueLocation> locations;
              locations = netCommon->getLocationsWithValue(atoi(&chatString[6]),locationsToIntersect,{});
              for (int a = 0; a < locations.size() && a < 100; a++) {
                LOG(INFO) << locations[a].blockIndex << ' '
                          << locations[a].memoryStart << ' '
                          << locations[a].memorySize << std::endl;
              }
              locationsToIntersect = locations;
              machine.ui().popup_time(3, "Captured %d locations",
                                      (int)locationsToIntersect.size());
            }
          } else if (netCommon &&
                     std::string(&chatString[0], 6) == std::string("/force")) {
            int value = stoi(std::string(chatString.begin(), chatString.end()).substr(7));
            machine.ui().popup_time(3, "Forcing %d locations to %d", int(locationsToIntersect.size()), value);
            for (int a = 0; a < locationsToIntersect.size(); a++) {
              //netCommon->forceLocation(locationsToIntersect[a],value);
              int value = atoi(&chatString[7]);
              std::string s = to_string(int(locationsToIntersect[a].ramRegion)) +
                "_" + to_string(locationsToIntersect[a].blockIndex) +
                "_" + to_string(locationsToIntersect[a].memoryStart) +
                "_" + to_string(locationsToIntersect[a].memorySize) +
                "_" + to_string(int(locationsToIntersect[a].memoryMask)) +
                "_" + to_string(value);

              LOG(INFO) << "ATTACHING " << (string("FORCE/") + s) << endl;
              netCommon->attachToNextInputs(string("FORCE/") + s, s);
            }
            locationsToIntersect.clear();
          } else if (netCommon &&
                     std::string(&chatString[0], 6) == std::string("/clear")) {
            locationsToIntersect.clear();
          }
        } else {
          chatString.push_back(0);
          // Send chat
          printf("SENDING CHAT %s\n", &chatString[0]);
          if (netCommon) {
            attotime curtime = machine.time();
            netCommon->attachToNextInputs(
                string("CHAT"),
                std::to_string(chatCounter) + "/" + netCommon->getMyUserName() +
                    ": " + std::string(&chatString[0], chatString.size()));
            chatCounter++;
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
#endif
  return false;
}

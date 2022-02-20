#ifndef __NSM_COMMON_H__
#define __NSM_COMMON_H__

#include "zlib.h"

#include "NSM_CommonInterface.h"

#ifndef _MSVC_LANG
#define _MSVC_LANG (0)
#endif
#define ASIO_STANDALONE 1
#define BOOST_VERSION (0)

#include "ChronoMap.hpp"
#include "CryptoHandler.hpp"
#include "LogHandler.hpp"
#include "MyPeer.hpp"
#include "NetEngine.hpp"
#include "SingleGameServer.hpp"
#include "TimeHandler.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifdef interface
#undef interface
#endif

int zlibGetMaxCompressedSize(int origSize);
int lzmaGetMaxCompressedSize(int origSize);

void lzmaCompress(unsigned char *destBuf, int &destSize, unsigned char *srcBuf,
                  int srcSize, int compressionLevel);

void lzmaUncompress(unsigned char *destBuf, int destSize, unsigned char *srcBuf,
                    int srcSize);

enum OrderingChannelType {
  ORDERING_CHANNEL_INPUTS,
  ORDERING_CHANNEL_BASE_DELAY,
  ORDERING_CHANNEL_SYNC,
  ORDERING_CHANNEL_CONST_DATA,
  ORDERING_CHANNEL_END
};

enum CustomPacketType {
  ID_INPUTS,
  ID_BASE_DELAY,
  ID_INITIAL_SYNC_PARTIAL,
  ID_INITIAL_SYNC_COMPLETE,
  ID_RESYNC_PARTIAL,
  ID_RESYNC_COMPLETE,
  ID_SETTINGS,
  ID_REJECT_NEW_HOST,
  ID_ACCEPT_NEW_HOST,
  ID_HOST_ACCEPTED,
  ID_SEND_PEER_ID,
  ID_CLIENT_INFO,
  ID_MAMEHUB_TIMESTAMP,
  ID_END
};

class Common : public CommonBase {
 protected:
  shared_ptr<wga::NetEngine> netEngine;
  wga::PrivateKey privateKey;
  wga::PublicKey publicKey;
  shared_ptr<wga::MyPeer> myPeer;

  std::vector<std::pair<BlockValueLocation, int> > forcedLocations;

  std::vector<std::shared_ptr<MemoryBlock> > initialBlocks;
  std::vector<std::shared_ptr<MemoryBlock> > blocks;

  std::unordered_map<std::string, std::string> dataToAttach;

 public:
  Common(const string& userId, const string &privateKeyString, unsigned short _port, const string& lobbyHostname, unsigned short lobbyPort, int _unmeasuredNoise, const string& gameName, bool fakeLag);

  virtual ~Common();

  virtual void createMemoryBlock(
      const std::string &name, unsigned char *ptr, int size);

  int getLargestPing();

  virtual int64_t getLastSendTime() {
    return lastSendTime;
  }

  virtual int64_t getCurrentTime() {
    return wga::GlobalClock::currentTimeMicros() - 10*1000*1000;
  }

  virtual std::string getGameName() {
    return myPeer->getGameName();
  }

  std::shared_ptr<MemoryBlock> getMemoryBlock(int i) { return blocks[i]; }

  virtual void attachToNextInputs(const string& key, const string& value) {
    if (dataToAttach.find(key) != dataToAttach.end()) {
      LOG(FATAL) << "Tried to attach twice to the same key: " << key;
    }
    dataToAttach[key] = value;
  }

  virtual std::set<int> getMyPlayers();

  virtual void setMyPlayers(std::set<int> newPlayers);

  std::string getLatencyString();

  std::string getStatisticsString();

  std::vector<BlockValueLocation> getLocationsWithValue(
      unsigned int value,
      const std::vector<BlockValueLocation> &locationsToIntersect,
      const std::vector<std::pair<unsigned char *, int> > &ramBlocks);

  void forceLocation(BlockValueLocation location, unsigned int value) {
    forcedLocations.push_back(
        std::pair<BlockValueLocation, int>(location, value));
  }

  virtual unordered_map<string, string> getStateChanges(const unordered_map<string, string>& inputMap);
  virtual void sendInputs(int64_t inputTimeMs,
                  const unordered_map<string, string> &inputMap);

  void updateForces(
      const std::vector<std::pair<unsigned char *, int> > &ramBlocks);

  vector<uint8_t> computeChecksum(running_machine *machine);

  virtual std::unordered_map<std::string, std::vector<std::string>> getAllInputValues(int64_t ts);

  virtual bool isHosting();

 protected:
  std::string userId;
  std::string doInflate(const unsigned char *inputString, int length);
  int64_t lastSendTime;
  int unmeasuredNoise;
  set<int> myPlayers;
  pair<int64_t, std::unordered_map<std::string, std::vector<std::string>>> cachedInputValues;

  shared_ptr<wga::SingleGameServer> server;
};

#endif

#ifndef __NSM_COMMON_INTERFACE__
#define __NSM_COMMON_INTERFACE__

#include <assert.h>
#include <stdlib.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_map>

#include "zlib.h"

#ifdef SHA1
#undef SHA1
#endif

#include "Headers.hpp"

#define SHA1(x)             "S" #x

#ifdef interface
#undef interface
#endif

#ifdef basename
#undef basename
#endif

using namespace std;

class running_machine;

class ChatLog {
 public:
  int playerID;
  time_t timeReceived;
  std::string message;

  ChatLog(int _playerID, time_t _timeReceived, const std::string &_message)
      : playerID(_playerID), timeReceived(_timeReceived), message(_message) {}
};

class MemoryBlock {
 public:
  std::string name;
  unsigned char *data;
  int size;
  bool ownsMemory;

  MemoryBlock(const std::string &_name, int _size);
  MemoryBlock(const std::string &_name, unsigned char *_data, int _size);
  ~MemoryBlock();

 private:
  MemoryBlock(MemoryBlock const &);
  MemoryBlock &operator=(MemoryBlock const &);
};

class BlockValueLocation {
 public:
  unsigned char ramRegion;
  int blockIndex, memoryStart, memorySize;
  unsigned char memoryMask;

  BlockValueLocation(unsigned char _ramRegion, int _blockIndex,
                     int _memoryStart, int _memorySize,
                     unsigned char _memoryMask)
      : ramRegion(_ramRegion),
        blockIndex(_blockIndex),
        memoryStart(_memoryStart),
        memorySize(_memorySize),
        memoryMask(_memoryMask) {}

  bool operator<(const BlockValueLocation &other) const {
    if (ramRegion < other.ramRegion)
      return true;
    else if (ramRegion > other.ramRegion)
      return false;

    if (blockIndex < other.blockIndex)
      return true;
    else if (blockIndex > other.blockIndex)
      return false;

    if (memoryStart < other.memoryStart)
      return true;
    else if (memoryStart > other.memoryStart)
      return false;

    if (memorySize < other.memorySize)
      return true;
    else if (memorySize > other.memorySize)
      return false;

    if (memoryMask < other.memoryMask)
      return true;
    else if (memoryMask > other.memoryMask)
      return false;

    return false;
  }
};

 class CommonBase {
 public:
  CommonBase() {}

  virtual ~CommonBase() {}

  virtual int getLargestPing() = 0;

  virtual void createMemoryBlock(
      const std::string &name, unsigned char *ptr, int size) = 0;

  virtual std::string getLatencyString() = 0;

  virtual std::string getStatisticsString() = 0;

  virtual std::set<int> getMyPlayers() = 0;

  virtual void setMyPlayers(std::set<int> newPlayers) = 0;

  virtual int64_t getLastSendTime() = 0;

  virtual int64_t getCurrentTime() = 0;

  virtual std::string getGameName() = 0;

  virtual std::vector<BlockValueLocation> getLocationsWithValue(
      unsigned int value,
      const std::vector<BlockValueLocation> &locationsToIntersect,
      const std::vector<std::pair<unsigned char *, int> > &ramBlocks) = 0;

  virtual void forceLocation(BlockValueLocation location,
                             unsigned int value) = 0;

  virtual void updateForces(
      const std::vector<std::pair<unsigned char *, int> > &ramBlocks) = 0;

  virtual void attachToNextInputs(const string& key, const string& value) = 0;

  virtual std::unordered_map<std::string, std::vector<std::string>> getAllInputValues(int64_t ts) = 0;

  virtual unordered_map<string, string> getStateChanges(const unordered_map<string, string>& inputMap) = 0;
  virtual void sendInputs(int64_t inputTimeMs,
                  const unordered_map<string, string> &inputMap) = 0;

  virtual void createInitialBlocks(running_machine *machine) {}

  virtual bool isHosting() = 0;
};

CommonBase* createNetCommon(const string& userId,
	const string& privateKeyString, unsigned short _port, const string& lobbyHostname, unsigned short lobbyPort, int _unmeasuredNoise, const string& gameName, bool fakeLag);
void deleteNetCommon();
string makePrivateKey();

extern CommonBase* netCommon;


#endif

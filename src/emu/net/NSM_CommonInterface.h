#ifndef __NSM_COMMON_INTERFACE__
#define __NSM_COMMON_INTERFACE__

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
#include <stdexcept>
#include <assert.h>
#include <stdlib.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "nsm.pb.h"

inline bool operator==(const google::protobuf::MessageLite& msg_a,
                const google::protobuf::MessageLite& msg_b) {
  return (msg_a.GetTypeName() == msg_b.GetTypeName()) &&
      (msg_a.SerializeAsString() == msg_b.SerializeAsString());
}

#include "zlib.h"

class ClientInterface;
class ServerInterface;
class CommonInterface;

CommonInterface *createGlobalServer(std::string _username, unsigned short _port, int _unmeasuredNoise,
                           bool _rollback);
void deleteGlobalServer();

CommonInterface *createGlobalClient(std::string _username);
void deleteGlobalClient();

extern CommonInterface *netClient;
extern CommonInterface *netServer;
extern CommonInterface *netCommon;

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

class PeerData {
 public:
  std::string name;
  std::list<nsm::PeerInputData> availableInputs;
  std::map<int, nsm::PeerInputData> delayedInputs;

  std::list<nsm::PeerInputData> oldInputs;
  nsm::Attotime startTime;
  nsm::Attotime lastInputTime;
  int nextGC;

  PeerData() {}

  PeerData(std::string _name, nsm::Attotime _startTime)
      : name(_name),
        startTime(_startTime),
        lastInputTime(startTime),
        nextGC(0) {}
};

class CommonInterface {
 public:
  CommonInterface() {}

  virtual ~CommonInterface() {}

  virtual int getLargestPing(int machineSeconds) = 0;

  virtual int getSecondsBetweenSync() = 0;

  virtual void setSecondsBetweenSync(int _secondsBetweenSync) = 0;

  virtual std::vector<std::shared_ptr<MemoryBlock> > createMemoryBlock(
      const std::string &name, unsigned char *ptr, int size) = 0;

  virtual bool update(running_machine *machine) = 0;

  nsm::Attotime newAttotime(int seconds, long long attoseconds) {
    nsm::Attotime at;
    at.set_seconds(seconds);
    at.set_attoseconds(attoseconds);
    return at;
  }

  virtual bool hasPeerWithID(int peerID) = 0;

  virtual std::string getLatencyString(int peerID) = 0;

  virtual std::string getStatisticsString() = 0;

  virtual void getPeerIDs(std::vector<int> &retval) = 0;

  virtual int getNumPeers() = 0;

  virtual int getPeerID(int a) = 0;

  virtual nsm::PeerInputData popInput(int peerID) = 0;

  virtual int getSelfPeerID() = 0;

  virtual std::string getPeerNameFromID(int id) = 0;

  virtual std::vector<BlockValueLocation> getLocationsWithValue(
      unsigned int value,
      const std::vector<BlockValueLocation> &locationsToIntersect,
      const std::vector<std::pair<unsigned char *, int> > &ramBlocks) = 0;

  virtual void forceLocation(BlockValueLocation location, unsigned int value) = 0;

  virtual void updateForces(
      const std::vector<std::pair<unsigned char *, int> > &ramBlocks) = 0;

  virtual void sendInputs(const nsm::Attotime &inputTime,
                  nsm::PeerInputData::PeerInputType inputType,
                  const nsm::InputState &inputState) = 0;
  virtual void sendInputs(const nsm::Attotime &inputTime,
                  nsm::PeerInputData::PeerInputType inputType,
                  const std::string &inputString) = 0;

  virtual void receiveInputs(const nsm::PeerInputDataList *inputDataList) = 0;

  virtual std::pair<int, nsm::Attotime> getOldestPeerInputTime() = 0;

  virtual int getPlayer() = 0;

  virtual void setPlayer(int newPlayer) = 0;

  virtual bool isRollback() = 0;

  virtual bool connect(unsigned short selfPort,const char *hostname,unsigned short port,running_machine *machine) {
    return false;
  }

  virtual bool serve() {
    return false;
  }

  virtual bool sync(running_machine* machine) = 0;

  virtual void createInitialBlocks(running_machine* machine) {

  }

  virtual void updateSyncCheck() {
  }

  virtual void shutdown() = 0;

  virtual int64_t getCurrentServerTime() {
    return 0;
  }

  virtual void setSyncTransferTime(int _syncTransferSeconds) {}

  virtual bool isInitComplete() { return false; }

  virtual void setBlockNewClients(bool b) {}

  virtual bool isBlockNewClients() { return false; }
};

#endif

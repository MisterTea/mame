#include "emu.h"
#undef SHA1

#include <chrono>
#include <stdexcept>

#include "ChronoMap.hpp"
#include "LogHandler.hpp"
#include "NSM_Common.h"
#include "NSM_CommonInterface.h"
#include "TimeHandler.hpp"
#include "lzma/C/LzmaDec.h"
#include "lzma/C/LzmaEnc.h"

using namespace std;

CommonBase *netCommon = NULL;
CommonBase *createNetCommon(const string &userId,
                            const string &privateKeyString,
                            unsigned short _port, const string &lobbyHostname,
                            unsigned short lobbyPort, int _unmeasuredNoise,
                            const string &gameName, bool fakeLag) {
  netCommon = new Common(userId, privateKeyString, _port, lobbyHostname,
                         lobbyPort, _unmeasuredNoise, gameName, fakeLag);
  return netCommon;
}

void deleteNetCommon() {
  if (netCommon) {
    delete netCommon;
  }
  netCommon = NULL;
}

string makePrivateKey() {
  auto randchar = []() -> char {
    const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    const size_t max_index = (sizeof(charset) - 1);
    return charset[rand() % max_index];
  };
  const int length = 16;
  std::string str(length, 0);
  std::generate_n(str.begin(), length, randchar);
  return str;
}

SRes OnProgress(const ICompressProgress *p, UInt64 inSize, UInt64 outSize) {
  // Update progress bar.
  return SZ_OK;
}
ICompressProgress g_ProgressCallback = {&OnProgress};

void *AllocForLzma(ISzAllocPtr p, size_t size) {
  void *ptr = malloc(size);
  if (!ptr) {
    LOG(INFO) << "CANNOT ALLOCATE BLOCK OF SIZE: " << (size / 1024.0 / 1024.0)
              << " MB!\n";
    throw std::runtime_error("FAILED TO ALLOCATE BLOCK");
  }
  return ptr;
}
void FreeForLzma(ISzAllocPtr p, void *address) { free(address); }
ISzAlloc SzAllocForLzma = {&AllocForLzma, &FreeForLzma};

int zlibGetMaxCompressedSize(int origSize) { return origSize * 1.01 + 256; }
int lzmaGetMaxCompressedSize(int origSize) {
  return origSize + origSize / 3 + 256 + LZMA_PROPS_SIZE;
}

void lzmaCompress(unsigned char *destBuf, int &destSize, unsigned char *srcBuf,
                  int srcSize, int compressionLevel) {
  SizeT propsSize = LZMA_PROPS_SIZE;

  SizeT lzmaDestSize = (SizeT)destSize;

  CLzmaEncProps props;
  LzmaEncProps_Init(&props);
  props.level = compressionLevel;  // compression level
  // props.dictSize = 1 << 16;
  props.dictSize = 1 << 24;
  props.writeEndMark = 1;  // 0 or 1
  LzmaEncProps_Normalize(&props);

  int res = LzmaEncode(destBuf + LZMA_PROPS_SIZE, &lzmaDestSize, srcBuf,
                       srcSize, &props, destBuf, &propsSize, props.writeEndMark,
                       &g_ProgressCallback, &SzAllocForLzma, &SzAllocForLzma);

  destSize = (int)lzmaDestSize + LZMA_PROPS_SIZE;

  LOG(INFO) << "COMPRESSED " << srcSize << " BYTES DOWN TO " << destSize
            << endl;

  if (res != SZ_OK || propsSize != LZMA_PROPS_SIZE) {
    LOG(INFO) << "ERROR COMPRESSING DATA\n";
    LOG(INFO) << res << ',' << propsSize << ',' << LZMA_PROPS_SIZE << endl;
    exit(1);
  }
}

void lzmaUncompress(unsigned char *destBuf, int destSize, unsigned char *srcBuf,
                    int srcSize) {
  SizeT lzmaDestSize = (SizeT)destSize;
  SizeT lzmaSrcSize = (SizeT)srcSize - LZMA_PROPS_SIZE;

  LOG(INFO) << "DECOMPRESSING " << srcSize << endl;

  ELzmaStatus finishStatus;
  int res = LzmaDecode(destBuf, &lzmaDestSize, srcBuf + LZMA_PROPS_SIZE,
                       &lzmaSrcSize, srcBuf, LZMA_PROPS_SIZE, LZMA_FINISH_END,
                       &finishStatus, &SzAllocForLzma);

  LOG(INFO) << "DECOMPRESSED " << srcSize << " BYTES DOWN TO " << lzmaDestSize
            << endl;

  if (res != SZ_OK || finishStatus != LZMA_STATUS_FINISHED_WITH_MARK) {
    LOG(INFO) << "ERROR DECOMPRESSING DATA\n";
    LOG(INFO) << res << ',' << finishStatus << endl;
    exit(1);
  }
}

MemoryBlock::MemoryBlock(const std::string &_name, int _valSize, int _count)
    : name(_name), valSize(_valSize), count(_count), ownsMemory(true) {
  data = (unsigned char *)malloc(_valSize*_count);
  if (!data) {
    throw std::runtime_error("Ran out of memory");
  }
  memset(data, 0, _valSize*_count);
}

MemoryBlock::MemoryBlock(const std::string &_name, unsigned char *_data,
                         int _valSize, int _count)
    : name(_name), data(_data), valSize(_valSize), count(_count), ownsMemory(false) {}

MemoryBlock::~MemoryBlock() {
  if (ownsMemory) {
    free(data);
    data = NULL;
  }
}

extern volatile bool memoryBlocksLocked;

Common::Common(const string &_userId, const string &privateKeyString,
               unsigned short _port, const string &lobbyHostname,
               unsigned short lobbyPort, int _unmeasuredNoise,
               const string &gameName, bool fakeLag)
    : userId(_userId),
      lastSendTime(0),
      unmeasuredNoise(_unmeasuredNoise),
      cachedInputValues(-1, {}) {
  if (fakeLag) {
    wga::GlobalClock::addNoise();
    wga::ALL_RPC_FLAKY = true;
  }

  bool noLobby = (lobbyHostname == "none");
  bool localLobby = (lobbyHostname == "self");

  if (userId == "none") {
    if (localLobby || noLobby) {
      userId = "Server";
    } else {
      userId = "Client";
    }
  }

  auto startConnectTime =
      chrono::duration_cast<chrono::microseconds>(
          chrono::high_resolution_clock::now().time_since_epoch())
          .count();
  netEngine.reset(new wga::NetEngine());
  if (privateKeyString == "none") {
    privateKey = wga::CryptoHandler::makePrivateKeyFromPassword(userId);
  } else {
    privateKey = wga::CryptoHandler::makePrivateKeyFromB64(privateKeyString);
  }
  publicKey = wga::CryptoHandler::makePublicFromPrivate(privateKey);
  LOG(INFO) << "Using public key: "
            << wga::CryptoHandler::keyToString(publicKey);

  if (localLobby || noLobby) {
    LOG(INFO) << "Running local lobby for id " << userId;
    server.reset(new wga::SingleGameServer(netEngine, lobbyPort, userId,
                                           publicKey, "Server", noLobby?1:2));
    server->start();
  }

  myPeer.reset(new wga::MyPeer(userId, privateKey, _port,
                               (localLobby || noLobby) ? "" : lobbyHostname, lobbyPort,
                               userId));
  if (myPeer->isHosting()) {
    LOG(INFO) << "Hosting game: " << gameName;
    myPeer->host(gameName);
  } else {
    LOG(INFO) << "Joining game: ";
    myPeer->join();
  }

  netEngine->start();
  myPeer->start();
  while (!myPeer->initialized()) {
    LOG(INFO) << "Waiting for initialization for peer...";
    wga::microsleep(1000 * 1000);
  }

  myPlayers = {myPeer->getPosition()};
  if (myPeer->getPosition() == -1) {
    LOGFATAL << "Somehow didn't get my player position";
  }

  if (myPeer->isHosting()) {
    machineTimeShift =
        (chrono::duration_cast<chrono::microseconds>(
             chrono::high_resolution_clock::now().time_since_epoch())
             .count() -
         startConnectTime);
    unordered_map<string, string> machineTimeShiftMap = {
        {"__BASE_TIME__", to_string(machineTimeShift)}};
    myPeer->updateState(2, machineTimeShiftMap);
  } else {
    myPeer->updateState(2, {{"__BASE_TIME__", to_string(0)}});
    auto retval = myPeer->getAllInputValues(1).at("__BASE_TIME__");
    for (auto it : retval) {
      if (it.second == "0") {
        continue;
      }
      machineTimeShift = stoll(it.second);
    }
  }

  if (getCurrentTime() < 0) {
    LOG(INFO) << "Waiting for time==0";
    wga::microsleep(std::max(int64_t(0), getCurrentTime() * -1));
  } else {
    LOG(INFO) << "AT TIME: " << getCurrentTime();
  }
}

std::string Common::getMyUserName() { return myPeer->getMyUserName(); }

set<int> Common::getMyPlayers() { return myPlayers; }

void Common::setMyPlayers(std::set<int> newPlayers) { myPlayers = newPlayers; }

Common::~Common() {
  myPeer->shutdown();
  server.reset();
  netEngine->shutdown();
}

void Common::createMemoryBlock(const std::string &name, unsigned char *ptr,
                               int valSize, int count) {
  if (blocks.size() == 39) {
    // throw ("OOPS");
  }
  // printf("Creating memory block at %X with size %d\n",ptr,size);
  blocks.push_back(shared_ptr<MemoryBlock>(new MemoryBlock(name, ptr, valSize, count)));
  initialBlocks.push_back(shared_ptr<MemoryBlock>(new MemoryBlock(name, valSize, count)));
}

time_t lastSecondChecked = 0;
double predictedPingMean = 100.0;
double predictedPingVariance = 10.0;
int numPingSamples = 0;

int Common::getLargestPing() {
  return max(35, 10 + int(ceil(myPeer->getHalfPingUpperBound() / 1000.0)));
}

string Common::getLatencyString() {
  /*
  for (std::map<wga::PublicKey, int>::iterator it = peerIDs.begin();
       it != peerIDs.end(); it++) {
    if (it->second == peerID) {
      char buf[4096];
      sprintf(buf, "Peer %d: %d ms", peerID,
              100);  // TODO: Replace with peer ping
      return string(buf);
    }
  }
  printf("ERROR GETTING LATENCY STRING\n");
  */

  auto latencyData = myPeer->getPeerLatency();
  string latencyString;
  int i = 0;
  for (const auto &it : latencyData) {
    i++;
    latencyString += "Peer " + to_string(i) + ": " +
                     to_string(int64_t(it.second.first / 1000.0)) + " / " +
                     to_string(int64_t(it.second.second / 1000.0)) + "\n";
  }
  return latencyString;
}

string Common::getStatisticsString() {
  /*
  RakNet::RakNetStatistics *rss;
  string retval;
  for (int a = 0; a < rakInterface->NumberOfConnections(); a++) {
    char message[4096];
    rss =
        rakInterface->GetStatistics(rakInterface->GetSystemAddressFromIndex(a));
    sprintf(message,
            "Sent: %d\n"
            "Recv: %d\n"
            "Loss: %.0f%%\n"
            "Latency: %dms\n",
            (int)rss->valueOverLastSecond[RakNet::ACTUAL_BYTES_SENT],
            (int)rss->valueOverLastSecond[RakNet::ACTUAL_BYTES_RECEIVED],
            rss->packetlossLastSecond,
            int((predictedPingMean + sqrt(predictedPingVariance) * 2) / 2));
    retval += string(message) + string("\n");
  }
  return retval;
  */
  return "TODO";
}

/*
  void addSubByteLocations(const vector<MemoryBlock> &blocks,unsigned char
  value,const set<BlockValueLocation>
  &locationsToIntersect,vector<BlockValueLocation> &newLocations) {
  //LOG(INFO) << "ON SIZE: " << sizeof(T) << endl;
  for(int a=0;a<(int)blocks.size();a++)
  {
  //LOG(INFO) << "ON BLOCK: " << a << " WITH SIZE " << blocks[a].size << endl;
  for(int b=0;b<int(blocks[a].size);b++)
  {
  //LOG(INFO) << "BLOCK INDEX: " << b << endl;
  //if(b<=(int(blocks[a].size)-sizeof(T)))
  //LOG(INFO) << "IN RANGE\n";
  //LOG(INFO) << "VALUE: " << *((T*)(blocks[a].data+b)) << endl;
  for(int c=3;c<8;c++) {
  unsigned char mask = 0;
  for(int d=0;d<c;d++) mask |= (1<<d);

  if((value&mask)!=value) continue; //Mask is not big enough

  int maxShift = 8-c;

  for(int d=0;d<=maxShift;d++) {
  if((value<<d) == ((blocks[a].data[b])&(mask<<d))) {
  bool doNotAdd=false;
  if(!doNotAdd)
  {
  BlockValueLocation bvl(a,b,1,(mask<<d));
  if(locationsToIntersect.empty()==false)
  {
  doNotAdd=false;
  if(locationsToIntersect.find(bvl)==locationsToIntersect.end())
  doNotAdd=true;
  if(!doNotAdd)
  {
  newLocations.push_back(bvl);
  }
  }
  else
  {
  newLocations.push_back(bvl);
  }
  }
  }
  }
  }
  }
  }
  }
*/

template <class T>
void addLocations(const vector<shared_ptr<MemoryBlock>> &blocks,
                  T value,
                  const set<BlockValueLocation> &locationsToIntersect,
                  vector<BlockValueLocation> &newLocations,
                  const vector<pair<unsigned char *, int>> &ramBlocks) {
  // LOG(INFO) << "ON SIZE: " << sizeof(T) << endl;
  for (int a = 0; a < (int)blocks.size(); a++) {
    // LOG(INFO) << "ON BLOCK: " << a << " WITH SIZE " << blocks[a].size <<
    // endl;
    if (blocks[a]->valSize != sizeof(T)) {
      continue;
    }
    for (int b = 0; b < int(blocks[a]->count); b++) {
      // LOG(INFO) << "BLOCK INDEX: " << b << endl;
      // if(b<=(int(blocks[a].size)-sizeof(T)))
      // LOG(INFO) << "IN RANGE\n";
      // LOG(INFO) << "VALUE: " << *((T*)(blocks[a].data+b)) << endl;
      //if (b > (int(blocks[a]->size) - sizeof(T))) continue;

      if (value == 0 || ((value) == *((T *)(blocks[a]->data + (b*blocks[a]->valSize))))) {
        bool doNotAdd = false;
        if (!doNotAdd) {
          BlockValueLocation bvl(0, a, b*blocks[a]->valSize, sizeof(T), 0);
          if (locationsToIntersect.empty() == false) {
            doNotAdd = false;
            if (locationsToIntersect.find(bvl) == locationsToIntersect.end())
              doNotAdd = true;
            if (!doNotAdd) {
              newLocations.push_back(bvl);
            }
          } else {
            newLocations.push_back(bvl);
          }
        }
      }
    }
  }
  // LOG(INFO) << "ON SIZE: " << sizeof(T) << endl;
  for (int a = 0; a < (int)ramBlocks.size(); a++) {
    // LOG(INFO) << "ON BLOCK: " << a << " WITH SIZE " << blocks[a].size <<
    // endl;
    for (int b = 0; b < int(ramBlocks[a].second); b++) {
      // LOG(INFO) << "BLOCK INDEX: " << b << endl;
      // if(b<=(int(blocks[a].size)-sizeof(T)))
      // LOG(INFO) << "IN RANGE\n";
      // LOG(INFO) << "VALUE: " << *((T*)(blocks[a].data+b)) << endl;
      if (b > (int(ramBlocks[a].second) - sizeof(T))) continue;
      if ((((T)value) == *((T *)(ramBlocks[a].first + b)))) {
        bool doNotAdd = false;
        if (!doNotAdd) {
          BlockValueLocation bvl(1, a, b, sizeof(T), 0);
          if (locationsToIntersect.empty() == false) {
            doNotAdd = false;
            if (locationsToIntersect.find(bvl) == locationsToIntersect.end())
              doNotAdd = true;
            if (!doNotAdd) {
              newLocations.push_back(bvl);
            }
          } else {
            newLocations.push_back(bvl);
          }
        }
      }
    }
  }
}

vector<BlockValueLocation> Common::getLocationsWithValue(
    unsigned int value, const vector<BlockValueLocation> &locationsToIntersect,
    const vector<pair<unsigned char *, int>> &ramBlocks) {
  set<BlockValueLocation> locationsSet(locationsToIntersect.begin(),
                                       locationsToIntersect.end());
  // LOG(INFO) << "CHECKING FOR " << value << endl;
  vector<BlockValueLocation> newLocations;
  addLocations<unsigned int>(blocks, value, locationsSet, newLocations,
                             ramBlocks);
  // addLocations<signed int>(blocks,value,locationsSet,newLocations);
  addLocations<unsigned short>(blocks, (unsigned short)value, locationsSet, newLocations,
                               ramBlocks);
  // addLocations<signed short>(blocks,value,locationsSet,newLocations);
  addLocations<unsigned char>(blocks, (unsigned char)value, locationsSet, newLocations,
                              ramBlocks);
  // addLocations<signed char>(blocks,value,locationsSet,newLocations);
  // addSubByteLocations(blocks,value,locationsSet,newLocations);
  return newLocations;
}

void Common::updateForces(const vector<pair<unsigned char *, int>> &ramBlocks) {
  for (int a = 0; a < forcedLocations.size(); a++) {
    BlockValueLocation &bvl = forcedLocations[a].first;
    if (bvl.ramRegion == 0) {
      if (bvl.memorySize == 1) {
        if (bvl.memoryMask > 0) {
          unsigned char curValue =
              ((*((unsigned char *)(blocks[bvl.blockIndex]->data +
                                    bvl.memoryStart))) &
               (~(bvl.memoryMask)));
          // Calculate the shift from the mask
          int shift = 0;
          unsigned char tmpMask = bvl.memoryMask;
          while ((tmpMask & 1) == 0) {
            shift++;
            tmpMask >>= 1;
          }
          *((unsigned char *)(blocks[bvl.blockIndex]->data + bvl.memoryStart)) =
              ((((unsigned char)forcedLocations[a].second) << shift) |
               curValue);
        } else {
          *((unsigned char *)(blocks[bvl.blockIndex]->data + bvl.memoryStart)) =
              (unsigned char)forcedLocations[a].second;
        }
      }
      if (bvl.memorySize == 2) {
        *((unsigned short *)(blocks[bvl.blockIndex]->data + bvl.memoryStart)) =
            (unsigned short)forcedLocations[a].second;
      }
      if (bvl.memorySize == 4) {
        *((unsigned int *)(blocks[bvl.blockIndex]->data + bvl.memoryStart)) =
            (unsigned int)forcedLocations[a].second;
      }
    } else {
      if (bvl.memorySize == 1) {
        if (bvl.memoryMask > 0) {
          unsigned char curValue =
              ((*((unsigned char *)(ramBlocks[bvl.blockIndex].first +
                                    bvl.memoryStart))) &
               (~(bvl.memoryMask)));
          // Calculate the shift from the mask
          int shift = 0;
          unsigned char tmpMask = bvl.memoryMask;
          while ((tmpMask & 1) == 0) {
            shift++;
            tmpMask >>= 1;
          }
          *((unsigned char *)(ramBlocks[bvl.blockIndex].first +
                              bvl.memoryStart)) =
              ((((unsigned char)forcedLocations[a].second) << shift) |
               curValue);
        } else {
          *((unsigned char *)(ramBlocks[bvl.blockIndex].first +
                              bvl.memoryStart)) =
              (unsigned char)forcedLocations[a].second;
        }
      }
      if (bvl.memorySize == 2) {
        *((unsigned short *)(ramBlocks[bvl.blockIndex].first +
                             bvl.memoryStart)) =
            (unsigned short)forcedLocations[a].second;
      }
      if (bvl.memorySize == 4) {
        *((unsigned int *)(ramBlocks[bvl.blockIndex].first + bvl.memoryStart)) =
            (unsigned int)forcedLocations[a].second;
      }
    }
  }
}

unordered_map<string, string> Common::getStateChanges(
    const unordered_map<string, string> &inputMap) {
  return myPeer->getStateChanges(inputMap);
}

void Common::sendInputs(int64_t inputTimeMs,
                        unordered_map<string, string> inputMap) {
  if (myPeer->getLivingPeerCount() == 0 && myPeer->getTotalPeerCount() > 1) {
    LOG(INFO) << "Finished!";
    cout << "FINISHED" << endl;
    myPeer->shutdown();
    exit(0);
  }
  if (inputTimeMs <= 1) {
    VLOG(1) << "SKIPPING FIRST INPUT BLOCK" << endl;
    return;
  }
  inputMap.insert(dataToAttach.begin(), dataToAttach.end());
  dataToAttach.clear();
  VLOG(1) << "SENDING INPUTS AT TIME " << inputTimeMs << endl;
  myPeer->updateState(inputTimeMs, inputMap);
  lastSendTime = inputTimeMs;
}

vector<uint8_t> Common::computeChecksum(running_machine *machine) {
  LOG(INFO) << "Computing checksums..." << endl;

  machine->save().dispatch_presave();

  // LOG(INFO) << "IN CRITICAL SECTION\n";
  // LOG(INFO) << "SERVER: Syncing with clients\n";
  vector<uint8_t> blockChecksums;
  for (int blockIndex = 0; blockIndex < int(blocks.size()); blockIndex++) {
    MemoryBlock &block = *(blocks[blockIndex]);
    MemoryBlock &initialBlock = *(initialBlocks[blockIndex]);
    uint8_t blockChecksum = 0;

    for (int a = 0; a < block.size(); a++) {
      blockChecksum = blockChecksum ^ block.data[a];
    }
    memcpy(initialBlock.data, block.data, block.size());
    blockChecksums.push_back(blockChecksum);
    printf("BLOCK %d CHECKSUM: %d\n", blockIndex, int(blockChecksum));
  }

  machine->save().dispatch_postload();

  return blockChecksums;
}

std::map<std::string, std::string> Common::getAllInputValues(
    int64_t ts, const std::string &key) {
  std::unordered_map<std::string, std::map<std::string, std::string>>
      allInputData;
  if (cachedInputValues.first == ts) {
    allInputData = cachedInputValues.second;
  } else {
    allInputData = myPeer->getAllInputValues(ts);
    int livingPeerCount = myPeer->getLivingPeerCount();
    if (livingPeerCount == 0 && myPeer->getTotalPeerCount() > 1) {
      return {};
    }
    cachedInputValues = make_pair(ts, allInputData);
  }

  if (allInputData.find(key) == allInputData.end()) {
    return {};
  } else {
    return allInputData.at(key);
  }
}

std::unordered_map<std::string, std::map<std::string, std::string>> Common::getAllInputValuesWithPrefix(
    int64_t ts, const std::string &keyPrefix) {
  std::unordered_map<std::string, std::map<std::string, std::string>>
      allInputData;
  if (cachedInputValues.first == ts) {
    allInputData = cachedInputValues.second;
  } else {
    allInputData = myPeer->getAllInputValues(ts);
    int livingPeerCount = myPeer->getLivingPeerCount();
    if (livingPeerCount == 0 && myPeer->getTotalPeerCount() > 1) {
      return {};
    }
    cachedInputValues = make_pair(ts, allInputData);
  }

  std::unordered_map<std::string, std::map<std::string, std::string>> retval;
  for (auto& it : allInputData) {
    if (it.first.compare(0, keyPrefix.length(), keyPrefix) == 0) {
      retval[it.first] = it.second;
    }
  }

  return retval;
}

bool Common::isHosting() { return myPeer->isHosting(); }

#include "NSM_Common.h"

#include "zlib.h"

Server *createGlobalServer(std::string _username,unsigned short _port, int _unmeasuredNoise,
  bool _rollback);

void deleteGlobalServer();

class running_machine;

class NameGuidAddressTriple {
public:
  std::string name;
  RakNet::RakNetGUID guid;
  RakNet::SystemAddress systemAddress;

NameGuidAddressTriple(
  std::string _name,
  RakNet::RakNetGUID _guid,
  RakNet::SystemAddress _systemAddress) :
  name(_name),
    guid(_guid),
    systemAddress(_systemAddress) {
    }
};

class Server : public Common
{
 public:
  bool syncOverride;

 protected:
  std::vector<std::shared_ptr<MemoryBlock> > initialBlocks;
  nsm::Attotime staleTime;
  int staleGeneration;

  int port;

  int syncCount;

  std::list<std::pair<unsigned char *,int> > syncPacketQueue;

  int syncTransferSeconds;

  std::vector<RakNet::RakNetGUID> acceptedPeers;
  std::map<RakNet::RakNetGUID,std::vector<RakNet::RakNetGUID> > waitingForAcceptFrom;
  int maxPeerID;
  std::map<RakNet::RakNetGUID,int> deadPeerIDs;
  std::vector<NameGuidAddressTriple> potentialCandidates;
  std::map<RakNet::RakNetGUID,std::string> candidateNames;

  bool blockNewClients;

  std::shared_ptr<std::thread> syncThread;
  bool syncReady;
  nsm::Sync syncProto;

 public:
  Server(std::string _username,int _port, int _unmeasuredNoise, bool _rollback);

  virtual ~Server();

  void shutdown();

  void acceptPeer(RakNet::RakNetGUID guidToAccept,running_machine *machine);

  void removePeer(RakNet::RakNetGUID guid,running_machine *machine);

  bool initializeConnection();

  std::vector<std::shared_ptr<MemoryBlock> > createMemoryBlock(const std::string& name, unsigned char* ptr,int size);

  void initialSync(const RakNet::RakNetGUID &sa,running_machine *machine);

  virtual nsm::PeerInputData popInput(int peerID);

  bool update(running_machine *machine);

  void sync(running_machine *machine);

  void popSyncQueue();

  void setSyncTransferTime(int _syncTransferSeconds)
  {
    syncTransferSeconds = _syncTransferSeconds;
  }

  void sendBaseDelay(int baseDelay);

  inline void setBlockNewClients(bool blockNewClients) {
    this->blockNewClients = blockNewClients;
  }

  inline bool isBlockNewClients() { return blockNewClients; }

private:
  void processPotentialCandidates(running_machine *machine);
};

#ifndef __NSM_CLIENT_H__
#define __NSM_CLIENT_H__

#include "NSM_Common.h"

#include "zlib.h"

#define MAX_COMPRESSED_OUTBUF_SIZE (1024*1024*64)

class running_machine;

class Client : public Common
{
 protected:

  std::vector<std::shared_ptr<MemoryBlock> > syncCheckBlocks;
  std::vector<unsigned char> incomingMsg;

  bool initComplete;

  unsigned char *syncPtr;

  bool firstResync;

  std::vector<unsigned char> initialSyncBuffer;

  RakNet::TimeUS timeBeforeSync;

  int syncGeneration;
  int syncSeconds;
  long long syncAttoseconds;

 public:
  Client(std::string _username);

  virtual void shutdown();

  virtual std::vector<std::shared_ptr<MemoryBlock> > createMemoryBlock(const std::string& name, unsigned char* ptr,int size);

  virtual bool connect(unsigned short selfPort,const char *hostname,unsigned short port,running_machine *machine);

  virtual void updateSyncCheck();

  virtual bool sync(running_machine *machine);

  virtual void revert(running_machine *machine);

  virtual bool update(running_machine *machine);

  virtual void loadInitialData(unsigned char *data,int size,running_machine *machine);
  virtual void createInitialBlocks(running_machine *machine);

  virtual bool resync(unsigned char *data,int size,running_machine *machine);

  virtual inline bool isInitComplete()
  {
    return initComplete;
  }

  virtual int getNumSessions();

  virtual int64_t getCurrentServerTime();
};

#endif

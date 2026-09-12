// license:BSD-3-Clause
// Emscripten stub: WGA/UDP netplay is not available in the browser.
// Offline play works with netCommon == nullptr; createNetCommon is a no-op.

#include "emu.h"
#include "NSM_CommonInterface.h"

#include <chrono>
#include <cstring>

using namespace std;

CommonBase *netCommon = nullptr;

MemoryBlock::MemoryBlock(const std::string &_name, int _size)
	: name(_name), data(nullptr), size(_size), ownsMemory(true)
{
	data = (unsigned char *)calloc(_size, 1);
}

MemoryBlock::MemoryBlock(const std::string &_name, unsigned char *_data, int _size)
	: name(_name), data(_data), size(_size), ownsMemory(false)
{
}

MemoryBlock::~MemoryBlock()
{
	if (ownsMemory && data)
		free(data);
}

namespace {

class EmscriptenCommon final : public CommonBase
{
public:
	explicit EmscriptenCommon(std::string gameName)
		: m_gameName(std::move(gameName))
		, m_start(std::chrono::steady_clock::now())
	{
	}

	int getLargestPing() override { return 50; }
	void createMemoryBlock(const std::string &, unsigned char *, int) override {}
	std::string getLatencyString() override { return "browser (offline)"; }
	std::string getStatisticsString() override { return ""; }
	std::string getMyUserName() override { return "browser"; }
	std::set<int> getMyPlayers() override { return { 0 }; }
	void setMyPlayers(std::set<int>) override {}
	int64_t getLastSendTime() override { return 0; }
	int64_t getCurrentTime() override
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now() - m_start).count();
	}
	void startNetplayClock() override {}
	std::string getGameName() override { return m_gameName; }
	std::vector<BlockValueLocation> getLocationsWithValue(
		unsigned int,
		const std::vector<BlockValueLocation> &,
		const std::vector<std::pair<unsigned char *, int>> &) override
	{
		return {};
	}
	void forceLocation(BlockValueLocation, unsigned int) override {}
	void updateForces(const std::vector<std::pair<unsigned char *, int>> &) override {}
	void attachToNextInputs(const string &, const string &) override {}
	std::map<std::string, std::string> getAllInputValues(int64_t, const std::string &) override { return {}; }
	unordered_map<string, string> getStateChanges(const unordered_map<string, string> &inputMap) override { return inputMap; }
	void sendInputs(int64_t, unordered_map<string, string>) override {}
	bool isHosting() override { return true; }
	void signalGameOver() override {}
	bool isGameOver() override { return false; }

private:
	std::string m_gameName;
	std::chrono::steady_clock::time_point m_start;
};

} // namespace

CommonBase *createNetCommon(const string &,
	const string &,
	unsigned short, const string &,
	unsigned short, int,
	const string &gameName, bool,
	int)
{
	deleteNetCommon();
	netCommon = new EmscriptenCommon(gameName);
	return netCommon;
}

void deleteNetCommon()
{
	delete netCommon;
	netCommon = nullptr;
}

void abortNetCommon()
{
	deleteNetCommon();
}

string makePrivateKey()
{
	return "emscripten-offline";
}

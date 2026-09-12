// license:BSD-3-Clause
// Browser netplay: WebRTC DataChannel transport (WGA/UDP is not available under
// Emscripten). Lockstep ChronoMap semantics mirror native MyPeer.

#include "emu.h"
#include "NSM_CommonInterface.h"

#include <emscripten.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>

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

// Minimal ChronoMap: intervals [start,end) of key/value changes.
class BrowserChronoMap
{
public:
	int64_t expiration() const { return m_expiration; }

	void put(int64_t start, int64_t end, unordered_map<string, string> data)
	{
		if (start < m_expiration || start >= end)
			return;
		if (start != m_expiration)
		{
			m_future[start] = make_tuple(start, end, std::move(data));
			return;
		}
		apply(start, end, std::move(data));
		while (true)
		{
			auto it = m_future.find(m_expiration);
			if (it == m_future.end())
				break;
			auto tup = std::move(it->second);
			m_future.erase(it);
			apply(get<0>(tup), get<1>(tup), std::move(get<2>(tup)));
		}
	}

	unordered_map<string, string> get_all(int64_t ts) const
	{
		unordered_map<string, string> out;
		if (ts < 0 || ts >= m_expiration)
			return out;
		for (auto const &kv : m_data)
		{
			auto it = kv.second.upper_bound(ts);
			if (it == kv.second.begin())
				continue;
			--it;
			out[kv.first] = it->second;
		}
		return out;
	}

private:
	void apply(int64_t start, int64_t end, unordered_map<string, string> data)
	{
		for (auto &kv : data)
			m_data[kv.first][start] = std::move(kv.second);
		m_expiration = end;
	}

	int64_t m_expiration = 0;
	map<string, map<int64_t, string>> m_data;
	map<int64_t, tuple<int64_t, int64_t, unordered_map<string, string>>> m_future;
};

EM_JS(int, mamehub_net_is_ready, (), {
	return (Module.mamehubNet && Module.mamehubNet.ready) ? 1 : 0;
});

EM_JS(int, mamehub_net_is_host, (), {
	return (Module.mamehubNet && Module.mamehubNet.isHost) ? 1 : 0;
});

EM_JS(int, mamehub_net_my_player, (), {
	return (Module.mamehubNet && typeof Module.mamehubNet.player === "number")
		? (Module.mamehubNet.player|0) : 0;
});

EM_JS(void, mamehub_net_send_json, (char const *json), {
	try {
		if (Module.mamehubNet && typeof Module.mamehubNet.send === "function")
			Module.mamehubNet.send(UTF8ToString(json));
	} catch (e) {}
});

EM_JS(double, mamehub_wall_ms, (), { return Date.now(); });

class EmscriptenCommon final : public CommonBase
{
public:
	EmscriptenCommon(std::string userId, std::string gameName, bool hosting, int player)
		: m_userId(std::move(userId))
		, m_gameName(std::move(gameName))
		, m_hosting(hosting)
		, m_player(player)
		, m_peerId(hosting ? "host" : "join")
	{
		m_peers[m_peerId] = BrowserChronoMap();
		m_peers[hosting ? "join" : "host"] = BrowserChronoMap();
		// Install JS → C++ receive hook once.
		EM_ASM({
			Module._mamehubNetOnMessage = function (text) {
				try {
					var len = lengthBytesUTF8(text) + 1;
					var ptr = _malloc(len);
					stringToUTF8(text, ptr, len);
					_mamehub_net_on_message(ptr);
					_free(ptr);
				} catch (e) { console.error("mamehub net recv", e); }
			};
			if (Module.mamehubNet && typeof Module.mamehubNet.setReceiveHook === "function")
				Module.mamehubNet.setReceiveHook(Module._mamehubNetOnMessage);
		});
	}

	int getLargestPing() override { return m_pingMs > 0 ? m_pingMs : 80; }
	void createMemoryBlock(const std::string &, unsigned char *, int) override {}
	std::string getLatencyString() override { return "webrtc ~" + to_string(getLargestPing()) + "ms"; }
	std::string getStatisticsString() override { return ""; }
	std::string getMyUserName() override { return m_userId; }
	std::set<int> getMyPlayers() override { return { m_player }; }
	void setMyPlayers(std::set<int> p) override
	{
		if (!p.empty())
			m_player = *p.begin();
	}
	int64_t getLastSendTime() override { return m_lastSendTime; }

	// Microseconds since agreed netplay epoch (0 before start), matching native.
	int64_t getCurrentTime() override
	{
		if (!m_clockStarted)
			return 0;
		using namespace std::chrono;
		return duration_cast<microseconds>(steady_clock::now() - m_epoch).count();
	}

	void startNetplayClock() override
	{
		if (m_clockStarted)
			return;

		wait_channel_ready();

		// Barrier 1: both peers finished loading (ChronoMap expiration > 1).
		publish(0, 2, {{"__NETPLAY_GAME_LOADED__", "1"}});
		wait_all_expiration(1);
		{
			auto vals = collect(1);
			auto it = vals.find("__NETPLAY_GAME_LOADED__");
			if (it == vals.end() || it->second.size() < 2)
				throw runtime_error("Netplay game-loaded barrier failed");
			for (auto const &kv : it->second)
				if (kv.second != "1")
					throw runtime_error("Peer failed game-loaded barrier");
		}

		// Barrier 2: host proposes wall-clock start (ms); guest sends 0.
		int64_t proposed = 0;
		if (m_hosting)
			proposed = int64_t(mamehub_wall_ms()) + 2500;
		publish(2, 3, {{"__NETPLAY_START_TIME__", to_string(proposed)}});
		wait_all_expiration(2);
		int64_t startWall = 0;
		int proposals = 0;
		{
			auto vals = collect(2);
			auto it = vals.find("__NETPLAY_START_TIME__");
			if (it == vals.end())
				throw runtime_error("Netplay start-time proposal missing");
			for (auto const &kv : it->second)
			{
				int64_t v = stoll(kv.second);
				if (v != 0)
				{
					startWall = v;
					++proposals;
				}
			}
		}
		if (proposals != 1 || startWall <= 0)
			throw runtime_error("Netplay peers did not agree on start time");

		publish(3, 4, {{"__NETPLAY_START_ACK__", to_string(startWall)}});
		wait_all_expiration(3);
		{
			auto vals = collect(3);
			auto it = vals.find("__NETPLAY_START_ACK__");
			if (it == vals.end() || it->second.size() < 2)
				throw runtime_error("Netplay start ACK barrier failed");
			for (auto const &kv : it->second)
				if (stoll(kv.second) != startWall)
					throw runtime_error("Netplay start ACK mismatch");
		}

		while (int64_t(mamehub_wall_ms()) < startWall)
			emscripten_sleep(5);

		m_epoch = chrono::steady_clock::now();
		m_clockStarted = true;
		m_lastSendTime = 0;
	}

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
	void attachToNextInputs(const string &key, const string &value) override
	{
		m_attach[key] = value;
	}

	std::map<std::string, std::string> getAllInputValues(int64_t ts, const std::string &key) override
	{
		if (m_gameOver)
			return {};
		if (ts < 1000)
			return {};
		wait_all_expiration(ts);
		if (m_gameOver)
			return {};
		auto all = collect(ts);
		auto it = all.find(key);
		if (it == all.end())
			return {};
		return it->second;
	}

	unordered_map<string, string> getStateChanges(const unordered_map<string, string> &inputMap) override
	{
		return inputMap;
	}

	void sendInputs(int64_t inputTimeMs, unordered_map<string, string> inputMap) override
	{
		if (m_gameOver || inputTimeMs <= 1)
			return;
		inputMap.insert(m_attach.begin(), m_attach.end());
		m_attach.clear();
		int64_t start = m_peers[m_peerId].expiration();
		if (inputTimeMs <= start)
			return;
		if (inputTimeMs <= m_lastSendTime)
			return;
		publish(start, inputTimeMs, std::move(inputMap));
		m_lastSendTime = inputTimeMs;
	}

	bool isHosting() override { return m_hosting; }
	void signalGameOver() override { m_gameOver = true; }
	bool isGameOver() override { return m_gameOver; }

	void on_message(string const &text)
	{
		// Expected: {"t":"put","id":"...","a":start,"b":end,"m":{...}}
		// Very small hand-rolled parse for our fixed schema.
		auto get_str = [&](char const *field) -> string {
			string key = string("\"") + field + "\":\"";
			auto p = text.find(key);
			if (p == string::npos)
				return {};
			p += key.size();
			auto e = text.find('"', p);
			if (e == string::npos)
				return {};
			return text.substr(p, e - p);
		};
		auto get_num = [&](char const *field) -> int64_t {
			string key = string("\"") + field + "\":";
			auto p = text.find(key);
			if (p == string::npos)
				return 0;
			p += key.size();
			return atoll(text.c_str() + p);
		};
		string type = get_str("t");
		if (type.empty())
		{
			// allow unquoted t via "t":"put" only
			if (text.find("\"t\":\"put\"") != string::npos)
				type = "put";
			else if (text.find("\"t\":\"ping\"") != string::npos)
				type = "ping";
			else if (text.find("\"t\":\"pong\"") != string::npos)
				type = "pong";
		}
		if (type == "put")
		{
			string id = get_str("id");
			int64_t a = get_num("a");
			int64_t b = get_num("b");
			unordered_map<string, string> map;
			auto mp = text.find("\"m\":{");
			if (mp != string::npos)
			{
				mp += 5;
				auto me = text.find('}', mp);
				if (me != string::npos)
				{
					string body = text.substr(mp, me - mp);
					size_t i = 0;
					while (i < body.size())
					{
						auto q1 = body.find('"', i);
						if (q1 == string::npos)
							break;
						auto q2 = body.find('"', q1 + 1);
						if (q2 == string::npos)
							break;
						string k = body.substr(q1 + 1, q2 - q1 - 1);
						auto q3 = body.find('"', q2 + 1);
						if (q3 == string::npos)
							break;
						auto q4 = body.find('"', q3 + 1);
						if (q4 == string::npos)
							break;
						string v = body.substr(q3 + 1, q4 - q3 - 1);
						map[k] = v;
						i = q4 + 1;
					}
				}
			}
			if (!id.empty() && id != m_peerId)
			{
				lock_guard<mutex> lock(m_mutex);
				m_peers[id].put(a, b, std::move(map));
			}
		}
		else if (type == "ping")
		{
			int64_t n = get_num("n");
			ostringstream oss;
			oss << "{\"t\":\"pong\",\"id\":\"" << m_peerId << "\",\"n\":" << n << "}";
			mamehub_net_send_json(oss.str().c_str());
		}
		else if (type == "pong")
		{
			int64_t n = get_num("n");
			int64_t now = int64_t(mamehub_wall_ms());
			if (n > 0 && now >= n)
				m_pingMs = int(std::min<int64_t>(600, std::max<int64_t>(40, now - n)));
		}
	}

private:
	void wait_channel_ready()
	{
		for (int i = 0; i < 12000 && !mamehub_net_is_ready(); ++i)
			emscripten_sleep(10);
		if (!mamehub_net_is_ready())
			throw runtime_error("WebRTC DataChannel not ready");
	}

	void publish(int64_t start, int64_t end, unordered_map<string, string> map)
	{
		{
			lock_guard<mutex> lock(m_mutex);
			m_peers[m_peerId].put(start, end, map);
		}
		ostringstream oss;
		oss << "{\"t\":\"put\",\"id\":\"" << m_peerId << "\",\"a\":" << start
			<< ",\"b\":" << end << ",\"m\":{";
		bool first = true;
		for (auto const &kv : map)
		{
			if (!first)
				oss << ',';
			first = false;
			oss << '"' << kv.first << "\":\"" << kv.second << '"';
		}
		oss << "}}";
		mamehub_net_send_json(oss.str().c_str());
	}

	void wait_all_expiration(int64_t ts)
	{
		while (!m_gameOver)
		{
			bool ready = true;
			{
				lock_guard<mutex> lock(m_mutex);
				for (auto const &kv : m_peers)
				{
					if (kv.second.expiration() <= ts)
					{
						ready = false;
						break;
					}
				}
			}
			if (ready)
				return;
			emscripten_sleep(1);
		}
	}

	unordered_map<string, map<string, string>> collect(int64_t ts)
	{
		unordered_map<string, map<string, string>> values;
		lock_guard<mutex> lock(m_mutex);
		for (auto const &peer : m_peers)
		{
			auto all = peer.second.get_all(ts);
			for (auto const &kv : all)
				values[kv.first].insert(make_pair(peer.first, kv.second));
		}
		return values;
	}

	string m_userId;
	string m_gameName;
	bool m_hosting;
	int m_player;
	string m_peerId;
	mutex m_mutex;
	map<string, BrowserChronoMap> m_peers;
	unordered_map<string, string> m_attach;
	int64_t m_lastSendTime = 0;
	bool m_clockStarted = false;
	bool m_gameOver = false;
	chrono::steady_clock::time_point m_epoch;
	int m_pingMs = 80;
};

EmscriptenCommon *s_instance = nullptr;

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
void mamehub_net_on_message(char *json)
{
	if (s_instance && json)
		s_instance->on_message(json);
}

} // extern "C"

CommonBase *createNetCommon(const string &userId,
	const string &,
	unsigned short, const string &,
	unsigned short, int,
	const string &gameName, bool,
	int)
{
	deleteNetCommon();
	bool hosting = mamehub_net_is_host() != 0;
	int player = mamehub_net_my_player();
	auto *inst = new EmscriptenCommon(
		userId.empty() ? (hosting ? "host" : "join") : userId,
		gameName,
		hosting,
		player);
	s_instance = inst;
	netCommon = inst;
	return netCommon;
}

void deleteNetCommon()
{
	s_instance = nullptr;
	delete netCommon;
	netCommon = nullptr;
}

void abortNetCommon()
{
	deleteNetCommon();
}

string makePrivateKey()
{
	return "emscripten-webrtc";
}

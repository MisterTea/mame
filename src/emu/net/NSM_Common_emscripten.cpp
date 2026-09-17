// license:BSD-3-Clause
// Browser netplay: WebRTC DataChannel transport (WGA/UDP is not available under
// Emscripten). Lockstep ChronoMap semantics mirror native MyPeer.

#include "emu.h"
#include "NSM_CommonInterface.h"

#include <emscripten.h>

#include <atomic>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

using namespace std;

CommonBase *netCommon = nullptr;

namespace {
unordered_map<string, string> s_offlineForced;
}

bool mamehubBrowserOfflineForced(const std::string &key)
{
	auto it = s_offlineForced.find(key);
	return it != s_offlineForced.end() && it->second == "1";
}

static void apply_offline_force_fields(char const *key, bool down)
{
	running_machine *machine = running_machine::emscripten_get_running_machine();
	if (!machine || !key || !machine->ioport().safe_to_read())
		return;
	string want(key);
	for (auto &port : machine->ioport().ports())
	{
		if (!port.second)
			continue;
		for (ioport_field &field : port.second->fields())
		{
			if (field.is_analog())
				continue;
			if ((string("INPUT/") + field.mamehub_id()) == want)
				field.set_value(down ? 1 : 0);
		}
	}
}

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

// WGA SlidingWindowEstimator analogue (95th %ile upper bound, last 256 samples).
class PingWindow
{
public:
	void add_sample_us(double sampleUs)
	{
		m_samples.push_back(sampleUs);
		while (m_samples.size() > 256)
			m_samples.pop_front();
		m_mean = 0;
		for (double s : m_samples)
			m_mean += s;
		if (!m_samples.empty())
			m_mean /= double(m_samples.size());
	}

	double mean_us() const { return m_mean; }

	double upper_us() const
	{
		if (m_samples.empty())
			return 0;
		vector<double> sorted(m_samples.begin(), m_samples.end());
		sort(sorted.begin(), sorted.end());
		size_t idx = min(sorted.size() - 1, size_t(ceil(sorted.size() * 0.95)) - 1);
		return sorted[idx];
	}

	bool empty() const { return m_samples.empty(); }

private:
	deque<double> m_samples;
	double m_mean = 0;
};

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

EM_JS(char *, mamehub_net_my_peer_id, (), {
	var id = (Module.mamehubNet && Module.mamehubNet.peerId)
		? String(Module.mamehubNet.peerId) : "p0";
	var len = lengthBytesUTF8(id) + 1;
	var ptr = _malloc(len);
	stringToUTF8(id, ptr, len);
	return ptr;
});

EM_JS(int, mamehub_net_peer_count, (), {
	if (!Module.mamehubNet || !Module.mamehubNet.peerIds || !Module.mamehubNet.peerIds.length)
		return 0;
	return Module.mamehubNet.peerIds.length|0;
});

EM_JS(char *, mamehub_net_peer_id_at, (int index), {
	if (!Module.mamehubNet || !Module.mamehubNet.peerIds || index < 0 ||
		index >= Module.mamehubNet.peerIds.length)
		return 0;
	var id = String(Module.mamehubNet.peerIds[index]);
	var len = lengthBytesUTF8(id) + 1;
	var ptr = _malloc(len);
	stringToUTF8(id, ptr, len);
	return ptr;
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
	EmscriptenCommon(std::string userId, std::string gameName, bool hosting, int player, std::string peerId)
		: m_userId(std::move(userId))
		, m_gameName(std::move(gameName))
		, m_hosting(hosting)
		, m_player(player)
		, m_peerId(std::move(peerId))
	{
		int n = mamehub_net_peer_count();
		if (n <= 0)
		{
			// Fallback for older shells: host + one joiner.
			m_peers[m_peerId] = BrowserChronoMap();
			m_peers[hosting ? "p1" : "p0"] = BrowserChronoMap();
			m_expectedPeers = 2;
		}
		else
		{
			for (int i = 0; i < n; ++i)
			{
				char *raw = mamehub_net_peer_id_at(i);
				if (!raw)
					continue;
				string id(raw);
				free(raw);
				if (!id.empty())
					m_peers[id] = BrowserChronoMap();
			}
			if (m_peers.find(m_peerId) == m_peers.end())
				m_peers[m_peerId] = BrowserChronoMap();
			m_expectedPeers = int(m_peers.size());
		}
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

	// Half-RTT 95th %ile (ms), ratcheted like native Common::getLargestPing.
	int getLargestPing() override
	{
		int measured = 80;
		{
			lock_guard<mutex> lock(m_pingMutex);
			double worstHalfUs = 0;
			for (auto const &kv : m_peerPing)
			{
				if (kv.second.empty())
					continue;
				worstHalfUs = max(worstHalfUs, kv.second.upper_us() / 2.0);
			}
			if (worstHalfUs > 0)
				measured = max(35, int(ceil(worstHalfUs / 1000.0)));
		}
		auto const now = chrono::steady_clock::now();
		if (measured >= m_effectivePingMs)
		{
			m_effectivePingMs = measured;
			m_lastPingDecrease = now;
		}
		else
		{
			int elapsedTenths = int(chrono::duration_cast<chrono::milliseconds>(
				now - m_lastPingDecrease).count() / 100);
			if (elapsedTenths > 0)
			{
				m_effectivePingMs = max(measured, m_effectivePingMs - elapsedTenths);
				m_lastPingDecrease += chrono::milliseconds(elapsedTenths * 100);
			}
		}
		return m_effectivePingMs;
	}
	void createMemoryBlock(const std::string &, unsigned char *, int) override {}
	// Native format: "Peer <seat>: <mean half-RTT ms> / <half-RTT upper ms>"
	std::string getLatencyString() override
	{
		lock_guard<mutex> lock(m_pingMutex);
		string out;
		for (auto const &kv : m_peerPing)
		{
			if (kv.second.empty())
				continue;
			int seat = peer_seat(kv.first);
			if (seat < 0)
				continue;
			if (!out.empty())
				out += "\n";
			out += "Peer " + to_string(seat) + ": " +
				to_string(int64_t(kv.second.mean_us() / 2000.0)) + " / " +
				to_string(int64_t(kv.second.upper_us() / 2000.0));
		}
		if (out.empty())
			out = "Peer ?: -- / " + to_string(m_effectivePingMs);
		return out;
	}
	std::string getStatisticsString() override
	{
		return "Peer " + to_string(m_player);
	}
	std::string getMyUserName() override { return m_userId; }
	std::set<int> getMyPlayers() override { return { m_player }; }
	void setMyPlayers(std::set<int> p) override
	{
		if (!p.empty())
			m_player = *p.begin();
	}
	int64_t getLastSendTime() override { return m_lastSendTime; }

	// Microseconds since agreed netplay epoch (0 before start), matching native
	// WGA GlobalClock − machineTimeShift. Guests apply m_timeShiftUs toward host.
	int64_t getCurrentTime() override
	{
		if (!m_clockStarted)
			return 0;
		return raw_steady_us() + m_timeShiftUs;
	}

	void startNetplayClock() override
	{
		if (m_clockStarted)
			return;

		wait_channel_ready();

		// Barrier 1: all peers finished loading (ChronoMap expiration > 1).
		publish(0, 2, {{"__NETPLAY_GAME_LOADED__", "1"}});
		wait_all_expiration(1);
		{
			auto vals = collect(1);
			auto it = vals.find("__NETPLAY_GAME_LOADED__");
			if (it == vals.end() || int(it->second.size()) < m_expectedPeers)
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
			if (it == vals.end() || int(it->second.size()) < m_expectedPeers)
				throw runtime_error("Netplay start ACK barrier failed");
			for (auto const &kv : it->second)
				if (stoll(kv.second) != startWall)
					throw runtime_error("Netplay start ACK mismatch");
		}

		while (int64_t(mamehub_wall_ms()) < startWall)
			emscripten_sleep(5);

		m_epoch = chrono::steady_clock::now();
		m_timeShiftUs = 0;
		m_clockStarted = true;
		m_lastSyncSendMs = 0;
		m_lastPingDecrease = chrono::steady_clock::now();
		m_effectivePingMs = 80;
		m_inputWaitEpisodes = 0;
		m_inputWaitSleeps = 0;
		m_lastInputWaitTs = -1;
		EM_ASM({
			if (typeof Module !== "undefined") {
				Module.__mamehubInputWaitEpisodes = 0;
				Module.__mamehubInputWaitSleeps = 0;
				Module.__mamehubInputWaitsPerMin = 0;
			}
		});
		// Advance past the barrier range so the first machine-time input publish
		// (and peer reads at ts>=1000) share a contiguous ChronoMap timeline.
		publish(4, 1000, {{"__NETPLAY_READY__", "1"}});
		m_lastSendTime = 1000;
		// WGA BiDirectionalRpc::initTimeShift sends a burst of PINGs before play;
		// warm the sliding-window estimator the same way.
		for (int i = 0; i < 12; ++i)
		{
			maybe_send_sync(true);
			emscripten_sleep(20);
		}
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

	void forceInput(const string &key, const string &value)
	{
		string mapped = remap_p1_force_key(key);
		if (value.empty())
			m_forced.erase(mapped);
		else
			m_forced[mapped] = value;
		// Also clear the unmapped P1 key if we remapped.
		if (mapped != key)
			m_forced.erase(key);
		string dump = value.empty() ? (mapped + " clear") : (mapped + "=" + value);
		EM_ASM({
			if (typeof Module !== "undefined" && Module.__mamehubDumpInputs) {
				var s = UTF8ToString($0);
				console.log("INPUT_DUMP cpp force " + s);
				if (typeof Module.printErr === "function")
					Module.printErr("INPUT_DUMP cpp force " + s);
			}
		}, dump.c_str());
		// Sticky forces are applied on the next sendInputs() from ioport —
		// never invent ChronoMap puts here.
	}

	void clearForcedInputs()
	{
		m_forced.clear();
	}

	// Modest clock skew for -fake_lag (native uses up to 30s WGA noise).
	void applyFakeLagNoise()
	{
		m_timeShiftUs = -1000 * int64_t((rand() % 200) + 50); // −50..−249 ms
	}

	int64_t local_expiration()
	{
		lock_guard<mutex> lock(m_mutex);
		auto it = m_peers.find(m_peerId);
		if (it == m_peers.end())
			return 0;
		return it->second.expiration();
	}

	std::map<std::string, std::string> getAllInputValues(int64_t ts, const std::string &key) override
	{
		if (m_gameOver)
			return {};
		if (!m_clockStarted)
			return {};
		if (ts < 1000)
			return {};

		// Wait for every remote peer to cover `ts`. Local coverage must come
		// from real sendInputs() polls only — never fabricate idle ChronoMap puts.
		// Count at most one wait-episode per machine timestamp (many fields share ts).
		bool enteredWait = false;
		while (!remotes_cover(ts))
		{
			if (m_gameOver)
				return {};
			if (!enteredWait)
			{
				enteredWait = true;
				if (ts != m_lastInputWaitTs)
				{
					m_lastInputWaitTs = ts;
					++m_inputWaitEpisodes;
					EM_ASM({
						if (typeof Module !== "undefined") {
							Module.__mamehubInputWaitFromCpp = 1;
							Module.__mamehubInputWaitEpisodes = $0;
							Module.__mamehubInputWaitSleeps = $1;
							var elapsedMin = Math.max(1 / 60, $2 / 60000.0);
							Module.__mamehubInputWaitsPerMin = $0 / elapsedMin;
						}
					}, (double)m_inputWaitEpisodes, (double)m_inputWaitSleeps,
						(double)chrono::duration_cast<chrono::milliseconds>(
							chrono::steady_clock::now() - m_epoch).count());
				}
			}
			++m_inputWaitSleeps;
			if ((m_inputWaitSleeps % 250) == 0)
			{
				EM_ASM({
					if (typeof Module !== "undefined") {
						Module.__mamehubInputWaitFromCpp = 1;
						Module.__mamehubInputWaitEpisodes = $0;
						Module.__mamehubInputWaitSleeps = $1;
						var elapsedMin = Math.max(1 / 60, $2 / 60000.0);
						Module.__mamehubInputWaitsPerMin = $0 / elapsedMin;
						console.log("INPUT_WAIT episodes=" + $0 +
							" sleeps=" + $1 +
							" per_min=" + Module.__mamehubInputWaitsPerMin.toFixed(1));
					}
				}, (double)m_inputWaitEpisodes, (double)m_inputWaitSleeps,
					(double)chrono::duration_cast<chrono::milliseconds>(
						chrono::steady_clock::now() - m_epoch).count());
			}
			maybe_send_sync(false);
			emscripten_sleep(1);
		}
		if (enteredWait)
		{
			EM_ASM({
				if (typeof Module !== "undefined") {
					Module.__mamehubInputWaitEpisodes = $0;
					Module.__mamehubInputWaitSleeps = $1;
					var elapsedMin = Math.max(1 / 60, $2 / 60000.0);
					Module.__mamehubInputWaitsPerMin = $0 / elapsedMin;
				}
			}, (double)m_inputWaitEpisodes, (double)m_inputWaitSleeps,
				(double)chrono::duration_cast<chrono::milliseconds>(
					chrono::steady_clock::now() - m_epoch).count());
		}
		if (m_gameOver)
			return {};
		int64_t localExp = local_expiration();
		if (localExp <= ts)
		{
			string msg = "getAllInputValues: missing local ChronoMap coverage ts=" +
				to_string(ts) + " local_expiration=" + to_string(localExp) +
				" key=" + key;
			EM_ASM({
				var s = UTF8ToString($0);
				console.error(s);
				if (typeof Module !== "undefined" && typeof Module.printErr === "function")
					Module.printErr(s);
			}, msg.c_str());
			throw runtime_error(msg);
		}

		auto all = collect(ts);
		// Compact Start/B-only dumps — full-map dumps per field starved machine catch-up
		// (~60s behind wall) so attract never advanced.
		if (EM_ASM_INT({ return (typeof Module !== "undefined" && Module.__mamehubDumpInputs) ? 1 : 0; }) &&
			(key.find("Start") != string::npos || key.find("/P1 B") != string::npos ||
			 key.find("/P2 B") != string::npos || key.find("/P3 B") != string::npos))
		{
			string wantVal = "(missing)";
			auto wit = all.find(key);
			if (wit != all.end())
			{
				wantVal.clear();
				for (auto const &pv : wit->second)
				{
					if (!wantVal.empty())
						wantVal += ",";
					wantVal += pv.first + ":" + pv.second;
				}
			}
			// Log only when this key's peer values change (per-key). A single
			// shared sig re-logged every Start/B field each frame and stalled catch-up.
			string thumb = "{" + wantVal + "}";
			auto &prev = m_lastDumpedReadByKey[key];
			if (thumb != prev)
			{
				prev = thumb;
				string line = "ts=" + to_string(ts) + " exp=" + to_string(localExp) +
					" " + key + "=" + thumb;
				EM_ASM({
					var s = UTF8ToString($0);
					console.log("INPUT_DUMP cpp chronomap " + s);
					if (typeof Module !== "undefined" && typeof Module.printErr === "function")
						Module.printErr("INPUT_DUMP cpp chronomap " + s);
				}, line.c_str());
			}
		}
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
		maybe_send_sync(false);
		// Sticky browser force_input / attach overwrite polled zeros for this
		// real emulator input sample only.
		for (auto const &kv : m_attach)
			inputMap[kv.first] = kv.second;
		m_attach.clear();
		for (auto const &kv : m_forced)
			inputMap[kv.first] = kv.second;
		int64_t start = m_peers[m_peerId].expiration();
		if (inputTimeMs <= start)
		{
			EM_ASM({
				if (typeof Module !== "undefined" && Module.__mamehubDumpInputs) {
					console.log("INPUT_DUMP cpp send SKIP end<=start end=" + $0 + " start=" + $1);
					if (typeof Module.printErr === "function")
						Module.printErr("INPUT_DUMP cpp send SKIP end<=start end=" + $0 + " start=" + $1);
				}
			}, (double)inputTimeMs, (double)start);
			return;
		}
		if (inputTimeMs <= m_lastSendTime)
		{
			EM_ASM({
				if (typeof Module !== "undefined" && Module.__mamehubDumpInputs) {
					console.log("INPUT_DUMP cpp send SKIP end<=lastSend end=" + $0 + " last=" + $1);
					if (typeof Module.printErr === "function")
						Module.printErr("INPUT_DUMP cpp send SKIP end<=lastSend end=" + $0 + " last=" + $1);
				}
			}, (double)inputTimeMs, (double)m_lastSendTime);
			return;
		}
		{
			// Compact send dump: Start/B only (full maps every ~16ms starve catch-up via DOM log).
			vector<string> parts;
			for (auto const &kv : inputMap)
			{
				if (kv.first.find("Start") == string::npos &&
					kv.first.find("/P1 B") == string::npos &&
					kv.first.find("/P2 B") == string::npos &&
					kv.first.find("/P3 B") == string::npos)
					continue;
				parts.push_back(kv.first + "=" + kv.second);
			}
			sort(parts.begin(), parts.end());
			string sig;
			for (auto const &p : parts)
			{
				if (!sig.empty())
					sig += " ";
				sig += p;
			}
			if (sig.empty())
				sig = "(no Start/B)";
			if (sig != m_lastDumpedInputSig)
			{
				m_lastDumpedInputSig = sig;
				string line = "end=" + to_string(inputTimeMs) + " start=" + to_string(start) + " " + sig;
				EM_ASM({
					if (typeof Module !== "undefined" && Module.__mamehubDumpInputs) {
						var s = UTF8ToString($0);
						console.log("INPUT_DUMP cpp send " + s);
						if (typeof Module.printErr === "function")
							Module.printErr("INPUT_DUMP cpp send " + s);
					}
				}, line.c_str());
			}
		}
		publish(start, inputTimeMs, std::move(inputMap));
		m_lastSendTime = inputTimeMs;
		EM_ASM({
			if (typeof Module !== "undefined")
				Module.__mamehubSendCount = (Module.__mamehubSendCount|0) + 1;
		});
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
			else if (text.find("\"t\":\"clock\"") != string::npos)
				type = "clock";
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
				auto pit = m_peers.find(id);
				if (pit != m_peers.end())
					pit->second.put(a, b, std::move(map));
			}
		}
		else if (type == "ping")
		{
			// Guest/host RTT probe. Reply with echo of n plus our netplay clock (µs).
			int64_t n = get_num("n");
			ostringstream oss;
			oss << "{\"t\":\"pong\",\"id\":\"" << m_peerId << "\",\"n\":" << n
				<< ",\"h\":" << getCurrentTime() << "}";
			mamehub_net_send_json(oss.str().c_str());
		}
		else if (type == "pong")
		{
			string id = get_str("id");
			int64_t n = get_num("n");
			int64_t h = get_num("h");
			int64_t nowWall = int64_t(mamehub_wall_ms());
			if (n > 0 && nowWall >= n)
			{
				int64_t rttMs = nowWall - n;
				rttMs = std::min<int64_t>(5000, std::max<int64_t>(1, rttMs));
				double rttUs = double(rttMs) * 1000.0;
				{
					lock_guard<mutex> lock(m_pingMutex);
					string peerKey = id.empty() ? string("remote") : id;
					m_peerPing[peerKey].add_sample_us(rttUs);
				}
				// Guests: estimate host clock from pong (h + RTT/2), EMA the
				// desired shift, then slew ±1ms/sample (WGA ClockSynchronizer).
				if (!m_hosting && h > 0 && m_clockStarted)
				{
					int64_t estimatedHostNow = h + int64_t(rttUs / 2.0);
					int64_t desiredShift = estimatedHostNow - raw_steady_us();
					if (!m_offsetEmaInit)
					{
						m_offsetEmaUs = double(desiredShift);
						m_offsetEmaInit = true;
					}
					else
						m_offsetEmaUs = m_offsetEmaUs * 0.9 + double(desiredShift) * 0.1;
					int64_t delta = int64_t(m_offsetEmaUs) - m_timeShiftUs;
					if (delta > 1000)
						delta = 1000;
					else if (delta < -1000)
						delta = -1000;
					m_timeShiftUs += delta;
				}
			}
		}
		else if (type == "clock")
		{
			// Host periodic broadcast — guests fuse into the same EMA target.
			int64_t h = get_num("h");
			if (!m_hosting && h > 0 && m_clockStarted)
			{
				int64_t desiredShift = h - raw_steady_us();
				if (!m_offsetEmaInit)
				{
					m_offsetEmaUs = double(desiredShift);
					m_offsetEmaInit = true;
				}
				else
					m_offsetEmaUs = m_offsetEmaUs * 0.85 + double(desiredShift) * 0.15;
				int64_t delta = int64_t(m_offsetEmaUs) - m_timeShiftUs;
				if (delta > 1000)
					delta = 1000;
				else if (delta < -1000)
					delta = -1000;
				m_timeShiftUs += delta;
			}
		}
	}

private:
	int peer_seat(string const &peerId) const
	{
		// Host is p0 / seat 0; guests p1, p2, … match mamehub player seats.
		if (peerId == "p0" || (peerId == m_peerId && m_hosting))
			return 0;
		if (peerId.size() >= 2 && peerId[0] == 'p' && isdigit(peerId[1]))
			return peerId[1] - '0';
		if (peerId == m_peerId)
			return m_player;
		return -1;
	}

	// Browser shell always sends P1 force_input ids; remap to this peer's seat
	// so ChronoMap keys match field.mamehub_id() (P2/P3/…).
	string remap_p1_force_key(string const &key) const
	{
		if (m_player <= 0)
			return key;
		static char const kP1[] = "INPUT/0/P1 ";
		static char const kA1[] = "ANALOG/0/P1 ";
		string prefix;
		string rest;
		if (key.compare(0, sizeof(kP1) - 1, kP1) == 0)
		{
			prefix = "INPUT/";
			rest = key.substr(sizeof(kP1) - 1);
		}
		else if (key.compare(0, sizeof(kA1) - 1, kA1) == 0)
		{
			prefix = "ANALOG/";
			rest = key.substr(sizeof(kA1) - 1);
		}
		else
			return key;
		return prefix + to_string(m_player) + "/P" + to_string(m_player + 1) + " " + rest;
	}

	void wait_channel_ready()
	{
		for (int i = 0; i < 12000 && !mamehub_net_is_ready(); ++i)
			emscripten_sleep(10);
		if (!mamehub_net_is_ready())
			throw runtime_error("WebRTC DataChannel not ready");
	}

	int64_t raw_steady_us() const
	{
		using namespace std::chrono;
		return duration_cast<microseconds>(steady_clock::now() - m_epoch).count();
	}

	// Continuous RTT + clock sync over DataChannel (replaces WGA ClockSynchronizer).
	void maybe_send_sync(bool force)
	{
		if (!m_clockStarted || m_gameOver)
			return;
		int64_t nowWall = int64_t(mamehub_wall_ms());
		if (!force && m_lastSyncSendMs != 0 && (nowWall - m_lastSyncSendMs) < 250)
			return;
		m_lastSyncSendMs = nowWall;
		if (m_hosting)
		{
			ostringstream oss;
			oss << "{\"t\":\"clock\",\"id\":\"" << m_peerId << "\",\"h\":" << getCurrentTime() << "}";
			mamehub_net_send_json(oss.str().c_str());
		}
		ostringstream ping;
		ping << "{\"t\":\"ping\",\"id\":\"" << m_peerId << "\",\"n\":" << nowWall << "}";
		mamehub_net_send_json(ping.str().c_str());
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
		wait_all_expiration_budget(ts, -1);
	}

	void wait_all_expiration_budget(int64_t ts, int maxIters)
	{
		for (int i = 0; maxIters < 0 || i < maxIters; ++i)
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
			if (m_gameOver)
				return;
			emscripten_sleep(1);
		}
	}

	// Like MyPeer::hasInputValuesAt — only remotes must cover `ts`.
	bool remotes_cover(int64_t ts)
	{
		lock_guard<mutex> lock(m_mutex);
		for (auto const &kv : m_peers)
		{
			if (kv.first == m_peerId)
				continue;
			if (kv.second.expiration() <= ts)
				return false;
		}
		return true;
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
	int m_expectedPeers = 2;
	mutex m_mutex;
	map<string, BrowserChronoMap> m_peers;
	unordered_map<string, string> m_attach;
	unordered_map<string, string> m_forced;
	string m_lastDumpedInputSig;
	unordered_map<string, string> m_lastDumpedReadByKey;
	int64_t m_lastSendTime = 0;
	bool m_clockStarted = false;
	bool m_gameOver = false;
	chrono::steady_clock::time_point m_epoch;
	int64_t m_timeShiftUs = 0;
	int64_t m_lastSyncSendMs = 0;
	mutex m_pingMutex;
	map<string, PingWindow> m_peerPing;
	int m_effectivePingMs = 80;
	chrono::steady_clock::time_point m_lastPingDecrease = chrono::steady_clock::now();
	double m_offsetEmaUs = 0;
	bool m_offsetEmaInit = false;
	// ChronoMap remotes_cover stalls (one episode per machine ts that blocked).
	uint64_t m_inputWaitEpisodes = 0;
	uint64_t m_inputWaitSleeps = 0;
	int64_t m_lastInputWaitTs = -1;
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
	const string &gameName, bool fakeLag,
	int)
{
	deleteNetCommon();
	bool hosting = mamehub_net_is_host() != 0;
	int player = mamehub_net_my_player();
	char *peerRaw = mamehub_net_my_peer_id();
	string peerId = peerRaw ? string(peerRaw) : (hosting ? "p0" : "p1");
	if (peerRaw)
		free(peerRaw);
	auto *inst = new EmscriptenCommon(
		userId.empty() ? (hosting ? "host" : "join") : userId,
		gameName,
		hosting,
		player,
		peerId);
	// Native -fake_lag adds GlobalClock noise; mirror a modest skew here so
	// continuous WebRTC clock sync has something to correct under test.
	if (fakeLag)
		inst->applyFakeLagNoise();
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

// Browser automation / virtual pad: sticky netplay input overrides.
extern "C" EMSCRIPTEN_KEEPALIVE void mamehub_browser_force_input(char const *key, char const *value)
{
	if (!key)
		return;
	string v = value ? value : "";
	if (v.empty() || v == "0")
		s_offlineForced.erase(key);
	else
		s_offlineForced[key] = "1";
	apply_offline_force_fields(key, !v.empty() && v != "0");
	if (s_instance)
		s_instance->forceInput(key, v);
}

extern "C" EMSCRIPTEN_KEEPALIVE void mamehub_browser_clear_forced_inputs(void)
{
	for (auto const &kv : s_offlineForced)
		apply_offline_force_fields(kv.first.c_str(), false);
	s_offlineForced.clear();
	if (s_instance)
		s_instance->clearForcedInputs();
}

extern "C" EMSCRIPTEN_KEEPALIVE void mamehub_browser_key(int scancode, int down)
{
	// Prefer sticky force_input (ChronoMap under -mamehub, ioport set_value
	// offline). SDL inject is optional and may be unavailable depending on
	// which SDL3 symbols the link exports.
	(void)scancode;
	(void)down;
}

// Milliseconds on the lockstep clock, or 0 before peers finish the start barrier.
extern "C" EMSCRIPTEN_KEEPALIVE int64_t mamehub_browser_netplay_time_ms(void)
{
	if (!s_instance)
		return 0;
	return s_instance->getCurrentTime() / 1000;
}

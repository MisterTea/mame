// license:BSD-3-Clause

#include "discord_service.h"

#define DISCORDPP_IMPLEMENTATION
#include "discordpp.h"

#include <chrono>
#include <cstdlib>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace mamehub {
namespace {

constexpr std::uint64_t APPLICATION_ID = 1545444437482676284ULL;

std::mutex s_mock_mutex;
std::string s_mock_username;
bool s_mock_configured = false;

std::string get_effective_mock_username()
{
	std::lock_guard<std::mutex> lock(s_mock_mutex);
	if (s_mock_configured)
		return s_mock_username;
	char const *env = std::getenv("MAMEHUB_DISCORD_MOCK");
	if (env && *env)
		return std::string(env);
	return std::string();
}

std::uint64_t hash_to_id(std::string const &str)
{
	std::hash<std::string> hasher;
	std::uint64_t h = hasher(str);
	if (h == 0)
		h = 1;
	return h;
}

std::string sanitize_filename(std::string const &input)
{
	std::string res;
	for (char c : input)
	{
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
			res += c;
		else
			res += '_';
	}
	return res;
}

std::string get_mock_dir()
{
	std::string dir = "/tmp/mamehub_mock";
	mkdir(dir.c_str(), 0777);
	return dir;
}

std::string escape_content(std::string const &src)
{
	std::string res;
	for (char c : src)
	{
		if (c == '\\')
			res += "\\\\";
		else if (c == '\n')
			res += "\\n";
		else if (c == '\r')
			res += "\\r";
		else
			res += c;
	}
	return res;
}

std::string unescape_content(std::string const &src)
{
	std::string res;
	for (size_t i = 0; i < src.size(); ++i)
	{
		if (src[i] == '\\' && i + 1 < src.size())
		{
			++i;
			if (src[i] == 'n')
				res += '\n';
			else if (src[i] == 'r')
				res += '\r';
			else if (src[i] == '\\')
				res += '\\';
			else
				res += src[i];
		}
		else
		{
			res += src[i];
		}
	}
	return res;
}

template <typename Predicate>
bool pump_until(Predicate predicate, std::chrono::seconds timeout)
{
	auto const deadline = std::chrono::steady_clock::now() + timeout;
	while (!predicate())
	{
		discordpp::RunCallbacks();
		if (std::chrono::steady_clock::now() >= deadline)
			return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return true;
}

std::string s_token_file_override;

std::string get_token_file_path()
{
	if (!s_token_file_override.empty())
		return s_token_file_override;
	char const *home = std::getenv("HOME");
	if (home && *home)
	{
		std::string dir = std::string(home) + "/.mamehub";
		mkdir(dir.c_str(), 0755);
		return dir + "/discord_token";
	}
	return "discord_token";
}

void save_tokens(std::string const &refresh, std::string const &access, int type)
{
	std::string path = get_token_file_path();
	std::ofstream ofs(path, std::ios::trunc);
	if (ofs.is_open())
	{
		ofs << refresh << "\n" << access << "\n" << type << "\n";
		ofs.close();
		chmod(path.c_str(), 0600);
	}
}

bool load_tokens(std::string &refresh, std::string &access, int &type)
{
	std::string path = get_token_file_path();
	std::ifstream ifs(path);
	if (!ifs.is_open())
		return false;
	std::getline(ifs, refresh);
	std::getline(ifs, access);
	std::string type_str;
	std::getline(ifs, type_str);
	if (refresh.empty() && access.empty())
		return false;
	try {
		type = std::stoi(type_str);
	} catch (...) {
		type = 1;
	}
	return true;
}

} // anonymous namespace

class discord_service::implementation
{
public:
	implementation()
	{
		client.SetApplicationId(APPLICATION_ID);
		client.SetMessageCreatedCallback([this] (std::uint64_t message_id)
		{
			auto message = client.GetMessageHandle(message_id);
			if (!message)
				return;
			auto lobby = message->Lobby();
			if (!lobby)
				return;
			std::lock_guard<std::mutex> guard(message_mutex);
			messages.push_back({ lobby->Id(), message->AuthorId(), message->Content() });
		});
	}

	discordpp::Client client;
	std::mutex message_mutex;
	std::deque<discord_message> messages;
	bool authenticated = false;
	discord_identity identity;

	// Mock mode support
	bool mock_mode = false;
	std::map<std::uint64_t, std::string> mock_lobby_secrets;
	std::map<std::uint64_t, size_t> mock_lines_read;
};

void discord_service::set_mock_user(std::string const &name)
{
	std::lock_guard<std::mutex> lock(s_mock_mutex);
	s_mock_username = name;
	s_mock_configured = true;
}

void discord_service::reset_mock()
{
	std::lock_guard<std::mutex> lock(s_mock_mutex);
	s_mock_username.clear();
	s_mock_configured = false;
	auto &svc = discord_service::instance();
	if (svc.m_impl)
	{
		std::lock_guard<std::mutex> guard(svc.m_impl->message_mutex);
		svc.m_impl->mock_mode = false;
		svc.m_impl->authenticated = false;
		svc.m_impl->identity = discord_identity{};
		svc.m_impl->mock_lobby_secrets.clear();
		svc.m_impl->mock_lines_read.clear();
		svc.m_impl->messages.clear();
	}
}

void discord_service::clear_mock_storage()
{
	system("rm -rf /tmp/mamehub_mock");
}

bool discord_service::is_mock_enabled()
{
	return !get_effective_mock_username().empty();
}

discord_service &discord_service::instance()
{
	static discord_service service;
	return service;
}

discord_service::discord_service() : m_impl(std::make_unique<implementation>())
{
}

discord_service::~discord_service()
{
	if (m_impl && (m_impl->client.GetStatus() != discordpp::Client::Status::Disconnected))
		m_impl->client.Disconnect();
}

bool discord_service::has_cached_token()
{
	std::string refresh, access;
	int type = 1;
	return load_tokens(refresh, access, type);
}

void discord_service::clear_cached_token()
{
	unlink(get_token_file_path().c_str());
}

void discord_service::override_token_file_path(std::string const &path)
{
	s_token_file_override = path;
}

bool discord_service::authenticate(discord_identity &identity, std::string &error)
{
	std::string mock_user = get_effective_mock_username();
	if (!mock_user.empty())
	{
		m_impl->mock_mode = true;
		m_impl->authenticated = true;
		m_impl->identity = { hash_to_id(mock_user), mock_user };
		identity = m_impl->identity;
		return true;
	}

	if (m_impl->authenticated)
	{
		auto user = m_impl->client.GetCurrentUserV2();
		if (user)
		{
			identity = { user->Id(), user->DisplayName() };
			return true;
		}
	}

	// Try cached credentials first if available
	std::string cached_refresh;
	std::string cached_access;
	int cached_type_int = 1;
	if (load_tokens(cached_refresh, cached_access, cached_type_int))
	{
		auto token_type = static_cast<discordpp::AuthorizationTokenType>(cached_type_int);

		// 1. Try connecting with existing access token
		if (!cached_access.empty())
		{
			struct op_result { bool successful = false; bool finished = false; };
			auto update = std::make_shared<op_result>();
			m_impl->client.UpdateToken(token_type, cached_access, [update] (discordpp::ClientResult res)
			{
				update->successful = res.Successful();
				update->finished = true;
			});
			if (pump_until([update] { return update->finished; }, std::chrono::seconds(5)) && update->successful)
			{
				m_impl->client.Connect();
				if (pump_until([&] { return m_impl->client.GetStatus() == discordpp::Client::Status::Ready; }, std::chrono::seconds(8)))
				{
					auto user = m_impl->client.GetCurrentUserV2();
					if (user)
					{
						m_impl->authenticated = true;
						m_impl->identity = { user->Id(), user->DisplayName() };
						identity = m_impl->identity;
						return true;
					}
				}
				if (m_impl->client.GetStatus() != discordpp::Client::Status::Disconnected)
					m_impl->client.Disconnect();
			}
		}

		// 2. If access token didn't connect, try refresh token
		if (!cached_refresh.empty())
		{
			struct ref_result {
				bool successful = false;
				bool finished = false;
				std::string access;
				std::string refresh;
				discordpp::AuthorizationTokenType type{};
			};
			auto ref = std::make_shared<ref_result>();
			m_impl->client.RefreshToken(APPLICATION_ID, cached_refresh,
				[ref] (discordpp::ClientResult result, std::string access, std::string refresh, discordpp::AuthorizationTokenType type, std::int32_t, std::string)
				{
					ref->successful = result.Successful();
					ref->access = std::move(access);
					ref->refresh = std::move(refresh);
					ref->type = type;
					ref->finished = true;
				});
			if (pump_until([ref] { return ref->finished; }, std::chrono::seconds(15)) && ref->successful)
			{
				save_tokens(ref->refresh, ref->access, static_cast<int>(ref->type));

				struct op_result { bool successful = false; bool finished = false; };
				auto update = std::make_shared<op_result>();
				m_impl->client.UpdateToken(ref->type, ref->access, [update] (discordpp::ClientResult res)
				{
					update->successful = res.Successful();
					update->finished = true;
				});
				if (pump_until([update] { return update->finished; }, std::chrono::seconds(5)) && update->successful)
				{
					m_impl->client.Connect();
					if (pump_until([&] { return m_impl->client.GetStatus() == discordpp::Client::Status::Ready; }, std::chrono::seconds(15)))
					{
						auto user = m_impl->client.GetCurrentUserV2();
						if (user)
						{
							m_impl->authenticated = true;
							m_impl->identity = { user->Id(), user->DisplayName() };
							identity = m_impl->identity;
							return true;
						}
					}
					if (m_impl->client.GetStatus() != discordpp::Client::Status::Disconnected)
						m_impl->client.Disconnect();
				}
			}
		}
	}

	auto verifier = m_impl->client.CreateAuthorizationCodeVerifier();
	discordpp::AuthorizationArgs args;
	args.SetClientId(APPLICATION_ID);
	args.SetScopes(discordpp::Client::GetDefaultCommunicationScopes());
	args.SetCodeChallenge(verifier.Challenge());

	struct auth_result { bool successful = false; bool finished = false; std::string code; std::string redirect; std::string error; };
	auto auth = std::make_shared<auth_result>();
	m_impl->client.Authorize(args, [auth] (discordpp::ClientResult result, std::string code, std::string redirect)
	{
		auth->successful = result.Successful();
		if (!auth->successful)
			auth->error = result.ToString();
		auth->code = std::move(code);
		auth->redirect = std::move(redirect);
		auth->finished = true;
	});
	if (!pump_until([auth] { return auth->finished; }, std::chrono::seconds(180)))
	{
		m_impl->client.AbortAuthorize();
		error = "Discord authorization timed out";
		return false;
	}
	if (!auth->successful)
	{
		error = auth->error;
		return false;
	}

	struct token_result { bool successful = false; bool finished = false; std::string access; std::string refresh; std::string error; discordpp::AuthorizationTokenType type{}; };
	auto token = std::make_shared<token_result>();
	m_impl->client.GetToken(APPLICATION_ID, auth->code, verifier.Verifier(), auth->redirect,
		[token] (discordpp::ClientResult result, std::string access, std::string refresh, discordpp::AuthorizationTokenType type, std::int32_t, std::string)
		{
			token->successful = result.Successful();
			if (!token->successful)
				token->error = result.ToString();
			token->access = std::move(access);
			token->refresh = std::move(refresh);
			token->type = type;
			token->finished = true;
		});
	if (!pump_until([token] { return token->finished; }, std::chrono::seconds(30)) || !token->successful)
	{
		error = token->finished ? token->error : "Discord token exchange timed out";
		return false;
	}

	struct operation_result { bool successful = false; bool finished = false; std::string error; };
	auto update = std::make_shared<operation_result>();
	m_impl->client.UpdateToken(token->type, token->access, [update] (discordpp::ClientResult result)
	{
		update->successful = result.Successful();
		if (!update->successful)
			update->error = result.ToString();
		update->finished = true;
	});
	if (!pump_until([update] { return update->finished; }, std::chrono::seconds(30)) || !update->successful)
	{
		error = update->finished ? update->error : "Discord token update timed out";
		return false;
	}

	m_impl->client.Connect();
	if (!pump_until([&] { return m_impl->client.GetStatus() == discordpp::Client::Status::Ready; }, std::chrono::seconds(30)))
	{
		error = "Discord connection did not become ready";
		return false;
	}
	auto user = m_impl->client.GetCurrentUserV2();
	if (!user)
	{
		error = "Discord did not return the authenticated user";
		return false;
	}
	save_tokens(token->refresh, token->access, static_cast<int>(token->type));
	m_impl->authenticated = true;
	m_impl->identity = { user->Id(), user->DisplayName() };
	identity = m_impl->identity;
	return true;
}

bool discord_service::is_authenticated() const
{
	return m_impl && m_impl->authenticated;
}

discord_identity const &discord_service::current_identity() const
{
	static discord_identity empty;
	return m_impl ? m_impl->identity : empty;
}

bool discord_service::create_or_join_lobby(std::string const &secret, std::uint64_t &lobby_id, std::string &error)
{
	if (m_impl->mock_mode)
	{
		lobby_id = hash_to_id(secret);
		m_impl->mock_lobby_secrets[lobby_id] = secret;

		// Ensure file exists and read initial messages count
		std::string dir = get_mock_dir();
		std::string path = dir + "/" + sanitize_filename(secret) + ".log";
		int fd = open(path.c_str(), O_CREAT | O_RDWR, 0666);
		if (fd >= 0)
			close(fd);

		// Record current line count so newly drained messages only come after joining
		std::ifstream file(path);
		size_t line_count = 0;
		std::string line;
		while (std::getline(file, line))
			++line_count;
		m_impl->mock_lines_read[lobby_id] = line_count;
		return true;
	}

	struct lobby_result { bool successful = false; bool finished = false; std::uint64_t id = 0; std::string error; };
	auto operation = std::make_shared<lobby_result>();
	m_impl->client.CreateOrJoinLobby(secret, [operation] (discordpp::ClientResult result, std::uint64_t id)
	{
		operation->successful = result.Successful();
		if (!operation->successful)
			operation->error = result.ToString();
		operation->id = id;
		operation->finished = true;
	});
	if (!pump_until([operation] { return operation->finished; }, std::chrono::seconds(30)))
	{
		error = "Discord lobby join timed out";
		return false;
	}
	lobby_id = operation->id;
	if (!operation->successful)
		error = operation->error;
	return operation->successful;
}

bool discord_service::leave_lobby(std::uint64_t lobby_id, std::string &error)
{
	if (m_impl->mock_mode)
	{
		m_impl->mock_lobby_secrets.erase(lobby_id);
		m_impl->mock_lines_read.erase(lobby_id);
		return true;
	}

	struct leave_result { bool successful = false; bool finished = false; std::string error; };
	auto operation = std::make_shared<leave_result>();
	m_impl->client.LeaveLobby(lobby_id, [operation] (discordpp::ClientResult result)
	{
		operation->successful = result.Successful();
		if (!operation->successful)
			operation->error = result.ToString();
		operation->finished = true;
	});
	if (!pump_until([operation] { return operation->finished; }, std::chrono::seconds(10)))
	{
		error = "Discord leave lobby timed out";
		return false;
	}
	if (!operation->successful)
		error = operation->error;
	return operation->successful;
}

bool discord_service::send_lobby_message(std::uint64_t lobby_id, std::string const &content, std::string &error)
{
	if (m_impl->mock_mode)
	{
		auto it = m_impl->mock_lobby_secrets.find(lobby_id);
		if (it == m_impl->mock_lobby_secrets.end())
		{
			error = "Mock lobby not found";
			return false;
		}
		std::string dir = get_mock_dir();
		std::string path = dir + "/" + sanitize_filename(it->second) + ".log";
		int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0666);
		if (fd < 0)
		{
			error = "Failed to open mock lobby file";
			return false;
		}
		flock(fd, LOCK_EX);
		std::string entry = std::to_string(m_impl->identity.id) + " " + escape_content(content) + "\n";
		write(fd, entry.data(), entry.size());
		flock(fd, LOCK_UN);
		close(fd);
		return true;
	}

	struct send_result { bool successful = false; bool finished = false; std::string error; };
	auto operation = std::make_shared<send_result>();
	m_impl->client.SendLobbyMessage(lobby_id, content, [operation] (discordpp::ClientResult result, std::uint64_t)
	{
		operation->successful = result.Successful();
		if (!operation->successful)
			operation->error = result.ToString();
		operation->finished = true;
	});
	if (!pump_until([operation] { return operation->finished; }, std::chrono::seconds(30)))
	{
		error = "Discord lobby message timed out";
		return false;
	}
	if (!operation->successful)
		error = operation->error;
	return operation->successful;
}

bool discord_service::get_lobby_messages(std::uint64_t lobby_id, std::vector<discord_message> &messages, std::string &error)
{
	if (m_impl->mock_mode)
	{
		auto it = m_impl->mock_lobby_secrets.find(lobby_id);
		if (it == m_impl->mock_lobby_secrets.end())
		{
			error = "Mock lobby not found";
			return false;
		}
		std::string dir = get_mock_dir();
		std::string path = dir + "/" + sanitize_filename(it->second) + ".log";
		int fd = open(path.c_str(), O_RDONLY);
		if (fd < 0)
		{
			messages.clear();
			return true;
		}
		flock(fd, LOCK_SH);
		std::vector<char> buffer;
		char buf[4096];
		ssize_t bytes = 0;
		while ((bytes = read(fd, buf, sizeof(buf))) > 0)
			buffer.insert(buffer.end(), buf, buf + bytes);
		flock(fd, LOCK_UN);
		close(fd);

		std::string file_content(buffer.begin(), buffer.end());
		std::istringstream stream(file_content);
		std::string line;
		std::vector<discord_message> result;
		while (std::getline(stream, line))
		{
			if (line.empty())
				continue;
			auto space_pos = line.find(' ');
			if (space_pos == std::string::npos)
				continue;
			std::uint64_t author_id = std::stoull(line.substr(0, space_pos));
			std::string content = unescape_content(line.substr(space_pos + 1));
			result.push_back({ lobby_id, author_id, std::move(content) });
		}
		messages = std::move(result);
		return true;
	}

	struct history_result { bool successful = false; bool finished = false; std::string error; std::vector<discord_message> messages; };
	auto operation = std::make_shared<history_result>();
	m_impl->client.GetLobbyMessagesWithLimit(lobby_id, 200,
		[operation] (discordpp::ClientResult result, std::vector<discordpp::MessageHandle> history)
		{
			operation->successful = result.Successful();
			if (!operation->successful)
				operation->error = result.ToString();
			for (auto it = history.rbegin(); it != history.rend(); ++it)
			{
				auto lobby = it->Lobby();
				if (lobby)
					operation->messages.push_back({ lobby->Id(), it->AuthorId(), it->Content() });
			}
			operation->finished = true;
		});
	if (!pump_until([operation] { return operation->finished; }, std::chrono::seconds(30)))
	{
		error = "Discord lobby history timed out";
		return false;
	}
	if (!operation->successful)
	{
		error = operation->error;
		return false;
	}
	messages = std::move(operation->messages);
	return true;
}

std::vector<discord_message> discord_service::drain_messages()
{
	pump();
	std::lock_guard<std::mutex> guard(m_impl->message_mutex);
	std::vector<discord_message> result(m_impl->messages.begin(), m_impl->messages.end());
	m_impl->messages.clear();
	return result;
}

void discord_service::pump()
{
	if (m_impl && m_impl->mock_mode)
	{
		std::string dir = get_mock_dir();
		for (auto const &pair : m_impl->mock_lobby_secrets)
		{
			std::uint64_t lobby_id = pair.first;
			std::string const &secret = pair.second;
			std::string path = dir + "/" + sanitize_filename(secret) + ".log";
			int fd = open(path.c_str(), O_RDONLY);
			if (fd < 0)
				continue;
			flock(fd, LOCK_SH);
			std::vector<char> buffer;
			char buf[4096];
			ssize_t bytes = 0;
			while ((bytes = read(fd, buf, sizeof(buf))) > 0)
				buffer.insert(buffer.end(), buf, buf + bytes);
			flock(fd, LOCK_UN);
			close(fd);

			std::string file_content(buffer.begin(), buffer.end());
			std::istringstream stream(file_content);
			std::string line;
			size_t line_index = 0;
			size_t &read_offset = m_impl->mock_lines_read[lobby_id];
			while (std::getline(stream, line))
			{
				if (line_index >= read_offset && !line.empty())
				{
					auto space_pos = line.find(' ');
					if (space_pos != std::string::npos)
					{
						std::uint64_t author_id = std::stoull(line.substr(0, space_pos));
						std::string content = unescape_content(line.substr(space_pos + 1));
						std::lock_guard<std::mutex> guard(m_impl->message_mutex);
						m_impl->messages.push_back({ lobby_id, author_id, std::move(content) });
					}
				}
				++line_index;
			}
			read_offset = line_index;
		}
		return;
	}

	discordpp::RunCallbacks();
}

} // namespace mamehub

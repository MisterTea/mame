// license:BSD-3-Clause

#include "discord_service.h"

#define DISCORDPP_IMPLEMENTATION
#include "discordpp.h"

#include <chrono>
#include <deque>
#include <mutex>
#include <thread>

namespace mamehub {
namespace {

constexpr std::uint64_t APPLICATION_ID = 1545444437482676284ULL;

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
};

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

bool discord_service::authenticate(discord_identity &identity, std::string &error)
{
	if (m_impl->authenticated)
	{
		auto user = m_impl->client.GetCurrentUserV2();
		if (user)
		{
			identity = { user->Id(), user->DisplayName() };
			return true;
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

	struct token_result { bool successful = false; bool finished = false; std::string access; std::string error; discordpp::AuthorizationTokenType type{}; };
	auto token = std::make_shared<token_result>();
	m_impl->client.GetToken(APPLICATION_ID, auth->code, verifier.Verifier(), auth->redirect,
		[token] (discordpp::ClientResult result, std::string access, std::string, discordpp::AuthorizationTokenType type, std::int32_t, std::string)
		{
			token->successful = result.Successful();
			if (!token->successful)
				token->error = result.ToString();
			token->access = std::move(access);
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
	m_impl->client.UpdateToken(token->type, std::move(token->access), [update] (discordpp::ClientResult result)
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
	m_impl->authenticated = true;
	identity = { user->Id(), user->DisplayName() };
	return true;
}

bool discord_service::create_or_join_lobby(std::string const &secret, std::uint64_t &lobby_id, std::string &error)
{
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

bool discord_service::send_lobby_message(std::uint64_t lobby_id, std::string const &content, std::string &error)
{
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
	discordpp::RunCallbacks();
}

} // namespace mamehub

// license:BSD-3-Clause

#include "discord_waiting_room.h"

#include <chrono>
#include <sstream>
#include <stdexcept>
#include <thread>

#if defined(UI_SDL)
#include <SDL2/SDL.h>
#endif

namespace mamehub {

namespace {

void headless_wait(discord_directory_server &directory, bool hosting, std::function<bool()> const &connection_finished)
{
	while (!connection_finished())
	{
		auto room = directory.waiting_room();
		if (hosting && room.can_start && !room.started)
		{
			std::string error;
			directory.start_game(error);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

} // anonymous namespace

void show_discord_waiting_room(discord_directory_server &directory, bool hosting, std::function<bool()> connection_finished)
{
#if defined(UI_SDL)
	bool const initialized_video = !(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO);
	if (initialized_video && (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0))
	{
		headless_wait(directory, hosting, connection_finished);
		return;
	}

	SDL_Window *window = SDL_CreateWindow("MAMEHub Discord Lobby", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 420, SDL_WINDOW_SHOWN);
	if (!window)
	{
		if (initialized_video)
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
		headless_wait(directory, hosting, connection_finished);
		return;
	}
	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!renderer)
		renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);

	while (!connection_finished())
	{
		auto room = directory.waiting_room();
		if (hosting && room.can_start && !room.started && discord_service::is_mock_enabled())
		{
			std::string error;
			directory.start_game(error);
		}
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			bool const start_key = (event.type == SDL_KEYDOWN) && ((event.key.keysym.sym == SDLK_RETURN) || (event.key.keysym.sym == SDLK_SPACE));
			bool const start_click = (event.type == SDL_MOUSEBUTTONUP) && (event.button.x >= 220) && (event.button.x <= 420) && (event.button.y >= 340) && (event.button.y <= 395);
			if (hosting && room.can_start && (start_key || start_click))
			{
				std::string error;
				directory.start_game(error);
			}
		}

		std::ostringstream title;
		title << "MAMEHub Lobby - " << room.members.size() << " player(s): ";
		for (std::size_t index = 0; index < room.members.size(); ++index)
		{
			if (index)
				title << ", ";
			title << room.members[index].display_name << (room.members[index].ready ? " [ready]" : " [connecting]");
		}
		if (hosting)
			title << (room.can_start ? " - press Enter or click Start" : " - waiting for direct endpoints");
		else
			title << " - waiting for host";
		SDL_SetWindowTitle(window, title.str().c_str());

		if (renderer)
		{
			SDL_SetRenderDrawColor(renderer, 20, 23, 31, 255);
			SDL_RenderClear(renderer);
			for (std::size_t index = 0; index < room.members.size(); ++index)
			{
				SDL_Rect row{ 70, 55 + int(index) * 42, 500, 30 };
				if (room.members[index].ready)
					SDL_SetRenderDrawColor(renderer, 52, 168, 83, 255);
				else
					SDL_SetRenderDrawColor(renderer, 120, 126, 140, 255);
				SDL_RenderFillRect(renderer, &row);
			}
			SDL_Rect button{ 220, 340, 200, 55 };
			if (hosting && room.can_start)
				SDL_SetRenderDrawColor(renderer, 88, 101, 242, 255);
			else
				SDL_SetRenderDrawColor(renderer, 60, 64, 74, 255);
			SDL_RenderFillRect(renderer, &button);
			SDL_RenderPresent(renderer);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}

	if (renderer)
		SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	if (initialized_video)
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
#else
	headless_wait(directory, hosting, connection_finished);
#endif
}

} // namespace mamehub

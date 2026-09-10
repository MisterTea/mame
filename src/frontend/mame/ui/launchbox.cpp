// license:BSD-3-Clause

#include "emu.h"
#include "ui/launchbox.h"

#include "ui/ui.h"
#include "ui/utils.h"

#include "fileio.h"
#include "sqlite3.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string_view>
#include <vector>

namespace ui {
namespace {

using statement_ptr = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

std::string normalize(std::string_view value)
{
	std::string result;
	bool space = false;
	for (unsigned char ch : value)
	{
		if ((ch < 0x80) && std::isalnum(ch))
		{
			if (space && !result.empty()) result.push_back(' ');
			result.push_back(std::tolower(ch));
			space = false;
		}
		else if (ch < 0x80)
		{
			space = true;
		}
	}
	return result;
}

void field(std::string &out, char const *label, unsigned char const *value)
{
	if (value && *value)
		out.append(label).append(reinterpret_cast<char const *>(value)).append("\n");
}

class launchbox_database
{
public:
	explicit launchbox_database(mame_ui_manager &mui)
		: m_mui(mui)
	{
		m_arcade = open("launchbox/arcade.sqlite3");
		sqlite3 *const manifest = open("launchbox/platforms.sqlite3");
		if (manifest)
		{
			sqlite3_stmt *raw = nullptr;
			if (SQLITE_OK == sqlite3_prepare_v2(manifest, "SELECT name,filename FROM platform", -1, &raw, nullptr))
			{
				statement_ptr stmt(raw, sqlite3_finalize);
				while (SQLITE_ROW == sqlite3_step(stmt.get()))
					m_platforms.emplace_back(reinterpret_cast<char const *>(sqlite3_column_text(stmt.get(), 0)), reinterpret_cast<char const *>(sqlite3_column_text(stmt.get(), 1)));
			}
			sqlite3_close(manifest);
		}
	}

	~launchbox_database() { if (m_arcade) sqlite3_close(m_arcade); }

	void arcade(game_driver const &driver, std::string &out) const
	{
		query(m_arcade, "SELECT name,developer,publisher,year,genre,play_mode,region,version,language,series,status,overview FROM arcade WHERE file_name=?1", driver.name, true, out);
	}

	void software(ui_software_info const &software, std::string &out) const
	{
		std::string const hint(normalize(software.driver ? software.driver->type.fullname() : std::string_view()));
		auto const *best = static_cast<std::pair<std::string, std::string> const *>(nullptr);
		int best_score = -1;
		for (auto const &candidate : m_platforms)
		{
			std::string const platform(normalize(candidate.first));
			int score = (hint.find(platform) != std::string::npos) ? 1000 : 0;
			for (std::size_t pos = 0; pos < platform.size(); )
			{
				std::size_t const end = platform.find(' ', pos);
				std::string const word(platform.substr(pos, end - pos));
				if (word.size() > 2 && hint.find(word) != std::string::npos) score += int(word.size());
				if (end == std::string::npos) break;
				pos = end + 1;
			}
			if (score > best_score) { best_score = score; best = &candidate; }
		}
		if (!best) return;
		sqlite3 *const db = open(std::string("launchbox/software/").append(best->second));
		query(db, "SELECT name,developer,publisher,release_date,genres,max_players,platform,release_type,esrb,rating,rating_count,overview FROM game WHERE normalized_name=?1", normalize(software.longname), false, out);
		if (db) sqlite3_close(db);
	}

private:
	sqlite3 *open(std::string const &name) const
	{
		emu_file file(m_mui.options().ui_path(), OPEN_FLAG_READ);
		if (file.open(name)) return nullptr;
		std::string const path(file.fullpath());
		file.close();
		sqlite3 *db = nullptr;
		if (SQLITE_OK != sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr))
		{
			if (db) sqlite3_close(db);
			return nullptr;
		}
		return db;
	}

	void query(sqlite3 *db, char const *sql, std::string const &key, bool arcade, std::string &out) const
	{
		if (!db) return;
		sqlite3_stmt *raw = nullptr;
		if (SQLITE_OK != sqlite3_prepare_v2(db, sql, -1, &raw, nullptr)) return;
		statement_ptr stmt(raw, sqlite3_finalize);
		sqlite3_bind_text(stmt.get(), 1, key.c_str(), -1, SQLITE_TRANSIENT);
		std::vector<std::string> best;
		int best_score = -1;
		while (SQLITE_ROW == sqlite3_step(stmt.get()))
		{
			int const score = 0;
			if (score > best_score)
			{
				best_score = score;
				best.clear();
				for (int i = 0; i < 12; ++i)
				{
					auto const *value = sqlite3_column_text(stmt.get(), i);
					best.emplace_back(value ? reinterpret_cast<char const *>(value) : "");
				}
			}
		}
		if (best.empty()) return;
		out.append("\nLaunchBox Games Database\n");
		static char const *const game_labels[] = { "Title: ", "Developer: ", "Publisher: ", "Released: ", "Genre: ", "Players: ", "Platform: ", "Release type: ", "Rating: ", "Community rating: ", "Rating votes: " };
		static char const *const arcade_labels[] = { "Title: ", "Developer: ", "Publisher: ", "Year: ", "Genre: ", "Play mode: ", "Region: ", "Version: ", "Language: ", "Series: ", "Emulation status: " };
		auto const &labels(arcade ? arcade_labels : game_labels);
		for (int i = 0; i < 11; ++i) field(out, labels[i], reinterpret_cast<unsigned char const *>(best[i].c_str()));
		if (!best[11].empty()) out.append("\n").append(best[11]).append("\n");
	}

	mame_ui_manager &m_mui;
	sqlite3 *m_arcade = nullptr;
	std::vector<std::pair<std::string, std::string> > m_platforms;
};

launchbox_database &database(mame_ui_manager &mui)
{
	return mui.get_session_data<launchbox_database, launchbox_database>(mui);
}

} // anonymous namespace

void append_launchbox_metadata(mame_ui_manager &mui, game_driver const &driver, std::string &text) { database(mui).arcade(driver, text); }
void append_launchbox_metadata(mame_ui_manager &mui, ui_software_info const &software, std::string &text) { database(mui).software(software, text); }

} // namespace ui

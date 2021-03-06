#pragma once

#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <vector>
#include <array>
#include <map>
#include <glm/glm.hpp>
#include "tas_input.h"

class tas_input_handler
{
public:
	static tas_input_handler& get() {
		static tas_input_handler instance;
		return instance;
	}

private:
	tas_input_handler();
	~tas_input_handler() = default;

private:
	std::map<std::string, tas_input> levelInputs;
	void load_input_from_file(std::filesystem::path filePath);
	std::chrono::steady_clock::time_point lastAutosaveCheck;
	std::chrono::minutes autosaveInterval{ 5 };
	uintmax_t autosaveFolderMaxSizeBytes{ 100 * 1024 * 1024 };

	void autosave();
	bool autosave_safe_to_save();

public:
	static inline int32_t inputTickCounter = 0;

	void set_record(bool newRecord);
	void set_playback(bool newPlayback);

	void get_inputs_from_files();
	void save_input_to_file(std::string hbinFileName);
	void autosave_check();
	void reload_playback_buffer(tas_input* input);

	void pre_tick();
	void post_tick();
	void pre_frame();
	void post_frame();
	void pre_loop();
	void post_loop();

	std::vector<std::string> get_loaded_levels();
	tas_input* get_inputs(std::string levelName);
	std::array<float, 256> get_rng_histogram_data();
	void clear_rng_histogram_data();
	void insert_dummy_rng_histogram_value();
	int32_t get_current_playback_tick();
	int32_t get_rng_advances_since_last_tick();

	bool this_tick_enter();
	bool this_tick_tab();
	bool this_tick_g();
	bool this_tick_mb(int btn);
};


#include "tas_input_handler.h"

#include <boost/algorithm/string/predicate.hpp> // for ends_with
#include <boost/circular_buffer.hpp>
#include <vector>
#include <queue>
#include <algorithm>
#include <iterator>
#include <string>
#include <sstream>
#include <fstream>
#include <atomic>
#include "halo_constants.h"
#include "halo_engine.h"
#include "helpers.h"

using std::vector, std::string;
using namespace halo;
using namespace halo::addr;

// TODO: replace with tas_input object 
static std::vector<input_moment> playback_buffer_current_level;
static std::atomic_bool record = false;
static std::atomic_bool playback = false;

static std::string last_level_name;
static int32_t last_input_tick;

static int32_t rng_count_since_last_tick;
static int32_t last_rng;
static boost::circular_buffer<float> rng_count_histogram_buffer(256);

tas_input_handler::tas_input_handler()
{
	lastAutosaveCheck = std::chrono::steady_clock::now() + autosaveInterval;
}

void tas_input_handler::set_record(bool newRecord)
{
	record = newRecord;
}

void tas_input_handler::set_playback(bool newPlayback)
{
	playback = newPlayback;
}

void tas_input_handler::load_input_from_file(std::filesystem::path filePath) {
	if (!std::filesystem::exists(filePath)) {
		return;
	}

	tas_input levelInput(filePath.filename().string());

	std::vector<input_moment> inputs;

	std::ifstream logFile(filePath.string(), std::ios::in | std::ios::binary);

	char buf[sizeof(input_moment)];
	while (!logFile.eof())
	{
		logFile.read(buf, sizeof(input_moment));
		if (!logFile) {
			break;
		}

		input_moment* im = reinterpret_cast<input_moment*>(&buf);

		inputs.push_back(*im);
	}

	levelInput.set_inputs(inputs);

	levelInputs[filePath.filename().string()] = levelInput;
}

void tas_input_handler::autosave()
{
	std::string rootAutosavePath = "HaloTASFiles/Recordings/_Autosave/";

	for (auto& inputSet : levelInputs) {

		const auto& inputName = inputSet.first;
		auto& input = inputSet.second;
		if (!input.is_dirty()) {
			continue;
		}

		// Get string with current time in filename-safe format
		std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::string file_time;
		std::stringstream ss;
		tm ltm;
		localtime_s(&ltm, &now);
		ss << std::put_time(&ltm, "%Y-%m-%d_%H-%M-%S");
		file_time = ss.str();

		auto originalFIleName = std::filesystem::path(inputName);
		auto autosaveFullPath = rootAutosavePath + originalFIleName.stem().string() + "_" + file_time + originalFIleName.extension().string();

		std::ofstream logFile(autosaveFullPath, std::ios::trunc | std::ios::binary);
		for (auto& inp : *input.input_buffer()) {
			logFile.write(reinterpret_cast<char*>(&inp), sizeof(inp));
		}
		logFile.close();

		input.clear_dirty_flag();
	}
}

bool tas_input_handler::autosave_safe_to_save()
{
	std::string rootAutosavePath = "HaloTASFiles/Recordings/_Autosave/";
	uintmax_t totalFileBytes = 0;

	for (const auto& file : std::filesystem::directory_iterator(rootAutosavePath)) {
		if (file.path().extension().string() != ".hbin") {
			continue;
		}
		if (!file.is_regular_file()) {
			continue;
		}
		totalFileBytes += std::filesystem::file_size(file);
	}

	if (totalFileBytes > autosaveFolderMaxSizeBytes) {

		size_t autosaveFolderMaxSizeMB = static_cast<size_t>(autosaveFolderMaxSizeBytes / 1024 / 1024);

		std::stringstream ss;
		ss << "The _Autosave folder is currently more than " << autosaveFolderMaxSizeMB << "MB. Do you want to continue saving anyways?";
		ss << std::endl << std::endl << "To stop this message, reduce the size of the _Autosave folder or increase the Autosave Max Folder Size Limit.";
		std::string message = ss.str();

		const int result = MessageBox(NULL, message.c_str(), "HaloTAS: Autosave Folder Size Limit Reached", MB_YESNO);

		if (result == IDYES) {
			return true;
		}
		else {
			return false;
		}
	}

	return true;
}

void tas_input_handler::get_inputs_from_files()
{
	levelInputs.clear();
	for (const auto& file : std::filesystem::directory_iterator("HaloTASFiles/Recordings")) {
		if (boost::algorithm::ends_with(file.path().string(), ".hbin")) {
			load_input_from_file(file.path());
		}
	}
}

void tas_input_handler::save_input_to_file(std::string hbinFileName)
{
	std::string rootRecordingsPath = "HaloTASFiles/Recordings/";

	if (levelInputs.count(hbinFileName)) {
		std::ofstream logFile(rootRecordingsPath + hbinFileName, std::ios::trunc | std::ios::binary);
		auto input = levelInputs[hbinFileName];

		for (auto& inp : *input.input_buffer()) {
			logFile.write(reinterpret_cast<char*>(&inp), sizeof(inp));
		}

		logFile.close();
	}
}

void tas_input_handler::autosave_check()
{
	auto now = std::chrono::steady_clock::now();

	if ((lastAutosaveCheck + autosaveInterval) < now) {
		if (autosave_safe_to_save()) {
			autosave();
		}
		lastAutosaveCheck = std::chrono::steady_clock::now();
	}
}

void tas_input_handler::reload_playback_buffer(tas_input* input)
{
	playback_buffer_current_level = *input->input_buffer();
}

void tas_input_handler::pre_tick()
{
	const int32_t tick = *SIMULATION_TICK;

	if (playback) {
		*DINPUT_MOUSEX = 0;
		*DINPUT_MOUSEY = 0;
	}

	if (last_level_name.compare(MAP_STRING) != 0 || tick == 0) {
		last_level_name = MAP_STRING;
		tas_input_handler::inputTickCounter = 0;
		rng_count_histogram_buffer.clear();
	}

	if (playback)
	{
		if (playback_buffer_current_level.size() > tas_input_handler::inputTickCounter) {
			input_moment savedIM = playback_buffer_current_level[tas_input_handler::inputTickCounter];

			memcpy(KEYBOARD_INPUT, savedIM.inputBuf, sizeof(savedIM.inputBuf));
			*PLAYER_YAW_ROTATION_RADIANS = savedIM.cameraYaw;
			*PLAYER_PITCH_ROTATION_RADIANS = savedIM.cameraPitch;
			*LEFTMOUSE = savedIM.leftMouse;
			*RIGHTMOUSE = savedIM.rightMouse;

			if (savedIM.middleMouse == 0) {
				*MIDDLEMOUSE = 0;
			}
			else {
				if (*MIDDLEMOUSE == 0) {
					*MIDDLEMOUSE = 1;
				}
			}
		}

		auto& gEngine = halo_engine::get();
		auto absoluteTickCount = tas_input_handler::inputTickCounter;
		if (absoluteTickCount >= 1000 && absoluteTickCount % 1000 == 0 && gEngine.is_present_enabled()) {
			SyncBin("HaloTASFiles\\tas.bin", NULL, 0x20001);
		}
	}

	tas_input_handler::inputTickCounter += 1;

	rng_count_since_last_tick = calc_rng_count(last_rng, *RNG, 5000);
	last_rng = *RNG;
	rng_count_histogram_buffer.push_back(static_cast<float>(rng_count_since_last_tick));

	// Keep people honest
	if (tick > 0 && tick % 3600 == 0) {
		auto& gEngine = halo_engine::get();
		gEngine.print_hud_text(L"HaloTAS is running!");
	}

}

static std::string recordCurrentPath;
void tas_input_handler::post_tick()
{
	const int32_t tick = *SIMULATION_TICK - 1;
		
	std::string currentMap{ MAP_STRING };
	std::replace(currentMap.begin(), currentMap.end(), '\\', '.');
	std::string saveFileRoot = "HaloTASFiles/Recordings/";

	if (tick == 0) {
		auto now = std::chrono::system_clock::now();
		auto epoch = now.time_since_epoch().count();
		recordCurrentPath = saveFileRoot + currentMap + "_" + std::to_string(epoch) + ".hbin";
	}

	if (record)
	{
		if (currentMap != "levels\\ui\\ui") {
			std::ofstream logFile(recordCurrentPath, std::ios::app | std::ios::binary);

			input_moment im;
			for (auto i = 0; i < sizeof(im.inputBuf); i++) {
				im.inputBuf[i] = KEYBOARD_INPUT[i];
			}
			im.tick = tick;
			im.inputMouseX = *DINPUT_MOUSEX;
			im.inputMouseY = *DINPUT_MOUSEY;
			im.cameraYaw = *PLAYER_YAW_ROTATION_RADIANS;
			im.cameraPitch = *PLAYER_PITCH_ROTATION_RADIANS;
			im.leftMouse = *LEFTMOUSE;
			im.middleMouse = *MIDDLEMOUSE;
			im.rightMouse = *RIGHTMOUSE;
			im.cameraLocation = *CAMERA_POSITION;
			im.rng = *RNG;

			logFile.write(reinterpret_cast<char*>(&im), sizeof(im));
			logFile.close();
		}
	}
}

void tas_input_handler::pre_frame()
{
	if (*MIDDLEMOUSE == 1) {
		*MIDDLEMOUSE = 2;
	}
}

void tas_input_handler::post_frame()
{

}

void tas_input_handler::pre_loop()
{
	// Test timing of level load
	//static std::chrono::time_point<std::chrono::steady_clock> start;
	/*const int32_t tick = *SIMULATION_TICK;

	if (tick > 1) {
		start = std::chrono::high_resolution_clock::now();
	}

	if (tick == 0) {
		auto finish = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> elapsed = finish - start;

		std::stringstream ss;
		ss << halo::addr::MAP_STRING << ": " << elapsed.count();
		std::string s = ss.str();
		MessageBox(NULL, s.c_str(), "", MB_OK);
	}*/

	// Fix for input buffer overflow
	std::string scan = { 0x2E, 0x00, 0x2E, 0x00, 0x2E, 0x00, 0x20, 0x00 };
	auto* inputSlot = INPUT_SLOT;
	for (int i = 0; i < 4; i++) {
		std::string inputBuf(inputSlot, inputSlot + 128);
		std::size_t n = inputBuf.find(scan);
		if (n != std::string::npos && n < 120) {
			inputSlot[n + 6] = 0x2E;
		}
		inputSlot += 140;
	}
}

void tas_input_handler::post_loop()
{

}

std::vector<std::string> tas_input_handler::get_loaded_levels()
{
	vector<string> names;
	names.reserve(levelInputs.size());
	for (const auto& level : levelInputs) {
		names.push_back(level.first);
	}
	return names;
}

tas_input* tas_input_handler::get_inputs(std::string levelName)
{
	if (levelInputs.count(levelName)) {
		return &levelInputs[levelName];
	}
	return nullptr;
}

std::array<float, 256> tas_input_handler::get_rng_histogram_data()
{
	std::array<float, 256> data{};

	auto part1 = rng_count_histogram_buffer.array_one();
	auto part2 = rng_count_histogram_buffer.array_two();

	memcpy_s(data.data(), sizeof(data), part1.first, sizeof(data[0]) * part1.second);
	memcpy_s(data.data() + part1.second, sizeof(data) - part1.second * sizeof(data[0]), part2.first, sizeof(data[0]) * part2.second);

	return data;
}

void tas_input_handler::clear_rng_histogram_data()
{
	rng_count_histogram_buffer.clear();
}

void tas_input_handler::insert_dummy_rng_histogram_value()
{
	rng_count_histogram_buffer.push_back(0);
}

int32_t tas_input_handler::get_current_playback_tick()
{
	return inputTickCounter;
}

int32_t tas_input_handler::get_rng_advances_since_last_tick()
{
	return rng_count_since_last_tick;
}

bool tas_input_handler::this_tick_enter()
{
	if (!playback)
		return false;

	if (!(playback_buffer_current_level.size() > tas_input_handler::inputTickCounter))
		return false;

	input_moment savedIM = playback_buffer_current_level[tas_input_handler::inputTickCounter];
	if (savedIM.inputBuf[to_underlying(KEYS::Enter)]) {
		return true;
	}
	return false;
}

bool tas_input_handler::this_tick_tab()
{
	if (!playback)
		return false;

	if (!(playback_buffer_current_level.size() > tas_input_handler::inputTickCounter))
		return false;

	input_moment savedIM = playback_buffer_current_level[tas_input_handler::inputTickCounter];
	if (savedIM.inputBuf[to_underlying(KEYS::Tab)]) {
		return true;
	}
	return false;
}

bool tas_input_handler::this_tick_g()
{
	if (!playback)
		return false;

	if (!(playback_buffer_current_level.size() > tas_input_handler::inputTickCounter))
		return false;

	input_moment savedIM = playback_buffer_current_level[tas_input_handler::inputTickCounter];
	if (savedIM.inputBuf[to_underlying(KEYS::G)]) {
		return true;
	}
	return false;
}

bool tas_input_handler::this_tick_mb(int btn)
{
	if (!playback)
		return false;

	if (!(playback_buffer_current_level.size() > tas_input_handler::inputTickCounter))
		return false;

	input_moment savedIM = playback_buffer_current_level[tas_input_handler::inputTickCounter];

	switch (btn) {
	case 0:
		if (savedIM.leftMouse > 0) {
			return true;
		}
		break;
	case 1:
		if (savedIM.rightMouse > 0) {
			return true;
		}
		break;
	case 2:
		if (savedIM.middleMouse > 0) {
			return true;
		}
		break;
	default:
		return false;
	}

	return false;
}

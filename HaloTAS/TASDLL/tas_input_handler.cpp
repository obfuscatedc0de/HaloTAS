#include "tas_input_handler.h"

#include <vector>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <atomic>
#include "halo_constants.h"

static std::vector<input_moment> inputs;
static std::atomic_bool record = false;
static std::atomic_bool playback = false;

void tas_input_handler::set_engine_run_frame_begin()
{
	*ADDR_RUN_FRAME_BEGIN_CODE = record || playback;
}

tas_input_handler::tas_input_handler()
{
	set_engine_run_frame_begin();
}

tas_input_handler::~tas_input_handler()
{
}

void tas_input_handler::set_record(bool newRecord)
{
	record = newRecord;
	if (record) {
		playback = false;
	}

	set_engine_run_frame_begin();
}

void tas_input_handler::set_playback(bool newPlayback)
{
	playback = newPlayback;
	if (playback) {
		record = false;
	}

	set_engine_run_frame_begin();
}

void tas_input_handler::load_inputs_current_level()
{
	std::string currentMapFile{ ADDR_MAP_STRING };
	std::replace(currentMapFile.begin(), currentMapFile.end(), '\\', '.');
	currentMapFile += ".hbin";

	inputs.clear();
	auto estimatedElements = std::filesystem::file_size(currentMapFile) / sizeof(input_moment) + 1;
	inputs.reserve(estimatedElements);
	
	std::ifstream logFile(currentMapFile, std::ios::in | std::ios::binary);
	
	char buf[sizeof(input_moment)];
	while(!logFile.eof())
	{
		logFile.read(buf, sizeof(input_moment));
		if (!logFile) {
			break;
		}

		input_moment* im = reinterpret_cast<input_moment*>(&buf);

		inputs.push_back(*im);
	}
}

static int32_t inputTickCounter = 0;
static std::string last_level_name;
static int32_t last_input_tick;

extern "C" __declspec(dllexport) void WINAPI CustomTickStart() {
	const int32_t tick = *ADDR_SIMULATION_TICK;

	if (playback) {
		*ADDR_DINPUT_MOUSEX = 0;
		*ADDR_DINPUT_MOUSEY = 0;
	}

	if (last_level_name.compare(ADDR_MAP_STRING) != 0) {
		last_level_name = ADDR_MAP_STRING;
		inputTickCounter = 0;
	}

	if (tick == last_input_tick)
		return;

	last_input_tick = tick;

	if (record)
	{
		std::string currentMap{ ADDR_MAP_STRING };
		std::replace(currentMap.begin(), currentMap.end(), '\\', '.');
		std::ofstream logFile(currentMap + ".hbin", std::ios::app | std::ios::binary); // levels.a10.a10.hbin

		input_moment im;
		for (auto i = 0; i < sizeof(im.inputBuf); i++) {
			im.inputBuf[i] = ADDR_KEYBOARD_INPUT[i];
		}
		im.tick = tick;
		//im.inputMouseX = *ADDR_DINPUT_MOUSEX;
		//im.inputMouseY = *ADDR_DINPUT_MOUSEY;
		im.cameraYaw = *ADDR_PLAYER_YAW_ROTATION_RADIANS;
		im.cameraPitch = *ADDR_PLAYER_PITCH_ROTATION_RADIANS;
		im.leftMouse = *ADDR_LEFTMOUSE;
		im.middleMouse = *ADDR_MIDDLEMOUSE;
		im.rightMouse = *ADDR_RIGHTMOUSE;
		im.cameraLocation = *ADDR_CAMERA_POSITION;

		logFile.write(reinterpret_cast<char*>(&im), sizeof(im));
		logFile.close();
	}

	if (playback)
	{
		if (inputs.size() > inputTickCounter) {
			input_moment savedIM = inputs[inputTickCounter];

			memcpy(ADDR_KEYBOARD_INPUT, savedIM.inputBuf, sizeof(savedIM.inputBuf));
			*ADDR_PLAYER_YAW_ROTATION_RADIANS = savedIM.cameraYaw;
			*ADDR_PLAYER_PITCH_ROTATION_RADIANS = savedIM.cameraPitch;
			*ADDR_LEFTMOUSE = savedIM.leftMouse;
			*ADDR_MIDDLEMOUSE = savedIM.middleMouse;
			*ADDR_RIGHTMOUSE = savedIM.rightMouse;
			//*ADDR_CAMERA_POSITION = savedIM.cameraLocation;
			//*ADDR_DINPUT_MOUSEX = savedIM.inputMouseX;
			//*ADDR_DINPUT_MOUSEY = savedIM.inputMouseY;
			//drift = glm::distance(*ADDR_CAMERA_POSITION,savedIM.cameraLocation);

			inputTickCounter += 1;
		}
	}

	__asm {
		call ADDR_TICK_REPLACED_FUNC
	}

	return;
}

extern "C" __declspec(dllexport) void WINAPI CustomFrameStart() {
	
}
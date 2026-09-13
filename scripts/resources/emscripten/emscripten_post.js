// MAME-JavaScript function mappings
var JSMAME = JSMAME || {};
JSMAME.get_machine = Module.cwrap('_ZN15running_machine30emscripten_get_running_machineEv', 'number');
JSMAME.get_ui = Module.cwrap('_ZN15running_machine17emscripten_get_uiEv', 'number');
JSMAME.get_sound = Module.cwrap('_ZN15running_machine20emscripten_get_soundEv', 'number');
JSMAME.ui_set_show_fps = Module.cwrap('_ZN15mame_ui_manager12set_show_fpsEb', '', ['number', 'number']);
JSMAME.ui_get_show_fps = Module.cwrap('_ZNK15mame_ui_manager8show_fpsEv', 'number', ['number']);
JSMAME.sound_manager_mute = Module.cwrap('_ZN13sound_manager4muteEbh', '', ['number', 'number', 'number']);
JSMAME.sdl_pauseaudio = Module.cwrap('SDL_PauseAudio', '', ['number']);
// SDL3: timestamp, keyboardID, rawcode, scancode, down
JSMAME.sdl_sendkeyboardkey = Module.cwrap('SDL_SendKeyboardKey', 'number', ['number', 'number', 'number', 'number', 'number']);
JSMAME.browser_key = Module.cwrap('mamehub_browser_key', '', ['number', 'number']);
JSMAME.force_input = Module.cwrap('mamehub_browser_force_input', '', ['string', 'string']);
JSMAME.clear_forced_inputs = Module.cwrap('mamehub_browser_clear_forced_inputs', '');
JSMAME.netplay_time_ms = Module.cwrap('mamehub_browser_netplay_time_ms', 'number', []);
JSMAME.show_input_settings = function () {
	if (typeof Module._mamehub_browser_show_input_settings === 'function')
		return Module._mamehub_browser_show_input_settings();
	return 0;
};

JSMAME.soft_reset = Module.cwrap('_ZN15running_machine21emscripten_soft_resetEv', null);
JSMAME.hard_reset = Module.cwrap('_ZN15running_machine21emscripten_hard_resetEv', null);
JSMAME.exit = Module.cwrap('_ZN15running_machine15emscripten_exitEv', null, []);
JSMAME.save = Module.cwrap('_ZN15running_machine15emscripten_saveEPKc', null, ['string']);
JSMAME.load = Module.cwrap('_ZN15running_machine15emscripten_loadEPKc', null, ['string']);

var JSMESS = JSMAME;

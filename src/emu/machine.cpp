// license:BSD-3-Clause
// copyright-holders:Aaron Giles
/***************************************************************************

    machine.c

    Controls execution of the core MAME system.

****************************************************************************

    Since there has been confusion in the past over the order of
    initialization and other such things, here it is, all spelled out
    as of January, 2008:

    main()
        - does platform-specific init
        - calls mame_execute() [mame.c]

        mame_execute() [mame.c]
            - calls mame_validitychecks() [validity.c] to perform validity checks on all compiled drivers
            - begins resource tracking (level 1)
            - calls create_machine [mame.c] to initialize the running_machine structure
            - calls init_machine() [mame.c]

            init_machine() [mame.c]
                - calls fileio_init() [fileio.c] to initialize file I/O info
                - calls config_init() [config.c] to initialize configuration system
                - calls input_init() [input.c] to initialize the input system
                - calls output_init() [output.c] to initialize the output system
                - calls state_init() [state.c] to initialize save state system
                - calls state_save_allow_registration() [state.c] to allow registrations
                - calls palette_init() [palette.c] to initialize palette system
                - calls render_init() [render.c] to initialize the rendering system
                - calls ui_init() [ui.c] to initialize the user interface
                - calls generic_machine_init() [machine/generic.c] to initialize generic machine structures
                - calls timer_init() [timer.c] to reset the timer system
                - calls osd_init() [osdepend.h] to do platform-specific initialization
                - calls input_port_init() [inptport.c] to set up the input ports
                - calls rom_init() [romload.c] to load the game's ROMs
                - calls memory_init() [memory.c] to process the game's memory maps
                - calls the driver's DRIVER_INIT callback
                - calls device_list_start() [devintrf.c] to start any devices
                - calls video_init() [video.c] to start the video system
                - calls tilemap_init() [tilemap.c] to start the tilemap system
                - calls crosshair_init() [crsshair.c] to configure the crosshairs
                - calls sound_init() [sound.c] to start the audio system
                - calls debugger_init() [debugger.c] to set up the debugger
                - calls the driver's MACHINE_START, SOUND_START, and VIDEO_START callbacks
                - calls cheat_init() [cheat.c] to initialize the cheat system
                - calls image_init() [image.c] to initialize the image system

            - calls config_load_settings() [config.c] to load the configuration file
            - calls nvram_load [machine/generic.c] to load NVRAM
            - calls ui_display_startup_screens() [ui.c] to display the startup screens
            - begins resource tracking (level 2)
            - calls soft_reset() [mame.c] to reset all systems

                -------------------( at this point, we're up and running )----------------------

            - calls scheduler->timeslice() [schedule.c] over and over until we exit
            - ends resource tracking (level 2), freeing all auto_mallocs and timers
            - calls the nvram_save() [machine/generic.c] to save NVRAM
            - calls config_save_settings() [config.c] to save the game's configuration
            - calls all registered exit routines [mame.c]
            - ends resource tracking (level 1), freeing all auto_mallocs and timers

        - exits the program

***************************************************************************/

#include "NSM_CommonInterface.h"

#include "emu.h"
#include "emuopts.h"
#include "osdepend.h"
#include "config.h"
#include "debugger.h"
#include "render.h"
#include "uiinput.h"
#include "crsshair.h"
#include "unzip.h"
#include "debug/debugvw.h"
#include "debug/debugcpu.h"
#include "dirtc.h"
#include "image.h"
#include "network.h"
#include "romload.h"
#include "ui/uimain.h"
#include <time.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <chrono>

#if defined(EMSCRIPTEN)
#include <emscripten.h>
#endif

using namespace std;
using namespace nsm;

//**************************************************************************
//  RUNNING MACHINE
//**************************************************************************

osd_interface &running_machine::osd() const
{
	return m_manager.osd();
}

//-------------------------------------------------
//  running_machine - constructor
//-------------------------------------------------

running_machine::running_machine(const machine_config &_config, machine_manager &manager)
	: m_side_effects_disabled(0),
		debug_flags(0),
		m_config(_config),
		m_system(_config.gamedrv()),
		m_manager(manager),
		m_current_phase(machine_phase::PREINIT),
		m_paused(false),
		m_hard_reset_pending(false),
		m_exit_pending(false),
		m_soft_reset_timer(nullptr),
		m_rand_seed(0x9d14abd7),
		m_ui_active(_config.options().ui_active()),
		m_basename(_config.gamedrv().name),
		m_sample_rate(_config.options().sample_rate()),
		m_saveload_schedule(saveload_schedule::NONE),
		m_saveload_schedule_time(attotime::zero),
		m_saveload_searchpath(nullptr),

		m_save(*this),
		m_memory(*this),
		m_ioport(*this),
		m_parameters(*this),
		m_scheduler(*this),
		m_dummy_space(_config, "dummy_space", &root_device(), 0)
{
	memset(&m_base_time, 0, sizeof(m_base_time));

	m_dummy_space.set_machine(*this);
	m_dummy_space.config_complete();

	// set the machine on all devices
	device_iterator iter(root_device());
	for (device_t &device : iter)
		device.set_machine(*this);

	// fetch core options
	if (options().debug())
		debug_flags = (DEBUG_FLAG_ENABLED | DEBUG_FLAG_CALL_HOOK) | (DEBUG_FLAG_OSD_ENABLED);
}


//-------------------------------------------------
//  ~running_machine - destructor
//-------------------------------------------------

running_machine::~running_machine()
{
}


//-------------------------------------------------
//  describe_context - return a string describing
//  which device is currently executing and its
//  PC
//-------------------------------------------------

std::string running_machine::describe_context() const
{
	device_execute_interface *executing = m_scheduler.currently_executing();
	if (executing != nullptr)
	{
		cpu_device *cpu = dynamic_cast<cpu_device *>(&executing->device());
		if (cpu != nullptr)
		{
			address_space &prg = cpu->space(AS_PROGRAM);
			return string_format(prg.is_octal() ? "'%s' (%0*o)" :  "'%s' (%0*X)", cpu->tag(), prg.logaddrchars(), cpu->pc());
		}
	}

	return std::string("(no context)");
}


//-------------------------------------------------
//  start - initialize the emulated machine
//-------------------------------------------------

void running_machine::start()
{
	// initialize basic can't-fail systems here
	m_configuration = std::make_unique<configuration_manager>(*this);
	m_input = std::make_unique<input_manager>(*this);
	m_output = std::make_unique<output_manager>(*this);
	m_render = std::make_unique<render_manager>(*this);
	m_bookkeeping = std::make_unique<bookkeeping_manager>(*this);

	// allocate a soft_reset timer
	m_soft_reset_timer = m_scheduler.timer_alloc(timer_expired_delegate(FUNC(running_machine::soft_reset), this));

	// initialize UI input
	m_ui_input = make_unique_clear<ui_input_manager>(*this);

	// init the osd layer
	m_manager.osd().init(*this);

	// create the video manager
	m_video = std::make_unique<video_manager>(*this);
	m_ui = manager().create_ui(*this);

  if(options().server()||options().client())
  {
    // Make the base time a constant for MAMEHub consistency
    m_base_time = 946080000ULL;
  }
  else
  {
	// initialize the base time (needed for doing record/playback)
	::time(&m_base_time);
  }

	// initialize the input system and input ports for the game
	// this must be done before memory_init in order to allow specifying
	// callbacks based on input port tags
	time_t newbase = m_ioport.initialize();
  if(options().server() || options().client())
  {
  }
  else
  {
	if (newbase != 0)
		m_base_time = newbase;
  }

	// initialize the streams engine before the sound devices start
	m_sound = std::make_unique<sound_manager>(*this);

	// resolve objects that can be used by memory maps
	for (device_t &device : device_iterator(root_device()))
		device.resolve_pre_map();

	// configure the address spaces, load ROMs (which needs
	// width/endianess of the spaces), then populate memory (which
	// needs rom bases), and finally initialize CPUs (which needs
	// complete address spaces).  These operations must proceed in this
	// order
	m_rom_load = make_unique_clear<rom_load_manager>(*this);
	m_memory.initialize();

	// save the random seed or save states might be broken in drivers that use the rand() method
	save().save_item(NAME(m_rand_seed));

	// initialize image devices
	m_image = std::make_unique<image_manager>(*this);
	m_tilemap = std::make_unique<tilemap_manager>(*this);
	m_crosshair = make_unique_clear<crosshair_manager>(*this);
	m_network = std::make_unique<network_manager>(*this);

	// initialize the debugger
	if ((debug_flags & DEBUG_FLAG_ENABLED) != 0)
	{
		m_debug_view = std::make_unique<debug_view_manager>(*this);
		m_debugger = std::make_unique<debugger_manager>(*this);
	}

	m_render->resolve_tags();

	manager().create_custom(*this);

	// resolve objects that are created by memory maps
	for (device_t &device : device_iterator(root_device()))
		device.resolve_post_map();

	// register callbacks for the devices, then start them
	add_notifier(MACHINE_NOTIFY_RESET, machine_notify_delegate(&running_machine::reset_all_devices, this));
	add_notifier(MACHINE_NOTIFY_EXIT, machine_notify_delegate(&running_machine::stop_all_devices, this));
	save().register_presave(save_prepost_delegate(FUNC(running_machine::presave_all_devices), this));
	start_all_devices();
	save().register_postload(save_prepost_delegate(FUNC(running_machine::postload_all_devices), this));
	manager().load_cheatfiles(*this);

  m_machine_time = attotime(0,0);
  isRollback = false;

	// if we're coming in with a savegame request, process it now
	const char *savegame = options().state();
	if (savegame[0] != 0)
		schedule_load(savegame);

	// if we're in autosave mode, schedule a load
	else if (options().autosave() && (m_system.flags & MACHINE_SUPPORTS_SAVE) != 0)
		schedule_load("auto");

	manager().update_machine();
}


char CORE_SEARCH_PATH[4096];
extern int doCatchup;
bool catchingUp = false;

bool doRollback = false;
attotime rollbackTime;

//-------------------------------------------------
//  run - execute the machine
//-------------------------------------------------

extern list< ChatLog > chatLogs;
extern std::unordered_map<int, wga::ChronoMap<int,InputState>> playerInputData;

void running_machine::processNetworkBuffer(PeerInputData *inputData,int peerID)
{
  if(inputData != NULL)
  {
    if(inputData->inputtype() == PeerInputData::INPUT)
    {
      //printf("GOT INPUT\n");
      int64_t inputTime = attotime(inputData->time().seconds(), inputData->time().attoseconds()).to_msec();

      for(int a=0;a<inputData->inputstate().players_size();a++) {
        //int player = inputData->inputstate().players(a);
        cout << "Peer " << peerID << " has input for player " << inputData->inputstate().players(a) << " at time " << inputTime << endl;
			}
			if (playerInputData.find(peerID) == playerInputData.end()) {
				cout << "MISSING PLAYERINPUTDATA" << endl;
				exit(1);
			}
			auto &onePlayerInputData = playerInputData[peerID];
			int64_t currentTime = onePlayerInputData.getCurrentTime();
			if (currentTime >= inputTime) {
						cout << "unexpected time " << currentTime << " " << inputTime << " " << peerID << "\n";
						exit(1);
			}
			onePlayerInputData.put(currentTime, inputTime, {{0, inputData->inputstate()}});
    }
    else if(inputData->inputtype() == PeerInputData::CHAT)
    {
      string buffer = inputData->inputbuffer();
      cout << "GOT CHAT " << buffer << endl;
      char buf[4096];
      sprintf(buf,"%s: %s",netCommon->getPeerNameFromID(peerID).c_str(),buffer.c_str());
      std::string chatAString = std::string(buf);
      //Figure out the index of who spoke and send that
      chatLogs.push_back(ChatLog(peerID,::time(NULL),chatAString));
    }
    else if(inputData->inputtype() == PeerInputData::FORCE_VALUE)
    {
      const string &buffer = inputData->inputbuffer();
      cout << "FORCING VALUE\n";
      int blockIndex,memoryStart,memorySize,value;
      unsigned char ramRegion,memoryMask;
      memcpy(&ramRegion,&buffer[0]+1,sizeof(int));
      memcpy(&blockIndex,&buffer[0]+2,sizeof(int));
      memcpy(&memoryStart,&buffer[0]+6,sizeof(int));
      memcpy(&memorySize,&buffer[0]+10,sizeof(int));
      memcpy(&memoryMask,&buffer[0]+14,sizeof(unsigned char));
      memcpy(&value,&buffer[0]+15,sizeof(int));
      // New force
      netCommon->forceLocation(BlockValueLocation(ramRegion,blockIndex,memoryStart,memorySize,memoryMask),value);
      ui().popup_time(3, "Server created new cheat");
    }
    else
    {
      printf("UNKNOWN INPUT BUFFER PACKET!!!\n");
    }
  }
  else
  {
    //break;
  }
}

std::chrono::time_point<std::chrono::system_clock> emulationStartTime;

int running_machine::run(bool quiet)
{
	int error = EMU_ERR_NONE;

  //JJG: Add media path to mess search path
  strcpy(CORE_SEARCH_PATH,options().media_path());

  vector<int> peerIDs;
#if 0
	// use try/catch for deep error recovery
	try
	{
#endif
		m_manager.http()->clear();

		// move to the init phase
		m_current_phase = machine_phase::INIT;

		// if we have a logfile, set up the callback
		if (options().log() && !quiet)
		{
			m_logfile = std::make_unique<emu_file>(OPEN_FLAG_WRITE | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_PATHS);
			osd_file::error filerr = m_logfile->open("error.log");
			assert_always(filerr == osd_file::error::NONE, "unable to open log file");

			using namespace std::placeholders;
			add_logerror_callback(std::bind(&running_machine::logfile_callback, this, _1));
		}

		// then finish setting up our local machine
		start();

		// load the configuration settings
		m_configuration->load_settings();

    //After loading config but before loading nvram, initialize the network
    if(netServer)
    {
      if((system().flags & MACHINE_SUPPORTS_SAVE) == 0)
      {
        ui().popup_time(10, "This game does not have complete save state support, desyncs may not be resolved correctly.");
      }
      //else //JJG: Even if save state support isn't complete, we should try to sync what we can.
      {
        netServer->setSecondsBetweenSync(options().secondsBetweenSync());
      }
    }
    if(netClient)
    {
      if((system().flags & MACHINE_SUPPORTS_SAVE) == 0)
      {
        ui().popup_time(10, "This game does not have complete save state support, desyncs may not be resolved correctly.");
      }
      else
      {
        //Client gets their secondsBetweenSync from server
      }
    }

    if(netServer)
    {
      if(!netServer->serve())
      {
        return EMU_ERR_FATALERROR;
      }
    }


    if(netClient)
    {
      /* specify the filename to save or load */
      //set_saveload_filename(machine, "1");
      //handle_load(machine);
      //if(netClient->getSecondsBetweenSync())
      //doPreSave(this);
      bool retval = netClient->connect(
        (unsigned short)options().selfport(),
        options().hostname(),
        (unsigned short)options().port(),
        this
        );
      printf("LOADED CLIENT\n");
      cout << "RAND/TIME AT INITIAL SYNC: " << m_rand_seed << ' ' << m_base_time << endl;
      if(!retval)
      {
        exit(EMU_ERR_FATALERROR);
      }
      //if(netClient->getSecondsBetweenSync())
      //doPostLoad(this);
    }

		// disallow save state registrations starting here.
		// Don't do it earlier, config load can create network
		// devices with timers.
		m_save.allow_registration(false);

		// load the NVRAM
		nvram_load();

		// set the time on RTCs (this may overwrite parts of NVRAM)
		set_rtc_datetime(system_time(m_base_time));

		sound().ui_mute(false);
		if (!quiet)
			sound().start_recording();

		// initialize ui lists
		// display the startup screens
		manager().ui_initialize(*this);

		// perform a soft reset -- this takes us to the running phase
		soft_reset();

		// handle initial load
		if (m_saveload_schedule != saveload_schedule::NONE)
			handle_saveload();

		export_http_api();

		m_hard_reset_pending = false;

#if defined(EMSCRIPTEN)
		// break out to our async javascript loop and halt
		emscripten_set_running_machine(this);
#endif

    printf("SOFT RESET FINISHED\n");

    emulationStartTime = std::chrono::system_clock::now();

    u64 trackFrameNumber=0;

    attotime largestEmulationTime(0,0);

		// run the CPUs until a reset or exit
		while ((!m_hard_reset_pending && !m_exit_pending) || m_saveload_schedule != saveload_schedule::NONE)
		{
      //printf("IN MAIN LOOP\n");
			g_profiler.start(PROFILER_EXTRA);

#if defined(EMSCRIPTEN)
			// break out to our async javascript loop and halt
			js_set_main_loop(this);
#endif

      attotime timeBefore = m_scheduler.time();
      attotime machineTimeBefore = machine_time();

			// execute CPUs if not paused
			if (!m_paused)
				m_scheduler.timeslice();
			// otherwise, just pump video updates through
			else
				m_video->frame_update();

      attotime timeAfter = m_scheduler.time();
      if (timeBefore > timeAfter) {
        cout << "OOPS! WE WENT BACK IN TIME SOMEHOW\n";
        exit(1);
      }
      if (timeAfter > largestEmulationTime) {
        largestEmulationTime = timeAfter;
        catchingUp = false;
      }
      bool timePassed = (timeBefore != timeAfter);
      bool secondPassed = false;
      bool tenthSecondPassed = false;

      if (timePassed) {
        //cout << "TIME MOVED FROM " << timeBefore << " TO " << timeAfter << endl;
        m_machine_time += (timeAfter - timeBefore);
        attotime machineTimeAfter = machine_time();
        secondPassed = machineTimeBefore.seconds() != machineTimeAfter.seconds();
        tenthSecondPassed = secondPassed || ((machineTimeBefore.attoseconds()/(ATTOSECONDS_PER_SECOND/10ULL)) != (machineTimeAfter.attoseconds()/(ATTOSECONDS_PER_SECOND/10ULL)));

        if (netCommon) {
          // Process any remaining packets.
          if(!netCommon->update(this)) {
            cout << "NETWORK FAILED\n";
            ::exit(1);
          }

          netCommon->getPeerIDs(peerIDs);
          for (int a=0;a<peerIDs.size();a++) {
            while(true) {
              nsm::PeerInputData input = netCommon->popInput(peerIDs[a]);
              if (input.has_time()) {
                processNetworkBuffer(&input, peerIDs[a]);
              } else {
                break;
              }
            }
          }
        }

      }

      static int lastSyncSecond = 0;
      //printf("EMULATION FINSIHED\n");

      static int firstTimeAtSecond=0;
      if(m_machine_time.seconds()>0 && m_scheduler.can_save() && timePassed && !firstTimeAtSecond) {
        firstTimeAtSecond = 1;
        if (netServer) {
          // Initial sync
          netServer->sync(this);
        }
        if (netClient) {
          // Load initial data
          netClient->createInitialBlocks(this);
        }
      }
      else if(m_machine_time.seconds()>0 && m_scheduler.can_save() && timePassed)
      {
        if(
          netServer &&
          lastSyncSecond != m_machine_time.seconds() &&
          netServer->getSecondsBetweenSync()>0 &&
          !netCommon->isRollback() &&
          (m_machine_time.seconds()%netServer->getSecondsBetweenSync())==0
          )
        {
          lastSyncSecond = m_machine_time.seconds();
          printf("SERVER SYNC AT TIME: %d\n",int(::time(NULL)));
          if (!m_scheduler.can_save())
          {
            printf("ANONYMOUS TIMER! COULD NOT DO FULL SYNC\n");
          }
          else
          {
            netServer->sync(this);
            //nvram_save(*this);
            cout << "RAND/TIME AT SYNC: " << m_rand_seed << ' ' << machine_time().seconds() << '.' << machine_time().attoseconds() << endl;
          }
        }

        if(
          netClient &&
          lastSyncSecond != m_machine_time.seconds() &&
          netClient->getSecondsBetweenSync()>0 &&
          !netCommon->isRollback() &&
          (m_machine_time.seconds()%netClient->getSecondsBetweenSync())==0
          )
        {
          lastSyncSecond = m_machine_time.seconds();
          if (!m_scheduler.can_save())
          {
            printf("ANONYMOUS TIMER! THIS COULD BE BAD (BUT HOPEFULLY ISN'T)\n");
          }
          else
          {
            //The client should update sync check just in case the server didn't have an anon timer
            m_save.dispatch_presave();
            netClient->updateSyncCheck();
            cout << "RAND/TIME AT SYNC: " << m_rand_seed << ' ' << machine_time().seconds() << '.' << machine_time().attoseconds() << endl;
            m_save.dispatch_postload();
          }
        }

        static clock_t lastSyncTime=clock();
        if(netCommon)
        {
          //printf("IN NET LOOP\n");
          lastSyncTime = clock();
          //TODO: Fix forces
          //netCommon->updateForces(getRawMemoryRegions());
          if(netServer)
          {
            netServer->update(this);


          }
          if(netClient)
          {
            //printf("IN CLIENT LOOP\n");
            pair<bool,bool> survivedAndGotSync;
            survivedAndGotSync.first = netClient->update(this);
            //printf("CLIENT UPDATED\n");
            if(survivedAndGotSync.first==false)
            {
              m_exit_pending = true;
              break;
            }

            // Don't try to resync on the same frame that you created
            // the sync check.
            if (lastSyncSecond != m_machine_time.seconds()) {
              bool gotSync = netClient->sync(this);
              if(gotSync)
              {
                if (!m_scheduler.can_save())
                {
                  printf("ANONYMOUS TIMER! THIS COULD BE BAD (BUT HOPEFULLY ISN'T)\n");
                }
                cout << "GOT SYNC FROM SERVER\n";
                cout << "RAND/TIME AT SYNC: " << m_rand_seed << ' ' << m_base_time << endl;
              }
            }
          }
        }
      }
			// handle save/load
			if (timePassed && m_saveload_schedule != saveload_schedule::NONE) {
				handle_saveload();
      } else if (timePassed && netCommon && netCommon->isRollback()) {
				/*
				We need to implement rollback without frame counts
        if (trackFrameNumber>0 && m_scheduler.can_save() && trackFrameNumber != inputFrameNumber) {
          isRollback = true;
          immediate_save("test");
          cout << "SAVING AT TIME " << m_machine_time << endl;
          isRollback = false;
          trackFrameNumber=0;
        }
        if(m_machine_time.seconds()>0 && m_scheduler.can_save() && tenthSecondPassed) {
          cout << "Tenth second passed: " << m_machine_time << endl;
          if (secondPassed) {
            cout << "Second passed" << endl;
          }
          trackFrameNumber = inputFrameNumber;
        }
        if(m_machine_time.seconds()>0 && m_scheduler.can_save()) {
          if (doRollback) {
            doRollback = false;
            isRollback = true;
            immediate_load("test");
            isRollback = false;
            catchingUp = true;
          }
        }
				*/
      }

			g_profiler.stop();
		}
		m_manager.http()->clear();

		// and out via the exit phase
		m_current_phase = machine_phase::EXIT;

		// save the NVRAM and configuration
		sound().ui_mute(true);
		if (options().nvram_save())
			nvram_save();
		m_configuration->save_settings();
#if 0
	}
	catch (emu_fatalerror &fatal)
	{
		osd_printf_error("Fatal error: %s\n", fatal.string());
		error = EMU_ERR_FATALERROR;
		if (fatal.exitcode() != 0)
			error = fatal.exitcode();
	}
	catch (emu_exception &)
	{
		osd_printf_error("Caught unhandled emulator exception\n");
		error = EMU_ERR_FATALERROR;
	}
	catch (binding_type_exception &btex)
	{
		osd_printf_error("Error performing a late bind of type %s to %s\n", btex.m_actual_type.name(), btex.m_target_type.name());
		error = EMU_ERR_FATALERROR;
	}
	catch (tag_add_exception &aex)
	{
		osd_printf_error("Tag '%s' already exists in tagged map\n", aex.tag());
		error = EMU_ERR_FATALERROR;
	}
	catch (std::exception &ex)
	{
		osd_printf_error("Caught unhandled %s exception: %s\n", typeid(ex).name(), ex.what());
		error = EMU_ERR_FATALERROR;
	}
	catch (...)
	{
		osd_printf_error("Caught unhandled exception\n");
		error = EMU_ERR_FATALERROR;
	}
#endif
	// make sure our phase is set properly before cleaning up,
	// in case we got here via exception
	m_current_phase = machine_phase::EXIT;

	// call all exit callbacks registered
	call_notifiers(MACHINE_NOTIFY_EXIT);
	util::archive_file::cache_clear();

	// close the logfile
	m_logfile.reset();
	return error;
}

//-------------------------------------------------
//  schedule_exit - schedule a clean exit
//-------------------------------------------------

void running_machine::schedule_exit()
{
	m_exit_pending = true;

	// if we're executing, abort out immediately
	m_scheduler.eat_all_cycles();

	// if we're autosaving on exit, schedule a save as well
	if (options().autosave() && (m_system.flags & MACHINE_SUPPORTS_SAVE) && this->time() > attotime::zero)
		schedule_save("auto");
}


//-------------------------------------------------
//  schedule_hard_reset - schedule a hard-reset of
//  the machine
//-------------------------------------------------

void running_machine::schedule_hard_reset()
{
	m_hard_reset_pending = true;

	// if we're executing, abort out immediately
	m_scheduler.eat_all_cycles();
}


//-------------------------------------------------
//  schedule_soft_reset - schedule a soft-reset of
//  the system
//-------------------------------------------------

void running_machine::schedule_soft_reset()
{
	m_soft_reset_timer->adjust(attotime::zero);

	// we can't be paused since the timer needs to fire
	resume();

	// if we're executing, abort out immediately
	m_scheduler.eat_all_cycles();
}


//-------------------------------------------------
//  get_statename - allow to specify a subfolder of
//  the state directory for state loading/saving,
//  very useful for MESS and consoles or computers
//  where you can have separate folders for diff
//  software
//-------------------------------------------------

std::string running_machine::get_statename(const char *option) const
{
	std::string statename_str("");
	if (option == nullptr || option[0] == 0)
		statename_str.assign("%g");
	else
		statename_str.assign(option);

	// strip any extension in the provided statename
	int index = statename_str.find_last_of('.');
	if (index != -1)
		statename_str = statename_str.substr(0, index);

	// handle %d in the template (for image devices)
	std::string statename_dev("%d_");
	int pos = statename_str.find(statename_dev);

	if (pos != -1)
	{
		// if more %d are found, revert to default and ignore them all
		if (statename_str.find(statename_dev.c_str(), pos + 3) != -1)
			statename_str.assign("%g");
		// else if there is a single %d, try to create the correct snapname
		else
		{
			int name_found = 0;

			// find length of the device name
			int end1 = statename_str.find("/", pos + 3);
			int end2 = statename_str.find("%", pos + 3);
			int end;

			if ((end1 != -1) && (end2 != -1))
				end = std::min(end1, end2);
			else if (end1 != -1)
				end = end1;
			else if (end2 != -1)
				end = end2;
			else
				end = statename_str.length();

			if (end - pos < 3)
				fatalerror("Something very wrong is going on!!!\n");

			// copy the device name to an std::string
			std::string devname_str;
			devname_str.assign(statename_str.substr(pos + 3, end - pos - 3));
			//printf("check template: %s\n", devname_str.c_str());

			// verify that there is such a device for this system
			for (device_image_interface &image : image_interface_iterator(root_device()))
			{
				// get the device name
				std::string tempdevname(image.brief_instance_name());
				//printf("check device: %s\n", tempdevname.c_str());

				if (devname_str.compare(tempdevname) == 0)
				{
					// verify that such a device has an image mounted
					if (image.basename_noext() != nullptr)
					{
						std::string filename(image.basename_noext());

						// setup snapname and remove the %d_
						strreplace(statename_str, devname_str.c_str(), filename.c_str());
						statename_str.erase(pos, 3);
						//printf("check image: %s\n", filename.c_str());

						name_found = 1;
					}
				}
			}

			// or fallback to default
			if (name_found == 0)
				statename_str.assign("%g");
		}
	}

	// substitute path and gamename up front
	strreplace(statename_str, "/", PATH_SEPARATOR);
	strreplace(statename_str, "%g", basename());

	return statename_str;
}


//-------------------------------------------------
//  compose_saveload_filename - composes a filename
//  for state loading/saving
//-------------------------------------------------

std::string running_machine::compose_saveload_filename(std::string &&filename, const char **searchpath)
{
	std::string result;

	// is this an absolute path?
	if (osd_is_absolute_path(filename))
	{
		// if so, this is easy
		if (searchpath != nullptr)
			*searchpath = nullptr;
		result = std::move(filename);
	}
	else
	{
		// this is a relative path; first specify the search path
		if (searchpath != nullptr)
			*searchpath = options().state_directory();

		// take into account the statename option
		const char *stateopt = options().state_name();
		std::string statename = get_statename(stateopt);
		result = string_format("%s%s%s.sta", statename, PATH_SEPARATOR, filename);
	}
	return result;
}


//-------------------------------------------------
//  set_saveload_filename - specifies the filename
//  for state loading/saving
//-------------------------------------------------

void running_machine::set_saveload_filename(std::string &&filename)
{
	// compose the save/load filename and persist it
	m_saveload_pending_file = compose_saveload_filename(std::move(filename), &m_saveload_searchpath);
}


//-------------------------------------------------
//  schedule_save - schedule a save to occur as
//  soon as possible
//-------------------------------------------------

void running_machine::schedule_save(std::string &&filename)
{
	// specify the filename to save or load
	set_saveload_filename(std::move(filename));

	// note the start time and set a timer for the next timeslice to actually schedule it
	m_saveload_schedule = saveload_schedule::SAVE;
	m_saveload_schedule_time = this->time();

	// we can't be paused since we need to clear out anonymous timers
	resume();
}


//-------------------------------------------------
//  immediate_save - save state.
//-------------------------------------------------

void running_machine::immediate_save(const char *filename)
{
	// specify the filename to save or load
	set_saveload_filename(filename);

	// set up some parameters for handle_saveload()
	m_saveload_schedule = saveload_schedule::SAVE;
	m_saveload_schedule_time = this->time();

	// jump right into the save, anonymous timers can't hurt us!
	handle_saveload();
}


//-------------------------------------------------
//  schedule_load - schedule a load to occur as
//  soon as possible
//-------------------------------------------------

void running_machine::schedule_load(std::string &&filename)
{
	// specify the filename to save or load
	set_saveload_filename(std::move(filename));

	// note the start time and set a timer for the next timeslice to actually schedule it
	m_saveload_schedule = saveload_schedule::LOAD;
	m_saveload_schedule_time = this->time();

	// we can't be paused since we need to clear out anonymous timers
	resume();
}


//-------------------------------------------------
//  immediate_load - load state.
//-------------------------------------------------

void running_machine::immediate_load(const char *filename)
{
	// specify the filename to save or load
	set_saveload_filename(filename);

	// set up some parameters for handle_saveload()
	m_saveload_schedule = saveload_schedule::LOAD;
	m_saveload_schedule_time = this->time();

	// jump right into the load, anonymous timers can't hurt us
	handle_saveload();
}


//-------------------------------------------------
//  rewind_capture - capture and append a new
//  state to the rewind list
//-------------------------------------------------

bool running_machine::rewind_capture()
{
	return m_save.rewind()->capture();
}


//-------------------------------------------------
//  rewind_step - a single step back through
//  rewind states
//-------------------------------------------------

bool running_machine::rewind_step()
{
	return m_save.rewind()->step();
}


//-------------------------------------------------
//  rewind_invalidate - mark all the future rewind
//  states as invalid
//-------------------------------------------------

void running_machine::rewind_invalidate()
{
	m_save.rewind()->invalidate();
}


//-------------------------------------------------
//  pause - pause the system
//-------------------------------------------------

void running_machine::pause()
{
  if(netCommon)
  {
    //Can't pause during netplay
    return;
  }
	// ignore if nothing has changed
	if (m_paused)
		return;
	m_paused = true;

	// call the callbacks
	call_notifiers(MACHINE_NOTIFY_PAUSE);
}


//-------------------------------------------------
//  resume - resume the system
//-------------------------------------------------

void running_machine::resume()
{
	// ignore if nothing has changed
	if (!m_paused)
		return;
	m_paused = false;

	// call the callbacks
	call_notifiers(MACHINE_NOTIFY_RESUME);
}


//-------------------------------------------------
//  toggle_pause - toggles the pause state
//-------------------------------------------------

void running_machine::toggle_pause()
{
	if (paused())
	{
		rewind_invalidate();
		resume();
	}
	else
		pause();
}


//-------------------------------------------------
//  add_notifier - add a notifier of the
//  given type
//-------------------------------------------------

void running_machine::add_notifier(machine_notification event, machine_notify_delegate callback, bool first)
{
	assert_always(m_current_phase == machine_phase::INIT, "Can only call add_notifier at init time!");

	if(first)
		m_notifier_list[event].push_front(std::make_unique<notifier_callback_item>(callback));

	// exit notifiers are added to the head, and executed in reverse order
	else if (event == MACHINE_NOTIFY_EXIT)
		m_notifier_list[event].push_front(std::make_unique<notifier_callback_item>(callback));

	// all other notifiers are added to the tail, and executed in the order registered
	else
		m_notifier_list[event].push_back(std::make_unique<notifier_callback_item>(callback));
}


//-------------------------------------------------
//  add_logerror_callback - adds a callback to be
//  called on logerror()
//-------------------------------------------------

void running_machine::add_logerror_callback(logerror_callback callback)
{
	assert_always(m_current_phase == machine_phase::INIT, "Can only call add_logerror_callback at init time!");
		m_string_buffer.reserve(1024);
	m_logerror_list.push_back(std::make_unique<logerror_callback_item>(callback));
}


//-------------------------------------------------
//  strlog - send an error logging string to the
//  debugger and any OSD-defined output streams
//-------------------------------------------------

void running_machine::strlog(const char *str) const
{
	// log to all callbacks
	for (auto &cb : m_logerror_list)
		cb->m_func(str);
}


//-------------------------------------------------
//  debug_break - breaks into the debugger, if
//  enabled
//-------------------------------------------------

void running_machine::debug_break()
{
	if ((debug_flags & DEBUG_FLAG_ENABLED) != 0)
		debugger().debug_break();
}

//-------------------------------------------------
//  base_datetime - retrieve the time of the host
//  system; useful for RTC implementations
//-------------------------------------------------

void running_machine::base_datetime(system_time &systime)
{
	systime.set(m_base_time);
}


//-------------------------------------------------
//  current_datetime - retrieve the current time
//  (offset by the base); useful for RTC
//  implementations
//-------------------------------------------------

void running_machine::current_datetime(system_time &systime)
{
	systime.set(m_base_time + this->time().seconds());
}


//-------------------------------------------------
//  set_rtc_datetime - set the current time on
//  battery-backed RTCs
//-------------------------------------------------

void running_machine::set_rtc_datetime(const system_time &systime)
{
	for (device_rtc_interface &rtc : rtc_interface_iterator(root_device()))
		if (rtc.has_battery())
			rtc.set_current_time(systime);
}


//-------------------------------------------------
//  rand - standardized random numbers
//-------------------------------------------------

// TODO: using this function in the core is strongly discouraged (can affect inp playback),
//       maybe we should consider moving this function to somewhere else instead.
u32 running_machine::rand()
{
	m_rand_seed = 1664525 * m_rand_seed + 1013904223;

	// return rotated by 16 bits; the low bits have a short period
	// and are frequently used
	return (m_rand_seed >> 16) | (m_rand_seed << 16);
}


//-------------------------------------------------
//  call_notifiers - call notifiers of the given
//  type
//-------------------------------------------------

void running_machine::call_notifiers(machine_notification which)
{
	for (auto& cb : m_notifier_list[which])
		cb->m_func();
}


//-------------------------------------------------
//  handle_saveload - attempt to perform a save
//  or load
//-------------------------------------------------

const int MAX_STATES = 10 * 5;
pair<attotime, vector<unsigned char> > states[MAX_STATES];
int onState = 0;

void running_machine::handle_saveload()
{
  if (!m_scheduler.can_save()) {
    throw emu_fatalerror("CANNOT SAVE!");
  }

  u32 const openflags = (m_saveload_schedule == saveload_schedule::LOAD) ? OPEN_FLAG_READ : (OPEN_FLAG_WRITE | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_PATHS);
	const char *opname = (m_saveload_schedule == saveload_schedule::LOAD) ? "load" : "save";

	// if no name, bail
	if (!m_saveload_pending_file.empty())
	{
		const char *const opname = (m_saveload_schedule == saveload_schedule::LOAD) ? "load" : "save";

		// if there are anonymous timers, we can't save just yet, and we can't load yet either
		// because the timers might overwrite data we have loaded
		if (!m_scheduler.can_save())
		{
			// if more than a second has passed, we're probably screwed
			if ((this->time() - m_saveload_schedule_time) > attotime::from_seconds(1))
				popmessage("Unable to %s due to pending anonymous timers. See error.log for details.", opname);
			else
				return; // return without cancelling the operation
		}
		else
		{
			u32 const openflags = (m_saveload_schedule == saveload_schedule::LOAD) ? OPEN_FLAG_READ : (OPEN_FLAG_WRITE | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_PATHS);

			// open the file
			emu_file file(m_saveload_searchpath, openflags);
			auto const filerr = file.open(m_saveload_pending_file);
			if (filerr == osd_file::error::NONE)
			{
				const char *const opnamed = (m_saveload_schedule == saveload_schedule::LOAD) ? "loaded" : "saved";

				// read/write the save state
				save_error saverr = (m_saveload_schedule == saveload_schedule::LOAD) ? m_save.read_file(file) : m_save.write_file(file);

				// handle the result
				switch (saverr)
				{
				case STATERR_ILLEGAL_REGISTRATIONS:
					popmessage("Error: Unable to %s state due to illegal registrations. See error.log for details.", opname);
					break;

				case STATERR_INVALID_HEADER:
					popmessage("Error: Unable to %s state due to an invalid header. Make sure the save state is correct for this machine.", opname);
					break;

				case STATERR_READ_ERROR:
					popmessage("Error: Unable to %s state due to a read error (file is likely corrupt).", opname);
					break;

				case STATERR_WRITE_ERROR:
					popmessage("Error: Unable to %s state due to a write error. Verify there is enough disk space.", opname);
					break;

				case STATERR_NONE:
					if (!(m_system.flags & MACHINE_SUPPORTS_SAVE))
						popmessage("State successfully %s.\nWarning: Save states are not officially supported for this machine.", opnamed);
					else
						popmessage("State successfully %s.", opnamed);
					break;

				default:
					popmessage("Error: Unknown error during state %s.", opnamed);
					break;
				}

				// close and perhaps delete the file
				if (saverr != STATERR_NONE && m_saveload_schedule == saveload_schedule::SAVE)
					file.remove_on_close();
			}
			else if (openflags == OPEN_FLAG_READ && filerr == osd_file::error::NOT_FOUND)
				// attempt to load a non-existent savestate, report empty slot
				popmessage("Error: No savestate file to load.", opname);
			else
				popmessage("Error: Failed to open file for %s operation.", opname);
		}
	}

	// unschedule the operation
	m_saveload_pending_file.clear();
	m_saveload_searchpath = nullptr;
	m_saveload_schedule = saveload_schedule::NONE;
}


//-------------------------------------------------
//  soft_reset - actually perform a soft-reset
//  of the system
//-------------------------------------------------

void running_machine::soft_reset(void *ptr, s32 param)
{
	logerror("Soft reset\n");

	// temporarily in the reset phase
	m_current_phase = machine_phase::RESET;

	// call all registered reset callbacks
	call_notifiers(MACHINE_NOTIFY_RESET);

	// now we're running
	m_current_phase = machine_phase::RUNNING;
}


//-------------------------------------------------
//  logfile_callback - callback for logging to
//  logfile
//-------------------------------------------------

void running_machine::logfile_callback(const char *buffer)
{
	if (m_logfile != nullptr)
	{
		m_logfile->puts(buffer);
		m_logfile->flush();
	}
}


//-------------------------------------------------
//  start_all_devices - start any unstarted devices
//-------------------------------------------------

void running_machine::start_all_devices()
{
	m_dummy_space.start();

	// iterate through the devices
	int last_failed_starts = -1;
	while (last_failed_starts != 0)
	{
		// iterate over all devices
		int failed_starts = 0;
		for (device_t &device : device_iterator(root_device()))
			if (!device.started())
			{
				// attempt to start the device, catching any expected exceptions
				try
				{
					// if the device doesn't have a machine yet, set it first
					if (device.m_machine == nullptr)
						device.set_machine(*this);

					// now start the device
					osd_printf_verbose("Starting %s '%s'\n", device.name(), device.tag());
					device.start();
				}

				// handle missing dependencies by moving the device to the end
				catch (device_missing_dependencies &)
				{
					// if we're the end, fail
					osd_printf_verbose("  (missing dependencies; rescheduling)\n");
					failed_starts++;
				}
			}

		// each iteration should reduce the number of failed starts; error if
		// this doesn't happen
		if (failed_starts == last_failed_starts)
			throw emu_fatalerror("Circular dependency in device startup!");
		last_failed_starts = failed_starts;
	}
}


//-------------------------------------------------
//  reset_all_devices - reset all devices in the
//  hierarchy
//-------------------------------------------------

void running_machine::reset_all_devices()
{
	// reset the root and it will reset children
	root_device().reset();
}


//-------------------------------------------------
//  stop_all_devices - stop all the devices in the
//  hierarchy
//-------------------------------------------------

void running_machine::stop_all_devices()
{
	// first let the debugger save comments
	if ((debug_flags & DEBUG_FLAG_ENABLED) != 0)
		debugger().cpu().comment_save();

	// iterate over devices and stop them
	for (device_t &device : device_iterator(root_device()))
		device.stop();
}


//-------------------------------------------------
//  presave_all_devices - tell all the devices we
//  are about to save
//-------------------------------------------------

void running_machine::presave_all_devices()
{
	for (device_t &device : device_iterator(root_device()))
		device.pre_save();
}


//-------------------------------------------------
//  postload_all_devices - tell all the devices we
//  just completed a load
//-------------------------------------------------

void running_machine::postload_all_devices()
{
	for (device_t &device : device_iterator(root_device()))
		device.post_load();
}


/***************************************************************************
    NVRAM MANAGEMENT
***************************************************************************/

/*-------------------------------------------------
    nvram_filename - returns filename of system's
    NVRAM depending of selected BIOS
-------------------------------------------------*/

std::string running_machine::nvram_filename(device_t &device) const
{
	// start with either basename or basename_biosnum
	std::ostringstream result;
	result << basename();
	if (root_device().system_bios() != 0 && root_device().default_bios() != root_device().system_bios())
		util::stream_format(result, "_%d", root_device().system_bios() - 1);

	// device-based NVRAM gets its own name in a subdirectory
	if (device.owner() != nullptr)
	{
		// add per software nvrams into one folder
		const char *software = nullptr;
		for (device_t *dev = &device; dev->owner() != nullptr; dev = dev->owner())
		{
			device_image_interface *intf;
			if (dev->interface(intf))
			{
				software = intf->basename_noext();
				break;
			}
		}
		if (software != nullptr && *software != '\0')
			result << PATH_SEPARATOR << software;

		std::string tag(device.tag());
		tag.erase(0, 1);
		strreplacechr(tag,':', '_');
		result << PATH_SEPARATOR << tag;
	}
	return result.str();
}

int nvram_size(running_machine &machine) {
	int retval=0;

	nvram_interface_iterator iter(machine.root_device());
	for (device_nvram_interface &nvram : nvram_interface_iterator(machine.root_device()))
		{
      std::string filename;
			emu_file file(machine.options().nvram_directory(), OPEN_FLAG_READ);
      if (file.open(machine.nvram_filename(nvram.device()).c_str()) == osd_file::error::NONE)
			{
				retval += file.size();
			}
		}
	return retval;
}

/*-------------------------------------------------
    nvram_load - load a system's NVRAM
-------------------------------------------------*/

void running_machine::nvram_load()
{
	int overrideNVram = 0;
	if(netCommon) {
          if(nvram_size(*this)>=32*1024*1024) {
            overrideNVram=1;
            ui().popup_time(3, "The NVRAM for this game is too big, not loading NVRAM.");
          }
	}

	for (device_nvram_interface &nvram : nvram_interface_iterator(root_device()))
	{
		emu_file file(options().nvram_directory(), OPEN_FLAG_READ);
		if (!overrideNVram && file.open(nvram_filename(nvram.device())) == osd_file::error::NONE)
		{
			nvram.nvram_load(file);
			file.close();
		}
		else
			nvram.nvram_reset();
	}
}


/*-------------------------------------------------
    nvram_save - save a system's NVRAM
-------------------------------------------------*/

void running_machine::nvram_save()
{
  static bool first=true;
	if(netCommon) {
          if(nvram_size(*this)>=32*1024*1024) {
            if(first) {
              ui().popup_time(3, "The NVRAM for this game is too big, not saving NVRAM.");
              first = false;
            }
            return;
          }
	}

	for (device_nvram_interface &nvram : nvram_interface_iterator(root_device()))
	{
		emu_file file(options().nvram_directory(), OPEN_FLAG_WRITE | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_PATHS);
		if (file.open(nvram_filename(nvram.device())) == osd_file::error::NONE)
		{
			nvram.nvram_save(file);
			file.close();
		}
	}
}


//**************************************************************************
//  OUTPUT
//**************************************************************************

void running_machine::popup_clear() const
{
	ui().popup_time(0, " ");
}

void running_machine::popup_message(util::format_argument_pack<std::ostream> const &args) const
{
	std::string const temp(string_format(args));
	ui().popup_time(temp.length() / 40 + 2, "%s", temp);
}


//**************************************************************************
//  CALLBACK ITEMS
//**************************************************************************

//-------------------------------------------------
//  notifier_callback_item - constructor
//-------------------------------------------------

running_machine::notifier_callback_item::notifier_callback_item(machine_notify_delegate func)
	: m_func(std::move(func))
{
}


//-------------------------------------------------
//  logerror_callback_item - constructor
//-------------------------------------------------

running_machine::logerror_callback_item::logerror_callback_item(logerror_callback func)
	: m_func(func)
{
}

void running_machine::export_http_api()
{
	if (m_manager.http()->is_active()) {
		m_manager.http()->add_http_handler("/api/machine", [this](http_manager::http_request_ptr request, http_manager::http_response_ptr response)
		{
			rapidjson::StringBuffer s;
			rapidjson::Writer<rapidjson::StringBuffer> writer(s);
			writer.StartObject();
			writer.Key("name");
			writer.String(m_basename.c_str());

			writer.Key("devices");
			writer.StartArray();

			device_iterator iter(this->root_device());
			for (device_t &device : iter)
				writer.String(device.tag());

			writer.EndArray();
			writer.EndObject();

			response->set_status(200);
			response->set_content_type("application/json");
			response->set_body(s.GetString());
		});
	}
}

//**************************************************************************
//  SYSTEM TIME
//**************************************************************************

//-------------------------------------------------
//  system_time - constructor
//-------------------------------------------------

system_time::system_time()
{
	set(0);
}

system_time::system_time(time_t t)
{
	set(t);
}


//-------------------------------------------------
//  set - fills out a system_time structure
//-------------------------------------------------

void system_time::set(time_t t)
{
	// FIXME: this crashes if localtime or gmtime returns nullptr
	time = t;
	local_time.set(*localtime(&t));
	utc_time.set(*gmtime(&t));
}


//-------------------------------------------------
//  get_tm_time - converts a tm struction to a
//  MAME mame_system_tm structure
//-------------------------------------------------

void system_time::full_time::set(struct tm &t)
{
  //JJG: Force clock to 1/1/2000.
  if(netCommon)
  {
    second  = 0;
    minute  = 0;
    hour  = 0;
    mday  = 0;
    month = 0;
    year  = 2000;
    weekday = 6;
    day   = 0;
    is_dst  = 0;
  }
  else
{
	second  = t.tm_sec;
	minute  = t.tm_min;
	hour    = t.tm_hour;
	mday    = t.tm_mday;
	month   = t.tm_mon;
	year    = t.tm_year + 1900;
	weekday = t.tm_wday;
	day  = t.tm_yday;
	is_dst  = t.tm_isdst;
}
}


//**************************************************************************
//  DUMMY ADDRESS SPACE
//**************************************************************************

READ8_MEMBER(dummy_space_device::read)
{
	throw emu_fatalerror("Attempted to read from generic address space (offs %X)\n", offset);
}

WRITE8_MEMBER(dummy_space_device::write)
{
	throw emu_fatalerror("Attempted to write to generic address space (offs %X = %02X)\n", offset, data);
}

void dummy_space_device::dummy(address_map &map)
{
	map(0x00000000, 0xffffffff).rw(FUNC(dummy_space_device::read), FUNC(dummy_space_device::write));
}

DEFINE_DEVICE_TYPE(DUMMY_SPACE, dummy_space_device, "dummy_space", "Dummy Space")

dummy_space_device::dummy_space_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock) :
	device_t(mconfig, DUMMY_SPACE, tag, owner, clock),
	device_memory_interface(mconfig, *this),
	m_space_config("dummy", ENDIANNESS_LITTLE, 8, 32, 0, address_map_constructor(), address_map_constructor(FUNC(dummy_space_device::dummy), this))
{
}

void dummy_space_device::device_start()
{
}

//-------------------------------------------------
//  memory_space_config - return a description of
//  any address spaces owned by this device
//-------------------------------------------------

device_memory_interface::space_config_vector dummy_space_device::memory_space_config() const
{
	return space_config_vector {
		std::make_pair(0, &m_space_config)
	};
}


//**************************************************************************
//  JAVASCRIPT PORT-SPECIFIC
//**************************************************************************

#if defined(EMSCRIPTEN)

running_machine * running_machine::emscripten_running_machine;

void running_machine::emscripten_main_loop()
{
	running_machine *machine = emscripten_running_machine;

	g_profiler.start(PROFILER_EXTRA);

	// execute CPUs if not paused
	if (!machine->m_paused)
	{
		device_scheduler * scheduler;
		scheduler = &(machine->scheduler());

		// Emscripten will call this function at 60Hz, so step the simulation
		// forward for the amount of time that has passed since the last frame
		const attotime frametime(0,HZ_TO_ATTOSECONDS(60));
		const attotime stoptime(scheduler->time() + frametime);

		while (!machine->m_paused && !machine->scheduled_event_pending() && scheduler->time() < stoptime)
		{
			scheduler->timeslice();
			// handle save/load
			if (machine->m_saveload_schedule != saveload_schedule::NONE)
			{
				machine->handle_saveload();
				break;
			}
		}
	}
	// otherwise, just pump video updates through
	else
		machine->m_video->frame_update();

	// cancel the emscripten loop if the system has been told to exit
	if (machine->exit_pending())
	{
		emscripten_cancel_main_loop();
	}

	g_profiler.stop();
}

void running_machine::emscripten_set_running_machine(running_machine *machine)
{
	emscripten_running_machine = machine;
	EM_ASM (
		JSMESS.running = true;
	);
	emscripten_set_main_loop(&(emscripten_main_loop), 0, 1);
}

running_machine * running_machine::emscripten_get_running_machine()
{
	return emscripten_running_machine;
}

ui_manager * running_machine::emscripten_get_ui()
{
	return &(emscripten_running_machine->ui());
}

sound_manager * running_machine::emscripten_get_sound()
{
	return &(emscripten_running_machine->sound());
}

void running_machine::emscripten_soft_reset() {
	emscripten_running_machine->schedule_soft_reset();
}
void running_machine::emscripten_hard_reset() {
	emscripten_running_machine->schedule_hard_reset();
}
void running_machine::emscripten_exit() {
	emscripten_running_machine->schedule_exit();
}
void running_machine::emscripten_save(const char *name) {
	emscripten_running_machine->schedule_save(name);
}
void running_machine::emscripten_load(const char *name) {
	emscripten_running_machine->schedule_load(name);
}

#endif /* defined(EMSCRIPTEN) */

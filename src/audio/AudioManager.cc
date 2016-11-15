#include "AudioManager.hh"
#include "assets/AssetManager.hh"
#include "core/Logging.hh"

#include <fmod_errors.h>
#include <fmod_common.h>
#include <boost/filesystem.hpp>

#include <sstream>
#include <cstdio>

namespace fs = boost::filesystem;

namespace sp
{
	const string AUDIO_BANK_DIR = "../assets/audio/Build/Desktop/";

	void FmodErrCheckFn(FMOD_RESULT result, const char *file, int line)
	{
		if (result != FMOD_OK)
		{
			char err[1024];
			snprintf(err, 1024, "%s(%d): FMOD error %d - %s", file, line,
				result, FMOD_ErrorString(result));
			Logf(err);
			Assert(false, err);
		}
	}

	AudioManager::AudioManager()
	{
		FMOD_CHECK(FMOD::Studio::System::create(&system));

		// project is authored for 5.1 sound
		FMOD::System* lowLevelSystem = NULL;
		FMOD_CHECK(system->getLowLevelSystem(&lowLevelSystem) );
		FMOD_CHECK(lowLevelSystem->setSoftwareFormat(0,
			FMOD_SPEAKERMODE_5POINT1, 0) );

		FMOD_CHECK(system->initialize(1024, FMOD_STUDIO_INIT_NORMAL,
			FMOD_INIT_NORMAL, nullptr));
	}

	AudioManager::~AudioManager()
	{
		for (FMOD::Studio::Bank *bank : banks)
		{
			FMOD_CHECK(bank->unload());
		}
		FMOD_CHECK(system->release());
	}

	bool AudioManager::Frame()
	{
		// TODO: update sound locations
		// FMOD_3D_ATTRIBUTES vec = {0};
		// vec.position.x = sinf(t) * 1.0f;        /* Rotate sound in a circle */
		// vec.position.y = 0;                     /* At ground level */
		// vec.position.z = cosf(t) * 1.0f;        /* Rotate sound in a circle */
		// FMOD_CHECK( objectInstance->set3DAttributes(&vec) );


		// TODO: update based on player orientation
		FMOD_3D_ATTRIBUTES listener_vec = {0};

		listener_vec.forward.x = 0;
		listener_vec.forward.y = 0;
		listener_vec.forward.z = 1;
		listener_vec.up.x = 0;
		listener_vec.up.y = 1;
		listener_vec.up.z = 0;

		FMOD_CHECK(system->setListenerAttributes(0, &listener_vec));
		FMOD_CHECK(system->update());

		return true;
	}

	void AudioManager::LoadBank(const string &bankFile)
	{
		FMOD::Studio::Bank *bank = nullptr;
		Logf("loading audio bank: %s", bankFile);

		FMOD_RESULT res = system->loadBankFile(bankFile.c_str(),
			FMOD_STUDIO_LOAD_BANK_NORMAL, &bank);
		FMOD_CHECK(res);

		if (FMOD_OK != res)
		{
			std::stringstream ss;
			ss << "Could not load bank file \"" << bankFile << "\"";
			throw invalid_argument(ss.str());
		}
	}

	void AudioManager::LoadProjectFiles()
	{
		fs::path bankDir(AUDIO_BANK_DIR);
		fs::directory_iterator end_iter;

		vector<string> bankFiles;

		if (fs::exists(bankDir) && fs::is_directory(bankDir))
		{
			for (fs::directory_iterator dir_iter(bankDir);
			     dir_iter != end_iter; ++dir_iter)
			{
				fs::directory_entry &dentry = *dir_iter;
				const string ext = dentry.path().extension().string();

				if (fs::is_regular_file(dentry.status())
				    && ext.compare(".bank") == 0)
				{
					this->LoadBank(dentry.path().string());
				}
		 	}
		}
	}

	void AudioManager::StartEvent(const string &eventName)
	{
		if (eventDescriptions.count(eventName) != 1)
		{
			FMOD::Studio::EventDescription *eventDescr = nullptr;
			FMOD_RESULT res = system->getEvent(eventName.c_str(), &eventDescr);
			if (res != FMOD_OK)
			{
				throw invalid_argument(eventName + " is not a valid event");
			}
			eventDescriptions[eventName] = eventDescr;
		}

		FMOD::Studio::EventInstance *instance = nullptr;

		FMOD_CHECK(eventDescriptions[eventName]->createInstance(&instance));
		FMOD_CHECK(instance->start());
	}
}

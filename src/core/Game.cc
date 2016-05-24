#include "core/Game.hh"
#include "core/Logging.hh"

#include "assets/Model.hh"

namespace sp
{
	Game::Game() : graphics(this)
	{
		duck = assets.LoadModel("duck");
		for (auto node : duck->ListNodes())
		{
			Debugf("Node: %s with %d meshes", node.node->name, node.node->meshes.size());
		}

		graphics.CreateContext();
	}

	Game::~Game()
	{
	}

	void Game::Start()
	{
		try
		{
			while (true)
			{
				if (ShouldStop()) break;
				if (!Frame()) break;
			}
		}
		catch (char const *err)
		{
			Errorf(err);
		}
	}

	bool Game::Frame()
	{
		if (!graphics.Frame()) return false;

		return true;
	}

	bool Game::ShouldStop()
	{
		return !graphics.HasActiveContext();
	}
}


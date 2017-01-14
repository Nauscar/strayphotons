#include "core/Game.hh"
#include "core/Logging.hh"

#include "game/GameLogic.hh"
#include "assets/Scene.hh"
#include "assets/AssetManager.hh"
#include "ecs/components/Renderable.hh"
#include "ecs/components/Transform.hh"
#include "ecs/components/Physics.hh"
#include "ecs/components/View.hh"

#include <cxxopts.hpp>
#include <glm/glm.hpp>
#include <cmath>

namespace sp
{
	GameLogic::GameLogic(Game *game)
		: game(game), input(&game->input), humanControlSystem(&game->entityManager, &game->input)
	{
	}

	void GameLogic::Init()
	{
		scene = GAssets.LoadScene(game->options["map"].as<string>(), &game->entityManager, game->physics);

		ecs::Entity player = scene->FindEntity("player");
		humanControlSystem.AssignController(player, game->physics);

		game->graphics.SetPlayerView(player);

		input->AddCharInputCallback([&](uint32 ch)
		{
			if (ch == 'q')
			{
				auto entity = game->entityManager.NewEntity();
				auto model = GAssets.LoadModel("dodecahedron");
				entity.Assign<ecs::Renderable>(model);
				auto transform = entity.Assign<ecs::Transform>();
				transform->Translate(glm::vec3(0, 5, 0));

				PhysxManager::ActorDesc desc;
				desc.transform = physx::PxTransform(physx::PxVec3(0, 5, 0));
				auto actor = game->physics.CreateActor(model, desc);

				if (actor)
				{
					auto physics = entity.Assign<ecs::Physics>(actor);
				}
			}
		});

		//game->audio.StartEvent("event:/german nonsense");
	}

	GameLogic::~GameLogic()
	{
	}

	bool GameLogic::Frame(double dtSinceLastFrame)
	{
		if (!humanControlSystem.Frame(dtSinceLastFrame))
		{
			return false;
		}
		return true;
	}
}

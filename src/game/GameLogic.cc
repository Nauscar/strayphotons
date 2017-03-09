#define _USE_MATH_DEFINES
#include <cmath>

#include "core/Game.hh"
#include "core/Logging.hh"
#include "core/CVar.hh"
#include "core/Console.hh"

#include "game/GameLogic.hh"
#include "assets/Scene.hh"
#include "assets/AssetManager.hh"
#include "ecs/components/Barrier.hh"
#include "ecs/components/Light.hh"
#include "ecs/components/LightSensor.hh"
#include "ecs/components/Name.hh"
#include "ecs/components/Physics.hh"
#include "ecs/components/Renderable.hh"
#include "ecs/components/Transform.hh"
#include "ecs/components/TriggerArea.hh"
#include "ecs/components/View.hh"
#include "ecs/components/Controller.hh"
#include "physx/PhysxUtils.hh"

#include <cxxopts.hpp>
#include <glm/glm.hpp>

namespace sp
{
	GameLogic::GameLogic(Game *game)
		: game(game), input(&game->input), humanControlSystem(&game->entityManager, &game->input, &game->physics), flashlightFixed(false), sunPos(0), funcs(this)
	{
		funcs.Register("loadscene", "Load a scene", &GameLogic::LoadScene);
		funcs.Register("reloadscene", "Reload current scene", &GameLogic::ReloadScene);
		funcs.Register("printdebug", "Print some debug info about the scene", &GameLogic::PrintDebug);
	}

	static CVar<float> CVarFlashlight("r.Flashlight", 100, "Flashlight intensity");
	static CVar<float> CVarFlashlightAngle("r.FlashlightAngle", 20, "Flashlight spot angle");
	static CVar<int> CVarFlashlightResolution("r.FlashlightResolution", 512, "Flashlight shadow map resolution");
	static CVar<float> CVarSunPosition("g.SunPosition", 0.2, "Sun angle");

	void GameLogic::Init()
	{
		if (game->options["map"].count())
		{
			LoadScene(game->options["map"].as<string>());
		}
		else
		{
			LoadScene("menu");
		}

		input->AddKeyInputCallback([&](int key, int state)
		{
			if (input->FocusLocked()) return;

			if (state == GLFW_PRESS)
			{
				if (key == GLFW_KEY_ESCAPE)
				{
					game->menuGui.OpenPauseMenu();
				}
				else if (key == GLFW_KEY_F5)
				{
					ReloadScene("");
				}
			}
		});

		input->AddCharInputCallback([&](uint32 ch)
		{
			if (input->FocusLocked()) return;
			if (ch == 'q') // Spawn dodecahedron
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
					auto physics = entity.Assign<ecs::Physics>(actor, model);
				}
			}
			else if (ch == 'p') // Toggle flashlight following player
			{
				if (flashlight.Valid())
				{
					auto transform = flashlight.Get<ecs::Transform>();
					ecs::Entity player = scene->FindEntity("player");
					auto playerTransform = player.Get<ecs::Transform>();
					flashlightFixed = !flashlightFixed;
					if (flashlightFixed)
					{
						transform->SetTransform(transform->GetModelTransform(game->entityManager));
						transform->SetRelativeTo(ecs::Entity());
					}
					else
					{
						transform->SetTransform(glm::mat4());
						transform->Translate(glm::vec3(0, -0.3, 0));
						transform->SetRelativeTo(player);
					}
				}
			}
			else if (ch == 'f') // Turn flashlight on and off
			{
				if (flashlight.Valid())
				{
					auto light = flashlight.Get<ecs::Light>();
					if (!light->intensity)
					{
						light->intensity = CVarFlashlight.Get(true);
					}
					else
					{
						light->intensity = 0;
					}
				}
			}
			else if (ch == 'g') // open/close barrier
			{
				for (auto e : game->entityManager.EntitiesWith<ecs::Barrier>())
				{
					auto barrierComp = e.Get<ecs::Barrier>();
					if (barrierComp->isOpen)
					{
						ecs::Barrier::Close(e, game->physics);
						Logf("closed");
					}
					else
					{
						ecs::Barrier::Open(e, game->physics);
						Logf("opened");
					}
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
		if (!scene) return true;
		ecs::Entity player = scene->FindEntity("player");
		if (!player.Valid()) return true;

		for (auto entity : game->entityManager.EntitiesWith<ecs::TriggerArea>())
		{
			auto area = entity.Get<ecs::TriggerArea>();
			auto transform = player.Get<ecs::Transform>();
			auto playerPos = transform->GetPosition();
			if (
				playerPos.x > area->boundsMin.x &&
				playerPos.y > area->boundsMin.y &&
				playerPos.z > area->boundsMin.z &&
				playerPos.x < area->boundsMax.x &&
				playerPos.y < area->boundsMax.y &&
				playerPos.z < area->boundsMax.z &&
				!area->triggered
			)
			{
				area->triggered = true;
				Debugf("Player at: %f %f %f", playerPos.x, playerPos.y, playerPos.z);
				Logf("Triggering event: %s", area->command);
				GConsoleManager.ParseAndExecute(area->command);
			}
		}

		ecs::Entity sun = scene->FindEntity("sun");
		if (sun.Valid())
		{
			if (CVarSunPosition.Get() == 0)
			{
				sunPos += dtSinceLastFrame * (0.05 + std::abs(sin(sunPos) * 0.1));
				if (sunPos > M_PI / 2.0) sunPos = -M_PI / 2.0;
			}
			else
			{
				sunPos = CVarSunPosition.Get();
			}

			auto transform = sun.Get<ecs::Transform>();
			transform->SetTransform(glm::mat4());
			transform->SetRotation(glm::mat4());
			transform->Rotate(glm::radians(-90.0), glm::vec3(1, 0, 0));
			transform->Rotate(sunPos, glm::vec3(0, 1, 0));
			transform->Translate(glm::vec3(sin(sunPos) * 40.0, cos(sunPos) * 40.0, 0));
		}

		if (CVarFlashlight.Changed())
		{
			auto light = flashlight.Get<ecs::Light>();
			if (light->intensity) light->intensity = CVarFlashlight.Get(true);
		}
		if (CVarFlashlightAngle.Changed())
		{
			auto light = flashlight.Get<ecs::Light>();
			light->spotAngle = glm::radians(CVarFlashlightAngle.Get(true));
		}
		if (CVarFlashlightResolution.Changed())
		{
			auto view = flashlight.Get<ecs::View>();
			view->extents = glm::ivec2(CVarFlashlightResolution.Get(true));
		}
		if (!humanControlSystem.Frame(dtSinceLastFrame))
		{
			return false;
		}

		// TODO: remove later
		// serves as debug, ensures that moving kinematic objects work
		for (auto e : game->entityManager.EntitiesWith<ecs::Barrier>())
		{
			auto barrierPhysics = e.Get<ecs::Physics>();
			physx::PxRigidDynamic *actor = barrierPhysics->dynamic;
			game->physics.Translate(actor, physx::PxVec3(0, 0.001, 0));
		}

		return true;
	}

	void GameLogic::LoadScene(const string &nameRef)
	{
		const string name = nameRef;
		game->graphics.RenderLoading();

		game->entityManager.DestroyAll();

		if (scene != nullptr)
		{
			for (auto &line : scene->unloadExecList)
			{
				GConsoleManager.ParseAndExecute(line);
			}
		}

		scene.reset();
		scene = GAssets.LoadScene(name, &game->entityManager, game->physics);
		if (!scene) return;

		ecs::Entity player = scene->FindEntity("player");
		humanControlSystem.AssignController(player, game->physics);

		game->graphics.SetPlayerView(player);

		for (auto &line : scene->autoExecList)
		{
			GConsoleManager.ParseAndExecute(line);
		}

		// Create flashlight entity
		flashlight = game->entityManager.NewEntity();
		auto transform = flashlight.Assign<ecs::Transform>();
		transform->Translate(glm::vec3(0, -0.3, 0));
		transform->SetRelativeTo(player);
		auto light = flashlight.Assign<ecs::Light>();
		light->tint = glm::vec3(1.0);
		light->spotAngle = glm::radians(CVarFlashlightAngle.Get(true));
		auto view = flashlight.Assign<ecs::View>();
		view->extents = glm::vec2(CVarFlashlightResolution.Get());
		view->clip = glm::vec2(0.1, 64);
	}

	void GameLogic::ReloadScene(const string &arg)
	{
		if (scene)
		{
			auto player = scene->FindEntity("player");
			if (arg == "reset")
			{
				LoadScene(scene->name);
			}
			else if (player.Valid() && player.Has<ecs::Transform>())
			{
				// Store the player position and set it back on the new player entity
				auto transform = player.Get<ecs::Transform>();
				auto position = transform->GetPosition();
				auto rotation = transform->rotate;

				LoadScene(scene->name);

				if (scene)
				{
					player = scene->FindEntity("player");
					if (player.Valid() && player.Has<ecs::HumanController>())
					{
						humanControlSystem.Teleport(player, position, rotation);
					}
				}
			}
		}
	}

	void GameLogic::PrintDebug(const string &)
	{
		Logf("Currently loaded scene: %s", scene ? scene->name : "none");
		if (!scene) return;
		auto player = scene->FindEntity("player");
		if (player.Valid() && player.Has<ecs::Transform>() && player.Has<ecs::HumanController>())
		{
			auto transform = player.Get<ecs::Transform>();
			auto controller = player.Get<ecs::HumanController>();
			auto position = transform->GetPosition();
			auto pxFeet = controller->pxController->getFootPosition();
			Logf("Player position: [%f, %f, %f], feet: %f", position.x, position.y, position.z, pxFeet.y);
			Logf("Player velocity: [%f, %f, %f]", controller->velocity.x, controller->velocity.y, controller->velocity.z);
			Logf("Player on ground: %s", controller->onGround ? "true" : "false");
		}
		else
		{
			Logf("Scene has no valid player");
		}

		for (auto &ent : game->entityManager.EntitiesWith<ecs::LightSensor>())
		{
			auto sensor = ent.Get<ecs::LightSensor>();
			auto i = sensor->illuminance;
			string name = ent.ToString();

			if (ent.Has<ecs::Name>())
			{
				name += " (" + ent.Get<ecs::Name>()->name + ")";
			}

			Logf("Light sensor %s: %f %f %f", name, i.r, i.g, i.b);
		}
	}
}

#pragma once

#include <Ecs.hh>
#include "ecs/components/Controller.hh"
#include "game/InputManager.hh"

#include "physx/PhysxManager.hh"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ecs
{
	class HumanControlSystem
	{
	public:
		HumanControlSystem(ecs::EntityManager *entities, sp::InputManager *input, sp::PhysxManager *physics);
		~HumanControlSystem();

		/**
		 * Call this once per frame
		 */
		bool Frame(double dtSinceLastFrame);

		/**
		 * Assigns a default HumanController to the given entity.
		 */
		ecs::Handle<HumanController> AssignController(ecs::Entity entity, sp::PhysxManager &px);

		/**
		 * Teleports the entity and properly syncs to physx.
		 */
		void Teleport(ecs::Entity entity, glm::vec3 position, glm::quat rotation = glm::quat());

	private:
		glm::vec3 CalculatePlayerVelocity(ecs::Entity entity, double dtSinceLastFrame, glm::vec3 inDirection, bool jump, bool sprint, bool crouch);
		void MoveEntity(ecs::Entity entity, double dtSinceLastFrame, glm::vec3 velocity);

		/**
		* Resize entity used for crouching and uncrouching. Can perform overlap checks to make sure resize is valid
		*/
		bool ResizeEntity(ecs::Entity entity, float height, float oldHeight, bool overlapCheck);

		/**
		* Pick up the object that the player is looking at and make it move at to a fixed location relative to camera
		*/
		void Interact(ecs::Entity entity, double dt);

		/**
		 * Rotate the object the player is currently holding, using mouse input.
		 * Returns true if there is currently a target.
		 */
		bool InteractRotate(ecs::Entity entity, double dt, glm::vec2 dCursor);

		ecs::EntityManager *entities;
		sp::InputManager *input;
		sp::PhysxManager *physics;

	};
}
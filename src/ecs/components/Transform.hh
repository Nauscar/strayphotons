#pragma once

#include "Common.hh"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Ecs.hh>

namespace ecs
{
	class Transform
	{
	public:
		Transform() {}
		void SetRelativeTo(ecs::Entity);

		/**
		 * Return the matrix for specifying the placement of the entity in the world.
		 * This involves computing the incremental model transforms for any entities
		 * that this placement is relative to.
		 */
		glm::mat4 GetModelTransform(ecs::EntityManager &manager);

		/**
		 * Change the local rotation by "radians" amount about the local "axis"
		 */
		void Rotate(float radians, glm::vec3 axis);

		/**
		 * Change the local position by an amount in the local x, y, z planes
		 */
		void Translate(glm::vec3 xyz);

		/**
		 * Change the local position to xyz
		 */
		void SetTransform(glm::mat4 mat);

		/**
		 * Change the local position to xyz
		 */
		void SetPosition(glm::vec3 pos);

		/**
		 * Get position from mat4
		 */
		glm::vec3 GetPosition();

		/**
		 * Change the local scale by an amount in the local x, y, z planes.
		 * This will propagate to Transforms that are relative to this one.
		 */
		void Scale(glm::vec3 xyz);

		glm::mat4 GetRotateMatrix();

		glm::mat4 translate;
		glm::mat4 scale;
		glm::quat rotate;

	private:
		ecs::Entity::Id relativeTo;
	};
}
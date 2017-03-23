#pragma once

#include "Common.hh"

#include <Ecs.hh>
#include <glm/glm.hpp>

namespace ecs
{
	struct VoxelInfo;
}

namespace sp
{
	static const int MAX_LIGHTS = 16;
	static const int MAX_MIRRORS = 16;
	static const int MAX_LIGHT_SENSORS = 32;
	static const int MAX_VOXEL_AREAS = 5;

	struct GLLightData
	{
		glm::vec3 position;
		float spotAngleCos;

		glm::vec3 tint;
		float intensity;

		glm::vec3 direction;
		float illuminance;

		glm::mat4 proj;
		glm::mat4 invProj;
		glm::mat4 view;
		glm::vec4 mapOffset;
		glm::vec2 clip;
		int gelId;
		float padding[1];
	};

	static_assert(sizeof(GLLightData) == 17 * 4 * sizeof(float), "GLLightData size incorrect");

	struct GLMirrorData
	{
		glm::mat4 modelMat;
		glm::mat4 reflectMat;
		glm::vec4 plane;
		glm::vec2 size;
		float padding[2];
	};

	static_assert(sizeof(GLMirrorData) == 10 * 4 * sizeof(float), "GLMirrorData size incorrect");

	struct GLLightSensorData
	{
		glm::vec3 position;
		float id0; // IDs are 4 bytes each of the 8 byte entity ID
		glm::vec3 direction;
		float id1;
	};

	static_assert(sizeof(GLLightSensorData) == 2 * 4 * sizeof(float), "GLLightSensorData size incorrect");

	struct GLVoxelArea
	{
		glm::vec3 min;
		float padding1[1];
		glm::vec3 max;
		float padding2[1];
	};

	static_assert(sizeof(GLVoxelArea) == 2 * 4 * sizeof(float), "GLVoxelArea size incorrect");

	struct GLVoxelInfo
	{
		glm::vec3 voxelGridCenter;
		float voxelSize;
		GLVoxelArea areas[MAX_VOXEL_AREAS];
	};

	static_assert(sizeof(GLVoxelInfo) == (1 + 2 * MAX_VOXEL_AREAS) * 4 * sizeof(float), "GLVoxelInfo size incorrect");

	int FillLightData(GLLightData *data, ecs::EntityManager &manager);
	int FillMirrorData(GLMirrorData *data, ecs::EntityManager &manager);
	void FillVoxelInfo(GLVoxelInfo *data, ecs::VoxelInfo &source);
}

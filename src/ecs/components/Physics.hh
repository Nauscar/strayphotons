#pragma once

#include <PxActor.h>	

namespace ECS
{
	struct Physics
	{
		Physics() {}
		Physics(physx::PxRigidActor* actor) : actor(actor) {}
		physx::PxRigidActor* actor;
	};
}

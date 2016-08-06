#include <PxScene.h>
#include "physx/PhysxManager.hh"

#include "assets/Model.hh"
#include "core/Logging.hh"
#include "core/CVar.hh"

#include <thread>

namespace sp
{
	using namespace physx;
	static CVar<float> CVarGravity("g.Gravity", -9.81f, "Force of gravity");

	PhysxManager::PhysxManager()
	{
		Logf("PhysX %d.%d.%d starting up", PX_PHYSICS_VERSION_MAJOR, PX_PHYSICS_VERSION_MINOR, PX_PHYSICS_VERSION_BUGFIX);

		PxTolerancesScale scale;

		pxFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, defaultAllocatorCallback, defaultErrorCallback);
		physics = PxCreatePhysics(PX_PHYSICS_VERSION, *pxFoundation, scale);
		Assert(physics, "PxCreatePhysics");

		pxCooking = PxCreateCooking(PX_PHYSICS_VERSION, *pxFoundation, PxCookingParams(scale));
		Assert(pxCooking, "PxCreateCooking");

		CreatePhysxScene();
		StartThread();
		StartSimulation();
	}

	PhysxManager::~PhysxManager()
	{
		StopSimulation();
		DestroyPhysxScene();

		pxCooking->release();
		physics->release();
		pxFoundation->release();
	}

	void PhysxManager::Frame(double timeStep)
	{
		while (resultsPending)
		{
			if (!simulate)
			{
				return;
			}

			Lock();
			if (scene->fetchResults())
			{
				// Lock continues to be held.
				resultsPending = false;
				break;
			}

			Unlock();
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}

		if (CVarGravity.Changed())
		{
			scene->setGravity(PxVec3(0.f, CVarGravity.Get(true), 0.f));

			vector<PxActor *> buffer(256, nullptr);
			size_t startIndex = 0;

			while (true)
			{
				size_t n = scene->getActors(PxActorTypeFlag::eRIGID_DYNAMIC, &buffer[0], buffer.size(), startIndex);

				for (size_t i = 0; i < n; i++)
				{
					auto dynamicActor = static_cast<PxRigidDynamic *>(buffer[i]);
					dynamicActor->wakeUp();
				}

				if (n < buffer.size())
					break;

				startIndex += n;
			}
		}

		scene->simulate((PxReal) timeStep);
		resultsPending = true;
		Unlock();
	}

	void PhysxManager::CreatePhysxScene()
	{
		PxSceneDesc sceneDesc(physics->getTolerancesScale());

		sceneDesc.gravity = PxVec3(0.f, CVarGravity.Get(true), 0.f);

		if (!sceneDesc.filterShader)
			sceneDesc.filterShader = PxDefaultSimulationFilterShader;

		dispatcher = PxDefaultCpuDispatcherCreate(1);
		sceneDesc.cpuDispatcher = dispatcher;

		scene = physics->createScene(sceneDesc);
		Assert(scene, "creating PhysX scene");

		Lock();
		PxMaterial *groundMat = physics->createMaterial(0.6f, 0.5f, 0.0f);
		PxRigidStatic *groundPlane = PxCreatePlane(*physics, PxPlane(0.f, 1.f, 0.f, 1.03f), *groundMat);
		scene->addActor(*groundPlane);
		Unlock();
	}

	void PhysxManager::DestroyPhysxScene()
	{
		Lock();
		scene->release();
		scene = nullptr;
		dispatcher->release();
		dispatcher = nullptr;
	}

	void PhysxManager::StartThread()
	{
		std::thread thread([&]
		{
			const double rate = 60.0; // frames/sec

			while (true)
			{
				double frameStart = glfwGetTime();

				if (simulate)
					Frame(1.0 / rate);

				double frameEnd = glfwGetTime();
				double sleepFor = 1.0 / rate + frameStart - frameEnd;

				std::this_thread::sleep_for(std::chrono::duration<double>(sleepFor));
			}
		});

		thread.detach();
	}

	void PhysxManager::StartSimulation()
	{
		Lock();
		simulate = true;
		Unlock();
	}

	void PhysxManager::StopSimulation()
	{
		Lock();
		simulate = false;
		Unlock();
	}

	void PhysxManager::Lock()
	{
		Assert(scene);
		scene->lockWrite();
	}

	void PhysxManager::Unlock()
	{
		Assert(scene);
		scene->unlockWrite();
	}

	void PhysxManager::ReadLock()
	{
		Assert(scene);
		scene->lockRead();
	}

	void PhysxManager::ReadUnlock()
	{
		Assert(scene);
		scene->unlockRead();
	}

	PxRigidActor *PhysxManager::CreateActor(shared_ptr<Model> model, PxTransform transform, PxMeshScale scale, bool dynamic)
	{
		Lock();
		PxRigidActor *actor;

		if (dynamic)
			actor = physics->createRigidDynamic(transform);
		else
			actor = physics->createRigidStatic(transform);

		PxMaterial *mat = physics->createMaterial(0.6f, 0.5f, 0.0f);

		for (auto prim : model->primitives)
		{
			auto posAttrib = prim->attributes[0];
			Assert(posAttrib.componentCount == 3);
			Assert(posAttrib.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

			// Build convex hull
			auto buffer = model->GetScene()->buffers[posAttrib.bufferName];

			PxConvexMeshDesc convexDesc;
			convexDesc.points.count = posAttrib.components;
			convexDesc.points.stride = posAttrib.byteStride;
			convexDesc.points.data = (PxVec3 *)(buffer.data.data() + posAttrib.byteOffset);
			convexDesc.flags = PxConvexFlag::eCOMPUTE_CONVEX;

			PxDefaultMemoryOutputStream buf;
			PxConvexMeshCookingResult::Enum result;

			if (!pxCooking->cookConvexMesh(convexDesc, buf, &result))
			{
				Errorf("Failed to cook PhysX hull for %s", model->name);
				return nullptr;
			}

			PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
			PxConvexMesh *hull = physics->createConvexMesh(input);

			actor->createShape(PxConvexMeshGeometry(hull, scale), *mat);
		}

		if (dynamic)
			PxRigidBodyExt::updateMassAndInertia(*static_cast<PxRigidDynamic *>(actor), 1.0f);

		scene->addActor(*actor);
		Unlock();
		return actor;
	}
}

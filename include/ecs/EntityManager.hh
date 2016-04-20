#ifndef ENTITY_MANAGER_H
#define ENTITY_MANAGER_H

#include <queue>
#include <iostream>
#include <functional>
#include <stdexcept>
#include <sstream>
#include "Common.hh"
#include "ecs/ComponentManager.hh"
#include "ecs/Entity.hh"


namespace sp
{
	template <typename T> struct identity
	{
		typedef T type;
	};

	class EntityManager
	{
	public:
		class EntityCollection
		{
		public:
			class Iterator : public std::iterator<std::input_iterator_tag, Entity>
			{
			public:
				Iterator(ComponentManager &cm, const ComponentManager::ComponentMask &compMask,
				         ComponentPoolEntityCollection *compEntColl, ComponentPoolEntityCollection::Iterator compIt);
				Iterator &operator++();
				bool operator==(Iterator &other);
				bool operator!=(Iterator &other);
				Entity operator*();
			private:
				ComponentManager &cm;
				const ComponentManager::ComponentMask &compMask;
				ComponentPoolEntityCollection* compEntColl;
				ComponentPoolEntityCollection::Iterator compIt;
			};

			// An IterateLock on compEntColl's component pool is needed so that
			// if any components are deleted they do not affect the ordering of any of the other
			// components in this pool (normally deletions are a swap-to-back operation)
			// this lock releases on destructions so it's okay if Exceptions are raised when iterating
			EntityCollection(ComponentManager &cm, const ComponentManager::ComponentMask &compMask,
			                 ComponentPoolEntityCollection compEntColl,
			                 unique_ptr<BaseComponentPool::IterateLock> &&iLock);
			Iterator begin();
			Iterator end();
		private:
			ComponentManager &cm;
			ComponentManager::ComponentMask compMask;
			ComponentPoolEntityCollection compEntColl;
			unique_ptr<BaseComponentPool::IterateLock> iLock;
		};

		EntityManager();

		Entity NewEntity();
		void Destroy(Entity e);
		bool Valid(Entity e) const;

		// DO NOT CACHE THIS POINTER, a component's pointer may change over time
		template <typename CompType, typename ...T>
		CompType *Assign(Entity e, T... args);

		template <typename CompType>
		void Remove(Entity e);

		void RemoveAllComponents(Entity e);

		template <typename CompType>
		bool Has(Entity e) const;

		// DO NOT CACHE THIS POINTER, a component's pointer may change over time
		template <typename CompType>
		CompType *Get(Entity e);

		template <typename ...CompTypes>
		EntityCollection EntitiesWith();
		EntityCollection EntitiesWith(ComponentManager::ComponentMask compMask);

		/**
		 * Call a callback function for each entity that has the given components.
		 * The component ptrs will be populated with the entity's components when
		 * the callback occurs.
		 *
		 * The types of the args in the given callback function specify that only entities
		 * which have all of these components should trigger a callback.
		 *
		 * During iteration, if any new entities qualify to have the callback called on them
		 * (because an earlier callback caused them to gain the right component types) they
		 * will not be iterated over in the same pass.
		 *
		 * If you will be removing components from the same set of entities that are being
		 * iterated over then you **CANNOT** depend on how many of these components will actually
		 * be iterated over before starting iteration; if A deletes B and A is iterated to first then
		 * B will not trigger a callback but if B is iterated to before A then both will trigger callbacks.
		 *
		 * Ex: the following snippet shows how to call a lambda function for each entity
		 *     that has "Comp1" and "Comp5" components:
		 *
		 *     EachWith([](Entity e, Comp1 *c1, Comp5 *c5) {
		 *         cout << "Entity " << e << " has C1 x " << c1->x << " and C5 y " << c5->y;
		 *     })
		 */
		template <typename ...CompTypes>
		Entity EachWith(typename identity<std::function<void(Entity entity, CompTypes *...)>>::type callback);


	private:

		static const size_t RECYCLE_ENTITY_COUNT = 2048;

		vector<Entity> entities;
		vector<uint16> entIndexToGen;
		std::queue<uint64> freeEntityIndexes;
		uint64 nextEntityIndex;
		ComponentManager compMgr;
	};


	EntityManager::EntityManager()
	{
		// entity 0 is reserved for the NULL Entity
		nextEntityIndex = 1;

		// update data structures for the NULL Entity
		// TODO-cs: EntityManager and ComponentManager really should just be one class...
		compMgr.entCompMasks.resize(1);
		entIndexToGen.push_back(0);
	}

	Entity EntityManager::NewEntity()
	{
		uint64 i;
		uint16 gen;
		if (freeEntityIndexes.size() >= RECYCLE_ENTITY_COUNT)
		{
			i = freeEntityIndexes.front();
			freeEntityIndexes.pop();
			gen = entIndexToGen.at(i);  // incremented at Entity destruction
			Assert(compMgr.entCompMasks[i] == ComponentManager::ComponentMask(),
				   "expected ent comp mask to be reset at destruction but it wasn't");
			compMgr.entCompMasks[i] = ComponentManager::ComponentMask();
		}
		else
		{
			i = nextEntityIndex++;
			gen = 0;
			entIndexToGen.push_back(gen);

			// add a blank comp mask without copying one in
			compMgr.entCompMasks.resize(compMgr.entCompMasks.size() + 1);

			Assert(entIndexToGen.size() == nextEntityIndex);
			Assert(compMgr.entCompMasks.size() == nextEntityIndex);
		}

		return Entity(i, gen);
	}

	void EntityManager::Destroy(Entity e)
	{
		if (!Valid(e))
		{
			std::stringstream ss;
			ss << "entity " << e << " is not valid; it may have already been destroyed.";
			throw std::invalid_argument(ss.str());
		}

		RemoveAllComponents(e);
		entIndexToGen.at(e.Index())++;
		freeEntityIndexes.push(e.Index());
	}

	bool EntityManager::Valid(Entity e) const
	{
		return e.Generation() == entIndexToGen.at(e.Index());
	}

	template <typename CompType, typename ...T>
	CompType *EntityManager::Assign(Entity e, T... args)
	{
		return compMgr.Assign<CompType>(e, args...);
	}

	template <typename CompType>
	void EntityManager::Remove(Entity e)
	{
		compMgr.Remove<CompType>(e);
	}

	void EntityManager::RemoveAllComponents(Entity e)
	{
		compMgr.RemoveAll(e);
	}

	template <typename CompType>
	bool EntityManager::Has(Entity e) const
	{
		return compMgr.Has<CompType>(e);
	}

	template <typename CompType>
	CompType *EntityManager::Get(Entity e)
	{
		return compMgr.Get<CompType>(e);
	}

	EntityManager::EntityCollection EntityManager::EntitiesWith(ComponentManager::ComponentMask compMask)
	{
		// find the smallest size component pool to iterate over
		size_t minSize;
		int minSizeCompIndex = -1;

		for (size_t i = 0; i < compMgr.ComponentTypeCount(); ++i)
		{
			if (!compMask.test(i))
			{
				continue;
			}

			size_t compSize = static_cast<BaseComponentPool *>(compMgr.componentPools.at(i))->Size();

			if (compSize < minSize || minSizeCompIndex == -1)
			{
				minSize = compSize;
				minSizeCompIndex = i;
			}
		}

		auto smallestCompPool = static_cast<BaseComponentPool *>(compMgr.componentPools.at(minSizeCompIndex));

		return EntityManager::EntityCollection(compMgr, compMask, smallestCompPool->Entities(),
		                                       smallestCompPool->CreateIterateLock());
	}

	template <typename ...CompTypes>
	EntityManager::EntityCollection EntityManager::EntitiesWith()
	{
		return EntitiesWith(compMgr.createMask<CompTypes...>());
	}

	EntityManager::EntityCollection::Iterator::Iterator(ComponentManager &cm,
	                                                    const ComponentManager::ComponentMask &compMask,
	                                                    ComponentPoolEntityCollection *compEntColl,
	                                                    ComponentPoolEntityCollection::Iterator compIt)
	: cm(cm), compMask(compMask), compEntColl(compEntColl), compIt(compIt)
	{}

	EntityManager::EntityCollection::Iterator &EntityManager::EntityCollection::Iterator::operator++()
	{
		while (++compIt != compEntColl->end())
		{
			Entity e = *compIt;
			auto entCompMask = cm.entCompMasks.at(e.Index());
			if ((entCompMask & compMask).any())
			{
				break;
			}
		}
		return *this;
	}

	bool EntityManager::EntityCollection::Iterator::operator==(Iterator &other)
	{
		return compMask == other.compMask && compIt == other.compIt;
	}
	bool EntityManager::EntityCollection::Iterator::operator!=(Iterator &other)
	{
		return !(*this == other);
	}

	Entity EntityManager::EntityCollection::Iterator::operator*()
	{
		return *compIt;
	}

	EntityManager::EntityCollection::EntityCollection(ComponentManager &cm,
	                                                  const ComponentManager::ComponentMask &compMask,
	                                                  ComponentPoolEntityCollection compEntColl,
	                                                  unique_ptr<BaseComponentPool::IterateLock> &&iLock)
	: cm(cm), compMask(compMask), compEntColl(compEntColl), iLock(std::move(iLock))
	{}

	EntityManager::EntityCollection::Iterator EntityManager::EntityCollection::begin()
	{
		return EntityManager::EntityCollection::Iterator(cm, compMask, &compEntColl, compEntColl.begin());
	}

	EntityManager::EntityCollection::Iterator EntityManager::EntityCollection::end()
	{
		return EntityManager::EntityCollection::Iterator(cm, compMask, &compEntColl, compEntColl.end());
	}


// private:
// 	ComponentManager &cm;
// 	ComponentManager::ComponentMask compMask;
// 	BaseComponentPool *pool;
// 	uint64 lastCompIndex;
// 	unique_ptr<BaseComponentPool::IterateLock> iLock;
}

#endif
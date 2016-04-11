#ifndef COMPONENT_STORAGE_H
#define COMPONENT_STORAGE_H

#include <unordered_map>
#include <bitset>
#include <queue>
#include <iterator>
#include <stdexcept>
#include "Common.hh"
#include "ecs/Entity.hh"
#define MAX_COMPONENTS 64


namespace sp
{
	class BaseComponentPool;
	// class BaseComponentPool::IterateLock;

	// Object to capture the state of entities at a given time.  Used for iterating over a
	// ComponentPool's Entities
	class ComponentPoolEntityCollection
	{
	public:
		class Iterator : public std::iterator<std::input_iterator_tag, Entity>
		{
		public:
			Iterator(BaseComponentPool &pool, uint64 compIndex);
			Iterator &operator++();
			Entity operator*();
		private:
			BaseComponentPool &pool;
			uint64 compIndex;
		};

		ComponentPoolEntityCollection(BaseComponentPool &pool);
		Iterator begin();
		Iterator end();
	private:
		BaseComponentPool &pool;
		uint64 lastCompIndex;
	};

	// These classes are meant to be internal to the ECS system and should not be known
	// to code outside of the ECS system
	class BaseComponentPool
	{
		friend class ComponentPoolEntityCollection::Iterator;
	public:
		/**
		* Creating this lock will enable "soft remove" mode on the given ComponentPool.
		* The destruction of this lock will re-enable normal deletion mode.
		*/
		class IterateLock : public NonCopyable
		{
		public:
			IterateLock(BaseComponentPool &pool);
			~IterateLock();
		private:
			BaseComponentPool &pool;
		};
		friend IterateLock;

		virtual ~BaseComponentPool() {}

		virtual void Remove(Entity e) = 0;
		virtual bool HasComponent(Entity e) const = 0;
		virtual size_t Size() const = 0;
		virtual ComponentPoolEntityCollection &&Entities() = 0;

		// as long as the resultant lock is not destroyed, the order that iteration occurs
		// over the components must stay the same.
		virtual IterateLock &&CreateIterateLock() = 0;

	private:
		// when toggleSoftRemove(true) is called then any Remove(e) calls
		// must guarentee to not alter the internal ordering of components.
		// When toggleSoftRemove(false) is called later then removals are allowed to rearrange
		// internal ordering again
		virtual void toggleSoftRemove(bool enabled) = 0;

		// method used by ComponentPoolEntityCollection::Iterator to find the next Entity
		virtual Entity entityAt(uint64 compIndex) = 0;

	};

	/**
	 * ComponentPool is a storage container for Entity components.
	 * It stores all components with a vector and so it only grows when new components are added.
	 * It "recycles" and keeps storage unfragmented by swapping components to the end when they are removed
	 * but does not guarantee the internal ordering of components based on insertion or Entity index.
	 * It allows efficient iteration since there are no holes in its component storage.
	 *
	 * TODO-cs: an incremental allocator instead of a vector will be better once the number
	 * components is very large; this should be implemeted.
	 */
	template <typename CompType>
	class ComponentPool : public BaseComponentPool
	{

	public:
		ComponentPool();

		// DO NOT CACHE THIS POINTER, a component's pointer may change over time
		CompType *NewComponent(Entity e);

		// DO NOT CACHE THIS POINTER, a component's pointer may change over time
		CompType *Get(Entity e);
		void Remove(Entity e) override;
		bool HasComponent(Entity e) const override;
		size_t Size() const override;
		ComponentPoolEntityCollection &&Entities() override;

		BaseComponentPool::IterateLock &&CreateIterateLock() override;

	private:
		static const uint64 INVALID_COMP_INDEX = static_cast<uint64>(-1);

		vector<std::pair<Entity, CompType> > components;
		uint64 lastCompIndex;
		std::unordered_map<uint64, uint64> entIndexToCompIndex;
		bool softRemoveMode;
		std::queue<uint64> softRemoveCompIndexes;

		void toggleSoftRemove(bool enabled) override;

		void softRemove(uint64 compIndex);
		void remove(uint64 compIndex);

		Entity entityAt(uint64 compIndex) override;
	};

	template <typename CompType>
	ComponentPool<CompType>::ComponentPool()
	{
		lastCompIndex = static_cast<uint64>(-1);
		softRemoveMode = false;
	}

	template <typename CompType>
	BaseComponentPool::IterateLock &&ComponentPool<CompType>::CreateIterateLock()
	{
		return BaseComponentPool::IterateLock(*static_cast<BaseComponentPool*>(this));
	}

	template <typename CompType>
	CompType *ComponentPool<CompType>::NewComponent(Entity e)
	{
		int newCompIndex = lastCompIndex + 1;
		lastCompIndex = newCompIndex;

		if (components.size() == newCompIndex)
		{
			// resize instead of push_back() to avoid an extra construction of CompType
			components.resize(components.size() + 1);
		}

		components.at(newCompIndex).first = e;
		entIndexToCompIndex[e.Index()] = newCompIndex;

		return &components.at(newCompIndex).second;
	}

	template <typename CompType>
	CompType *ComponentPool<CompType>::Get(Entity e)
	{
		if (!HasComponent(e))
		{
			return nullptr;
		}

		uint64 compIndex = entIndexToCompIndex.at(e.Index());
		return components.at(compIndex).second;
	}

	template <typename CompType>
	void ComponentPool<CompType>::Remove(Entity e)
	{
		if (!HasComponent(e))
		{
			throw std::runtime_error("cannot remove component because the entity does not have one");
		}

		entIndexToCompIndex.at(e.Index()) = ComponentPool<CompType>::INVALID_COMP_INDEX;

		uint64 removeIndex = entIndexToCompIndex.at(e.Index());

		if (softRemoveMode)
		{
			softRemove(removeIndex);
		}
		else
		{
			remove(removeIndex);
		}
	}

	template <typename CompType>
	void ComponentPool<CompType>::remove(uint64 compIndex)
	{
		// Swap this component to the end and decrement the container last index

		// TODO-cs: swap is done via copy instead of via move.  Investigate performance difference
		// if this is changed.  My guess is that caching won't work as well if move is used but it will
		// make removing a component quicker (avoids two copy operations)
		auto temp = components.at(lastCompIndex);
		components.at(lastCompIndex) = components.at(compIndex);
		components.at(compIndex) = temp;

		lastCompIndex--;
	}

	template <typename CompType>
	void ComponentPool<CompType>::softRemove(uint64 compIndex)
	{
		// mark the component as the "Null" Entity and add this component index to queue of
		// components to be deleted when "soft remove" mode is disabled.
		// "Null" Entities will never be iterated over
		Assert(compIndex < components.size());

		components[compIndex].first = Entity::Entity();
		softRemoveCompIndexes.push(compIndex);
	}

	template <typename CompType>
	bool ComponentPool<CompType>::HasComponent(Entity e) const
	{
		auto compIndex = entIndexToCompIndex.find(e.Index());
		return compIndex != entIndexToCompIndex.end() && compIndex->second != ComponentPool<CompType>::INVALID_COMP_INDEX;
	}

	template <typename CompType>
	size_t ComponentPool<CompType>::Size() const
	{
		return lastCompIndex + 1;
	}

	template <typename CompType>
	void ComponentPool<CompType>::toggleSoftRemove(bool enabled)
	{
		if (enabled)
		{
			if (softRemoveMode)
			{
				throw runtime_error("soft remove mode is already active");
			}
		}
		else
		{
			if (!softRemoveMode)
			{
				throw runtime_error("soft remove mode is already inactive");
			}

			// must perform proper removes for everything that has been "soft removed"
			while (!softRemoveCompIndexes.empty())
			{
				uint64 compIndex = softRemoveCompIndexes.front();
				softRemoveCompIndexes.pop();
				remove(compIndex);
			}
		}

		softRemoveMode = enabled;
	}

	template <typename CompType>
	Entity ComponentPool<CompType>::entityAt(uint64 compIndex)
	{
		Assert(compIndex < components.size());
		return components[compIndex].first;
	}

	BaseComponentPool::IterateLock::IterateLock(BaseComponentPool &pool): pool(pool)
	{
		pool.toggleSoftRemove(true);
	}

	BaseComponentPool::IterateLock::~IterateLock()
	{
		pool.toggleSoftRemove(false);
	}

	ComponentPoolEntityCollection::ComponentPoolEntityCollection(BaseComponentPool &pool)
		: pool(pool)
	{
		// keep track of the last component at creation time.  This way, if new components
		// are created during iteration they will be added at the end and we will not iterate over them
		lastCompIndex = pool.Size() - 1;
	}

	ComponentPoolEntityCollection::Iterator ComponentPoolEntityCollection::begin()
	{
		return ComponentPoolEntityCollection::Iterator(pool, 0);
	}

	ComponentPoolEntityCollection::Iterator ComponentPoolEntityCollection::end()
	{
		return ComponentPoolEntityCollection::Iterator(pool, lastCompIndex + 1);
	}

	ComponentPoolEntityCollection::Iterator::Iterator(BaseComponentPool &pool, uint64 compIndex)
		: pool(pool), compIndex(compIndex)
	{}

	ComponentPoolEntityCollection::Iterator &ComponentPoolEntityCollection::Iterator::operator++()
	{
		compIndex++;
		return *this;
	}

	Entity ComponentPoolEntityCollection::Iterator::operator*()
	{
		Assert(compIndex < pool.Size());
		return pool.entityAt(compIndex);
	}

	template <typename CompType>
	ComponentPoolEntityCollection &&ComponentPool<CompType>::Entities()
	{
		return std::move(ComponentPoolEntityCollection(*this));
	}
}

#endif
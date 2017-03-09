#pragma once

#include "Common.hh"
#include "core/CVar.hh"
#include <functional>

namespace sp
{
	template <typename ParamType>
	class CFunc : public CVarBase
	{
	public:
		typedef std::function<void(const ParamType &)> Callback;

		CFunc(const string &name, const string &description, Callback callback)
			: CVarBase(name, description), callback(callback)
		{
		}

		CFunc(const string &name, Callback callback) : CFunc(name, "", callback)
		{
		}

		string StringValue()
		{
			return "CFunc:" + GetName();
		}

		void SetFromString(const string &newValue)
		{
			ParamType value;
			std::stringstream in(newValue);
			in >> value;

			callback(value);
		}

		bool IsValueType()
		{
			return false;
		}

	private:
		Callback callback;
	};

	template <typename ThisType, typename ParamType>
	class CFuncCollection
	{
	public:
		typedef void(ThisType::* Callback)(const string &);

		CFuncCollection(ThisType *parent) : parent(parent) {}

		void Register(const string &name, const string &description, Callback callback)
		{
			collection.push_back(make_shared<CFunc<ParamType>>(
									 name, description,
									 std::bind(callback, parent, std::placeholders::_1)));
		}

		void Register(const string &name, Callback callback)
		{
			Register(name, "", callback);
		}
	private:
		ThisType *parent;
		vector<shared_ptr<CFunc<ParamType>>> collection;
	};
}
